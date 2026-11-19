// ================================================================================================
//  VectorRender.cpp  -  MyVectorRender (CPU 3D pipeline) and MyVectorText (vector-stroke text)
//
//  Performance notes that shaped this file:
//    * The queues (polys / keys / points / batch lists) are reserved ONCE; clearing keeps capacity, so a frame allocates nothing.
//    * The per-mesh scratch arrays are STATIC.  A local `MyVector vv[kMaxMeshVerts]` is zero-filled by MyVector's constructor on every
//      call (about 32 KB even for an 8-vertex cube), which dominated the frame cost in scenes with many small objects.
//    * Trigonometry comes from the MathPrecalculation tables (FAST_SIN / FAST_SINCOS_DEG / FAST_UNIT_CIRCLE), never the C runtime.
//    * Shades are quantised so neighbouring faces share a colour; Flush merges consecutive same-colour polygons into one batched fill.
//    * Everything runs on ONE thread (the render thread); nothing here is locked.
// ================================================================================================

#include "Includes.h"                    // global platform / renderer definitions (must come first)
#include "VectorRender.h"
#include "Renderer.h"
#include "Configuration.h"
#include "Lights.h"                      // the vector sun, brightness and flat mode live in the Light system now

extern LightsManager lightsManager;      // defined in main.cpp
#include "MathPrecalculation.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <string>

using namespace MyGeom;
using MyShapes::Face;
using MyShapes::Mesh;
using MyShapes::Shade;

namespace MyVR
{
    MyVectorRender& MyVectorRender::Instance()
    {
        static MyVectorRender s_instance;                // constructed on first use: no static-initialisation-order trouble
        return s_instance;
    }

    void MyVectorRender::Reserve()
    {
        if (m_reserved) return;
        m_polys.reserve(kMaxPolys);
        m_keys.reserve(kMaxPolys);
        m_pts.reserve(static_cast<size_t>(kMaxPolys) * 5);     // most faces are triangles / quads / pentagons
        m_batchPts.reserve(kMaxPolys);
        m_batchCnt.reserve(kMaxPolys);
        m_reserved = true;
    }

    void MyVectorRender::BeginFrame(float w, float h, float focalScale, float centreYFraction)
    {
        Reserve();
        m_polys.clear();                                 // clear() keeps the capacity: no allocation
        m_pts.clear();
        m_keys.clear();
        m_statLod = m_statCulled = m_statBatches = 0;

        m_f.w        = w;
        m_f.h        = h;
        m_f.focal    = h * focalScale;
        m_f.cx       = w * 0.5f;
        m_f.cy       = h * centreYFraction;
        lightsManager.SetVectorSun(MyVector(-0.40f, 0.60f, -0.70f));     // the shared sun, up-left toward the viewer
        m_f.lightDir = lightsManager.GetVectorSunView();
        // Mat3's default constructor leaves it uninitialised, so give the camera a defined state until ApplyCamera runs.
        m_f.viewRot       = Mat3::FromQuat(MyQuaternion::Identity());
        m_f.worldFromView = Mat3::FromQuat(MyQuaternion::Identity());
        m_f.camPos        = MyVector::Zero();
    }

    //==============================================================================================
    // Camera / projection
    //==============================================================================================
    void MyVectorRender::ApplyCamera(const MyVector& target, float pitchDeg, float yawDeg, float dist)
    {
        const MyQuaternion q = MyQuaternion::Euler(pitchDeg, yawDeg, 0.0f);
        m_f.camPos        = target + q * MyVector(0.0f, 0.0f, -dist);
        m_f.viewRot       = Mat3::FromQuat(MyQuaternion::Inverse(q));            // world -> view
        m_f.worldFromView = Mat3::FromQuat(q);                                   // view -> world
    }

    bool MyVectorRender::ProjectPoint(const MyVector& world, Vector2& out) const
    {
        const MyVector v = m_f.viewRot.Mul(world - m_f.camPos);
        if (v.z < 0.1f) return false;
        const float invZ = m_f.focal / v.z;
        out = Vector2(m_f.cx + v.x * invZ, m_f.cy - v.y * invZ);
        return true;
    }

    bool MyVectorRender::ScreenToPlane(float sx, float sy, const MyVector& n, float d, MyVector& out) const
    {
        const MyVector viewDir((sx - m_f.cx) / m_f.focal, -(sy - m_f.cy) / m_f.focal, 1.0f);
        const MyVector dir = m_f.worldFromView.Mul(viewDir);
        const float denom = n.Dot(dir);
        if (MyMath::Abs(denom) < 1e-6f) return false;
        const float t = (d - n.Dot(m_f.camPos)) / denom;
        if (t <= 0.0f) return false;
        out = m_f.camPos + dir * t;
        return true;
    }

    // Largest axis-aligned rectangle of the floor (y = planeY) that is fully visible; room is left for a title above and a HUD below.
    Rect2 MyVectorRender::FloorField(float planeY) const
    {
        Rect2 f = { -8.0f, 8.0f, -5.0f, 5.0f };
        MyVector tl, tr, bl, br;
        const float L = 0.05f * m_f.w, R = 0.95f * m_f.w, T = 0.14f * m_f.h, B = 0.86f * m_f.h;
        if (ScreenToPlane(L, T, MyVector::Up(), planeY, tl) && ScreenToPlane(R, T, MyVector::Up(), planeY, tr) &&
            ScreenToPlane(L, B, MyVector::Up(), planeY, bl) && ScreenToPlane(R, B, MyVector::Up(), planeY, br))
        {
            f.x0 = MyMath::Max(tl.x, bl.x);  f.x1 = MyMath::Min(tr.x, br.x);
            f.y0 = MyMath::Max(bl.z, br.z);  f.y1 = MyMath::Min(tl.z, tr.z);
        }
        return f;
    }

    // FloorField with the screen margins given (fractions of the screen) and a width mix for a tilted camera: the far edge of the floor is
    // narrower on screen than the near edge, so FloorField is limited by the far edge.  mix 0 = that far-limited width; mix 1 = the width at the
    // middle of the area; in between gives a bigger, nearer picture whose far corners may clip a little (the caller scrolls the view).
    Rect2 MyVectorRender::FloorFieldMix(float planeY, float T, float B, float L, float R, float mix) const
    {
        Rect2 f = { -8.0f, 8.0f, -5.0f, 5.0f };
        MyVector tl, tr, bl, br, ml, mr;
        const float midY = 0.5f * (T + B) * m_f.h;
        if (ScreenToPlane(L * m_f.w, T * m_f.h, MyVector::Up(), planeY, tl) && ScreenToPlane(R * m_f.w, T * m_f.h, MyVector::Up(), planeY, tr) &&
            ScreenToPlane(L * m_f.w, B * m_f.h, MyVector::Up(), planeY, bl) && ScreenToPlane(R * m_f.w, B * m_f.h, MyVector::Up(), planeY, br) &&
            ScreenToPlane(L * m_f.w, midY, MyVector::Up(), planeY, ml) && ScreenToPlane(R * m_f.w, midY, MyVector::Up(), planeY, mr))
        {
            const float x0 = MyMath::Max(tl.x, bl.x), x1 = MyMath::Min(tr.x, br.x);
            const float wFar = x1 - x0, wMid = mr.x - ml.x, cx = 0.5f * (x0 + x1);
            const float w = wFar + (wMid - wFar) * mix;
            f.x0 = cx - 0.5f * w; f.x1 = cx + 0.5f * w;
            f.y0 = MyMath::Max(bl.z, br.z); f.y1 = MyMath::Min(tl.z, tr.z);
        }
        return f;
    }

    // Same for the vertical plane z = 0 (side-scrolling games): x0..x1 along world X, y0..y1 along world Y.
    Rect2 MyVectorRender::SideField() const
    {
        Rect2 f = { -9.0f, 9.0f, -5.0f, 5.0f };
        MyVector tl, tr, bl, br;
        const float L = 0.03f * m_f.w, R = 0.97f * m_f.w, T = 0.17f * m_f.h, B = 0.88f * m_f.h;
        if (ScreenToPlane(L, T, MyVector::Forward(), 0.0f, tl) && ScreenToPlane(R, T, MyVector::Forward(), 0.0f, tr) &&
            ScreenToPlane(L, B, MyVector::Forward(), 0.0f, bl) && ScreenToPlane(R, B, MyVector::Forward(), 0.0f, br))
        {
            f.x0 = MyMath::Max(tl.x, bl.x);  f.x1 = MyMath::Min(tr.x, br.x);
            f.y0 = MyMath::Max(bl.y, br.y);  f.y1 = MyMath::Min(tl.y, tr.y);
        }
        return f;
    }

    //==============================================================================================
    // Polygon queue
    //==============================================================================================
    // Offscreen and sub-pixel polygons cost a draw call and show nothing: reject them here.
    bool MyVectorRender::PolyVisible(const Vector2* p, int n) const
    {
        float minX = p[0].x, maxX = p[0].x, minY = p[0].y, maxY = p[0].y;
        for (int i = 1; i < n; ++i)
        {
            if (p[i].x < minX) minX = p[i].x; else if (p[i].x > maxX) maxX = p[i].x;
            if (p[i].y < minY) minY = p[i].y; else if (p[i].y > maxY) maxY = p[i].y;
        }
        if (maxX < 0.0f || minX > m_f.w || maxY < 0.0f || minY > m_f.h) return false;
        return (maxX - minX) >= 0.9f || (maxY - minY) >= 0.9f;
    }

    void MyVectorRender::PushPoly(const Vector2* p, int n, const MyColor& c, float depth)
    {
        m_polys.push_back({ static_cast<int>(m_pts.size()), n, c });
        for (int i = 0; i < n; ++i) m_pts.push_back(p[i]);
        m_keys.push_back({ depth, static_cast<int>(m_polys.size()) - 1 });
    }

    // Sorts far to near and fills.  Consecutive polygons of ONE colour become ONE batch (one path geometry + one fill instead of one per
    // polygon: shadows, grid lines and pellets collapse to a few calls).
    void MyVectorRender::Flush(Renderer& r)
    {
        // far to near; equal depths keep their SUBMISSION order (std::sort is not stable, so the index is the tie-break)
        std::sort(m_keys.begin(), m_keys.end(), [](const SortKey& a, const SortKey& b) { return (a.depth != b.depth) ? (a.depth > b.depth) : (a.index < b.index); });

        m_batchPts.clear(); m_batchCnt.clear();
        MyColor curColor;
        bool haveColor = false;
        int batches = 0;
        auto flushBatch = [&]()
        {
            if (m_batchCnt.empty()) return;
            r.DrawFilledPolygonBatch(m_batchPts.data(), m_batchCnt.data(), static_cast<int>(m_batchCnt.size()), curColor);
            m_batchPts.clear(); m_batchCnt.clear();
            ++batches;
        };
        for (const SortKey& key : m_keys)
        {
            const Poly& p = m_polys[key.index];
            if (!haveColor || p.color.r != curColor.r || p.color.g != curColor.g || p.color.b != curColor.b || p.color.a != curColor.a || m_batchCnt.size() >= 384)
            {
                flushBatch();
                curColor = p.color;
                haveColor = true;
            }
            m_batchPts.push_back(&m_pts[p.first]);
            m_batchCnt.push_back(p.count);
        }
        flushBatch();
        m_statBatches = batches;
    }

    //==============================================================================================
    // Mesh submission
    //==============================================================================================
    void MyVectorRender::SubmitMesh(const Mesh& mesh, const Mat3& modelRot, const MyVector& scale, const MyVector& pos,
                                    const MyColor& base, bool drawFirst, bool keepShape)
    {
        const size_t nv = mesh.verts.size();
        if (nv == 0 || nv > static_cast<size_t>(kMaxMeshVerts)) return;

        const MyVector T = m_f.viewRot.Mul(pos - m_f.camPos);                // instance centre in view space
        if (!drawFirst)
        {
            const float worldR = mesh.radius * MyMath::Max(scale.x, MyMath::Max(scale.y, scale.z));
            if (T.z + worldR < 0.1f) { ++m_statCulled; return; }
            if (T.z > worldR + 0.1f)
            {
                const float inv = m_f.focal / T.z;
                const float pr = worldR * inv, sx = m_f.cx + T.x * inv, sy = m_f.cy - T.y * inv;
                if (sx + pr < 0.0f || sx - pr > m_f.w || sy + pr < 0.0f || sy - pr > m_f.h) { ++m_statCulled; return; }
                // The thresholds scale with the vector-quality level: a slower machine swaps models for dots (and drops tiny ones) sooner.
                if (keepShape)
                {
                    if (pr < kLodCullPx) { ++m_statCulled; return; }                                              // only a sub-pixel speck is skipped
                }
                else if (pr < kLodBillboardPx * m_vq.lodMul)
                {
                    if (pr < kLodCullPx * m_vq.lodMul) { ++m_statCulled; return; }
                    static const float kHx[6] = { 1.0f, 0.5f, -0.5f, -1.0f, -0.5f, 0.5f }, kHy[6] = { 0.0f, 0.866f, 0.866f, 0.0f, -0.866f, -0.866f };
                    Vector2 hex[6];
                    for (int i = 0; i < 6; ++i) hex[i] = Vector2(sx + kHx[i] * pr, sy + kHy[i] * pr);
                    const MyColor& src = (!lightsManager.GetVectorFlat() && !mesh.faces.empty() && mesh.faces[0].fixedColor) ? mesh.faces[0].color : base;
                    const float lk = 0.72f * lightsManager.GetVectorBrightness();                                                        // distant dots obey the torch-light too
                    PushPoly(hex, 6, MyColor(static_cast<uint8_t>(src.r * lk), static_cast<uint8_t>(src.g * lk), static_cast<uint8_t>(src.b * lk), static_cast<uint8_t>(src.a * m_meshAlpha)), T.z);
                    ++m_statLod;
                    return;
                }
            }
        }

        // M turns normals (rotation only); Ms = M * diag(scale) takes a local vertex straight to view space: view = Ms * local + T.
        const Mat3 M  = m_f.viewRot * modelRot;
        const Mat3 Ms = M.Scaled(scale);

        static MyVector vv[kMaxMeshVerts];                                   // view-space vertices
        static Vector2  sv[kMaxMeshVerts];                                   // their screen positions (valid where vv[i].z >= 0.1)
        for (size_t i = 0; i < nv; ++i)
        {
            const MyVector v = Ms.Mul(mesh.verts[i]) + T;
            vv[i] = v;
            if (v.z >= 0.1f)                                                 // ONE perspective divide per vertex, shared by every face that uses it
            {
                const float invZ = m_f.focal / v.z;
                sv[i] = Vector2(m_f.cx + v.x * invZ, m_f.cy - v.y * invZ);
            }
        }

        static Vector2 pp[kMaxPolyVerts];
        for (const Face& face : mesh.faces)
        {
            if (m_polys.size() >= static_cast<size_t>(kMaxPolys)) return;

            const MyVector n = M.Mul(face.normal);                           // rotation-only: valid while scale is mild
            if (n.Dot(vv[face.idx[0]]) >= 0.0f) continue;                    // faces away from the eye (eye = view-space origin)

            const int cnt = static_cast<int>(face.idx.size());
            float zSum = 0.0f;
            bool ok = true;
            for (int k = 0; k < cnt; ++k)
            {
                const int vi = face.idx[k];
                if (vv[vi].z < 0.1f) { ok = false; break; }                  // behind / too close to the camera
                pp[k] = sv[vi];
                zSum += vv[vi].z;
            }
            if (!ok || !PolyVisible(pp, cnt)) continue;

            float shade = (face.glow || lightsManager.GetVectorFlat()) ? 1.0f : lightsManager.VectorLambert(n) * face.tone;   // ambient + diffuse (flat: full brightness)
            shade = std::floor(shade * 10.0f + 0.5f) * 0.1f;                  // quantise: neighbouring faces share a colour -> bigger batches
            const MyColor& bc = (face.fixedColor && !lightsManager.GetVectorFlat()) ? face.color : base;          // per-face paint overrides the instance colour (except in flat mode)
            shade *= lightsManager.GetVectorBrightness();                                             // after quantising, so dim light stays smooth
            PushPoly(pp, cnt, MyColor(static_cast<uint8_t>(bc.r * shade), static_cast<uint8_t>(bc.g * shade), static_cast<uint8_t>(bc.b * shade), static_cast<uint8_t>(bc.a * m_meshAlpha)),
                     drawFirst ? 1.0e9f : zSum * FAST_INV_INT(cnt) - face.depthBias);          // average depth: table reciprocal instead of a divide
        }
    }

    // A big CONVEX body (a planet): its visible faces never overlap, so their draw order does not matter.  Every face gets the SAME depth
    // plus a tiny per-colour rank, so faces of one colour sit side by side after the depth sort and merge into one batched fill - roughly
    // 160 faces become a few dozen fills.  depthBias < 0 draws the body later (nearer): clouds, haze.
    void MyVectorRender::SubmitMeshGrouped(const Mesh& mesh, const Mat3& modelRot, const MyVector& scale, const MyVector& pos,
                                           const MyColor& base, float depthBias)
    {
        const size_t nv = mesh.verts.size();
        if (nv == 0 || nv > static_cast<size_t>(kMaxMeshVerts)) return;
        const MyVector T = m_f.viewRot.Mul(pos - m_f.camPos);
        if (T.z < 0.5f) return;
        const Mat3 M  = m_f.viewRot * modelRot;
        const Mat3 Ms = M.Scaled(scale);                                     // scale folded into the matrix: one multiply per vertex
        static MyVector vv[kMaxMeshVerts];
        for (size_t i = 0; i < nv; ++i) vv[i] = Ms.Mul(mesh.verts[i]) + T;

        static std::vector<uint32_t> colourKey;                              // grow-only scratch (no per-call allocation once warm)
        static std::vector<int> order;
        colourKey.clear();
        const size_t firstKey = m_keys.size();
        static Vector2 pp[kMaxPolyVerts];
        for (const Face& face : mesh.faces)
        {
            if (m_polys.size() >= static_cast<size_t>(kMaxPolys)) break;
            const MyVector n = M.Mul(face.normal);
            if (n.Dot(vv[face.idx[0]]) >= 0.0f) continue;
            const int cnt = static_cast<int>(face.idx.size());
            bool ok = true;
            for (int k = 0; k < cnt; ++k)
            {
                const MyVector& v = vv[face.idx[k]];
                if (v.z < 0.1f) { ok = false; break; }
                const float invZ = m_f.focal / v.z;
                pp[k] = Vector2(m_f.cx + v.x * invZ, m_f.cy - v.y * invZ);
            }
            if (!ok || !PolyVisible(pp, cnt)) continue;
            float shade = face.glow ? 1.0f : lightsManager.VectorLambert(n) * face.tone;
            // 20 brightness steps for the big grouped bodies: a smooth terminator.  Below detail 0.75 only 10 steps: half as many colours means
            // roughly half as many fill batches for the thousands of faces of a planet (Vector Quality reaches the planets through here).
            const float steps = (m_vq.detail > 0.75f) ? 20.0f : 10.0f;
            shade = std::floor(shade * steps + 0.5f) / steps;
            const MyColor& bc = face.fixedColor ? face.color : base;
            const MyColor c(static_cast<uint8_t>(bc.r * shade), static_cast<uint8_t>(bc.g * shade), static_cast<uint8_t>(bc.b * shade), bc.a);
            PushPoly(pp, cnt, c, T.z);
            colourKey.push_back((static_cast<uint32_t>(c.r) << 24) | (static_cast<uint32_t>(c.g) << 16) | (static_cast<uint32_t>(c.b) << 8) | c.a);
        }
        const size_t count = colourKey.size();
        order.resize(count);
        for (size_t i = 0; i < count; ++i) order[i] = static_cast<int>(i);
        std::sort(order.begin(), order.end(), [&](int a, int b) { return colourKey[a] < colourKey[b]; });
        float rank = 0.0f;
        for (size_t i = 0; i < count; ++i)
        {
            if (i > 0 && colourKey[order[i]] != colourKey[order[i - 1]]) rank += 1.0f;
            m_keys[firstKey + order[i]].depth = T.z + depthBias + rank * 1.0e-4f;                  // one colour = one depth = one batch
        }
    }

    void MyVectorRender::SubmitArrow(const Mesh& arrowMesh, const MyVector& origin, const MyVector& v, const MyColor& color, float thickness)
    {
        const float len = v.Magnitude();
        if (len < 0.02f) return;
        const MyQuaternion q = MyQuaternion::FromToRotation(MyVector::Right(), v);   // trig-free alignment of +X onto v
        SubmitMesh(arrowMesh, Mat3::FromQuat(q), MyVector(len, thickness, thickness), origin, color);
    }

    void MyVectorRender::SubmitObject(const Mesh& mesh, const MyQuaternion& q, const MyVector& scale, const MyVector& pos,
                                      const MyColor& color, float shadowY, const MyColor& shadowColor, float shadowDepth, float shadowBias)
    {
        const Mat3 rot = Mat3::FromQuat(q);
        SubmitShadow(mesh, rot, scale, pos, shadowY, shadowColor, shadowDepth, shadowBias);
        SubmitMesh(mesh, rot, scale, pos, color);
    }

    //==============================================================================================
    // Level of detail
    //==============================================================================================
    // A triangle-faced sphere of F faces covering radius r shows about F / 2 faces of area 2.3 * r^2 / F each, so one face's edge is
    // sqrt(2.3 * r^2 / F) pixels; once that falls below ~5.5 px extra faces are invisible work, so a coarser tier is used.  The minimum edge
    // grows with the quality level (lodMul), so Low / Auto-under-load drop tiers sooner.  Pass a projected radius that does not change as
    // the object moves (computed at a fixed distance) so the tier cannot flicker.
    const Mesh& MyVectorRender::PickMeshLod(const Mesh* const* tiers, int topTier, float projectedRadiusPx) const
    {
        const float minEdge = 5.5f * m_vq.lodMul;
        const float minEdge2 = minEdge * minEdge;
        for (int t = topTier; t > 0; --t)
        {
            const float faces = static_cast<float>(tiers[t]->faces.size());
            if (faces > 0.0f && 2.3f * projectedRadiusPx * projectedRadiusPx / faces >= minEdge2) return *tiers[t];
        }
        return *tiers[0];
    }

    // The dense (subdiv 5, 20480 faces) planet only pays off when the globe is big: its faces are ~2 px across at a projected radius of ~190 px,
    // so smaller than that the light one looks identical and costs a quarter.  It is ONLY used at full quality (detail >= 1).
    const Mesh& MyVectorRender::PlanetLod(const Mesh& hi, const Mesh& lo, const Mesh& mn, const MyVector& pos, float radius) const
    {
        if (m_vq.detail <= 0.45f && !mn.verts.empty()) return mn;
        if (hi.verts.empty() || m_vq.detail < 0.999f) return lo;
        const float z = m_f.viewRot.Mul(pos - m_f.camPos).z;
        if (z < 0.5f) return lo;
        return (radius * m_f.focal / z >= 190.0f * m_vq.lodMul) ? hi : lo;
    }

    //==============================================================================================
    // World polygons, shadows, floor
    //==============================================================================================
    void MyVectorRender::SubmitWorldPoly(const MyVector* pts, int n, const MyColor& color, float depth, float bias)
    {
        if (n < 3 || n > kMaxPolyVerts || m_polys.size() >= static_cast<size_t>(kMaxPolys)) return;
        // Transform to view space and CLIP against the near plane (dropping the whole polygon when one corner was behind the camera made big
        // floors, grids and lines vanish as the camera moved toward a map edge).
        const float kNear = 0.5f;
        static MyVector vs[kMaxPolyVerts];                                   // static scratch: no per-call zero-fill (runs thousands of times a frame)
        bool anyBehind = false, anyFront = false;
        for (int k = 0; k < n; ++k)
        {
            vs[k] = m_f.viewRot.Mul(pts[k] - m_f.camPos);
            if (vs[k].z < kNear) anyBehind = true; else anyFront = true;
        }
        if (!anyFront) return;                                               // entirely behind the camera
        static MyVector clipped[kMaxPolyVerts * 2];
        const MyVector* src = vs;
        int cnt = n;
        if (anyBehind)                                                       // Sutherland-Hodgman against z = kNear
        {
            cnt = 0;
            for (int k = 0; k < n; ++k)
            {
                const MyVector& a = vs[k]; const MyVector& b = vs[(k + 1) % n];
                const bool ain = a.z >= kNear, bin = b.z >= kNear;
                if (ain) clipped[cnt++] = a;
                if (ain != bin)
                {
                    const float f = (kNear - a.z) / (b.z - a.z);
                    clipped[cnt++] = MyVector(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, kNear);
                }
            }
            if (cnt < 3) return;
            src = clipped;
        }
        static Vector2 pp[kMaxPolyVerts * 2];
        float zSum = 0.0f;
        for (int k = 0; k < cnt; ++k)
        {
            const float invZ = m_f.focal / src[k].z;
            pp[k] = Vector2(m_f.cx + src[k].x * invZ, m_f.cy - src[k].y * invZ);
            zSum += src[k].z;
        }
        if (!PolyVisible(pp, cnt)) { ++m_statCulled; return; }
        PushPoly(pp, cnt, color, (depth > 0.0f) ? depth : zSum * FAST_INV_INT(cnt) - bias);
    }

    MyVector MyVectorRender::LightTravel()
    {
        static const MyVector d = MyVector(-0.35f, -1.0f, -0.25f).Normalized();
        return d;
    }

    MyVector MyVectorRender::ProjectToPlaneY(const MyVector& p, float planeY)
    {
        const MyVector L = LightTravel();
        float s = (planeY - p.y) / L.y;
        if (s < 0.0f) s = 0.0f;
        MyVector q = p + L * s;
        q.y = planeY + 0.004f;                                               // hair above the surface: no z-fight
        return q;
    }

    // Shadow of a convex mesh projected straight down onto a stepped terrain top (the land is a row of cube columns).
    void MyVectorRender::SubmitTerrainShadow(const Mesh& mesh, const Mat3& rot, const MyVector& scale, const MyVector& pos, const MyColor& color)
    {
        const TerrainShadowCtx& T = m_tshadow;
        if (!T.top) return;
        const float invW = 1.0f / T.colW;
        const Mat3 rs = rot.Scaled(scale);                                   // scale folded into the matrix: one multiply per vertex
        static MyVector wv[kMaxMeshVerts];
        // Height cue: the higher the object is above the land under its centre, the smaller its shadow (down to 35 % at 11+ units up).
        const float h0 = pos.y - T.top((std::floor(pos.x * invW) + 0.5f) * T.colW, T.arg);
        const float k = MyMath::Clamp(1.0f - 0.06f * MyMath::Max(0.0f, h0), 0.35f, 1.0f);
        for (size_t i = 0; i < mesh.verts.size(); ++i)
        {
            const MyVector w = rs.Mul(mesh.verts[i]) + pos;
            const float wx = pos.x + (w.x - pos.x) * k;                                     // shrunk about the object's centre
            const float xc = (std::floor(wx * invW) + 0.5f) * T.colW;                       // centre of the column this vertex is over
            const float z = MyMath::Clamp(T.zc + (w.z - pos.z) * k, T.zc - T.zHalf, T.zc + T.zHalf);   // centred on the top face, never over its edges
            wv[i] = MyVector(wx, T.top(xc, T.arg) + 0.004f, z);                            // a hair above the top: no z-fight
        }
        MyVector fp[kMaxPolyVerts];
        for (const Face& face : mesh.faces)
        {
            if (rot.Mul(face.normal).y <= 0.0f) continue;                                   // straight-down light: only up-facing faces form the silhouette
            const int n = static_cast<int>(face.idx.size());
            if (n > kMaxPolyVerts) continue;
            for (int q = 0; q < n; ++q) fp[q] = wv[face.idx[q]];
            SubmitWorldPoly(fp, n, color, T.depth, (T.depth > 0.0f) ? 0.0f : 0.15f);        // explicit depth (T.depth) or real depth with a small bias: just after the tops it lies on
        }
    }

    void MyVectorRender::SubmitTopEllipse(const MyVector& centre, const MyVector& ax, const MyVector& az, float rx, float rz, const MyColor& color, float bias)
    {
        if (!m_vq.shadows) return;                                           // it is a shadow: honour the Vector Shadows setting
        const auto& ring = FAST_UNIT_CIRCLE(8);                              // precalculated octagon: no trig per frame
        if (ring.size() < 8) return;
        MyVector pts[8];
        for (int i = 0; i < 8; ++i) pts[i] = centre + ax * (ring[i].x * rx) + az * (ring[i].y * rz);
        SubmitWorldPoly(pts, 8, color, 0.0f, bias);
    }

    void MyVectorRender::SubmitShadow(const Mesh& mesh, const Mat3& rot, const MyVector& scale, const MyVector& pos, float planeY,
                                      const MyColor& color, float depth, float bias)
    {
        if (!m_vq.shadows) return;                                           // the Video tab's "Vector Shadows" switch (or the Auto level under load)
        if (mesh.verts.empty() || mesh.verts.size() > static_cast<size_t>(kMaxMeshVerts)) return;
        {   // A shadow under an object smaller than a few pixels is invisible: skip the projection altogether.
            const MyVector tv = m_f.viewRot.Mul(pos - m_f.camPos);
            if (tv.z > 0.1f && mesh.radius * MyMath::Max(scale.x, MyMath::Max(scale.y, scale.z)) * m_f.focal / tv.z < 4.0f * m_vq.lodMul) return;
        }
        if (m_tshadow.active) { SubmitTerrainShadow(mesh, rot, scale, pos, color); return; }     // landscape games: land the shadow on the column tops
        const MyVector L = LightTravel();
        const Mat3 rs = rot.Scaled(scale);
        static MyVector wv[kMaxMeshVerts];
        // Height cue: the higher the object is above the plane, the smaller its shadow (down to 35 % at 11+ units), shrunk about where its centre lands.
        const float k = MyMath::Clamp(1.0f - 0.06f * MyMath::Max(0.0f, pos.y - planeY), 0.35f, 1.0f);
        const MyVector c = ProjectToPlaneY(pos, planeY);
        for (size_t i = 0; i < mesh.verts.size(); ++i)
        {
            const MyVector q = ProjectToPlaneY(rs.Mul(mesh.verts[i]) + pos, planeY);
            wv[i] = MyVector(c.x + (q.x - c.x) * k, q.y, c.z + (q.z - c.z) * k);
        }
        MyVector fp[kMaxPolyVerts];
        for (const Face& face : mesh.faces)
        {
            if (rot.Mul(face.normal).Dot(L) >= 0.0f) continue;               // faces away from the light
            const int n = static_cast<int>(face.idx.size());
            if (n > kMaxPolyVerts) continue;
            for (int q = 0; q < n; ++q) fp[q] = wv[face.idx[q]];
            SubmitWorldPoly(fp, n, color, depth, bias);
        }
    }

    void MyVectorRender::SubmitHullShadow(const Mesh& mesh, const Mat3& rot, const MyVector& scale, const MyVector& pos, float planeY,
                                          const MyColor& color, float depth, float bias)
    {
        if (!m_vq.shadows) return;
        const size_t nv = mesh.verts.size();
        if (nv < 3 || nv > static_cast<size_t>(kMaxMeshVerts)) return;
        {   // invisible under a tiny object: same test as SubmitShadow
            const MyVector tv = m_f.viewRot.Mul(pos - m_f.camPos);
            if (tv.z > 0.1f && mesh.radius * MyMath::Max(scale.x, MyMath::Max(scale.y, scale.z)) * m_f.focal / tv.z < 4.0f * m_vq.lodMul) return;
        }

        // Project every vertex along the light ray onto the plane (the maths of ProjectToPlaneY, with the light direction fetched once).
        const MyVector L = LightTravel();
        const float invLy = 1.0f / L.y;
        const Mat3 rs = rot.Scaled(scale);
        static MyVector wv[kMaxMeshVerts];
        static int order[kMaxMeshVerts];
        const float k = MyMath::Clamp(1.0f - 0.06f * MyMath::Max(0.0f, pos.y - planeY), 0.35f, 1.0f);
        const float cs = MyMath::Max(0.0f, (planeY - pos.y) * invLy), cx = pos.x + L.x * cs, cz = pos.z + L.z * cs;
        for (size_t i = 0; i < nv; ++i)
        {
            const MyVector p = rs.Mul(mesh.verts[i]) + pos;
            float s = (planeY - p.y) * invLy;
            if (s < 0.0f) s = 0.0f;
            wv[i] = MyVector(cx + (p.x + L.x * s - cx) * k, planeY + 0.004f, cz + (p.z + L.z * s - cz) * k);
            order[i] = static_cast<int>(i);
        }

        // Andrew's monotone chain on (x, z): sort by x then z, build the lower hull, then the upper hull.
        std::sort(order, order + nv, [](int a, int b) { return wv[a].x < wv[b].x || (wv[a].x == wv[b].x && wv[a].z < wv[b].z); });
        static int hull[kMaxMeshVerts + 1];
        int h = 0;
        auto cross = [](int o, int a, int b) { return (wv[a].x - wv[o].x) * (wv[b].z - wv[o].z) - (wv[a].z - wv[o].z) * (wv[b].x - wv[o].x); };
        for (size_t i = 0; i < nv; ++i)
        {
            while (h >= 2 && cross(hull[h - 2], hull[h - 1], order[i]) <= 0.0f) --h;
            hull[h++] = order[i];
        }
        const int lowerEnd = h + 1;
        for (int i = static_cast<int>(nv) - 2; i >= 0; --i)
        {
            while (h >= lowerEnd && cross(hull[h - 2], hull[h - 1], order[i]) <= 0.0f) --h;
            hull[h++] = order[i];
        }
        --h;                                                                 // the last point repeats the first
        if (h < 3) return;

        // A convex hull can be cut into fans that share hull[0]; each fan stays within the polygon size limit.
        static MyVector poly[kMaxPolyVerts];
        int start = 1;
        while (start < h - 1)
        {
            int n = 0;
            poly[n++] = wv[hull[0]];
            const int stop = (std::min)(h, start + (kMaxPolyVerts - 1));
            for (int i = start; i < stop; ++i) poly[n++] = wv[hull[i]];
            if (n >= 3) SubmitWorldPoly(poly, n, color, depth, bias);
            start = stop - 1;                                                // the next fan begins at this fan's last point
        }
    }

    void MyVectorRender::SubmitFloor(const Rect2& f, const MyColor& floorCol, const MyColor& gridCol, float spacing)
    {
        if (spacing <= 0.0f) return;
        const float m = 4.0f;                                                // draw a little beyond the playfield
        const MyVector quad[4] = { MyVector(f.x0 - m, 0, f.y0 - m), MyVector(f.x1 + m, 0, f.y0 - m),
                                   MyVector(f.x1 + m, 0, f.y1 + m), MyVector(f.x0 - m, 0, f.y1 + m) };
        SubmitWorldPoly(quad, 4, floorCol, kDepthFloor);
        const float t = 0.035f;
        for (float x = std::ceil(f.x0 / spacing) * spacing; x <= f.x1; x += spacing)
        {
            const MyVector q[4] = { MyVector(x - t, 0.002f, f.y0), MyVector(x + t, 0.002f, f.y0), MyVector(x + t, 0.002f, f.y1), MyVector(x - t, 0.002f, f.y1) };
            SubmitWorldPoly(q, 4, gridCol, kDepthGrid);
        }
        for (float z = std::ceil(f.y0 / spacing) * spacing; z <= f.y1; z += spacing)
        {
            const MyVector q[4] = { MyVector(f.x0, 0.002f, z - t), MyVector(f.x1, 0.002f, z - t), MyVector(f.x1, 0.002f, z + t), MyVector(f.x0, 0.002f, z + t) };
            SubmitWorldPoly(q, 4, gridCol, kDepthGrid);
        }
    }

    //==============================================================================================
    // Explosions
    //==============================================================================================
    void MyVectorRender::SubmitFx(const MyExplosionFX& fx, const MyBlastStyles& bs, const FxMeshes& meshes)
    {
        if (!m_vq.fx || !meshes.puff || !meshes.cube || !meshes.octa) return;     // the Video tab's "Vector Effects" switch
        const Mesh& puff = *meshes.puff;
        const Mesh& cube = *meshes.cube;
        const Mesh& octa = *meshes.octa;
        static float cs[25], sn[25];                                         // the 24-segment unit circle, built once
        static bool init = false;
        if (!init) { for (int i = 0; i <= 24; ++i) { const float a = static_cast<float>(i) * MyMath::kTwoPi / 24.0f; cs[i] = std::cos(a); sn[i] = std::sin(a); } init = true; }
        const Mat3 ident = Mat3::FromQuat(MyQuaternion::Identity());
        const int stride = m_vq.ringStride;                                  // 1, 2 or 3: 24, 12 or 8 segments (all divide 24)

        // ---- the signature blast styles --------------------------------------------------------------------------------------------
        for (size_t i = 0; i < bs.Capacity(); ++i)
        {
            if (m_polys.size() >= kFxPolyBudget) return;                     // keep headroom for the ships and shots drawn around the blast
            const MyBlastParticle& p = bs.At(i);
            if (p.life <= 0.0f || p.delay > 0.0f) continue;
            const int aq = static_cast<int>(MyBlastStyles::Alpha(p) * 7.0f + 0.5f);   // opacity in 7 steps so neighbouring particles share a colour and batch
            if (aq <= 0) continue;
            const float k = static_cast<float>(aq) / 7.0f;
            const float prog = MyBlastStyles::Progress(p);
            const uint8_t A = static_cast<uint8_t>(36 * aq);
            const MyColor col(static_cast<uint8_t>(p.r * k), static_cast<uint8_t>(p.g * k), static_cast<uint8_t>(p.b * k), A);
            switch (p.type)
            {
            case MyBlastType::Ember:                                        // a glowing bead that shrinks as it cools
            {
                const float e = p.size * (0.4f + 0.6f * (1.0f - prog));
                SubmitMesh(puff, ident, MyVector(e, e, e), p.pos, col);
                break;
            }
            case MyBlastType::Shard:                                        // a tumbling plate (flat < 1) or a cube; Pixelate shards snap to a grid and blink out
            {
                MyVector pos = p.pos;
                if (p.thick > 0.0f)
                {
                    if (prog > 0.6f && (static_cast<int>(p.life * 24.0f) & 1)) break;
                    pos = MyVector(std::floor(pos.x / p.thick) * p.thick, std::floor(pos.y / p.thick) * p.thick, std::floor(pos.z / p.thick) * p.thick);
                }
                const float e = p.size * (0.5f + 0.5f * (1.0f - prog));
                SubmitMesh(cube, Mat3::FromQuat(p.orient), MyVector(e, e * p.flat, e), pos, MyColor(p.r, p.g, p.b, 255));
                break;
            }
            case MyBlastType::Bolt:                                         // a streak along its direction; lightning links flicker
            {
                if (p.thick > 0.0f && p.vel.SqrMagnitude() < 1.0e-4f && (static_cast<int>(p.life * 60.0f) & 1)) break;
                const float len = p.size * (0.6f + 0.4f * (1.0f - prog));
                const MyQuaternion q = MyQuaternion::FromToRotation(MyVector::Right(), p.dir);
                SubmitMesh(octa, Mat3::FromQuat(q), MyVector(len, p.thick, p.thick), p.pos, col);
                break;
            }
            case MyBlastType::Orb:                                          // a translucent glowing shell
            {
                const float R = MyBlastStyles::Radius(p);
                SubmitMesh(puff, ident, MyVector(R, R, R), p.pos, MyColor(p.r, p.g, p.b, static_cast<uint8_t>(18 * aq)));
                break;
            }
            default:                                                        // Ring: thin quads round a circle in the plane of p.dir
            {
                const float rr = MyBlastStyles::Radius(p);
                if (rr < 0.05f) break;
                MyVector u = p.dir.Cross(MyVector::Up());
                if (u.SqrMagnitude() < 1.0e-4f) u = p.dir.Cross(MyVector::Right());
                u = u.Normalized();
                const MyVector v = p.dir.Cross(u);
                const float th = p.thick * (1.0f - 0.5f * prog);
                const float ri = MyMath::Max(0.0f, rr - th), ro = rr + th;
                for (int s = 0; s < 24; s += stride)
                {
                    const int s1 = s + stride;
                    const MyVector q[4] = { p.pos + u * (ri * cs[s]) + v * (ri * sn[s]), p.pos + u * (ro * cs[s]) + v * (ro * sn[s]),
                                            p.pos + u * (ro * cs[s1]) + v * (ro * sn[s1]), p.pos + u * (ri * cs[s1]) + v * (ri * sn[s1]) };
                    SubmitWorldPoly(q, 4, col, 0.0f, 0.0f);
                }
                break;
            }
            }
        }

        // ---- the explosion family: shock-waves, fireballs, sparks, smoke, flak ---------------------------------------------------------
        for (size_t i = 0; i < fx.Capacity(); ++i)
        {
            if (m_polys.size() >= kFxPolyBudget) return;
            const MyExpParticle& p = fx.At(i);
            if (p.life <= 0.0f) continue;
            const float prog = MyExplosionFX::Progress(p);
            const int aq = static_cast<int>(MyExplosionFX::Alpha(p) * 7.0f + 0.5f);
            if (aq <= 0) continue;
            const float k = static_cast<float>(aq) / 7.0f;                   // fades brightness as well as opacity (it works on any background)
            const uint8_t A = static_cast<uint8_t>(36 * aq);
            const float R = MyExplosionFX::Radius(p);
            switch (p.type)
            {
            case MyExpType::Shockwave:
            case MyExpType::Implosion:                                      // rings: thin quads round a circle in the plane of p.normal
            {
                MyVector u = p.normal.Cross(MyVector::Up());
                if (u.SqrMagnitude() < 1.0e-4f) u = p.normal.Cross(MyVector::Right());
                u = u.Normalized();
                const MyVector v = p.normal.Cross(u);
                for (int rp = 0; rp < p.ripples; ++rp)                      // the second ring ripples along behind the first, thinner and fainter
                {
                    const float rr = R * (1.0f - 0.17f * static_cast<float>(rp));
                    if (rr < 0.05f) continue;
                    const float th = p.thickness * (1.0f - 0.55f * prog) * (1.0f - 0.35f * static_cast<float>(rp));
                    const float rk = k * (1.0f - 0.45f * static_cast<float>(rp));
                    const MyColor col(static_cast<uint8_t>(p.r * rk), static_cast<uint8_t>(p.g * rk), static_cast<uint8_t>(p.b * rk), A);
                    const float ri = MyMath::Max(0.0f, rr - th), ro = rr + th;
                    for (int s = 0; s < 24; s += stride)
                    {
                        const int s1 = s + stride;
                        const MyVector q[4] = { p.pos + u * (ri * cs[s]) + v * (ri * sn[s]), p.pos + u * (ro * cs[s]) + v * (ro * sn[s]),
                                                p.pos + u * (ro * cs[s1]) + v * (ro * sn[s1]), p.pos + u * (ri * cs[s1]) + v * (ri * sn[s1]) };
                        SubmitWorldPoly(q, 4, col, 0.0f, 0.0f);
                    }
                }
                break;
            }
            case MyExpType::Fireball:                                       // white-hot -> the blast colour -> dark red as it cools
            {
                const float hr = 255.0f, hg = 248.0f, hb = 215.0f, cr = 110.0f, cg = 28.0f, cb = 18.0f;
                float fr, fg, fb;
                if (prog < 0.35f) { const float f = prog / 0.35f;           fr = hr + (p.r - hr) * f; fg = hg + (p.g - hg) * f; fb = hb + (p.b - hb) * f; }
                else              { const float f = (prog - 0.35f) / 0.65f; fr = p.r + (cr - p.r) * f; fg = p.g + (cg - p.g) * f; fb = p.b + (cb - p.b) * f; }
                SubmitMesh(puff, ident, MyVector(R, R, R), p.pos, MyColor(static_cast<uint8_t>(fr * k), static_cast<uint8_t>(fg * k), static_cast<uint8_t>(fb * k), A));
                break;
            }
            case MyExpType::Sparks:                                         // a streak along the direction of travel, shrinking as it dies
            {
                if (p.vel.SqrMagnitude() < 1.0e-4f) break;
                const float len = p.size * (0.5f + 0.5f * (1.0f - prog));
                const MyQuaternion q = MyQuaternion::FromToRotation(MyVector::Right(), p.vel.Normalized());
                SubmitMesh(octa, Mat3::FromQuat(q), MyVector(len, len * 0.12f, len * 0.12f), p.pos, MyColor(static_cast<uint8_t>(p.r * k), static_cast<uint8_t>(p.g * k), static_cast<uint8_t>(p.b * k), A));
                break;
            }
            case MyExpType::Smoke:
                SubmitMesh(puff, ident, MyVector(R, R, R), p.pos, MyColor(static_cast<uint8_t>(p.r * k), static_cast<uint8_t>(p.g * k), static_cast<uint8_t>(p.b * k), A));
                break;
            default:                                                        // flak: a near-black puff with a red core that dies first
                SubmitMesh(puff, ident, MyVector(R, R, R), p.pos, MyColor(static_cast<uint8_t>(34 * k), static_cast<uint8_t>(30 * k), static_cast<uint8_t>(36 * k), A));
                if (prog < 0.5f) { const float c = R * 0.55f, f = 1.0f - 2.0f * prog; SubmitMesh(puff, ident, MyVector(c, c, c), p.pos, MyColor(static_cast<uint8_t>(255 * f), static_cast<uint8_t>(90 * f), static_cast<uint8_t>(40 * f), 255)); }
                break;
            }
        }
    }

    void MyVectorRender::SubmitCubeBooms(const MyCubeExplosion& boom, const Mesh& cube, float shadowY)
    {
        for (size_t i = 0; i < boom.Capacity(); ++i)
        {
            if (m_polys.size() >= kFxCubePolyBudget) break;
            const MyCubeParticle& p = boom.At(i);
            if (p.life <= 0.0f) continue;
            const float s = p.size * (0.35f + 0.65f * MyCubeExplosion::Fade(p));      // shrink as the debris dies
            const MyVector scale(s, s, s);
            const Mat3 rot = Mat3::FromQuat(p.orient);
            if (shadowY > -1.0e8f)
                SubmitShadow(cube, rot, scale, p.pos, shadowY, MyColor(10, 12, 24, 255), kDepthShadow);
            SubmitMesh(cube, rot, scale, p.pos, MyColor(p.r, p.g, p.b, 255));
        }
    }

    //==============================================================================================
    // Quality governor
    //==============================================================================================
    void MyVectorRender::UpdateQuality(const QualitySettings& c, float dt, bool resetScene)
    {
        const float automaticDetail = m_autoQuality.Update(dt, c.targetFps, c.quality == 0, resetScene);
        float detail;
        switch (c.quality)
        {
        case 1:  detail = 0.25f; break;
        case 2:  detail = 0.60f; break;
        case 3:  detail = 1.00f; break;
        default: detail = automaticDetail; break;
        }

        const float userDist = MyMath::Clamp(c.detailDist, 0.5f, 2.0f);
        m_vq.detail     = detail;
        if (resetScene || m_lodDetail < 0.0f || MyMath::Abs(detail - m_lodDetail) >= 0.2f || detail >= 0.999f)
            m_lodDetail = detail;
        m_vq.lodMul     = (1.0f + (1.0f - m_lodDetail) * 2.5f) / userDist;                    // 1 at full detail; 3.5 at the minimum; the user's Detail Distance divides it
        // Hysteresis, and only the lowest levels drop shadows: off below 0.25 (the fixed Low level and Auto's bottom steps), back on above 0.35.  Auto moves the detail in steps of
        // 0.1 / 0.05 around its target; a hard threshold made every shadow pop on and off each time it crossed, and higher thresholds kept them off for minutes in a busy scene.
        if (detail < 0.25f) m_shadowsOn = false; else if (detail > 0.35f) m_shadowsOn = true;
        m_vq.shadows    = c.shadows && m_shadowsOn;
        m_vq.fx         = c.effects;
        m_vq.ringStride = (detail > 0.75f) ? 1 : ((detail > 0.45f) ? 2 : 3);
        m_vq.particles  = MyMath::Clamp(c.particles, 0.25f, 1.0f) * (0.35f + 0.65f * detail);
    }

    void MyVectorRender::UpdateQualityFromConfig(float dt, bool resetScene)
    {
        const MyConfig& c = config.myConfig;
        QualitySettings s;
        s.quality    = c.vectorQuality;
        s.targetFps  = c.vectorTargetFps;
        s.detailDist = static_cast<float>(c.vectorDetailDist);          // long double in the config; plain float in the hot paths
        s.shadows    = c.vectorShadows;
        s.particles  = static_cast<float>(c.vectorParticles);
        s.effects    = c.vectorEffects;
        UpdateQuality(s, dt, resetScene);
    }

    void MyVectorRender::ApplyParticleDensity(MyCubeExplosion& boom, MyExplosionFX& fx, MyBlastStyles& bs) const
    {
        boom.SetDensity(m_vq.particles);                                 // cheap, so it is simply set every frame
        fx.SetDensity(m_vq.particles);
        bs.SetDensity(m_vq.particles);
    }

    // Hue wheel built from ONE table lookup: cos(a - 120) and cos(a - 240) are rotations of (cos a, sin a).
    MyColor HueColor(float hue01, float value)
    {
        float s, c;
        FAST_SINCOS_DEG(hue01 * 360.0f, s, c);
        const float r = 0.5f + 0.5f * c;
        const float g = 0.5f + 0.5f * (-0.5f * c + 0.8660254f * s);
        const float b = 0.5f + 0.5f * (-0.5f * c - 0.8660254f * s);
        return MyColor(static_cast<uint8_t>(255.0f * r * value), static_cast<uint8_t>(255.0f * g * value),
                       static_cast<uint8_t>(255.0f * b * value), 255);
    }
}

//==================================================================================================
// MyVectorText
//==================================================================================================
namespace MyVectorText
{
    float SinW(float a) { return FAST_SIN(MyMath::Repeat(a, MyMath::kTwoPi)); }       // table sine with an O(1) wrap (animations never stop)

    void TextF(Renderer& r, float x, float y, const MyColor& col, float size, const wchar_t* fmt, ...)
    {
        wchar_t buf[256];
        va_list args;
        va_start(args, fmt);
        std::vswprintf(buf, sizeof(buf) / sizeof(buf[0]), fmt, args);   // truncates (and terminates) when the text is too long
        va_end(args);
        buf[(sizeof(buf) / sizeof(buf[0])) - 1] = L'\0';
        r.DrawMyText(std::wstring(buf), Vector2(x, y), col, size);
    }

    // The 7-segment layout shared by DrawNumber and the digit glyphs: A=1 B=2 C=4 D=8 E=16 F=32 G=64.
    static const uint8_t kSevenMask[10] = { 63, 6, 91, 79, 102, 109, 125, 7, 127, 111 };
    static const float   kSevenSeg[7][4] = { {-0.3f, 0.5f, 0.3f, 0.5f}, {0.3f, 0.5f, 0.3f, 0.0f}, {0.3f, 0.0f, 0.3f, -0.5f},
                                             {-0.3f, -0.5f, 0.3f, -0.5f}, {-0.3f, 0.0f, -0.3f, -0.5f}, {-0.3f, 0.5f, -0.3f, 0.0f},
                                             {-0.3f, 0.0f, 0.3f, 0.0f} };

    // Symbols and punctuation: ! @ # $ % ^ & * ( ) _ + = - < > ; : ' " , . / ? ` ~  (lower case letters map to the capitals).  Dots are short strokes so every consumer of the font draws them.
    static const Glyph* GetSymbolGlyph(wchar_t c)
    {
        switch (c)
        {
        case L'!': { static const Glyph g = { 2, { {0.0f, 0.5f, 0.0f, -0.15f}, {0.0f, -0.42f, 0.0f, -0.5f} } }; return &g; }
        case L'@': { static const Glyph g = { 12, { {0.3f, -0.15f, 0.3f, -0.25f}, {0.3f, -0.25f, 0.15f, -0.5f}, {0.15f, -0.5f, -0.15f, -0.5f}, {-0.15f, -0.5f, -0.3f, -0.3f}, {-0.3f, -0.3f, -0.3f, 0.3f}, {-0.3f, 0.3f, -0.15f, 0.5f}, {-0.15f, 0.5f, 0.15f, 0.5f}, {0.15f, 0.5f, 0.3f, 0.3f}, {-0.1f, 0.15f, 0.1f, 0.15f}, {0.1f, 0.15f, 0.1f, -0.15f}, {0.1f, -0.15f, -0.1f, -0.15f}, {-0.1f, -0.15f, -0.1f, 0.15f} } }; return &g; }
        case L'#': { static const Glyph g = { 4, { {-0.08f, 0.5f, -0.16f, -0.5f}, {0.16f, 0.5f, 0.08f, -0.5f}, {-0.3f, 0.18f, 0.3f, 0.18f}, {-0.3f, -0.18f, 0.3f, -0.18f} } }; return &g; }
        case L'$': { static const Glyph g = { 7, { {0.3f, 0.5f, -0.3f, 0.5f}, {-0.3f, 0.5f, -0.3f, 0.0f}, {-0.3f, 0.0f, 0.3f, 0.0f}, {0.3f, 0.0f, 0.3f, -0.5f}, {0.3f, -0.5f, -0.3f, -0.5f}, {0.0f, 0.62f, 0.0f, 0.5f}, {0.0f, -0.5f, 0.0f, -0.62f} } }; return &g; }
        case L'%': { static const Glyph g = { 9, { {-0.3f, 0.5f, -0.08f, 0.5f}, {-0.08f, 0.5f, -0.08f, 0.14f}, {-0.08f, 0.14f, -0.3f, 0.14f}, {-0.3f, 0.14f, -0.3f, 0.5f}, {0.08f, -0.14f, 0.3f, -0.14f}, {0.3f, -0.14f, 0.3f, -0.5f}, {0.3f, -0.5f, 0.08f, -0.5f}, {0.08f, -0.5f, 0.08f, -0.14f}, {-0.3f, -0.5f, 0.3f, 0.5f} } }; return &g; }
        case L'^': { static const Glyph g = { 2, { {-0.25f, 0.0f, 0.0f, 0.5f}, {0.0f, 0.5f, 0.25f, 0.0f} } }; return &g; }
        case L'&': { static const Glyph g = { 8, { {0.0f, 0.5f, -0.2f, 0.35f}, {-0.2f, 0.35f, -0.2f, 0.15f}, {-0.2f, 0.15f, 0.3f, -0.5f}, {0.0f, 0.5f, 0.2f, 0.35f}, {0.2f, 0.35f, -0.3f, -0.3f}, {-0.3f, -0.3f, -0.15f, -0.5f}, {-0.15f, -0.5f, 0.1f, -0.5f}, {0.1f, -0.5f, 0.3f, -0.15f} } }; return &g; }
        case L'*': { static const Glyph g = { 3, { {0.0f, 0.3f, 0.0f, -0.3f}, {-0.25f, 0.15f, 0.25f, -0.15f}, {-0.25f, -0.15f, 0.25f, 0.15f} } }; return &g; }
        case L'(': { static const Glyph g = { 3, { {0.1f, 0.5f, -0.1f, 0.3f}, {-0.1f, 0.3f, -0.1f, -0.3f}, {-0.1f, -0.3f, 0.1f, -0.5f} } }; return &g; }
        case L')': { static const Glyph g = { 3, { {-0.1f, 0.5f, 0.1f, 0.3f}, {0.1f, 0.3f, 0.1f, -0.3f}, {0.1f, -0.3f, -0.1f, -0.5f} } }; return &g; }
        case L'_': { static const Glyph g = { 1, { {-0.3f, -0.5f, 0.3f, -0.5f} } }; return &g; }
        case L'+': { static const Glyph g = { 2, { {-0.25f, 0.0f, 0.25f, 0.0f}, {0.0f, 0.25f, 0.0f, -0.25f} } }; return &g; }
        case L'=': { static const Glyph g = { 2, { {-0.25f, 0.12f, 0.25f, 0.12f}, {-0.25f, -0.12f, 0.25f, -0.12f} } }; return &g; }
        case L'<': { static const Glyph g = { 2, { {0.2f, 0.35f, -0.2f, 0.0f}, {-0.2f, 0.0f, 0.2f, -0.35f} } }; return &g; }
        case L'>': { static const Glyph g = { 2, { {-0.2f, 0.35f, 0.2f, 0.0f}, {0.2f, 0.0f, -0.2f, -0.35f} } }; return &g; }
        case L';': { static const Glyph g = { 3, { {0.0f, 0.2f, 0.0f, 0.12f}, {0.0f, -0.15f, 0.0f, -0.3f}, {0.0f, -0.3f, -0.08f, -0.45f} } }; return &g; }
        case L'\'': { static const Glyph g = { 1, { {0.0f, 0.5f, 0.0f, 0.25f} } }; return &g; }
        case L'\"': { static const Glyph g = { 2, { {-0.1f, 0.5f, -0.1f, 0.25f}, {0.1f, 0.5f, 0.1f, 0.25f} } }; return &g; }
        case L',': { static const Glyph g = { 2, { {0.0f, -0.4f, 0.0f, -0.5f}, {0.0f, -0.5f, -0.08f, -0.62f} } }; return &g; }
        case L'.': { static const Glyph g = { 1, { {0.0f, -0.42f, 0.0f, -0.5f} } }; return &g; }
        case L'/': { static const Glyph g = { 1, { {-0.25f, -0.5f, 0.25f, 0.5f} } }; return &g; }
        case L'?': { static const Glyph g = { 7, { {-0.3f, 0.3f, -0.15f, 0.5f}, {-0.15f, 0.5f, 0.15f, 0.5f}, {0.15f, 0.5f, 0.3f, 0.3f}, {0.3f, 0.3f, 0.3f, 0.15f}, {0.3f, 0.15f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, -0.15f}, {0.0f, -0.42f, 0.0f, -0.5f} } }; return &g; }
        case L'`': { static const Glyph g = { 1, { {-0.1f, 0.5f, 0.05f, 0.3f} } }; return &g; }
        case L'~': { static const Glyph g = { 3, { {-0.3f, -0.05f, -0.15f, 0.1f}, {-0.15f, 0.1f, 0.15f, -0.1f}, {0.15f, -0.1f, 0.3f, 0.05f} } }; return &g; }
        default: return nullptr;
        }
    }
    const Glyph* GetGlyph(wchar_t c)
    {
        if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - 32);          // lower case draws as the capital
        static const Glyph G = { 5, { {-0.3f, 0.5f, 0.3f, 0.5f}, {-0.3f, 0.5f, -0.3f, -0.5f}, {-0.3f, -0.5f, 0.3f, -0.5f}, {0.3f, -0.5f, 0.3f, 0.0f}, {0.3f, 0.0f, 0.0f, 0.0f} } };
        static const Glyph A = { 3, { {-0.3f, -0.5f, 0.0f, 0.5f}, {0.0f, 0.5f, 0.3f, -0.5f}, {-0.15f, -0.1f, 0.15f, -0.1f} } };
        static const Glyph M = { 4, { {-0.3f, -0.5f, -0.3f, 0.5f}, {-0.3f, 0.5f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.3f, 0.5f}, {0.3f, 0.5f, 0.3f, -0.5f} } };
        static const Glyph E = { 4, { {-0.3f, 0.5f, 0.3f, 0.5f}, {-0.3f, 0.5f, -0.3f, -0.5f}, {-0.3f, 0.0f, 0.2f, 0.0f}, {-0.3f, -0.5f, 0.3f, -0.5f} } };
        static const Glyph O = { 8, { {-0.3f, 0.3f, -0.15f, 0.5f}, {-0.15f, 0.5f, 0.15f, 0.5f}, {0.15f, 0.5f, 0.3f, 0.3f}, {0.3f, 0.3f, 0.3f, -0.3f},
                                      {0.3f, -0.3f, 0.15f, -0.5f}, {0.15f, -0.5f, -0.15f, -0.5f}, {-0.15f, -0.5f, -0.3f, -0.3f}, {-0.3f, -0.3f, -0.3f, 0.3f} } };
        static const Glyph D = { 6, { {-0.3f, -0.5f, -0.3f, 0.5f}, {-0.3f, 0.5f, 0.1f, 0.5f}, {0.1f, 0.5f, 0.3f, 0.25f}, {0.3f, 0.25f, 0.3f, -0.25f}, {0.3f, -0.25f, 0.1f, -0.5f}, {0.1f, -0.5f, -0.3f, -0.5f} } };
        static const Glyph W = { 4, { {-0.3f, 0.5f, -0.15f, -0.5f}, {-0.15f, -0.5f, 0.0f, 0.1f}, {0.0f, 0.1f, 0.15f, -0.5f}, {0.15f, -0.5f, 0.3f, 0.5f} } };
        if (c >= L'0' && c <= L'9')
        {
            static Glyph dg[10]; static bool built = false;                                    // built once from the 7-segment masks the score digits use
            if (!built)
            {
                for (int d = 0; d < 10; ++d)
                {
                    dg[d].n = 0;
                    for (int sg = 0; sg < 7; ++sg) if (kSevenMask[d] & (1 << sg)) { for (int k = 0; k < 4; ++k) dg[d].s[dg[d].n][k] = kSevenSeg[sg][k]; ++dg[d].n; }
                }
                built = true;
            }
            return &dg[c - L'0'];
        }
        static const Glyph V = { 2, { {-0.3f, 0.5f, 0.0f, -0.5f}, {0.0f, -0.5f, 0.3f, 0.5f} } };
        static const Glyph R = { 7, { {-0.3f, -0.5f, -0.3f, 0.5f}, {-0.3f, 0.5f, 0.2f, 0.5f}, {0.2f, 0.5f, 0.3f, 0.35f}, {0.3f, 0.35f, 0.3f, 0.15f},
                                      {0.3f, 0.15f, 0.2f, 0.0f}, {0.2f, 0.0f, -0.3f, 0.0f}, {0.0f, 0.0f, 0.3f, -0.5f} } };
        static const Glyph S = { 5, { {0.3f, 0.5f, -0.3f, 0.5f}, {-0.3f, 0.5f, -0.3f, 0.0f}, {-0.3f, 0.0f, 0.3f, 0.0f}, {0.3f, 0.0f, 0.3f, -0.5f}, {0.3f, -0.5f, -0.3f, -0.5f} } };
        static const Glyph H = { 3, { {-0.3f, -0.5f, -0.3f, 0.5f}, {0.3f, -0.5f, 0.3f, 0.5f}, {-0.3f, 0.0f, 0.3f, 0.0f} } };
        static const Glyph I = { 3, { {-0.2f, 0.5f, 0.2f, 0.5f}, {0.0f, 0.5f, 0.0f, -0.5f}, {-0.2f, -0.5f, 0.2f, -0.5f} } };
        static const Glyph P = { 4, { {-0.3f, -0.5f, -0.3f, 0.5f}, {-0.3f, 0.5f, 0.3f, 0.5f}, {0.3f, 0.5f, 0.3f, 0.0f}, {0.3f, 0.0f, -0.3f, 0.0f} } };
        static const Glyph N = { 3, { {-0.3f, -0.5f, -0.3f, 0.5f}, {-0.3f, 0.5f, 0.3f, -0.5f}, {0.3f, -0.5f, 0.3f, 0.5f} } };
        static const Glyph Colon = { 2, { {0.0f, 0.28f, 0.0f, 0.16f}, {0.0f, -0.16f, 0.0f, -0.28f} } };
        // The rest of the alphabet and a dash, so any name ("FLAT GRID", "CYLINDER", "TRIANGLE PRISM" ...) can be written in full.
        static const Glyph B = { 8, { {-0.3f, -0.5f, -0.3f, 0.5f}, {-0.3f, 0.5f, 0.2f, 0.5f}, {0.2f, 0.5f, 0.3f, 0.3f}, {0.3f, 0.3f, 0.2f, 0.0f},
                                      {0.2f, 0.0f, -0.3f, 0.0f}, {0.2f, 0.0f, 0.3f, -0.3f}, {0.3f, -0.3f, 0.2f, -0.5f}, {0.2f, -0.5f, -0.3f, -0.5f} } };
        static const Glyph C = { 3, { {0.3f, 0.5f, -0.3f, 0.5f}, {-0.3f, 0.5f, -0.3f, -0.5f}, {-0.3f, -0.5f, 0.3f, -0.5f} } };
        static const Glyph F = { 3, { {-0.3f, -0.5f, -0.3f, 0.5f}, {-0.3f, 0.5f, 0.3f, 0.5f}, {-0.3f, 0.0f, 0.2f, 0.0f} } };
        static const Glyph J = { 4, { {0.0f, 0.5f, 0.3f, 0.5f}, {0.15f, 0.5f, 0.15f, -0.3f}, {0.15f, -0.3f, 0.0f, -0.5f}, {0.0f, -0.5f, -0.3f, -0.3f} } };
        static const Glyph K = { 3, { {-0.3f, -0.5f, -0.3f, 0.5f}, {-0.3f, 0.0f, 0.3f, 0.5f}, {-0.1f, 0.15f, 0.3f, -0.5f} } };
        static const Glyph L = { 2, { {-0.3f, 0.5f, -0.3f, -0.5f}, {-0.3f, -0.5f, 0.3f, -0.5f} } };
        static const Glyph Q = { 9, { {-0.3f, 0.3f, -0.15f, 0.5f}, {-0.15f, 0.5f, 0.15f, 0.5f}, {0.15f, 0.5f, 0.3f, 0.3f}, {0.3f, 0.3f, 0.3f, -0.3f},
                                      {0.3f, -0.3f, 0.15f, -0.5f}, {0.15f, -0.5f, -0.15f, -0.5f}, {-0.15f, -0.5f, -0.3f, -0.3f}, {-0.3f, -0.3f, -0.3f, 0.3f}, {0.05f, -0.2f, 0.35f, -0.55f} } };
        static const Glyph T = { 2, { {-0.3f, 0.5f, 0.3f, 0.5f}, {0.0f, 0.5f, 0.0f, -0.5f} } };
        static const Glyph U = { 3, { {-0.3f, 0.5f, -0.3f, -0.5f}, {-0.3f, -0.5f, 0.3f, -0.5f}, {0.3f, -0.5f, 0.3f, 0.5f} } };
        static const Glyph X = { 2, { {-0.3f, 0.5f, 0.3f, -0.5f}, {-0.3f, -0.5f, 0.3f, 0.5f} } };
        static const Glyph Y = { 3, { {-0.3f, 0.5f, 0.0f, 0.0f}, {0.3f, 0.5f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, -0.5f} } };
        static const Glyph Z = { 3, { {-0.3f, 0.5f, 0.3f, 0.5f}, {0.3f, 0.5f, -0.3f, -0.5f}, {-0.3f, -0.5f, 0.3f, -0.5f} } };
        static const Glyph Dash = { 1, { {-0.2f, 0.0f, 0.2f, 0.0f} } };
        switch (c) { case L'S': return &S; case L'H': return &H; case L'I': return &I; case L'P': return &P; case L'N': return &N; case L':': return &Colon;
                     case L'G': return &G; case L'A': return &A; case L'M': return &M; case L'E': return &E; case L'O': return &O; case L'V': return &V; case L'D': return &D; case L'W': return &W; case L'R': return &R;
                     case L'B': return &B; case L'C': return &C; case L'F': return &F; case L'J': return &J; case L'K': return &K; case L'L': return &L; case L'Q': return &Q;
                     case L'T': return &T; case L'U': return &U; case L'X': return &X; case L'Y': return &Y; case L'Z': return &Z; case L'-': return &Dash; default: return GetSymbolGlyph(c); }
    }

    void DrawNumber(Renderer& r, int value, int digits, float x, float y, float h, const MyColor& col, float time, float pop)
    {
        const float hh = h * (1.0f + 0.35f * pop);                              // score "pop" when points are scored
        const float adv = hh * 0.85f, th = hh * 0.085f;
        int divisor = 1;
        for (int i = 0; i < digits - 1; ++i) divisor *= 10;
        bool leading = true;
        for (int i = 0; i < digits; ++i)
        {
            const int d = (value / divisor) % 10;
            divisor /= 10;
            if (d != 0 || i == digits - 1) leading = false;
            const MyColor c = leading ? Shade(col, 0.28f) : col;                // dim leading zeros
            float s, co;
            FAST_SINCOS_DEG(26.0f * FAST_SIN(time * 2.0f + i * 0.8f), s, co);   // each digit swings about its own vertical axis
            const float cx = x + i * adv;
            for (int sg = 0; sg < 7; ++sg)
            {
                if (!(kSevenMask[d] & (1 << sg))) continue;
                const float ax = kSevenSeg[sg][0], ay = kSevenSeg[sg][1], bx = kSevenSeg[sg][2], by = kSevenSeg[sg][3];
                const float pa = 1.0f / (1.0f + (-ax * s) * 0.9f);              // tiny perspective from the swing depth
                const float pb = 1.0f / (1.0f + (-bx * s) * 0.9f);
                const float x0 = cx + ax * co * hh * pa, y0 = y - ay * hh * pa;
                const float x1 = cx + bx * co * hh * pb, y1 = y - by * hh * pb;
                const float dx = x1 - x0, dy = y1 - y0;
                const float len = std::sqrt(dx * dx + dy * dy);
                if (len < 0.001f) continue;
                const float nx = -dy / len * th, ny = dx / len * th;            // stroke half-thickness, perpendicular
                const Vector2 quad[4] = { Vector2(x0 + nx, y0 + ny), Vector2(x1 + nx, y1 + ny), Vector2(x1 - nx, y1 - ny), Vector2(x0 - nx, y0 - ny) };
                r.DrawFilledPolygon(quad, 4, c);
            }
        }
    }

    void DrawBanner(Renderer& r, const wchar_t* text, float cx, float cy, float h, const MyColor& col, float time, float age)
    {
        const int n = static_cast<int>(std::wcslen(text));
        const float adv = h * 0.80f, x0 = cx - static_cast<float>(n - 1) * adv * 0.5f;
        const MyColor hot(static_cast<uint8_t>((std::min)(255, col.r + 110)), static_cast<uint8_t>((std::min)(255, col.g + 110)), static_cast<uint8_t>((std::min)(255, col.b + 110)), 255);
        for (int i = 0; i < n; ++i)
        {
            const Glyph* gl = GetGlyph(text[i]);
            if (!gl) continue;                                                  // spaces
            const float appear = MyMath::Clamp01((age - static_cast<float>(i) * 0.10f) / 0.45f);     // letters arrive one after another
            if (appear <= 0.0f) continue;
            const float ease = 1.0f - (1.0f - appear) * (1.0f - appear);
            const float lx = x0 + static_cast<float>(i) * adv;
            const float ly = cy + h * 0.07f * SinW(time * 3.2f + static_cast<float>(i) * 0.7f) - (1.0f - ease) * h * 2.5f;     // drops in, then bobs
            const float sc = (0.6f + 0.4f * ease) * (1.0f + 0.05f * SinW(time * 5.0f + static_cast<float>(i)));
            float s, co;
            FAST_SINCOS_DEG(30.0f * SinW(time * 1.7f + static_cast<float>(i) * 0.9f), s, co);                          // swing about the vertical axis
            const float flick = ((static_cast<int>(time * 14.0f) + i * 3) % 23 == 0) ? 0.55f : 1.0f;                   // phosphor flicker
            for (int pass = 0; pass < 3; ++pass)
            {
                const float th = h * (pass == 0 ? 0.080f : (pass == 1 ? 0.040f : 0.016f)) * sc;
                const MyColor c = (pass == 0) ? Shade(col, 0.30f * flick) : ((pass == 1) ? Shade(col, flick) : Shade(hot, flick));
                for (int k = 0; k < gl->n; ++k)
                {
                    const float ax = gl->s[k][0], ay = gl->s[k][1], bx = gl->s[k][2], by = gl->s[k][3];
                    const float pa = 1.0f / (1.0f + (-ax * s) * 0.9f), pb = 1.0f / (1.0f + (-bx * s) * 0.9f);
                    const float xa = lx + ax * co * h * sc * pa, ya = ly - ay * h * sc * pa;
                    const float xb = lx + bx * co * h * sc * pb, yb = ly - by * h * sc * pb;
                    const float dx = xb - xa, dy = yb - ya, len = std::sqrt(dx * dx + dy * dy);
                    if (len < 0.001f) continue;
                    const float nx = -dy / len * th, ny = dx / len * th;
                    const Vector2 quad[4] = { Vector2(xa + nx, ya + ny), Vector2(xb + nx, yb + ny), Vector2(xb - nx, yb - ny), Vector2(xa - nx, ya - ny) };
                    r.DrawFilledPolygon(quad, 4, c);
                }
            }
        }
    }
}

// ---- forwarders: the brightness / flat state is owned by the Light system (declared in VectorRender.h) ----
void  MyVR::MyVectorRender::SetMeshLight(float l) { lightsManager.SetVectorBrightness(l); }
float MyVR::MyVectorRender::GetMeshLight() const  { return lightsManager.GetVectorBrightness(); }
void  MyVR::MyVectorRender::SetMeshFlat(bool f)   { lightsManager.SetVectorFlat(f); }
bool  MyVR::MyVectorRender::GetMeshFlat() const   { return lightsManager.GetVectorFlat(); }
