// ================================================================================================
//  VectorShapes.cpp  -  implementation of the reusable mesh builders declared in VectorShapes.h
//
//  Every function builds geometry only (vertices, faces, normals, tones, fixed paint); nothing is drawn here and no game state is read.
//  All of it runs ONCE at start-up, so clarity wins over micro-optimisation, but nothing allocates more than the mesh itself needs.
//  See VectorShapes.h for the full list, grouped by purpose, and for usage notes.
// ================================================================================================
#include "Includes.h"                    // global platform / renderer definitions (must come first)
#include "VectorShapes.h"
#include "MathPrecalculation.h"        // FAST_UNIT_CIRCLE: precalculated unit circles so building a mesh needs no trig

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>

namespace MyShapes
{
    // Computes the bounding radius used for culling and level of detail.
    void FinalizeMesh(Mesh& m)
    {
        float r2 = 0.0f;
        for (const MyVector& v : m.verts) r2 = MyMath::Max(r2, v.SqrMagnitude());
        m.radius = std::sqrt(r2);
    }

    // Arrow along +X: tail cap, shaft, a step ring, then a cone head with its tip at x = 1.
    // The ring points come from the precalculated unit-circle table - no trig at build time.
    Mesh BuildArrowMesh()
    {
        Mesh m;
        const auto& ring = FAST_UNIT_CIRCLE(kArrowSegments);
        const int N = kArrowSegments;
        if (static_cast<int>(ring.size()) != N) return m;                    // table missing: empty mesh (nothing drawn)

        const float rs = 0.035f, rh = 0.09f, shaftEnd = 0.8f, headLen = 1.0f - shaftEnd;
        for (int i = 0; i < N; ++i) m.verts.push_back(MyVector(0.0f,     rs * ring[i].x, rs * ring[i].y));   // S0: shaft start
        for (int i = 0; i < N; ++i) m.verts.push_back(MyVector(shaftEnd, rs * ring[i].x, rs * ring[i].y));   // S1: shaft end
        for (int i = 0; i < N; ++i) m.verts.push_back(MyVector(shaftEnd, rh * ring[i].x, rh * ring[i].y));   // H : head base
        m.verts.push_back(MyVector(1.0f, 0.0f, 0.0f));                                                        // tip
        const int tip = 3 * N;

        for (int i = 0; i < N; ++i)
        {
            const int j = (i + 1) % N;
            const MyVector radial = MyVector(0.0f, ring[i].x + ring[j].x, ring[i].y + ring[j].y).Normalized();

            Face side;                                                       // shaft side quad
            side.idx = { i, j, N + j, N + i };
            side.normal = radial;
            m.faces.push_back(side);

            Face step;                                                       // flat ring joining shaft to head base
            step.idx = { N + i, N + j, 2 * N + j, 2 * N + i };
            step.normal = MyVector(-1.0f, 0.0f, 0.0f);
            m.faces.push_back(step);

            Face cone;                                                       // cone side triangle
            cone.idx = { 2 * N + i, 2 * N + j, tip };
            cone.normal = MyVector(rh, headLen * radial.y, headLen * radial.z).Normalized();   // perpendicular to the slant
            m.faces.push_back(cone);
        }

        Face cap;                                                            // tail cap (an N-gon facing -X)
        for (int i = 0; i < N; ++i) cap.idx.push_back(i);
        cap.normal = MyVector(-1.0f, 0.0f, 0.0f);
        m.faces.push_back(cap);
        return m;
    }

    Mesh BuildCubeMesh()
    {
        Mesh m;
        for (int i = 0; i < 8; ++i)                                          // bit0 = x, bit1 = y, bit2 = z
            m.verts.push_back(MyVector((i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f));
        struct Q { int a, b, c, d; float nx, ny, nz; };
        const Q quads[6] = { {1,3,7,5, 1,0,0}, {0,4,6,2, -1,0,0}, {2,6,7,3, 0,1,0},
                             {0,1,5,4, 0,-1,0}, {4,5,7,6, 0,0,1}, {0,2,3,1, 0,0,-1} };
        for (const Q& q : quads)
        {
            Face f;
            f.idx = { q.a, q.b, q.c, q.d };
            f.normal = MyVector(q.nx, q.ny, q.nz);
            m.faces.push_back(f);
        }
        return m;
    }

    Mesh BuildOctahedronMesh()                                       // 6 vertices on the axes, 8 triangular faces
    {
        Mesh m;
        m.verts = { MyVector(0.5f, 0, 0), MyVector(-0.5f, 0, 0), MyVector(0, 0.5f, 0),
                    MyVector(0, -0.5f, 0), MyVector(0, 0, 0.5f), MyVector(0, 0, -0.5f) };
        for (int sx = 0; sx < 2; ++sx)
            for (int sy = 0; sy < 2; ++sy)
                for (int sz = 0; sz < 2; ++sz)
                {
                    Face f;
                    f.idx = { sx, 2 + sy, 4 + sz };                          // one vertex from each axis pair
                    f.normal = MyVector(sx ? -1.0f : 1.0f, sy ? -1.0f : 1.0f, sz ? -1.0f : 1.0f).Normalized();
                    m.faces.push_back(f);
                }
        return m;
    }

    // Adds one convex face.  The normal comes from the first three vertices and is flipped to point away from
    // `interior`, so the builders below never need hand-written normals.
    void AddFaceAuto(Mesh& m, std::initializer_list<int> idx, const MyVector& interior, float tone,
                     bool fixedColor, const MyColor& color, bool glow)
    {
        Face f;
        f.idx.assign(idx.begin(), idx.end());
        MyVector centroid;
        for (int i : f.idx) centroid += m.verts[i];
        centroid = centroid / static_cast<float>(f.idx.size());
        MyVector n = (m.verts[f.idx[1]] - m.verts[f.idx[0]]).Cross(m.verts[f.idx[2]] - m.verts[f.idx[0]]).Normalized();
        if (n.Dot(centroid - interior) < 0.0f) n = -n;
        f.normal = n; f.tone = tone; f.fixedColor = fixedColor; f.color = color; f.glow = glow;
        m.faces.push_back(f);
    }

    // A proper little fighter, nose along +X, wings along +/-Z, up = +Y, about 1 unit long (x from -0.5 to +0.5):
    //   tapered 6-sided fuselage with a pointed nose, a glass canopy, swept wings, twin tail fins and two engine
    //   nozzles with glowing exhaust.  Hull panels take the instance (team) colour; glass, metal and glow are fixed.
    Mesh BuildShipMesh()
    {
        Mesh m;
        auto V = [&](float x, float y, float z) { m.verts.push_back(MyVector(x, y, z)); return static_cast<int>(m.verts.size()) - 1; };
        const MyVector core(0.0f, 0.0f, 0.0f);

        // ---- fuselage: nose point + three hexagonal stations (top, upper-right, lower-right, bottom, lower-left, upper-left)
        auto station = [&](float x, float hw, float ht, float hb, int out[6])
        {
            out[0] = V(x,  ht,          0.0f);
            out[1] = V(x,  ht * 0.5f,   hw);
            out[2] = V(x, -hb * 0.5f,   hw);
            out[3] = V(x, -hb,          0.0f);
            out[4] = V(x, -hb * 0.5f,  -hw);
            out[5] = V(x,  ht * 0.5f,  -hw);
        };
        const int nose = V(0.50f, -0.004f, 0.0f);
        int rb[6], rc[6], rd[6];
        station( 0.26f, 0.065f, 0.075f, 0.055f, rb);
        station(-0.12f, 0.100f, 0.100f, 0.070f, rc);
        station(-0.45f, 0.085f, 0.075f, 0.055f, rd);
        for (int i = 0; i < 6; ++i)
        {
            const int j = (i + 1) % 6;
            AddFaceAuto(m, { nose, rb[i], rb[j] },        core, 1.00f);              // nose cone
            AddFaceAuto(m, { rb[i], rb[j], rc[j], rc[i] }, core, (i == 0) ? 1.0f : 0.92f);   // forward body (top panels lightest)
            AddFaceAuto(m, { rc[i], rc[j], rd[j], rd[i] }, core, 0.86f);             // rear body
        }
        AddFaceAuto(m, { rd[0], rd[1], rd[2], rd[3], rd[4], rd[5] }, core, 0.5f, true, MyColor(60, 62, 74, 255));   // tail plate

        // ---- cockpit canopy: a glass lens sitting on top of the fuselage
        {
            const int F = V(0.32f, 0.078f, 0.0f), L = V(0.17f, 0.095f, 0.055f), R = V(0.00f, 0.095f, 0.0f),
                      Rt = V(0.17f, 0.095f, -0.055f), T = V(0.17f, 0.160f, 0.0f);
            const MyVector inside(0.17f, 0.080f, 0.0f);
            const MyColor glass(95, 185, 255, 255);
            AddFaceAuto(m, { T, F, L },   inside, 1.0f, true, glass);
            AddFaceAuto(m, { T, L, R },   inside, 1.0f, true, glass);
            AddFaceAuto(m, { T, R, Rt },  inside, 1.0f, true, glass);
            AddFaceAuto(m, { T, Rt, F },  inside, 1.0f, true, glass);
        }

        // ---- swept wings (thin slabs); leading edge, tip and trailing edge get faces, the root hides in the hull
        auto wing = [&](float s)
        {
            const float wx[4] = { 0.12f, -0.22f, -0.36f, -0.38f }, wz[4] = { 0.07f, 0.44f, 0.44f, 0.10f };
            const float y = -0.012f, t = 0.012f;
            int tp[4], bt[4];
            for (int i = 0; i < 4; ++i) { tp[i] = V(wx[i], y + t, s * wz[i]); bt[i] = V(wx[i], y - t, s * wz[i]); }
            const MyVector c(-0.12f, y, s * 0.25f);
            AddFaceAuto(m, { tp[0], tp[1], tp[2], tp[3] }, c, 0.80f);                 // upper surface
            AddFaceAuto(m, { bt[0], bt[1], bt[2], bt[3] }, c, 0.55f);                 // underside
            for (int i = 0; i < 3; ++i) { const int j = i + 1; AddFaceAuto(m, { tp[i], tp[j], bt[j], bt[i] }, c, 0.70f); }
        };
        wing(1.0f); wing(-1.0f);

        // ---- twin vertical tail fins
        auto fin = [&](float zc)
        {
            const float px[4] = { -0.16f, -0.42f, -0.44f, -0.31f }, py[4] = { 0.08f, 0.08f, 0.27f, 0.27f };
            const float t = 0.012f;
            int fr[4], bk[4];
            for (int i = 0; i < 4; ++i) { fr[i] = V(px[i], py[i], zc + t); bk[i] = V(px[i], py[i], zc - t); }
            const MyVector c(-0.30f, 0.17f, zc);
            AddFaceAuto(m, { fr[0], fr[1], fr[2], fr[3] }, c, 0.90f);
            AddFaceAuto(m, { bk[0], bk[1], bk[2], bk[3] }, c, 0.90f);
            for (int i = 1; i < 4; ++i) { const int j = (i + 1) % 4; AddFaceAuto(m, { fr[i], fr[j], bk[j], bk[i] }, c, 0.85f); }
        };
        fin(0.075f); fin(-0.075f);

        // ---- engine nozzles: dark metal boxes whose rear face glows
        auto engine = [&](float cz)
        {
            const float cx = -0.46f, cy = -0.005f, hx = 0.035f, hy = 0.030f, hz = 0.030f;
            int v[8];
            for (int i = 0; i < 8; ++i) v[i] = V(cx + ((i & 1) ? hx : -hx), cy + ((i & 2) ? hy : -hy), cz + ((i & 4) ? hz : -hz));
            const MyVector c(cx, cy, cz);
            const MyColor metal(72, 74, 88, 255), fire(255, 150, 50, 255);
            AddFaceAuto(m, { v[1], v[3], v[7], v[5] }, c, 1.0f, true, metal);
            AddFaceAuto(m, { v[0], v[4], v[6], v[2] }, c, 1.0f, true, fire, true);   // rear: glowing exhaust port
            AddFaceAuto(m, { v[2], v[6], v[7], v[3] }, c, 1.0f, true, metal);
            AddFaceAuto(m, { v[0], v[1], v[5], v[4] }, c, 1.0f, true, metal);
            AddFaceAuto(m, { v[4], v[5], v[7], v[6] }, c, 1.0f, true, metal);
            AddFaceAuto(m, { v[0], v[2], v[3], v[1] }, c, 1.0f, true, metal);
        };
        engine(0.045f); engine(-0.045f);
        return m;
    }

    // Exhaust flame: a self-lit four-sided spike. The base is at x = 0 and the tip trails away along -X.
    Mesh BuildFlameMesh()
    {
        Mesh m;
        m.verts = { MyVector(0.0f, 0.5f, 0.0f), MyVector(0.0f, 0.0f, 0.5f), MyVector(0.0f, -0.5f, 0.0f), MyVector(0.0f, 0.0f, -0.5f), MyVector(-1.0f, 0.0f, 0.0f) };
        const MyColor fire(255, 175, 60, 255);
        for (int i = 0; i < 4; ++i) AddFaceAuto(m, { i, (i + 1) % 4, 4 }, MyVector(-0.3f, 0.0f, 0.0f), 1.0f, true, fire, true);
        return m;
    }

    // Face from an index vector (for polygons with many vertices); same normal rule as AddFaceAuto.
    void AddFaceVec(Mesh& m, const std::vector<int>& idx, const MyVector& interior, float tone,
                    bool fixedColor, const MyColor& color, bool glow)
    {
        Face f;
        f.idx = idx;
        MyVector centroid;
        for (int i : idx) centroid += m.verts[i];
        centroid = centroid / static_cast<float>(idx.size());
        MyVector n = (m.verts[idx[1]] - m.verts[idx[0]]).Cross(m.verts[idx[2]] - m.verts[idx[0]]).Normalized();
        if (n.Dot(centroid - interior) < 0.0f) n = -n;
        f.normal = n; f.tone = tone; f.fixedColor = fixedColor; f.color = color; f.glow = glow;
        m.faces.push_back(f);
    }

    // Axis-aligned box centred at c with half-extents h (six faces).
    void AddBoxFaces(Mesh& m, const MyVector& c, const MyVector& h, float tone, bool fixedColor, const MyColor& color)
    {
        int v[8];
        for (int i = 0; i < 8; ++i)
        {
            m.verts.push_back(MyVector(c.x + ((i & 1) ? h.x : -h.x), c.y + ((i & 2) ? h.y : -h.y), c.z + ((i & 4) ? h.z : -h.z)));
            v[i] = static_cast<int>(m.verts.size()) - 1;
        }
        AddFaceAuto(m, { v[1], v[3], v[7], v[5] }, c, tone, fixedColor, color);
        AddFaceAuto(m, { v[0], v[4], v[6], v[2] }, c, tone, fixedColor, color);
        AddFaceAuto(m, { v[2], v[6], v[7], v[3] }, c, tone, fixedColor, color);
        AddFaceAuto(m, { v[0], v[1], v[5], v[4] }, c, tone, fixedColor, color);
        AddFaceAuto(m, { v[4], v[5], v[7], v[6] }, c, tone, fixedColor, color);
        AddFaceAuto(m, { v[0], v[2], v[3], v[1] }, c, tone, fixedColor, color);
    }

    // Ground-launched rocket, nose along +X, about 1 unit long (x from -0.5 to +0.5): octagonal body with a dark band,
    // a white pointed nose cone, four swept tail fins and an engine nozzle.
    Mesh BuildRocketMesh()
    {
        Mesh m;
        const auto& ring = FAST_UNIT_CIRCLE(8);                                       // precalculated octagon: no trig at build time
        if (ring.size() != 8) return m;
        auto V = [&](float x, float y, float z) { m.verts.push_back(MyVector(x, y, z)); return static_cast<int>(m.verts.size()) - 1; };
        const MyVector core(0.0f, 0.0f, 0.0f);
        auto station = [&](float x, float r, int out[8]) { for (int i = 0; i < 8; ++i) out[i] = V(x, r * ring[i].x, r * ring[i].y); };
        int A[8], S1[8], S2[8], B[8], C[8], N[8];
        station(-0.45f, 0.070f, A);   station(-0.20f, 0.070f, S1);   station(-0.06f, 0.070f, S2);
        station( 0.14f, 0.070f, B);   station( 0.32f, 0.048f, C);    station(-0.53f, 0.048f, N);
        const int tip = V(0.50f, 0.0f, 0.0f);
        const MyColor band(45, 45, 56, 255), nose(236, 236, 242, 255), metal(70, 72, 84, 255);
        for (int i = 0; i < 8; ++i)
        {
            const int j = (i + 1) % 8;
            AddFaceAuto(m, { A[i],  A[j],  S1[j], S1[i] }, core, 1.00f);                        // body (instance colour)
            AddFaceAuto(m, { S1[i], S1[j], S2[j], S2[i] }, core, 1.00f, true, band);            // dark band
            AddFaceAuto(m, { S2[i], S2[j], B[j],  B[i]  }, core, 0.95f);                        // body (instance colour)
            AddFaceAuto(m, { B[i],  B[j],  C[j],  C[i]  }, core, 1.00f, true, nose);            // white lower nose
            AddFaceAuto(m, { C[i],  C[j],  tip }, core, 1.00f, true, nose);                     // pointed tip
            AddFaceAuto(m, { N[i],  N[j],  A[j],  A[i]  }, core, 1.00f, true, metal);           // nozzle bell
        }
        AddFaceVec(m, { N[0], N[1], N[2], N[3], N[4], N[5], N[6], N[7] }, core, 0.5f, true, MyColor(30, 30, 38, 255));
        for (int k = 0; k < 4; ++k)                                                       // four fins, 90 degrees apart around the X axis
        {
            auto rot = [&](float y, float z, float& oy, float& oz)
            {
                switch (k) { case 0: oy = y; oz = z; break; case 1: oy = -z; oz = y; break; case 2: oy = -y; oz = -z; break; default: oy = z; oz = -y; }
            };
            const float px[4] = { -0.20f, -0.45f, -0.51f, -0.36f }, pr[4] = { 0.066f, 0.066f, 0.24f, 0.24f };
            const float t = 0.010f;
            int fr[4], bk[4];
            for (int i = 0; i < 4; ++i)
            {
                float y0, z0, y1, z1;
                rot(pr[i], t, y0, z0);  rot(pr[i], -t, y1, z1);
                fr[i] = V(px[i], y0, z0);  bk[i] = V(px[i], y1, z1);
            }
            float cy, cz;
            rot(0.15f, 0.0f, cy, cz);
            const MyVector c(-0.34f, cy, cz);
            AddFaceAuto(m, { fr[0], fr[1], fr[2], fr[3] }, c, 0.90f, true, MyColor(210, 60, 52, 255));
            AddFaceAuto(m, { bk[0], bk[1], bk[2], bk[3] }, c, 0.90f, true, MyColor(210, 60, 52, 255));
            for (int i = 1; i < 4; ++i) { const int j = (i + 1) % 4; AddFaceAuto(m, { fr[i], fr[j], bk[j], bk[i] }, c, 0.85f, true, MyColor(190, 52, 46, 255)); }
        }
        return m;
    }

    // Flying saucer (Defender lander, Scramble UFO), up = +Y, about 1 unit across: layered hull, a ring of rim lights,
    // a glass dome and three landing legs.
    Mesh BuildSaucerMesh()
    {
        Mesh m;
        const int N = 10;
        const auto& ring = FAST_UNIT_CIRCLE(N);
        if (static_cast<int>(ring.size()) != N) return m;
        auto station = [&](float y, float r, int out[10])
        {
            for (int i = 0; i < N; ++i) { m.verts.push_back(MyVector(r * ring[i].x, y, r * ring[i].y)); out[i] = static_cast<int>(m.verts.size()) - 1; }
        };
        int s0[10], s1[10], s2[10], s3[10], s4[10], s5[10], s6[10];
        station(-0.10f, 0.20f, s0);  station(-0.05f, 0.42f, s1);  station(0.00f, 0.52f, s2);
        station( 0.06f, 0.40f, s3);  station( 0.07f, 0.22f, s4);  station(0.17f, 0.17f, s5);  station(0.24f, 0.07f, s6);
        m.verts.push_back(MyVector(0.0f, 0.27f, 0.0f));
        const int apex = static_cast<int>(m.verts.size()) - 1;
        const MyVector core(0.0f, 0.05f, 0.0f);
        const MyColor glass(110, 225, 255, 255), light(255, 244, 140, 255), dark(52, 58, 72, 255);

        std::vector<int> bottom;
        for (int i = 0; i < N; ++i) bottom.push_back(s0[i]);
        AddFaceVec(m, bottom, core, 0.5f, true, dark);                                   // underside plate
        for (int i = 0; i < N; ++i)
        {
            const int j = (i + 1) % N;
            AddFaceAuto(m, { s0[i], s0[j], s1[j], s1[i] }, core, 0.70f);                 // lower hull
            AddFaceAuto(m, { s1[i], s1[j], s2[j], s2[i] }, core, 0.85f);                 // belly curve
            if (i & 1) AddFaceAuto(m, { s2[i], s2[j], s3[j], s3[i] }, core, 1.0f, true, light, true);   // glowing rim light
            else       AddFaceAuto(m, { s2[i], s2[j], s3[j], s3[i] }, core, 1.0f);                       // rim hull
            AddFaceAuto(m, { s3[i], s3[j], s4[j], s4[i] }, core, 1.0f);                  // top deck
            AddFaceAuto(m, { s4[i], s4[j], s5[j], s5[i] }, core, 1.0f, true, glass);     // dome, lower
            AddFaceAuto(m, { s5[i], s5[j], s6[j], s6[i] }, core, 1.0f, true, glass);     // dome, upper
            AddFaceAuto(m, { s6[i], s6[j], apex }, core, 1.0f, true, glass);             // dome, top
        }
        for (int i : { 0, 3, 7 })                                                        // three legs at roughly 120 degrees (ring points 0, 108, 252)
            AddBoxFaces(m, MyVector(0.30f * ring[i].x, -0.20f, 0.30f * ring[i].y), MyVector(0.020f, 0.10f, 0.020f), 0.8f, true, dark);
        return m;
    }

    // Mutant: a spiky alien urchin - small core with six long spikes (tumbles on every axis).
    Mesh BuildMutantMesh()
    {
        Mesh m;
        auto V = [&](const MyVector& p) { m.verts.push_back(p); return static_cast<int>(m.verts.size()) - 1; };
        const MyVector origin(0.0f, 0.0f, 0.0f);
        const int cx0 = V(MyVector(0.24f, 0, 0)), cx1 = V(MyVector(-0.24f, 0, 0)), cy0 = V(MyVector(0, 0.24f, 0)),
                  cy1 = V(MyVector(0, -0.24f, 0)), cz0 = V(MyVector(0, 0, 0.24f)), cz1 = V(MyVector(0, 0, -0.24f));
        for (int sx = 0; sx < 2; ++sx)
            for (int sy = 0; sy < 2; ++sy)
                for (int sz = 0; sz < 2; ++sz)
                    AddFaceAuto(m, { sx ? cx1 : cx0, sy ? cy1 : cy0, sz ? cz1 : cz0 }, origin, 0.9f);   // core octahedron
        const MyVector axes[6] = { MyVector(1, 0, 0), MyVector(-1, 0, 0), MyVector(0, 1, 0), MyVector(0, -1, 0), MyVector(0, 0, 1), MyVector(0, 0, -1) };
        for (int a = 0; a < 6; ++a)
        {
            const MyVector u = axes[a];
            const MyVector p1 = (MyMath::Abs(u.y) < 0.5f) ? MyVector(0, 1, 0) : MyVector(1, 0, 0);
            const MyVector p2 = u.Cross(p1);
            const int b0 = V(u * 0.16f + p1 * 0.09f + p2 * 0.09f), b1 = V(u * 0.16f - p1 * 0.09f + p2 * 0.09f),
                      b2 = V(u * 0.16f - p1 * 0.09f - p2 * 0.09f), b3 = V(u * 0.16f + p1 * 0.09f - p2 * 0.09f);
            const int tip = V(u * 0.50f);
            const int bs[4] = { b0, b1, b2, b3 };
            for (int k = 0; k < 4; ++k) AddFaceAuto(m, { tip, bs[k], bs[(k + 1) % 4] }, origin, (k & 1) ? 0.75f : 0.95f);
        }
        return m;
    }

    // ---- Mutant UFO: the hostile cousin of the friendly-looking saucer.  Same disc idea, but built to look like a predator: a low, razor-edged armoured hull (twelve-sided, with a sharp
    //      rim), six swept blade-spikes that stab out of the rim, six fangs hanging under the belly round a glowing red emitter, a flat armoured dome with a narrow amber "eye" band
    //      and red-glowing slits cut into the rim.  Hull panels take the instance colour; armour plates, glass and glow are fixed.  Radius about 0.8.  It is drawn with BuildMutantUfoRingMesh
    //      (a ring of hooked teeth) spinning the other way round it, so the ship animates even though both meshes are rigid.
    Mesh BuildMutantUfoMesh()
    {
        Mesh m;
        const int N = 12;
        const float twoPi = 6.28318530718f;
        auto station = [&](float y, float r, int* out)
        {
            for (int i = 0; i < N; ++i)
            {
                const float a = twoPi * static_cast<float>(i) / static_cast<float>(N);
                m.verts.push_back(MyVector(r * std::cos(a), y, r * std::sin(a)));
                out[i] = static_cast<int>(m.verts.size()) - 1;
            }
        };
        int s0[12], s1[12], s2[12], s3[12], s4[12], s5[12];
        station(-0.16f, 0.12f, s0);    // belly hub
        station(-0.09f, 0.46f, s1);    // lower hull
        station( 0.00f, 0.72f, s2);    // the sharp rim
        station( 0.07f, 0.50f, s3);    // upper hull
        station( 0.11f, 0.26f, s4);    // dome base
        station( 0.19f, 0.15f, s5);    // dome top
        m.verts.push_back(MyVector(0.0f, 0.21f, 0.0f));
        const int apex = static_cast<int>(m.verts.size()) - 1;
        const MyVector core(0.0f, 0.0f, 0.0f);
        const MyColor armour(46, 48, 58, 255), plate(70, 62, 70, 255), redGlow(255, 40, 30, 255), amber(255, 150, 40, 255), eye(255, 200, 70, 255);

        std::vector<int> belly;
        for (int i = 0; i < N; ++i) belly.push_back(s0[i]);
        AddFaceVec(m, belly, core, 0.5f, true, redGlow, true);                           // the emitter under the belly (glows red)
        for (int i = 0; i < N; ++i)
        {
            const int j = (i + 1) % N;
            AddFaceAuto(m, { s0[i], s0[j], s1[j], s1[i] }, core, 0.70f, true, armour);   // lower hull: dark armour
            AddFaceAuto(m, { s1[i], s1[j], s2[j], s2[i] }, core, 0.85f, (i % 3) == 1, plate);   // belly curve (every third panel a lighter plate)
            if ((i % 3) == 0) AddFaceAuto(m, { s2[i], s2[j], s3[j], s3[i] }, core, 1.0f, true, redGlow, true);   // glowing red slits in the rim
            else              AddFaceAuto(m, { s2[i], s2[j], s3[j], s3[i] }, core, 1.0f);                        // rim hull (instance colour)
            AddFaceAuto(m, { s3[i], s3[j], s4[j], s4[i] }, core, 0.95f);                 // upper armour
            AddFaceAuto(m, { s4[i], s4[j], s5[j], s5[i] }, core, 1.0f, true, (i % 2) ? amber : eye, true);   // narrow glowing eye band
            AddFaceAuto(m, { s5[i], s5[j], apex }, core, 0.9f, true, armour);            // flat armoured cap
        }
        // six blade-spikes stabbing out of the rim (at the odd ring points): a flat four-sided pyramid, wide in the horizontal plane, thin vertically
        for (int i = 1; i < N; i += 2)
        {
            const float a = twoPi * static_cast<float>(i) / static_cast<float>(N), ca = std::cos(a), sa = std::sin(a);
            const float tx = -sa, tz = ca;                                               // tangent (sideways) at this point of the rim
            const int b0 = static_cast<int>(m.verts.size());
            m.verts.push_back(MyVector(0.62f * ca + 0.11f * tx, 0.00f, 0.62f * sa + 0.11f * tz));   // base, left
            m.verts.push_back(MyVector(0.66f * ca, 0.045f, 0.66f * sa));                             // base, top
            m.verts.push_back(MyVector(0.62f * ca - 0.11f * tx, 0.00f, 0.62f * sa - 0.11f * tz));   // base, right
            m.verts.push_back(MyVector(0.66f * ca, -0.045f, 0.66f * sa));                            // base, bottom
            m.verts.push_back(MyVector(1.12f * ca, -0.02f, 1.12f * sa));                             // the tip, swept slightly down
            for (int k = 0; k < 4; ++k) AddFaceAuto(m, { b0 + 4, b0 + k, b0 + ((k + 1) % 4) }, core, (k & 1) ? 0.8f : 1.0f, true, plate);
        }
        // six fangs under the hull, leaning out a little (at the even ring points)
        for (int i = 0; i < N; i += 2)
        {
            const float a = twoPi * static_cast<float>(i) / static_cast<float>(N), ca = std::cos(a), sa = std::sin(a);
            const float tx = -sa, tz = ca;
            const int b0 = static_cast<int>(m.verts.size());
            m.verts.push_back(MyVector(0.40f * ca + 0.06f * tx, -0.10f, 0.40f * sa + 0.06f * tz));
            m.verts.push_back(MyVector(0.46f * ca, -0.10f, 0.46f * sa + 0.0f));
            m.verts.push_back(MyVector(0.40f * ca - 0.06f * tx, -0.10f, 0.40f * sa - 0.06f * tz));
            m.verts.push_back(MyVector(0.34f * ca, -0.10f, 0.34f * sa));
            m.verts.push_back(MyVector(0.52f * ca, -0.46f, 0.52f * sa));                             // the fang tip
            for (int k = 0; k < 4; ++k) AddFaceAuto(m, { b0 + 4, b0 + k, b0 + ((k + 1) % 4) }, core, (k & 1) ? 0.75f : 0.95f, true, armour);
        }
        FinalizeMesh(m);
        return m;
    }

    // The spinning ring that goes with BuildMutantUfoMesh: eight hooked teeth in a circle, each a thin pyramid reaching down and curling inward, in glowing red-orange metal.
    Mesh BuildMutantUfoRingMesh()
    {
        Mesh m;
        const int T = 8;
        const float twoPi = 6.28318530718f;
        const MyVector core(0.0f, 0.0f, 0.0f);
        const MyColor metal(190, 175, 170, 255), hotTip(255, 90, 40, 255);
        for (int i = 0; i < T; ++i)
        {
            const float a = twoPi * static_cast<float>(i) / static_cast<float>(T), ca = std::cos(a), sa = std::sin(a);
            const float tx = -sa, tz = ca;
            const int b0 = static_cast<int>(m.verts.size());
            m.verts.push_back(MyVector(0.80f * ca + 0.07f * tx, 0.03f, 0.80f * sa + 0.07f * tz));
            m.verts.push_back(MyVector(0.86f * ca, 0.03f, 0.86f * sa));
            m.verts.push_back(MyVector(0.80f * ca - 0.07f * tx, 0.03f, 0.80f * sa - 0.07f * tz));
            m.verts.push_back(MyVector(0.74f * ca, 0.03f, 0.74f * sa));
            m.verts.push_back(MyVector(0.70f * ca + 0.0f * tx, -0.30f, 0.70f * sa));                  // the hook: down and curled in
            for (int k = 0; k < 4; ++k) AddFaceAuto(m, { b0 + 4, b0 + k, b0 + ((k + 1) % 4) }, core, (k & 1) ? 0.8f : 1.0f, true, (k & 1) ? metal : hotTip, (k & 1) == 0);
        }
        FinalizeMesh(m);
        return m;
    }

    // Gyrospace drone (see the declaration in VectorShapes.h for the layout).  Body = a 10-sided lathe about the Z axis from the sensor lens at z = -0.30 to a rear cone at z = +0.38.
    Mesh BuildDroneMesh()
    {
        Mesh m;
        const int N = 10;
        const float twoPi = 6.28318530718f;
        const MyVector core(0.0f, 0.0f, 0.0f);
        auto station = [&](float z, float r, int* out)
        {
            for (int i = 0; i < N; ++i)
            {
                const float a = twoPi * static_cast<float>(i) / static_cast<float>(N);
                m.verts.push_back(MyVector(r * std::cos(a), r * std::sin(a), z));
                out[i] = static_cast<int>(m.verts.size()) - 1;
            }
        };
        int s0[10], s1[10], s2[10], s3[10], s4[10], s5[10];
        station(-0.30f, 0.13f, s0);     // the lens rim
        station(-0.24f, 0.27f, s1);
        station(-0.10f, 0.31f, s2);     // front of the equatorial band
        station( 0.10f, 0.31f, s3);     // rear of the band
        station( 0.24f, 0.25f, s4);
        station( 0.34f, 0.10f, s5);
        m.verts.push_back(MyVector(0.0f, 0.0f, 0.39f));
        const int apex = static_cast<int>(m.verts.size()) - 1;
        const MyColor amber(255, 190, 60, 255), band(52, 56, 70, 255), dark(60, 64, 78, 255);

        std::vector<int> lens;
        for (int i = 0; i < N; ++i) lens.push_back(s0[i]);
        AddFaceVec(m, lens, core, 1.0f, true, amber, true);                                    // the glowing sensor lens, facing the player
        for (int i = 0; i < N; ++i)
        {
            const int j = (i + 1) % N;
            AddFaceAuto(m, { s0[i], s0[j], s1[j], s1[i] }, core, (i & 1) ? 0.80f : 0.92f);     // front shoulder: hull colour
            AddFaceAuto(m, { s1[i], s1[j], s2[j], s2[i] }, core, (i & 1) ? 0.88f : 1.00f);
            AddFaceAuto(m, { s2[i], s2[j], s3[j], s3[i] }, core, 0.70f, true, band);            // the dark equatorial band
            AddFaceAuto(m, { s3[i], s3[j], s4[j], s4[i] }, core, (i & 1) ? 0.80f : 0.92f);
            AddFaceAuto(m, { s4[i], s4[j], s5[j], s5[i] }, core, (i & 1) ? 0.70f : 0.82f);
            AddFaceAuto(m, { s5[i], s5[j], apex }, core, 0.6f, true, dark);                     // the rear cone (thruster housing)
        }
        // solar wings: boom, metal frame, three blue cells (in front of the frame, so they show from the player's side)
        const MyColor metal(122, 128, 142, 255), frame(156, 164, 180, 255), cellA(24, 48, 140, 255), cellB(36, 68, 178, 255);
        for (int sg = -1; sg <= 1; sg += 2)
        {
            const float s = static_cast<float>(sg);
            AddBoxFaces(m, MyVector(s * 0.46f, 0.0f, 0.0f), MyVector(0.16f, 0.030f, 0.030f), 0.9f, true, metal);                    // the boom
            AddBoxFaces(m, MyVector(s * 0.88f, 0.0f, 0.012f), MyVector(0.37f, 0.30f, 0.020f), 0.9f, true, frame);                   // the frame
            for (int k = 0; k < 3; ++k)
                AddBoxFaces(m, MyVector(s * (0.64f + 0.24f * static_cast<float>(k)), 0.0f, -0.006f), MyVector(0.10f, 0.26f, 0.030f), 1.0f, true, (k & 1) ? cellB : cellA);   // the cells
        }
        // antenna: a mast on top with a small dish
        AddBoxFaces(m, MyVector(0.0f, 0.44f, 0.04f), MyVector(0.015f, 0.14f, 0.015f), 0.9f, true, metal);
        AddBoxFaces(m, MyVector(0.0f, 0.60f, 0.04f), MyVector(0.11f, 0.018f, 0.075f), 1.0f, true, MyColor(206, 210, 222, 255));
        FinalizeMesh(m);
        return m;
    }

    // Humanoid (Defender's people): head, torso, legs and arms.  Origin at the centre, 1 unit tall, facing +X.
    Mesh BuildHumanMesh()
    {
        Mesh m;
        const MyColor skin(240, 200, 160, 255), pants(50, 62, 130, 255);
        AddBoxFaces(m, MyVector(0.0f,  0.40f,  0.0f),   MyVector(0.080f, 0.080f, 0.080f), 1.0f, true,  skin);       // head
        AddBoxFaces(m, MyVector(0.0f,  0.12f,  0.0f),   MyVector(0.070f, 0.200f, 0.120f), 1.0f, false, MyColor());  // torso (instance colour = shirt)
        AddBoxFaces(m, MyVector(0.0f, -0.28f,  0.050f), MyVector(0.050f, 0.220f, 0.045f), 1.0f, true,  pants);      // legs
        AddBoxFaces(m, MyVector(0.0f, -0.28f, -0.050f), MyVector(0.050f, 0.220f, 0.045f), 1.0f, true,  pants);
        AddBoxFaces(m, MyVector(0.0f,  0.12f,  0.180f), MyVector(0.040f, 0.180f, 0.040f), 1.0f, true,  skin);       // arms
        AddBoxFaces(m, MyVector(0.0f,  0.12f, -0.180f), MyVector(0.040f, 0.180f, 0.040f), 1.0f, true,  skin);
        return m;
    }

    // Pac-Man ghost: domed head, round shoulders, a scalloped hem and two eyes on the +X side.  Feet at y = 0, 1 unit tall.
    Mesh BuildGhostMesh(int frame, bool scared)
    {
        Mesh m;
        const int N = 24;
        float cs[N], sn[N];
        for (int i = 0; i < N; ++i) FAST_SINCOS_DEG(360.0f * static_cast<float>(i) / static_cast<float>(N), sn[i], cs[i]);       // x = cos, z = sin
        auto V = [&](float x, float y, float z) { m.verts.push_back(MyVector(x, y, z)); return static_cast<int>(m.verts.size()) - 1; };
        // body: a skirt ring whose height varies round the ring (four scallops; the second frame is the first shifted half a scallop), then straight flanks and a rounded dome
        static const float kY[6] = { 0.26f, 0.50f, 0.68f, 0.82f, 0.92f, 0.98f }, kR[6] = { 0.50f, 0.50f, 0.47f, 0.39f, 0.27f, 0.13f };
        int hem[N], ring[6][N];
        for (int i = 0; i < N; ++i)
        {
            const float ang = 4.0f * 6.2831853f * static_cast<float>(i) / static_cast<float>(N) + (frame ? 3.14159265f : 0.0f);
            const float yh = 0.17f * (0.5f + 0.5f * FAST_COS(MyMath::Repeat(ang, MyMath::kTwoPi)));       // 0 at the feet, 0.17 in the notches
            hem[i] = V(0.47f * cs[i], yh, 0.47f * sn[i]);
        }
        for (int s = 0; s < 6; ++s) for (int i = 0; i < N; ++i) ring[s][i] = V(kR[s] * cs[i], kY[s], kR[s] * sn[i]);
        const int apex = V(0.0f, 1.04f, 0.0f);
        const MyVector core(0.0f, 0.5f, 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const int j = (i + 1) % N;
            AddFaceAuto(m, { hem[i], hem[j], ring[0][j], ring[0][i] }, core, (i & 1) ? 0.80f : 0.88f);        // skirt: alternate panels a little darker, so the scallops read
            for (int s = 0; s < 5; ++s) AddFaceAuto(m, { ring[s][i], ring[s][j], ring[s + 1][j], ring[s + 1][i] }, core, 1.0f);
            AddFaceAuto(m, { ring[5][i], ring[5][j], apex }, core, 1.0f);
        }
        // the body's radius at height y (for pasting the face onto the surface)
        auto rAt = [&](float y)
        {
            if (y <= kY[1]) return kR[1];
            for (int s = 1; s < 5; ++s) if (y <= kY[s + 1]) return kR[s] + (kR[s + 1] - kR[s]) * (y - kY[s]) / (kY[s + 1] - kY[s]);
            return kR[5];
        };
        // a flat quad pasted on the surface at the given z / y corners, pushed `push` out from it
        auto paste = [&](float z0, float z1, float y0, float y1, float push, const MyColor& col)
        {
            auto X = [&](float z, float y) { const float r = rAt(y); return std::sqrt(MyMath::Max(0.0f, r * r - z * z)) + push; };
            const int a = V(X(z0, y0), y0, z0), b = V(X(z1, y0), y0, z1), c = V(X(z1, y1), y1, z1), d = V(X(z0, y1), y1, z0);
            AddFaceAuto(m, { a, b, c, d }, MyVector(0.0f, (y0 + y1) * 0.5f, (z0 + z1) * 0.5f), 1.0f, true, col);
        };
        if (!scared)
            for (int e = -1; e <= 1; e += 2)                                                                  // big white eyes with blue pupils looking forward
            {
                const float zc = 0.18f * static_cast<float>(e);
                paste(zc - 0.085f, zc + 0.085f, 0.56f, 0.82f, 0.004f, MyColor(250, 250, 255, 255));
                paste(zc - 0.045f + 0.012f * static_cast<float>(e), zc + 0.045f + 0.012f * static_cast<float>(e), 0.58f, 0.72f, 0.010f, MyColor(30, 50, 210, 255));
            }
        else
        {
            for (int e = -1; e <= 1; e += 2)                                                                  // frightened: two small pale dots ...
            {
                const float zc = 0.17f * static_cast<float>(e);
                paste(zc - 0.045f, zc + 0.045f, 0.64f, 0.74f, 0.004f, MyColor(255, 225, 190, 255));
            }
            for (int k = 0; k < 8; ++k)                                                                       // ... and a zig-zag mouth
            {
                const float z0 = -0.28f + 0.07f * static_cast<float>(k), z1 = z0 + 0.07f;
                const float ya = (k & 1) ? 0.34f : 0.40f, yb = (k & 1) ? 0.40f : 0.34f;
                auto X = [&](float z) { return std::sqrt(MyMath::Max(0.0f, 0.25f - z * z)) + 0.005f; };
                const int a = V(X(z0), ya - 0.025f, z0), b = V(X(z1), yb - 0.025f, z1), c = V(X(z1), yb + 0.025f, z1), d = V(X(z0), ya + 0.025f, z0);
                AddFaceAuto(m, { a, b, c, d }, MyVector(0.0f, 0.37f, 0.5f * (z0 + z1)), 1.0f, true, MyColor(255, 225, 200, 255));
            }
        }
        return m;
    }

    Mesh BuildGhostEyesMesh()
    {
        Mesh m;
        auto V = [&](float x, float y, float z) { m.verts.push_back(MyVector(x, y, z)); return static_cast<int>(m.verts.size()) - 1; };
        auto quad = [&](float x, float z0, float z1, float y0, float y1, const MyColor& col, float nx)                  // a quad facing +X (nx > 0) or -X
        {
            const int a = V(x, y0, z0), b = V(x, y0, z1), c = V(x, y1, z1), d = V(x, y1, z0);
            AddFaceAuto(m, { a, b, c, d }, MyVector(x - nx, (y0 + y1) * 0.5f, (z0 + z1) * 0.5f), 1.0f, true, col);
        };
        for (int e = -1; e <= 1; e += 2)
        {
            const float zc = 0.18f * static_cast<float>(e);
            quad(0.0f, zc - 0.085f, zc + 0.085f, 0.56f, 0.82f, MyColor(250, 250, 255, 255), 1.0f);                // white of the eye, seen from the front ...
            quad(0.0f, zc - 0.085f, zc + 0.085f, 0.56f, 0.82f, MyColor(250, 250, 255, 255), -1.0f);               // ... and from behind
            quad(0.010f, zc - 0.045f + 0.012f * static_cast<float>(e), zc + 0.045f + 0.012f * static_cast<float>(e), 0.58f, 0.72f, MyColor(30, 50, 210, 255), 1.0f);   // the pupil looks ahead
        }
        for (Face& f : m.faces) f.glow = true;                                 // an eaten ghost's eyes GLOW: self-lit, full brightness whatever the light (main.cpp adds a pulsing halo)
        return m;
    }

    // Marks the last `count` faces of a mesh as self-lit paint (engine glow, lamps).
    void MarkGlow(Mesh& m, size_t count, const MyColor& col)
    {
        for (size_t i = m.faces.size() - count; i < m.faces.size(); ++i) { m.faces[i].fixedColor = true; m.faces[i].glow = true; m.faces[i].color = col; }
    }

    // Shared icosphere generator: an icosahedron, optionally subdivided (each triangle -> four).
    void BuildIcoSphere(int subdiv, std::vector<MyVector>& v, std::vector<std::array<int, 3>>& tris)
    {
        const float t = 1.6180339887f;
        v = { MyVector(-1, t, 0), MyVector(1, t, 0), MyVector(-1, -t, 0), MyVector(1, -t, 0), MyVector(0, -1, t), MyVector(0, 1, t),
              MyVector(0, -1, -t), MyVector(0, 1, -t), MyVector(t, 0, -1), MyVector(t, 0, 1), MyVector(-t, 0, -1), MyVector(-t, 0, 1) };
        for (MyVector& p : v) p = p.Normalized();
        tris = { {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                 {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1} };
        for (int s = 0; s < subdiv; ++s)
        {
            std::map<std::pair<int, int>, int> mid;
            auto midpoint = [&](int a, int b)
            {
                const std::pair<int, int> key((std::min)(a, b), (std::max)(a, b));
                const auto it = mid.find(key);
                if (it != mid.end()) return it->second;
                v.push_back(((v[a] + v[b]) * 0.5f).Normalized());
                const int idx = static_cast<int>(v.size()) - 1;
                mid[key] = idx;
                return idx;
            };
            std::vector<std::array<int, 3>> next;
            for (const auto& tr : tris)
            {
                const int a = tr[0], b = tr[1], c = tr[2];
                const int ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
                next.push_back({ a, ab, ca }); next.push_back({ b, bc, ab }); next.push_back({ c, ca, bc }); next.push_back({ ab, bc, ca });
            }
            tris.swap(next);
        }
    }

    // A weathered, lumpy rock: icosphere vertices pushed in / out at random, squashed per axis, every face with its own tone
    // (so it reads as faceted stone rather than a clean polyhedron).  Nominal radius about 1.
    Mesh BuildRockMesh(int subdiv, unsigned seed, float lump)
    {
        Mesh m;
        unsigned rng = seed;
        auto rnd = [&]() { rng = rng * 1664525u + 1013904223u; return static_cast<float>((rng >> 8) & 0xFFFFFF) / 16777216.0f; };
        std::vector<MyVector> v;
        std::vector<std::array<int, 3>> tris;
        BuildIcoSphere(subdiv, v, tris);
        const MyVector squash(0.85f + 0.30f * rnd(), 0.78f + 0.30f * rnd(), 0.85f + 0.30f * rnd());
        for (MyVector& p : v) p = p.Scale(squash) * (1.0f - lump * 0.5f + lump * rnd());           // radial lumps and dents
        m.verts = v;
        static const float tones[4] = { 0.66f, 0.78f, 0.90f, 1.0f };
        for (const auto& tr : tris)
            AddFaceAuto(m, { tr[0], tr[1], tr[2] }, MyVector(0.0f, 0.0f, 0.0f), tones[static_cast<int>(rnd() * 4.0f) & 3]);
        return m;
    }

    // ---- procedural planet: lattice value noise, summed over four octaves --------------------------------------------------
    float Hash3(int x, int y, int z)
    {
        unsigned h = static_cast<unsigned>(x) * 374761393u + static_cast<unsigned>(y) * 668265263u + static_cast<unsigned>(z) * 2147483647u;
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= h >> 16;
        return static_cast<float>(h & 0xFFFFFFu) / 16777216.0f;
    }
    float ValueNoise3(const MyVector& p)
    {
        const float fx = std::floor(p.x), fy = std::floor(p.y), fz = std::floor(p.z);
        const int ix = static_cast<int>(fx), iy = static_cast<int>(fy), iz = static_cast<int>(fz);
        float tx = p.x - fx, ty = p.y - fy, tz = p.z - fz;
        tx = tx * tx * (3.0f - 2.0f * tx); ty = ty * ty * (3.0f - 2.0f * ty); tz = tz * tz * (3.0f - 2.0f * tz);     // smoothstep
        auto L = [](float a, float b, float t) { return a + (b - a) * t; };
        return L(L(L(Hash3(ix, iy, iz),     Hash3(ix + 1, iy, iz),     tx), L(Hash3(ix, iy + 1, iz),     Hash3(ix + 1, iy + 1, iz),     tx), ty),
                 L(L(Hash3(ix, iy, iz + 1), Hash3(ix + 1, iy, iz + 1), tx), L(Hash3(ix, iy + 1, iz + 1), Hash3(ix + 1, iy + 1, iz + 1), tx), ty), tz);
    }
    float Fbm3(const MyVector& p)                                           // roughly 0 .. 0.94
    {
        float sum = 0.0f, amp = 0.5f, freq = 1.0f;
        for (int o = 0; o < 4; ++o) { sum += amp * ValueNoise3(p * freq); freq *= 2.03f; amp *= 0.5f; }
        return sum;
    }

    // A realistic asteroid (Planet Assault): an elongated, flattened potato whose surface is pushed about by two scales of fractal noise (big lumps and rough ground), then
    // pitted with impact CRATERS - each a bowl with a slightly raised rim.  Face tones follow the height of the ground (craters and valleys read darker, ridges lighter) with a
    // little random grain, so it shades like cratered stone rather than a clean polyhedron.  The tone is the only colouring, so the instance colour (grey-brown, or the green
    // of a power-up rock) still tints the whole rock.  subdiv 2 = 320 faces (the big rocks), 1 = 80, 0 = 20.  Nominal radius about 1.
    Mesh BuildAsteroidMesh(int subdiv, unsigned seed)
    {
        Mesh m;
        unsigned rng = seed;
        auto rnd = [&]() { rng = rng * 1664525u + 1013904223u; return static_cast<float>((rng >> 8) & 0xFFFFFF) / 16777216.0f; };
        std::vector<MyVector> v;
        std::vector<std::array<int, 3>> tris;
        BuildIcoSphere(subdiv, v, tris);

        // the overall shape: one long axis, one squashed axis
        const MyVector squash(1.12f + 0.30f * rnd(), 0.70f + 0.18f * rnd(), 0.86f + 0.18f * rnd());
        const MyVector noiseOff(rnd() * 40.0f, rnd() * 40.0f, rnd() * 40.0f);

        // craters: centre direction, angular radius (radians) and depth.  Coarse meshes cannot show small pits, so they get fewer, wider ones.
        struct Crater { MyVector c; float r, depth; };
        Crater cr[8];
        const int craters = (subdiv >= 2) ? 8 : ((subdiv == 1) ? 5 : 3);
        for (int i = 0; i < craters; ++i)
        {
            const float z = 2.0f * rnd() - 1.0f, a = 6.2831853f * rnd(), s = std::sqrt(MyMath::Max(0.0f, 1.0f - z * z));
            cr[i].c = MyVector(s * std::cos(a), z, s * std::sin(a));
            cr[i].r = (subdiv >= 2 ? 0.30f : 0.42f) + 0.28f * rnd();
            cr[i].depth = 0.10f + 0.10f * rnd();
        }

        std::vector<float> hgt(v.size());                                                   // per-vertex ground height (1 = the base sphere), for the face tones
        for (size_t i = 0; i < v.size(); ++i)
        {
            const MyVector d = v[i];                                                        // unit direction
            float r = 1.0f + 0.50f * (Fbm3(d * 1.6f + noiseOff) - 0.45f) + 0.10f * (Fbm3(d * 5.0f + noiseOff * 1.7f) - 0.45f);
            for (int k = 0; k < craters; ++k)
            {
                const float cd = MyMath::Clamp(d.Dot(cr[k].c), -1.0f, 1.0f);
                if (cd < 0.5f) continue;                                                    // farther than 60 degrees: cannot be inside this crater
                const float t = std::acos(cd) / cr[k].r;                                    // 0 at the centre, 1 at the rim
                if (t < 1.0f) r -= cr[k].depth * (1.0f - t * t);                            // the bowl
                const float rim = (t - 1.05f) / 0.22f;
                r += 0.035f * std::exp(-rim * rim);                                         // the raised lip just outside it
            }
            hgt[i] = r;
            v[i] = d.Scale(squash) * r;
        }
        m.verts = v;
        for (const auto& tr : tris)
        {
            const float h = (hgt[tr[0]] + hgt[tr[1]] + hgt[tr[2]]) * (1.0f / 3.0f);
            const float tone = MyMath::Clamp(0.50f + (h - 0.78f) * 1.45f + (rnd() - 0.5f) * 0.14f, 0.40f, 1.0f);     // low ground dark, high ground light
            AddFaceAuto(m, { tr[0], tr[1], tr[2] }, MyVector(0.0f, 0.0f, 0.0f), tone);
        }
        return m;
    }

    // ---- the worlds of Planet Assault ---------------------------------------------------------------------------------------------
    // A new world every 5 waves (one "stage"): stage 0 is the real Earth, then a Mars-like desert, an ice world, an alien jungle, a volcanic
    // world, a golden desert and an ocean world, then round again.  The planets are DENSE icospheres (2562 vertices, 5120 faces): at that
    // density flat shading reads as a smooth, detailed surface instead of a faceted vector one.  The geometry and the per-face terrain
    // numbers (height, moisture, latitude, fine detail) are built once; changing stage just repaints the faces from a different palette
    // (and tints the clouds and the atmosphere), which is a few thousand blends and costs nothing at run time.
    const PlanetPalette kPlanetPal[7] = {
        { MyColor(16, 48, 118, 255),  MyColor(48, 128, 188, 255),  MyColor(216, 198, 142, 255), MyColor(84, 150, 70, 255),   MyColor(44, 110, 54, 255),  MyColor(206, 178, 108, 255), MyColor(122, 114, 104, 255), MyColor(242, 246, 252, 255), MyColor(228, 240, 252, 255), MyColor(110, 165, 255, 34), MyColor(255, 255, 255, 255), L"EARTH" },
        { MyColor(92, 38, 28, 255),   MyColor(150, 76, 48, 255),   MyColor(214, 164, 112, 255), MyColor(178, 98, 58, 255),   MyColor(146, 78, 50, 255),  MyColor(206, 146, 86, 255),  MyColor(100, 66, 54, 255),   MyColor(236, 226, 216, 255), MyColor(244, 238, 232, 255), MyColor(255, 160, 100, 34), MyColor(240, 200, 180, 255), L"MARS" },
        { MyColor(60, 110, 180, 255), MyColor(130, 190, 232, 255), MyColor(226, 238, 246, 255), MyColor(200, 222, 240, 255), MyColor(160, 196, 226, 255), MyColor(214, 230, 244, 255), MyColor(120, 150, 184, 255), MyColor(255, 255, 255, 255), MyColor(240, 248, 255, 255), MyColor(170, 220, 255, 34), MyColor(240, 248, 255, 255), L"ICE WORLD" },
        { MyColor(40, 14, 92, 255),   MyColor(86, 40, 160, 255),   MyColor(232, 190, 240, 255), MyColor(150, 214, 60, 255),  MyColor(60, 150, 70, 255),  MyColor(230, 200, 90, 255),  MyColor(110, 70, 130, 255),  MyColor(240, 230, 255, 255), MyColor(232, 214, 250, 255), MyColor(190, 120, 255, 34), MyColor(230, 200, 255, 255), L"JUNGLE WORLD" },
        { MyColor(150, 24, 8, 255),   MyColor(240, 96, 18, 255),   MyColor(60, 44, 40, 255),    MyColor(44, 36, 36, 255),    MyColor(66, 50, 46, 255),   MyColor(96, 72, 60, 255),    MyColor(84, 78, 82, 255),    MyColor(190, 186, 190, 255), MyColor(210, 206, 208, 255), MyColor(255, 110, 40, 34), MyColor(90, 70, 66, 255),    L"VOLCANIC WORLD" },
        { MyColor(160, 100, 40, 255), MyColor(210, 150, 70, 255),  MyColor(238, 214, 150, 255), MyColor(222, 176, 86, 255),  MyColor(190, 140, 64, 255), MyColor(240, 200, 110, 255), MyColor(150, 110, 70, 255),  MyColor(250, 240, 214, 255), MyColor(250, 244, 226, 255), MyColor(255, 200, 110, 34), MyColor(255, 236, 190, 255), L"GOLDEN WORLD" },
        { MyColor(4, 40, 92, 255),    MyColor(20, 130, 170, 255),  MyColor(230, 226, 190, 255), MyColor(70, 170, 120, 255),  MyColor(30, 116, 92, 255),  MyColor(200, 200, 130, 255), MyColor(100, 120, 120, 255), MyColor(240, 250, 250, 255), MyColor(226, 244, 248, 255), MyColor(90, 220, 230, 34), MyColor(230, 250, 252, 255), L"OCEAN WORLD" } };

    MyColor LerpColor(const MyColor& a, const MyColor& b, float t)
    {
        t = MyMath::Clamp01(t);
        return MyColor(static_cast<uint8_t>(a.r + (b.r - a.r) * t), static_cast<uint8_t>(a.g + (b.g - a.g) * t), static_cast<uint8_t>(a.b + (b.b - a.b) * t), 255);
    }
    // The colour of one patch of a world.  Every transition is a smooth blend, not a threshold: seas deepen gradually from a surf line,
    // beaches fade into lowland, lowland into forest or desert by moisture, then into bare rock and snow with height, and the polar
    // caps have a soft edge.  h = height field (sea level 0.48), mo = moisture, lat = |latitude|, d = fine detail noise.
    MyColor PlanetPaint(const PlanetPalette& P, float h, float mo, float lat, float d)
    {
        auto ss = [](float a, float b, float x) { const float t = MyMath::Clamp01((x - a) / (b - a)); return t * t * (3.0f - 2.0f * t); };
        MyColor c;
        if (h < 0.48f)
        {
            c = LerpColor(P.shallow, P.deep, ss(0.0f, 0.16f, 0.48f - h));
            c = LerpColor(c, P.beach, (1.0f - ss(0.0f, 0.014f, 0.48f - h)) * 0.55f);          // the surf line along every coast
        }
        else
        {
            const float e = h - 0.48f;
            const float dry = MyMath::Clamp01((mo - 0.5f) * 8.0f) * MyMath::Clamp01((0.6f - lat) * 4.0f);
            c = LerpColor(LerpColor(P.low, P.forest, (0.62f - mo) * 3.0f), P.desert, dry * 0.9f);
            c = LerpColor(c, P.rock, ss(0.12f, 0.28f, e));
            c = LerpColor(c, P.snow, MyMath::Max(ss(0.26f, 0.36f, e), 0.75f * ss(0.55f, 0.78f, lat)));
            c = LerpColor(c, P.beach, 1.0f - ss(0.0f, 0.03f, e));                             // sandy shore
        }
        c = LerpColor(c, P.ice, ss(0.78f, 0.86f, lat + (mo - 0.5f) * 0.1f));                  // polar caps
        const float bright = 0.90f + 0.22f * d;                                               // fine surface detail
        auto q = [&](float v) { const int i = static_cast<int>(MyMath::Clamp(v * bright, 0.0f, 255.0f)); return static_cast<uint8_t>((i / 8) * 8 + 4); };   // quantised: faces of one colour batch together
        return MyColor(q(c.r), q(c.g), q(c.b), 255);
    }

    // Fbm3 with a chosen number of octaves, scaled to the same 0 .. 0.94 range so thresholds tuned for the four-octave version still hold.  Dense planets use 5-6 octaves: their
    // faces are small enough to show the finer ones.
    float Fbm3N(const MyVector& p, int octaves)
    {
        float sum = 0.0f, amp = 0.5f, freq = 1.0f, total = 0.0f;
        for (int o = 0; o < octaves; ++o) { sum += amp * ValueNoise3(p * freq); total += amp; freq *= 2.03f; amp *= 0.5f; }
        return sum * (0.9375f / total);
    }

    // Repaints a procedural planet from another palette (a few thousand blends: free at run time).
    void PaintPlanet(Mesh& m, const PlanetTerrain& t, const PlanetPalette& P)
    {
        const size_t n = (std::min)(m.faces.size(), t.h.size());
        for (size_t i = 0; i < n; ++i) m.faces[i].color = PlanetPaint(P, t.h[i], t.m[i], t.lat[i], t.d[i]);
    }

    // The invented worlds: continents from low-frequency noise, mountain relief (ridged noise) on the land, flat seas.  Radius 1.
    // subdiv 4 = 5120 faces, subdiv 5 = 20480 faces (10242 vertices).  The dense version also uses more noise octaves, so it shows finer coastlines, mountain chains and
    // rocky grain; its relief is deep enough that the mountains catch the light and cast a ragged terminator, which is most of what makes it read as a real world.
    Mesh BuildPlanetMesh(int subdiv, PlanetTerrain& t)
    {
        Mesh m;
        std::vector<MyVector> v;
        std::vector<std::array<int, 3>> tris;
        BuildIcoSphere(subdiv, v, tris);
        const bool dense = subdiv >= 5;
        const int oct = dense ? 6 : 4;
        const float fineF = dense ? 28.0f : 16.0f;
        const MyVector seed(3.7f, 1.3f, 9.1f), wet(11.0f, 5.0f, 2.0f), fine(21.0f, 7.0f, 13.0f);
        std::vector<float> n(v.size());
        for (size_t i = 0; i < v.size(); ++i)
        {
            n[i] = Fbm3N(v[i] * 2.4f + seed, oct);                                           // below 0.48 is sea
            const float relief = MyMath::Max(0.0f, n[i] - 0.48f);
            const float ridge = 1.0f - MyMath::Abs(MyMath::Clamp(2.0f * Fbm3N(v[i] * 6.5f + fine, oct) - 1.0f, -1.0f, 1.0f));     // ridged noise: sharp mountain crests
            const float rough = dense ? 0.025f * relief * (Fbm3N(v[i] * 26.0f + seed * 1.7f, 3) - 0.47f) : 0.0f;                  // rocky grain, dense mesh only
            m.verts.push_back(v[i] * (1.0f + 0.13f * relief + 0.11f * relief * ridge + rough));
        }
        t.h.clear(); t.m.clear(); t.lat.clear(); t.d.clear();
        t.h.reserve(tris.size()); t.m.reserve(tris.size()); t.lat.reserve(tris.size()); t.d.reserve(tris.size());
        const MyVector origin(0.0f, 0.0f, 0.0f);
        for (const auto& tr : tris)
        {
            const MyVector c = ((v[tr[0]] + v[tr[1]] + v[tr[2]]) * (1.0f / 3.0f)).Normalized();
            t.h.push_back((n[tr[0]] + n[tr[1]] + n[tr[2]]) * (1.0f / 3.0f));
            t.m.push_back(Fbm3N(c * 3.1f + wet, oct));
            t.lat.push_back(MyMath::Abs(c.y));
            t.d.push_back(Fbm3N(c * fineF + fine, dense ? 5 : 4));
            AddFaceAuto(m, { tr[0], tr[1], tr[2] }, origin, 1.0f, true, kPlanetPal[0].deep);   // painted by PaintPlanet
        }
        PaintPlanet(m, t, kPlanetPal[0]);
        return m;
    }

    // Stage 0's world: the REAL Earth.  Assets/planets/earth_hi.rgb is a 512 x 256 equirectangular map (raw RGB24, baked from NASA's Blue
    // Marble land / shallow-water image); each face of a dense icosphere takes the map colour (bilinearly sampled at its centre and corners, the centre counting double, which
    // anti-aliases it), broken up by a little fine noise, and the land is raised by ridged relief noise (the sea stays flat).  subdiv 5 (20480 faces) is the high-detail version.
    // Returns false (and the noise world is used instead) if the map file is missing.
    bool BuildEarthMappedMesh(Mesh& m, const std::filesystem::path& mapFile, int subdiv)
    {
        const int W = 512, H = 256;
        std::vector<unsigned char> px(static_cast<size_t>(W) * H * 3);
        {
            std::ifstream in(mapFile, std::ios::binary);
            if (!in) return false;
            in.read(reinterpret_cast<char*>(px.data()), static_cast<std::streamsize>(px.size()));
            if (in.gcount() != static_cast<std::streamsize>(px.size())) return false;
        }
        auto sample = [&](const MyVector& d, float out[3])
        {
            const MyVector nn = d.Normalized();
            const float fx = (MyMath::Atan2(nn.z, nn.x) / MyMath::kTwoPi + 0.5f) * static_cast<float>(W) - 0.5f;
            const float fy = (0.5f - MyMath::Asin(MyMath::Clamp(nn.y, -1.0f, 1.0f)) / MyMath::kPi) * static_cast<float>(H) - 0.5f;
            const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
            const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
            for (int ch = 0; ch < 3; ++ch)
            {
                auto t = [&](int x, int y) { x = ((x % W) + W) % W; y = (std::max)(0, (std::min)(H - 1, y)); return static_cast<float>(px[(static_cast<size_t>(y) * W + x) * 3 + ch]); };
                const float a = t(x0, y0) + (t(x0 + 1, y0) - t(x0, y0)) * tx, b = t(x0, y0 + 1) + (t(x0 + 1, y0 + 1) - t(x0, y0 + 1)) * tx;
                out[ch] = a + (b - a) * ty;
            }
        };
        std::vector<MyVector> v;
        std::vector<std::array<int, 3>> tris;
        BuildIcoSphere(subdiv, v, tris);
        const bool dense = subdiv >= 5;
        const MyVector seed(5.1f, 2.7f, 8.3f), fine(31.0f, 17.0f, 9.0f);
        for (size_t i = 0; i < v.size(); ++i)
        {
            float s[3];
            sample(v[i], s);
            const bool ocean = s[2] > s[0] + 22.0f && s[2] > s[1];
            float relief = 0.0f;
            if (!ocean)
            {
                const float base = MyMath::Max(0.0f, Fbm3N(v[i] * 5.5f + seed, dense ? 6 : 4) - 0.30f);
                const float ridge = 1.0f - MyMath::Abs(MyMath::Clamp(2.0f * Fbm3N(v[i] * 11.0f + fine, 4) - 1.0f, -1.0f, 1.0f));      // mountain chains
                relief = (dense ? 0.060f : 0.045f) * base * (0.65f + 0.70f * ridge);
            }
            m.verts.push_back(v[i] * (1.0f + relief));
        }
        for (const auto& tr : tris)
        {
            const MyVector c = ((v[tr[0]] + v[tr[1]] + v[tr[2]]) * (1.0f / 3.0f)).Normalized();
            float acc[3] = { 0.0f, 0.0f, 0.0f }, s[3];
            sample(c, s);                                   for (int ch = 0; ch < 3; ++ch) acc[ch] += 2.0f * s[ch];
            for (int k = 0; k < 3; ++k) { sample(v[tr[k]], s); for (int ch = 0; ch < 3; ++ch) acc[ch] += s[ch]; }
            for (int ch = 0; ch < 3; ++ch) acc[ch] *= 0.2f;
            const float detail = 0.93f + 0.14f * Fbm3N(c * (dense ? 34.0f : 18.0f) + fine, 4);
            auto q = [&](float val) { const int i = static_cast<int>(MyMath::Clamp(val * detail, 0.0f, 255.0f)); return static_cast<uint8_t>((i / 6) * 6 + 3); };
            AddFaceAuto(m, { tr[0], tr[1], tr[2] }, MyVector(0.0f, 0.0f, 0.0f), 1.0f, true, MyColor(q(acc[0]), q(acc[1]), q(acc[2]), 255));
        }
        return true;
    }

    // Cloud layer: only the faces where a second noise field is high, translucent white, hovering just above the surface.  The noise is DOMAIN-WARPED (the lookup point is
    // pushed about by three more noise fields), which turns blobs into swirling, streaky weather fronts; denser cloud is more opaque (four steps).
    Mesh BuildCloudMesh(int subdiv, float radius, float coverage)
    {
        Mesh m;
        std::vector<MyVector> v;
        std::vector<std::array<int, 3>> tris;
        BuildIcoSphere(subdiv, v, tris);
        const MyVector seed(8.2f, 4.4f, 1.9f);
        for (const MyVector& p : v) m.verts.push_back(p * radius);
        for (const auto& tr : tris)
        {
            const MyVector c = ((v[tr[0]] + v[tr[1]] + v[tr[2]]) * (1.0f / 3.0f)).Normalized();
            const MyVector warp(Fbm3N(c * 2.0f + seed * 0.5f, 3) - 0.47f, Fbm3N(c * 2.0f + seed * 0.9f + MyVector(5.2f, 1.3f, 7.7f), 3) - 0.47f, Fbm3N(c * 2.0f + MyVector(2.1f, 8.3f, 4.4f), 3) - 0.47f);
            const float d = Fbm3N((c + warp * 0.55f) * 3.4f + seed, 5);
            if (d < coverage) continue;                                             // clear sky here
            const float over = d - coverage;
            AddFaceAuto(m, { tr[0], tr[1], tr[2] }, MyVector(0.0f, 0.0f, 0.0f), 0.92f, true, MyColor(255, 255, 255, (over > 0.14f) ? 200 : ((over > 0.09f) ? 165 : ((over > 0.04f) ? 125 : 80))));
        }
        return m;
    }

    // Atmosphere: a faint shell just outside the planet; its rim shows as a soft glow round the limb.  Dense (subdiv 4) so the limb is a smooth circle, not a polygon.
    Mesh BuildHaloMesh(int subdiv, float radius, const MyColor& col)
    {
        Mesh m;
        std::vector<MyVector> v;
        std::vector<std::array<int, 3>> tris;
        BuildIcoSphere(subdiv, v, tris);
        for (const MyVector& p : v) m.verts.push_back(p * radius);
        for (const auto& tr : tris)
            AddFaceAuto(m, { tr[0], tr[1], tr[2] }, MyVector(0.0f, 0.0f, 0.0f), 1.0f, true, col, true);
        return m;
    }

    // Hostile raider (enemy fighter): forward-swept angular wings, a long dart fuselage, a glowing red cockpit eye and twin
    // glowing engines.  Nose along +X, wings along +/-Z, about 1 unit long.
    Mesh BuildRaiderMesh()
    {
        Mesh m;
        auto V = [&](float x, float y, float z) { m.verts.push_back(MyVector(x, y, z)); return static_cast<int>(m.verts.size()) - 1; };
        const MyVector core(0.0f, 0.0f, 0.0f);
        const int nose = V(0.50f, 0.0f, 0.0f);
        const int rb[4] = { V(0.12f, 0.075f, 0.0f), V(0.12f, 0.0f, 0.075f), V(0.12f, -0.055f, 0.0f), V(0.12f, 0.0f, -0.075f) };
        const int rc[4] = { V(-0.34f, 0.100f, 0.0f), V(-0.34f, 0.0f, 0.100f), V(-0.34f, -0.070f, 0.0f), V(-0.34f, 0.0f, -0.100f) };
        for (int i = 0; i < 4; ++i)
        {
            const int j = (i + 1) % 4;
            AddFaceAuto(m, { nose, rb[i], rb[j] }, core, 1.00f);
            AddFaceAuto(m, { rb[i], rb[j], rc[j], rc[i] }, core, 0.86f);
        }
        AddFaceAuto(m, { rc[0], rc[1], rc[2], rc[3] }, core, 0.5f, true, MyColor(50, 40, 44, 255));

        auto wing = [&](float s)                                                                 // forward-swept: the tips reach ahead of the roots
        {
            const float wx[4] = { 0.00f, 0.22f, 0.10f, -0.32f }, wz[4] = { 0.07f, 0.46f, 0.46f, 0.10f };
            const float y = -0.010f, t = 0.014f;
            int tp[4], bt[4];
            for (int i = 0; i < 4; ++i) { tp[i] = V(wx[i], y + t, s * wz[i]); bt[i] = V(wx[i], y - t, s * wz[i]); }
            const MyVector c(-0.05f, y, s * 0.27f);
            AddFaceAuto(m, { tp[0], tp[1], tp[2], tp[3] }, c, 0.78f);
            AddFaceAuto(m, { bt[0], bt[1], bt[2], bt[3] }, c, 0.52f);
            for (int i = 0; i < 3; ++i) { const int j = i + 1; AddFaceAuto(m, { tp[i], tp[j], bt[j], bt[i] }, c, 0.68f); }
            AddBoxFaces(m, MyVector(0.20f, y, s * 0.46f), MyVector(0.11f, 0.026f, 0.022f), 1.0f, true, MyColor(70, 62, 66, 255));   // wing-tip cannon
        };
        wing(1.0f); wing(-1.0f);

        {   // cockpit: a small pyramid that glows red like an eye
            const int a = V(0.30f, 0.060f, 0.0f), b = V(0.12f, 0.085f, 0.045f), c = V(0.04f, 0.095f, 0.0f), d = V(0.12f, 0.085f, -0.045f), e = V(0.16f, 0.135f, 0.0f);
            const MyVector inside(0.16f, 0.07f, 0.0f);
            AddFaceAuto(m, { e, a, b }, inside); AddFaceAuto(m, { e, b, c }, inside); AddFaceAuto(m, { e, c, d }, inside); AddFaceAuto(m, { e, d, a }, inside);
            MarkGlow(m, 4, MyColor(255, 60, 50, 255));
        }
        for (int e = -1; e <= 1; e += 2)                                                         // twin engines with glowing exhaust
        {
            const size_t before = m.faces.size();
            AddBoxFaces(m, MyVector(-0.37f, -0.005f, 0.05f * static_cast<float>(e)), MyVector(0.04f, 0.032f, 0.032f), 1.0f, true, MyColor(60, 58, 66, 255));
            m.faces[before + 1].glow = true; m.faces[before + 1].color = MyColor(255, 140, 40, 255);   // the -X face is the exhaust port
        }
        return m;
    }

    // Heavy warship (death-ship / command ship): armoured block hull, a pointed prow, a raised bridge, two side pods and three
    // big engines.  Nose along +X, about 1 unit long.
    Mesh BuildWarshipMesh()
    {
        Mesh m;
        auto V = [&](float x, float y, float z) { m.verts.push_back(MyVector(x, y, z)); return static_cast<int>(m.verts.size()) - 1; };
        const MyColor dark(58, 60, 72, 255);
        AddBoxFaces(m, MyVector(-0.06f, 0.0f, 0.0f), MyVector(0.30f, 0.075f, 0.13f), 1.0f, false, MyColor());          // hull (instance colour)
        const int p0 = V(0.24f, 0.075f, 0.13f), p1 = V(0.24f, 0.075f, -0.13f), p2 = V(0.24f, -0.075f, -0.13f), p3 = V(0.24f, -0.075f, 0.13f), tip = V(0.50f, -0.010f, 0.0f);
        const MyVector inside(0.0f, 0.0f, 0.0f);
        AddFaceAuto(m, { tip, p0, p1 }, inside, 1.0f); AddFaceAuto(m, { tip, p1, p2 }, inside, 0.8f);
        AddFaceAuto(m, { tip, p2, p3 }, inside, 0.6f); AddFaceAuto(m, { tip, p3, p0 }, inside, 0.8f);                // prow
        AddBoxFaces(m, MyVector(-0.08f, 0.135f, 0.0f), MyVector(0.13f, 0.06f, 0.055f), 0.92f, false, MyColor());      // bridge tower
        AddBoxFaces(m, MyVector(0.0f, 0.18f, 0.0f), MyVector(0.04f, 0.012f, 0.04f), 1.0f, true, MyColor(255, 70, 60, 255));
        MarkGlow(m, 6, MyColor(255, 70, 60, 255));                                                                     // bridge beacon
        for (int s = -1; s <= 1; s += 2)
        {
            AddBoxFaces(m, MyVector(-0.12f, -0.01f, 0.30f * static_cast<float>(s)), MyVector(0.24f, 0.05f, 0.065f), 1.0f, true, dark);      // side pod
            AddBoxFaces(m, MyVector(-0.06f, -0.01f, 0.20f * static_cast<float>(s)), MyVector(0.07f, 0.022f, 0.075f), 1.0f, true, dark);     // strut
        }
        for (int e = -1; e <= 1; ++e)                                                                                   // three engines
        {
            const size_t before = m.faces.size();
            AddBoxFaces(m, MyVector(-0.375f, 0.0f, 0.075f * static_cast<float>(e)), MyVector(0.045f, 0.04f, 0.035f), 1.0f, true, dark);
            m.faces[before + 1].glow = true; m.faces[before + 1].color = MyColor(255, 130, 40, 255);
        }
        return m;
    }

    // Aerodynamic bomb, nose along +X, about 1 unit long: rounded body, yellow band, pointed nose and four tail fins.
    Mesh BuildBombMesh()
    {
        Mesh m;
        const auto& ring = FAST_UNIT_CIRCLE(8);
        if (ring.size() != 8) return m;
        auto V = [&](float x, float y, float z) { m.verts.push_back(MyVector(x, y, z)); return static_cast<int>(m.verts.size()) - 1; };
        const MyVector core(0.0f, 0.0f, 0.0f);
        auto station = [&](float x, float r, int out[8]) { for (int i = 0; i < 8; ++i) out[i] = V(x, r * ring[i].x, r * ring[i].y); };
        int a[8], b[8], c[8], d[8], e[8], f[8];
        station(-0.36f, 0.07f, a);  station(-0.20f, 0.14f, b);  station(0.02f, 0.18f, c);  station(0.14f, 0.18f, d);  station(0.32f, 0.12f, e);  station(0.44f, 0.05f, f);
        const int tail = V(-0.50f, 0.0f, 0.0f), nose = V(0.50f, 0.0f, 0.0f);
        const MyColor band(245, 205, 60, 255), dark(40, 42, 36, 255);
        for (int i = 0; i < 8; ++i)
        {
            const int j = (i + 1) % 8;
            AddFaceAuto(m, { tail, a[j], a[i] }, core, 0.8f, true, dark);
            AddFaceAuto(m, { a[i], a[j], b[j], b[i] }, core, 0.9f);
            AddFaceAuto(m, { b[i], b[j], c[j], c[i] }, core, 1.0f);
            AddFaceAuto(m, { c[i], c[j], d[j], d[i] }, core, 1.0f, true, band);
            AddFaceAuto(m, { d[i], d[j], e[j], e[i] }, core, 1.0f);
            AddFaceAuto(m, { e[i], e[j], f[j], f[i] }, core, 0.95f);
            AddFaceAuto(m, { f[i], f[j], nose }, core, 0.9f, true, dark);
        }
        for (int k = 0; k < 4; ++k)                                                                // four tail fins, 90 degrees apart
        {
            auto rot = [&](float y, float z, float& oy, float& oz) { switch (k) { case 0: oy = y; oz = z; break; case 1: oy = -z; oz = y; break; case 2: oy = -y; oz = -z; break; default: oy = z; oz = -y; } };
            const float px[4] = { -0.20f, -0.44f, -0.50f, -0.36f }, pr[4] = { 0.12f, 0.07f, 0.24f, 0.24f };
            int fr[4], bk[4];
            for (int i = 0; i < 4; ++i) { float y0, z0, y1, z1; rot(pr[i], 0.010f, y0, z0); rot(pr[i], -0.010f, y1, z1); fr[i] = V(px[i], y0, z0); bk[i] = V(px[i], y1, z1); }
            float cy, cz; rot(0.16f, 0.0f, cy, cz);
            const MyVector fc(-0.34f, cy, cz);
            AddFaceAuto(m, { fr[0], fr[1], fr[2], fr[3] }, fc, 0.9f, true, dark);
            AddFaceAuto(m, { bk[0], bk[1], bk[2], bk[3] }, fc, 0.9f, true, dark);
            for (int i = 1; i < 4; ++i) { const int j = (i + 1) % 4; AddFaceAuto(m, { fr[i], fr[j], bk[j], bk[i] }, fc, 0.85f, true, dark); }
        }
        return m;
    }

    // Fuel tank: a vertical octagonal drum with a white band, a domed top and a steel base ring.  y from -0.5 to +0.5.
    Mesh BuildFuelTankMesh()
    {
        Mesh m;
        const auto& ring = FAST_UNIT_CIRCLE(8);
        if (ring.size() != 8) return m;
        auto V = [&](float x, float y, float z) { m.verts.push_back(MyVector(x, y, z)); return static_cast<int>(m.verts.size()) - 1; };
        const MyVector core(0.0f, 0.0f, 0.0f);
        auto station = [&](float y, float r, int out[8]) { for (int i = 0; i < 8; ++i) out[i] = V(r * ring[i].x, y, r * ring[i].y); };
        int s0[8], s1[8], s2[8], s3[8], s4[8], s5[8];
        station(-0.50f, 0.34f, s0);  station(-0.40f, 0.38f, s1);  station(-0.12f, 0.38f, s2);  station(0.14f, 0.38f, s3);  station(0.38f, 0.34f, s4);  station(0.48f, 0.16f, s5);
        const int top = V(0.0f, 0.50f, 0.0f);
        const MyColor steel(120, 124, 136, 255), white(238, 238, 244, 255);
        std::vector<int> bottom;
        for (int i = 0; i < 8; ++i) bottom.push_back(s0[i]);
        AddFaceVec(m, bottom, core, 0.5f, true, steel);
        for (int i = 0; i < 8; ++i)
        {
            const int j = (i + 1) % 8;
            AddFaceAuto(m, { s0[i], s0[j], s1[j], s1[i] }, core, 1.0f, true, steel);            // base ring
            AddFaceAuto(m, { s1[i], s1[j], s2[j], s2[i] }, core, 1.0f);                         // drum (instance colour)
            AddFaceAuto(m, { s2[i], s2[j], s3[j], s3[i] }, core, 1.0f, true, white);            // band
            AddFaceAuto(m, { s3[i], s3[j], s4[j], s4[i] }, core, 1.0f);
            AddFaceAuto(m, { s4[i], s4[j], s5[j], s5[i] }, core, 0.95f);                         // shoulder
            AddFaceAuto(m, { s5[i], s5[j], top }, core, 0.9f, true, steel);                      // cap
        }
        return m;
    }

    // Anti-air turret: a wide octagonal base, a neck, a domed head and a long gun barrel pointing along +X.  y from -0.5 to +0.5.
    Mesh BuildTurretMesh(bool withGun)
    {
        Mesh m;
        const auto& ring = FAST_UNIT_CIRCLE(8);
        if (ring.size() != 8) return m;
        auto V = [&](float x, float y, float z) { m.verts.push_back(MyVector(x, y, z)); return static_cast<int>(m.verts.size()) - 1; };
        const MyVector core(0.0f, 0.0f, 0.0f);
        auto station = [&](float y, float r, int out[8]) { for (int i = 0; i < 8; ++i) out[i] = V(r * ring[i].x, y, r * ring[i].y); };
        int a[8], b[8], c[8], d[8], e[8], f[8], g[8];
        station(-0.50f, 0.50f, a);  station(-0.28f, 0.46f, b);  station(-0.28f, 0.28f, c);  station(0.02f, 0.28f, d);  station(0.02f, 0.40f, e);  station(0.22f, 0.36f, f);  station(0.38f, 0.16f, g);
        const int top = V(0.0f, 0.46f, 0.0f);
        const MyColor steel(110, 114, 128, 255);
        for (int i = 0; i < 8; ++i)
        {
            const int j = (i + 1) % 8;
            AddFaceAuto(m, { a[i], a[j], b[j], b[i] }, core, 0.85f, true, steel);               // base
            AddFaceAuto(m, { b[i], b[j], c[j], c[i] }, core, 1.0f, true, steel);                // base top step
            AddFaceAuto(m, { c[i], c[j], d[j], d[i] }, core, 0.9f);                             // neck
            AddFaceAuto(m, { d[i], d[j], e[j], e[i] }, core, 0.8f, true, steel);                // head underside
            AddFaceAuto(m, { e[i], e[j], f[j], f[i] }, core, 1.0f);                             // head (instance colour)
            AddFaceAuto(m, { f[i], f[j], g[j], g[i] }, core, 1.0f);
            AddFaceAuto(m, { g[i], g[j], top }, core, 0.95f);                                    // dome
        }
        if (withGun) AddBoxFaces(m, MyVector(0.46f, 0.16f, 0.0f), MyVector(0.30f, 0.045f, 0.045f), 1.0f, true, MyColor(46, 48, 58, 255));   // barrel
        return m;
    }

    // TIE-style fighter: a spherical cockpit with a window, two long hexagonal solar panels joined by a strut.
    // The cockpit window faces +X; the panels are vertical plates at +/-Z.  About 1 unit tall.
    Mesh BuildTieMesh()
    {
        Mesh m;
        const int N = 8;
        const auto& ring = FAST_UNIT_CIRCLE(N);
        if (static_cast<int>(ring.size()) != N) return m;
        auto V = [&](float x, float y, float z) { m.verts.push_back(MyVector(x, y, z)); return static_cast<int>(m.verts.size()) - 1; };
        const MyVector core(0.0f, 0.0f, 0.0f);
        const MyColor hull(112, 116, 128, 255), glass(30, 60, 90, 255), panel(46, 48, 60, 255), rib(78, 82, 96, 255);
        auto station = [&](float x, float r, int out[8]) { for (int i = 0; i < N; ++i) out[i] = V(x, r * ring[i].x, r * ring[i].y); };
        int r0[8], r1[8], r2[8], r3[8], r4[8];
        station(-0.17f, 0.07f, r0);  station(-0.09f, 0.15f, r1);  station(0.0f, 0.18f, r2);  station(0.09f, 0.15f, r3);  station(0.17f, 0.07f, r4);
        const int back = V(-0.20f, 0.0f, 0.0f), front = V(0.20f, 0.0f, 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const int j = (i + 1) % N;
            AddFaceAuto(m, { back, r0[j], r0[i] }, core, 0.7f, true, hull);
            AddFaceAuto(m, { r0[i], r0[j], r1[j], r1[i] }, core, 0.9f, true, hull);
            AddFaceAuto(m, { r1[i], r1[j], r2[j], r2[i] }, core, 1.0f, true, hull);
            AddFaceAuto(m, { r2[i], r2[j], r3[j], r3[i] }, core, 1.0f, true, hull);
            AddFaceAuto(m, { r3[i], r3[j], r4[j], r4[i] }, core, 0.9f, true, hull);
            AddFaceAuto(m, { r4[i], r4[j], front }, core, 1.0f, true, glass);                    // cockpit window
        }
        AddBoxFaces(m, MyVector(0.0f, 0.0f, 0.0f), MyVector(0.03f, 0.03f, 0.50f), 1.0f, true, rib);    // strut between the panels
        for (int s = -1; s <= 1; s += 2)                                                           // hexagonal panels
        {
            const float zc = 0.50f * static_cast<float>(s), t = 0.022f;
            int fr[6], bk[6];
            for (int i = 0; i < 6; ++i)
            {
                const float ang = 1.0471976f * static_cast<float>(i);                                // 60 degrees
                const float px = 0.44f * std::cos(ang), py = 0.44f * std::sin(ang);
                fr[i] = V(px, py, zc + t); bk[i] = V(px, py, zc - t);
            }
            const MyVector c(0.0f, 0.0f, zc);
            AddFaceVec(m, { fr[0], fr[1], fr[2], fr[3], fr[4], fr[5] }, c, 1.0f, true, panel);
            AddFaceVec(m, { bk[0], bk[1], bk[2], bk[3], bk[4], bk[5] }, c, 1.0f, true, panel);
            for (int i = 0; i < 6; ++i) { const int j = (i + 1) % 6; AddFaceAuto(m, { fr[i], fr[j], bk[j], bk[i] }, c, 0.9f, true, rib); }
        }
        return m;
    }

    // Pac-Man 3D: one HALF of the sphere (top or bottom) with a dark flat inside; the game hinges the two halves apart to chomp.
    Mesh BuildPacHalfMesh(bool top)
    {
        Mesh m;
        const int N = 12;
        const auto& ring = FAST_UNIT_CIRCLE(N);
        if (static_cast<int>(ring.size()) != N) return m;
        auto V = [&](float x, float y, float z) { m.verts.push_back(MyVector(x, y, z)); return static_cast<int>(m.verts.size()) - 1; };
        const float sgn = top ? 1.0f : -1.0f;
        const MyVector core(0.0f, 0.0f, 0.0f);
        int s0[12], s1[12], s2[12], s3[12];
        const float ys[4] = { 0.0f, 0.22f, 0.40f, 0.49f }, rs[4] = { 0.50f, 0.45f, 0.30f, 0.12f };      // latitude stations of a 0.5-radius sphere
        int* st[4] = { s0, s1, s2, s3 };
        for (int k = 0; k < 4; ++k) for (int i = 0; i < N; ++i) st[k][i] = V(rs[k] * ring[i].x, sgn * ys[k], rs[k] * ring[i].y);
        const int pole = V(0.0f, sgn * 0.5f, 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const int j = (i + 1) % N;
            AddFaceAuto(m, { s0[i], s0[j], s1[j], s1[i] }, core, 1.0f);
            AddFaceAuto(m, { s1[i], s1[j], s2[j], s2[i] }, core, 1.0f);
            AddFaceAuto(m, { s2[i], s2[j], s3[j], s3[i] }, core, 0.95f);
            AddFaceAuto(m, { s3[i], s3[j], pole }, core, 0.9f);
        }
        std::vector<int> cap;                                                                          // flat inner face, dark red
        for (int i = 0; i < N; ++i) cap.push_back(s0[i]);
        AddFaceVec(m, cap, MyVector(0.0f, sgn * 0.25f, 0.0f), 0.6f, true, MyColor(120, 20, 28, 255));
        return m;
    }

    // Voxel mesh from an ASCII picture.  rows[0] is the top row.  '.' = empty, '#' = instance colour, a = cyan lamp, r = red lamp,
    // y = yellow, w = white, k = dark metal, s = skin, b = blue.  `depth` layers are extruded in Z.  Only faces that touch empty
    // space are generated, and corners are shared on the voxel lattice, so even a busy sprite stays small.
    Mesh BuildVoxelMesh(const std::vector<std::string>& rows, int depth)
    {
        Mesh m;
        const int H = static_cast<int>(rows.size());
        int W = 0;
        for (const std::string& r : rows) W = (std::max)(W, static_cast<int>(r.size()));
        const int D = depth;
        const float vs = 1.0f / static_cast<float>((std::max)(W, H));                                 // longest side = 1 unit
        auto at = [&](int x, int j, int z) -> char
        {
            if (x < 0 || x >= W || j < 0 || j >= H || z < 0 || z >= D) return '.';
            return (x < static_cast<int>(rows[j].size())) ? rows[j][x] : '.';
        };
        std::vector<int> lattice(static_cast<size_t>((W + 1) * (H + 1) * (D + 1)), -1);
        auto corner = [&](int i, int j, int k) -> int
        {
            int& slot = lattice[static_cast<size_t>((k * (H + 1) + j) * (W + 1) + i)];
            if (slot < 0)
            {
                m.verts.push_back(MyVector((static_cast<float>(i) - W * 0.5f) * vs, (H * 0.5f - static_cast<float>(j)) * vs, (static_cast<float>(k) - D * 0.5f) * vs));
                slot = static_cast<int>(m.verts.size()) - 1;
            }
            return slot;
        };
        static const int kDelta[6][3] = { {1, 0, 0}, {-1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, -1} };      // +x -x up down +z -z
        static const int kQuad[6][4][3] = {
            { {1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1} }, { {0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0} },
            { {0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1} }, { {0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0} },
            { {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1} }, { {0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0} } };
        static const float kTone[6] = { 0.85f, 0.85f, 1.0f, 0.6f, 0.9f, 0.7f };
        for (int k = 0; k < D; ++k)
            for (int j = 0; j < H; ++j)
                for (int x = 0; x < W; ++x)
                {
                    const char c = at(x, j, k);
                    if (c == '.' || c == ' ') continue;
                    bool fixed = true, glow = false;
                    MyColor col(255, 255, 255, 255);
                    switch (c)
                    {
                    case '#': fixed = false; break;
                    case 'a': col = MyColor(90, 235, 255, 255); glow = true; break;
                    case 'r': col = MyColor(255, 70, 60, 255);  glow = true; break;
                    case 'y': col = MyColor(255, 220, 70, 255); break;
                    case 'w': col = MyColor(240, 240, 246, 255); break;
                    case 's': col = MyColor(240, 200, 160, 255); break;
                    case 'b': col = MyColor(50, 70, 160, 255); break;
                    default:  col = MyColor(52, 54, 66, 255); break;                               // 'k' and anything else: dark metal
                    }
                    const MyVector centre((static_cast<float>(x) + 0.5f - W * 0.5f) * vs, (H * 0.5f - static_cast<float>(j) - 0.5f) * vs, (static_cast<float>(k) + 0.5f - D * 0.5f) * vs);
                    for (int f = 0; f < 6; ++f)
                    {
                        if (at(x + kDelta[f][0], j + kDelta[f][1], k + kDelta[f][2]) != '.') continue;      // hidden face: skip
                        int q[4];
                        for (int v = 0; v < 4; ++v) q[v] = corner(x + kQuad[f][v][0], j + kQuad[f][v][1], k + kQuad[f][v][2]);
                        AddFaceAuto(m, { q[0], q[1], q[2], q[3] }, centre, kTone[f], fixed, col, glow);
                    }
                }
        return m;
    }

    // Articulated person, built from rigid parts that the game hinges at the shoulders and hips.  All parts are 1 unit tall in total,
    // facing +X, left / right = +/-Z.  The arm and leg meshes have their ORIGIN AT THE JOINT and hang down along -Y, so rotating them
    // about the Z axis swings them like real limbs.
    Mesh BuildHumBodyMesh()                                                     // head + torso (torso takes the instance colour = shirt)
    {
        Mesh m;
        AddBoxFaces(m, MyVector(0.0f, 0.40f, 0.0f), MyVector(0.080f, 0.080f, 0.080f), 1.0f, true, MyColor(240, 200, 160, 255));
        AddBoxFaces(m, MyVector(0.0f, 0.12f, 0.0f), MyVector(0.070f, 0.200f, 0.120f), 1.0f, false, MyColor());
        return m;
    }
    Mesh BuildHumArmMesh()
    {
        Mesh m;
        AddBoxFaces(m, MyVector(0.0f, -0.17f, 0.0f), MyVector(0.040f, 0.170f, 0.040f), 1.0f, true, MyColor(240, 200, 160, 255));
        return m;
    }
    Mesh BuildHumLegMesh()
    {
        Mesh m;
        AddBoxFaces(m, MyVector(0.0f, -0.22f, 0.0f), MyVector(0.050f, 0.220f, 0.045f), 1.0f, true, MyColor(50, 62, 130, 255));
        return m;
    }

    // A gun barrel that pivots at its own origin (the game turns it about Z to track a target).  About 0.9 units long.
    Mesh BuildGunMesh()
    {
        Mesh m;
        AddBoxFaces(m, MyVector(0.34f, 0.0f, 0.0f), MyVector(0.34f, 0.05f, 0.05f), 1.0f, true, MyColor(46, 48, 58, 255));
        AddBoxFaces(m, MyVector(0.72f, 0.0f, 0.0f), MyVector(0.05f, 0.075f, 0.075f), 1.0f, true, MyColor(96, 100, 114, 255));      // muzzle ring
        return m;
    }

    Mesh BuildDiscMesh()                                              // unit floor disc in the XZ plane (32-gon)
    {
        Mesh m;
        const auto& ring = FAST_UNIT_CIRCLE(32);
        Face f;
        for (size_t i = 0; i < ring.size(); ++i)
        {
            m.verts.push_back(MyVector(ring[i].x, 0.0f, ring[i].y));
            f.idx.push_back(static_cast<int>(i));
        }
        f.normal = MyVector::Up();
        if (!ring.empty()) m.faces.push_back(f);
        return m;
    }

    Mesh BuildPuffMesh()
    {
        Mesh m;
        std::vector<MyVector> v;
        std::vector<std::array<int, 3>> tris;
        BuildIcoSphere(1, v, tris);
        m.verts = v;
        for (const auto& tr : tris) AddFaceAuto(m, { tr[0], tr[1], tr[2] }, MyVector(0.0f, 0.0f, 0.0f), 1.0f, false, MyColor(), true);     // glow: full brightness
        return m;
    }

    // A glass marble: an 80-face icosphere whose faces are painted in two alternating swirl bands, so you can SEE it roll.
    Mesh BuildMarbleMesh(const MyColor& a, const MyColor& b)
    {
        Mesh m;
        std::vector<MyVector> v;
        std::vector<std::array<int, 3>> tris;
        BuildIcoSphere(2, v, tris);
        m.verts = v;
        for (const auto& tr : tris)
        {
            const MyVector c = (v[tr[0]] + v[tr[1]] + v[tr[2]]) * (1.0f / 3.0f);
            // a curving swirl: the band index bends with the height, so the stripes wrap round the ball like glass-marble ribbons
            const float s = (c.x + 0.8f * c.y + 0.4f * c.z) * 3.0f + 1.1f * c.y * c.z + 8.0f;
            const int band = static_cast<int>(std::floor(s)) & 1;
            const float fine = 0.90f + 0.20f * (0.5f + 0.5f * FAST_SIN(c.x * 17.0f + c.z * 11.0f));            // subtle per-face sparkle
            AddFaceAuto(m, { tr[0], tr[1], tr[2] }, MyVector(0.0f, 0.0f, 0.0f), 1.0f, true, Shade(band ? a : b, fine));
        }
        return m;
    }

    // The red SPIKE ROLLER: a ball with twelve sharp spikes (the icosahedron's corners pulled out), red body, pale spike tips.
    Mesh BuildSpikeBallMesh()
    {
        Mesh m;
        std::vector<MyVector> v;
        std::vector<std::array<int, 3>> tris;
        BuildIcoSphere(2, v, tris);
        for (int i = 0; i < 12 && i < static_cast<int>(v.size()); ++i) v[i] = v[i] * 1.75f;                  // the first twelve vertices are the icosahedron corners
        m.verts = v;
        for (const auto& tr : tris)
        {
            const bool tip = (tr[0] < 12 || tr[1] < 12 || tr[2] < 12);
            const MyVector c = (v[tr[0]] + v[tr[1]] + v[tr[2]]) * (1.0f / 3.0f);
            const float k = 0.85f + 0.15f * FAST_SIN(c.x * 13.0f + c.y * 7.0f);
            AddFaceAuto(m, { tr[0], tr[1], tr[2] }, MyVector(0.0f, 0.0f, 0.0f), 1.0f, true, tip ? MyColor(255, 225, 200, 255) : Shade(MyColor(205, 55, 45, 255), k));
        }
        return m;
    }

    // A smooth ball (162 vertices, 320 faces) for planets: the instance colour paints it.
    Mesh BuildBallMesh()
    {
        Mesh m;
        std::vector<MyVector> v;
        std::vector<std::array<int, 3>> tris;
        BuildIcoSphere(2, v, tris);
        m.verts = v;
        for (const auto& tr : tris) AddFaceAuto(m, { tr[0], tr[1], tr[2] }, MyVector(0.0f, 0.0f, 0.0f), 1.0f);
        return m;
    }

    // A patrol robot for Jumpman: treads, red torso, grey head with a glowing visor, antenna and arms.  Faces +X, feet at y = 0.
    Mesh BuildRobotMesh()
    {
        Mesh m;
        AddBoxFaces(m, MyVector(0.0f, 0.14f, 0.0f), MyVector(0.30f, 0.12f, 0.26f), 1.0f, true, MyColor(70, 76, 92, 255));                // treads
        AddBoxFaces(m, MyVector(0.0f, 0.48f, 0.0f), MyVector(0.24f, 0.22f, 0.20f), 1.0f, true, MyColor(196, 60, 50, 255));              // torso
        AddBoxFaces(m, MyVector(0.02f, 0.84f, 0.0f), MyVector(0.17f, 0.13f, 0.16f), 1.0f, true, MyColor(150, 156, 172, 255));           // head
        AddBoxFaces(m, MyVector(0.19f, 0.86f, 0.0f), MyVector(0.025f, 0.04f, 0.11f), 1.0f, true, MyColor(255, 220, 60, 255));           // visor
        MarkGlow(m, 6, MyColor(255, 225, 70, 255));
        AddBoxFaces(m, MyVector(0.0f, 1.08f, 0.0f), MyVector(0.02f, 0.10f, 0.02f), 1.0f, true, MyColor(120, 124, 136, 255));            // antenna
        AddBoxFaces(m, MyVector(0.0f, 1.20f, 0.0f), MyVector(0.05f, 0.04f, 0.05f), 1.0f, true, MyColor(255, 80, 80, 255));
        MarkGlow(m, 6, MyColor(255, 90, 90, 255));
        AddBoxFaces(m, MyVector(0.02f, 0.50f, 0.30f), MyVector(0.06f, 0.18f, 0.05f), 1.0f, true, MyColor(120, 126, 142, 255));          // arms
        AddBoxFaces(m, MyVector(0.02f, 0.50f, -0.30f), MyVector(0.06f, 0.18f, 0.05f), 1.0f, true, MyColor(120, 126, 142, 255));
        return m;
    }

    // A winged demon: red body, horns, bat wings and glowing eyes.  Faces +X.
    Mesh BuildDemonMesh()
    {
        Mesh m;
        AddBoxFaces(m, MyVector(0.0f, 0.38f, 0.0f), MyVector(0.20f, 0.26f, 0.16f), 1.0f, true, MyColor(176, 34, 34, 255));              // torso
        AddBoxFaces(m, MyVector(0.10f, 0.78f, 0.0f), MyVector(0.14f, 0.13f, 0.13f), 1.0f, true, MyColor(200, 46, 40, 255));            // head
        AddBoxFaces(m, MyVector(0.13f, 0.97f, 0.09f), MyVector(0.03f, 0.10f, 0.03f), 1.0f, true, MyColor(236, 226, 196, 255));         // horns
        AddBoxFaces(m, MyVector(0.13f, 0.97f, -0.09f), MyVector(0.03f, 0.10f, 0.03f), 1.0f, true, MyColor(236, 226, 196, 255));
        AddBoxFaces(m, MyVector(0.235f, 0.81f, 0.06f), MyVector(0.02f, 0.025f, 0.03f), 1.0f, true, MyColor(255, 230, 60, 255));        // eyes
        MarkGlow(m, 6, MyColor(255, 230, 70, 255));
        AddBoxFaces(m, MyVector(0.235f, 0.81f, -0.06f), MyVector(0.02f, 0.025f, 0.03f), 1.0f, true, MyColor(255, 230, 60, 255));
        MarkGlow(m, 6, MyColor(255, 230, 70, 255));
        AddBoxFaces(m, MyVector(-0.12f, 0.58f, 0.46f), MyVector(0.16f, 0.30f, 0.025f), 0.9f, true, MyColor(120, 20, 60, 255));         // wings
        AddBoxFaces(m, MyVector(-0.12f, 0.58f, -0.46f), MyVector(0.16f, 0.30f, 0.025f), 0.9f, true, MyColor(120, 20, 60, 255));
        AddBoxFaces(m, MyVector(0.0f, 0.04f, 0.09f), MyVector(0.07f, 0.12f, 0.06f), 1.0f, true, MyColor(140, 26, 26, 255));            // legs
        AddBoxFaces(m, MyVector(0.0f, 0.04f, -0.09f), MyVector(0.07f, 0.12f, 0.06f), 1.0f, true, MyColor(140, 26, 26, 255));
        AddBoxFaces(m, MyVector(-0.30f, 0.30f, 0.0f), MyVector(0.14f, 0.03f, 0.03f), 1.0f, true, MyColor(150, 30, 30, 255));          // tail
        return m;
    }

    // A monster generator: a heap of bones around a smouldering core.
    Mesh BuildGeneratorMesh()
    {
        Mesh m;
        AddBoxFaces(m, MyVector(0.0f, 0.18f, 0.0f), MyVector(0.46f, 0.18f, 0.46f), 1.0f, true, MyColor(84, 70, 60, 255));
        static const float bx[6] = { -0.30f, 0.28f, 0.0f, -0.12f, 0.20f, 0.0f }, bz[6] = { 0.20f, -0.26f, 0.0f, -0.30f, 0.26f, 0.30f }, bh[6] = { 0.30f, 0.34f, 0.46f, 0.26f, 0.28f, 0.22f };
        for (int i = 0; i < 6; ++i)
            AddBoxFaces(m, MyVector(bx[i], 0.36f + bh[i] * 0.5f, bz[i]), MyVector(0.05f, bh[i] * 0.5f, 0.05f), 1.0f, true, MyColor(236, 230, 210, 255));
        AddBoxFaces(m, MyVector(0.0f, 0.62f, 0.0f), MyVector(0.14f, 0.14f, 0.14f), 1.0f, true, MyColor(255, 110, 40, 255));
        MarkGlow(m, 6, MyColor(255, 120, 40, 255));
        return m;
    }

    Mesh BuildChestMesh()
    {
        Mesh m;
        AddBoxFaces(m, MyVector(0.0f, 0.14f, 0.0f), MyVector(0.28f, 0.14f, 0.18f), 1.0f, true, MyColor(140, 84, 36, 255));
        AddBoxFaces(m, MyVector(0.0f, 0.32f, 0.0f), MyVector(0.29f, 0.05f, 0.19f), 1.0f, true, MyColor(250, 210, 70, 255));
        AddBoxFaces(m, MyVector(0.0f, 0.18f, 0.185f), MyVector(0.05f, 0.06f, 0.015f), 1.0f, true, MyColor(255, 240, 150, 255));
        return m;
    }

    // DEATH: a tall dark human in a hooded robe with an empty black face, two glowing red eyes and a scythe.  Faces +X, feet at y = 0.
    Mesh BuildDeathMesh()
    {
        Mesh m;
        const MyColor robe(66, 44, 96, 255), cloth(44, 30, 66, 255), steel(170, 170, 190, 255);
        AddBoxFaces(m, MyVector(0.0f, 0.20f, 0.08f), MyVector(0.06f, 0.20f, 0.05f), 1.0f, true, cloth);                  // legs
        AddBoxFaces(m, MyVector(0.0f, 0.20f, -0.08f), MyVector(0.06f, 0.20f, 0.05f), 1.0f, true, cloth);
        AddBoxFaces(m, MyVector(0.0f, 0.62f, 0.0f), MyVector(0.17f, 0.26f, 0.16f), 1.0f, true, robe);                    // robe
        AddBoxFaces(m, MyVector(0.0f, 0.30f, 0.0f), MyVector(0.20f, 0.12f, 0.19f), 0.9f, true, robe);                    // ragged hem
        AddBoxFaces(m, MyVector(0.0f, 0.86f, 0.0f), MyVector(0.14f, 0.06f, 0.24f), 1.0f, true, robe);                    // shoulders
        AddBoxFaces(m, MyVector(0.05f, 0.62f, 0.27f), MyVector(0.05f, 0.20f, 0.05f), 1.0f, true, cloth);                 // arms
        AddBoxFaces(m, MyVector(0.05f, 0.62f, -0.27f), MyVector(0.05f, 0.20f, 0.05f), 1.0f, true, cloth);
        AddBoxFaces(m, MyVector(0.02f, 1.02f, 0.0f), MyVector(0.14f, 0.15f, 0.13f), 1.0f, true, cloth);                  // hood
        AddBoxFaces(m, MyVector(0.145f, 1.02f, 0.0f), MyVector(0.012f, 0.09f, 0.09f), 1.0f, true, MyColor(6, 6, 10, 255)); // the void inside it
        AddBoxFaces(m, MyVector(0.16f, 1.04f, 0.05f), MyVector(0.012f, 0.025f, 0.03f), 1.0f, true, MyColor(255, 30, 30, 255));   // eyes
        MarkGlow(m, 6, MyColor(255, 40, 40, 255));
        AddBoxFaces(m, MyVector(0.16f, 1.04f, -0.05f), MyVector(0.012f, 0.025f, 0.03f), 1.0f, true, MyColor(255, 30, 30, 255));
        MarkGlow(m, 6, MyColor(255, 40, 40, 255));
        AddBoxFaces(m, MyVector(0.10f, 0.72f, -0.36f), MyVector(0.02f, 0.72f, 0.02f), 1.0f, true, MyColor(90, 70, 60, 255));     // scythe pole
        AddBoxFaces(m, MyVector(0.28f, 1.42f, -0.36f), MyVector(0.20f, 0.025f, 0.02f), 1.0f, true, steel);                       // scythe blade
        return m;
    }

    // ================================================================================================
    //  CREATURE KIT - articulated, hand-modelled dungeon monsters
    //  Each creature is a body mesh plus separately animated limbs (arms, legs, wings) that hinge at their own origin and are
    //  swung by SubmitCreature, exactly like the articulated human.  The pieces are built from three primitives:
    //      AddBar    a square-section bar from point A to point B with a different radius at each end (limbs, horns, claws, clubs,
    //                tails, scythe blades - any diagonal)
    //      AddTaper  a box that narrows (or widens) along Y (torsos, hips, necks, sleeves)
    //      AddFrustum an 8-sided tapered cone (rounded ghost / robe bodies)
    //  plus AddMembrane (a thin double-sided face for bat wings).  Everything is built once at start-up.
    // ================================================================================================
    void AddBar(Mesh& m, const MyVector& a, const MyVector& b, float ra, float rb, const MyColor& col, float tone)
    {
        const MyVector d = b - a;
        const float len = d.Magnitude();
        if (len < 1.0e-4f) return;
        const MyVector dn = d / len;
        const MyVector ref = (MyMath::Abs(dn.y) < 0.9f) ? MyVector::Up() : MyVector::Right();
        const MyVector u = dn.Cross(ref).Normalized(), v = dn.Cross(u).Normalized();
        const int base = static_cast<int>(m.verts.size());
        static const float su[4] = { 1.0f, -1.0f, -1.0f, 1.0f }, sv[4] = { 1.0f, 1.0f, -1.0f, -1.0f };
        for (int k = 0; k < 4; ++k) m.verts.push_back(a + u * (su[k] * ra) + v * (sv[k] * ra));
        for (int k = 0; k < 4; ++k) m.verts.push_back(b + u * (su[k] * rb) + v * (sv[k] * rb));
        const MyVector mid = (a + b) * 0.5f;
        for (int k = 0; k < 4; ++k)
        {
            const int n = (k + 1) & 3;
            AddFaceAuto(m, { base + k, base + n, base + 4 + n, base + 4 + k }, mid, tone, true, col);
        }
        AddFaceAuto(m, { base, base + 1, base + 2, base + 3 }, mid, tone * 0.8f, true, col);
        AddFaceAuto(m, { base + 4, base + 5, base + 6, base + 7 }, mid, tone, true, col);
    }

    void AddTaper(Mesh& m, const MyVector& c0, float hx0, float hz0, const MyVector& c1, float hx1, float hz1, const MyColor& col, float tone)
    {
        const int base = static_cast<int>(m.verts.size());
        static const float sx[4] = { -1.0f, 1.0f, 1.0f, -1.0f }, sz[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
        for (int k = 0; k < 4; ++k) m.verts.push_back(MyVector(c0.x + sx[k] * hx0, c0.y, c0.z + sz[k] * hz0));
        for (int k = 0; k < 4; ++k) m.verts.push_back(MyVector(c1.x + sx[k] * hx1, c1.y, c1.z + sz[k] * hz1));
        const MyVector mid = (c0 + c1) * 0.5f;
        for (int k = 0; k < 4; ++k)
        {
            const int n = (k + 1) & 3;
            AddFaceAuto(m, { base + k, base + n, base + 4 + n, base + 4 + k }, mid, tone, true, col);
        }
        AddFaceAuto(m, { base, base + 1, base + 2, base + 3 }, mid, tone * 0.8f, true, col);
        AddFaceAuto(m, { base + 4, base + 5, base + 6, base + 7 }, mid, tone, true, col);
    }

    // A thin face visible from both sides (wing membrane): the same polygon twice with opposite normals.
    void AddMembrane(Mesh& m, std::initializer_list<MyVector> pts, const MyColor& col, float tone)
    {
        std::vector<int> idx;
        MyVector centroid;
        for (const MyVector& p : pts) { m.verts.push_back(p); idx.push_back(static_cast<int>(m.verts.size()) - 1); centroid += p; }
        centroid = centroid / static_cast<float>(idx.size());
        const MyVector n = (m.verts[idx[1]] - m.verts[idx[0]]).Cross(m.verts[idx[2]] - m.verts[idx[0]]).Normalized();
        AddFaceVec(m, idx, centroid - n, tone, true, col);
        AddFaceVec(m, idx, centroid + n, tone * 0.8f, true, col);
    }

    // ---- shared builders for the second batch of dungeon monsters ---------------------------------------------------------------------------------------------
    // A parametrised two-legged, two-armed figure in the grunt's proportions (torso, neck, head, legs and arms that hinge at the creature's own hinge points).  `bulk` widens it,
    // `hgt` stretches it, `headS` sizes the head.  The caller adds horns, weapons, eyes, robes ... to c.body / the limbs afterwards (the head centre is returned through headY).
    static void CrHumanoid(Creature& c, const MyColor& skin, const MyColor& skinD, const MyColor& cloth, const MyColor& metal, float bulk, float hgt, float headS, float& headY)
    {
        Mesh& b = c.body;
        const float hip = 0.40f * hgt, top = 0.78f * hgt;
        AddTaper(b, MyVector(0.0f, hip * 0.9f, 0.0f), 0.17f * bulk, 0.15f * bulk, MyVector(0.0f, hip * 1.15f, 0.0f), 0.19f * bulk, 0.17f * bulk, cloth);              // hips
        AddTaper(b, MyVector(0.01f, hip * 1.15f, 0.0f), 0.19f * bulk, 0.17f * bulk, MyVector(0.0f, top, 0.0f), 0.25f * bulk, 0.22f * bulk, skin);                   // chest
        AddTaper(b, MyVector(0.03f, top, 0.0f), 0.09f * bulk, 0.09f * bulk, MyVector(0.05f, top + 0.08f * hgt, 0.0f), 0.08f * bulk, 0.08f * bulk, skinD);            // neck
        headY = top + 0.18f * hgt;
        AddBoxFaces(b, MyVector(0.09f, headY, 0.0f), MyVector(0.11f * headS, 0.10f * headS, 0.10f * headS), 1.0f, true, skin);                                       // head
        c.armY = top - 0.01f; c.armZ = 0.31f * bulk; c.legY = hip * 1.15f; c.legZ = 0.12f * bulk;
        c.armSwing = 38.0f; c.legSwing = 36.0f; c.rate = 8.0f; c.bob = 0.04f;
        const float ll = c.legY;
        for (int s = -1; s <= 1; s += 2)
        {
            Mesh& l = (s > 0) ? c.legR : c.legL;
            AddTaper(l, MyVector(0.0f, 0.0f, 0.0f), 0.085f * bulk, 0.075f * bulk, MyVector(0.01f, -0.55f * ll, 0.0f), 0.065f * bulk, 0.06f * bulk, cloth);              // thigh
            AddTaper(l, MyVector(0.01f, -0.55f * ll, 0.0f), 0.06f * bulk, 0.055f * bulk, MyVector(0.02f, -0.88f * ll, 0.0f), 0.055f * bulk, 0.05f * bulk, skinD);         // shin
            AddBoxFaces(l, MyVector(0.06f, -0.96f * ll, 0.0f), MyVector(0.10f * bulk, 0.045f, 0.065f * bulk), 1.0f, true, metal);                                       // foot
            Mesh& a = (s > 0) ? c.armR : c.armL;
            const float al = 0.40f * hgt;
            AddTaper(a, MyVector(0.0f, 0.0f, 0.0f), 0.065f * bulk, 0.065f * bulk, MyVector(0.01f, -0.55f * al, 0.0f), 0.055f * bulk, 0.055f * bulk, skin);               // upper arm
            AddTaper(a, MyVector(0.01f, -0.55f * al, 0.0f), 0.055f * bulk, 0.055f * bulk, MyVector(0.03f, -0.92f * al, 0.0f), 0.05f * bulk, 0.05f * bulk, skinD);        // forearm
            AddBoxFaces(a, MyVector(0.04f, -al, 0.0f), MyVector(0.055f * bulk, 0.045f, 0.055f * bulk), 1.0f, true, skin);                                              // hand
        }
    }
    // A bat-style wing pair (bones + membrane) scaled by `k`, hinged at the creature's wingX / wingY / wingZ.
    static void CrWings(Creature& c, float k, const MyColor& membrane, const MyColor& bone)
    {
        for (int s = -1; s <= 1; s += 2)
        {
            const float z = static_cast<float>(s) * k;
            Mesh& w = (s > 0) ? c.wingR : c.wingL;
            AddBar(w, MyVector(0.0f, 0.0f, 0.04f * z), MyVector(-0.04f * k, 0.26f * k, 0.50f * z), 0.03f * k, 0.02f * k, bone);
            AddBar(w, MyVector(-0.04f * k, 0.26f * k, 0.50f * z), MyVector(-0.30f * k, 0.16f * k, 0.92f * z), 0.016f * k, 0.006f * k, bone);
            AddBar(w, MyVector(-0.04f * k, 0.26f * k, 0.50f * z), MyVector(-0.40f * k, -0.06f * k, 0.76f * z), 0.016f * k, 0.006f * k, bone);
            AddBar(w, MyVector(-0.04f * k, 0.26f * k, 0.50f * z), MyVector(-0.26f * k, -0.22f * k, 0.48f * z), 0.016f * k, 0.006f * k, bone);
            AddMembrane(w, { MyVector(0.0f, 0.0f, 0.04f * z), MyVector(-0.04f * k, 0.26f * k, 0.50f * z), MyVector(-0.30f * k, 0.16f * k, 0.92f * z), MyVector(-0.40f * k, -0.06f * k, 0.76f * z),
                             MyVector(-0.26f * k, -0.22f * k, 0.48f * z), MyVector(-0.06f * k, -0.18f * k, 0.12f * z) }, membrane, 0.9f);
        }
    }
    // A jelly blob (slime) of overall size `k`: stacked tapers, two eyes and a glossy highlight.
    static void CrBlob(Creature& c, float k, const MyColor& jelly, const MyColor& jellyD, const MyColor& eye)
    {
        Mesh& b = c.body;
        AddTaper(b, MyVector(0.0f, 0.00f, 0.0f), 0.30f * k, 0.30f * k, MyVector(0.0f, 0.14f * k, 0.0f), 0.38f * k, 0.38f * k, jellyD);
        AddTaper(b, MyVector(0.0f, 0.14f * k, 0.0f), 0.38f * k, 0.38f * k, MyVector(0.0f, 0.32f * k, 0.0f), 0.32f * k, 0.32f * k, jelly);
        AddTaper(b, MyVector(0.0f, 0.32f * k, 0.0f), 0.32f * k, 0.32f * k, MyVector(0.0f, 0.46f * k, 0.0f), 0.20f * k, 0.20f * k, jelly);
        AddTaper(b, MyVector(0.0f, 0.46f * k, 0.0f), 0.20f * k, 0.20f * k, MyVector(0.0f, 0.55f * k, 0.0f), 0.08f * k, 0.08f * k, jelly);
        for (int s = -1; s <= 1; s += 2)
        {
            AddBoxFaces(b, MyVector(0.30f * k, 0.38f * k, 0.10f * k * static_cast<float>(s)), MyVector(0.025f * k, 0.05f * k, 0.04f * k), 1.0f, true, MyColor(250, 250, 250, 255));
            AddBoxFaces(b, MyVector(0.325f * k, 0.38f * k, 0.10f * k * static_cast<float>(s)), MyVector(0.012f * k, 0.03f * k, 0.022f * k), 1.0f, true, eye);
        }
        AddBoxFaces(b, MyVector(0.20f * k, 0.50f * k, -0.12f * k), MyVector(0.03f * k, 0.03f * k, 0.03f * k), 1.0f, true, MyColor(235, 255, 235, 255));          // gloss
        c.bob = 0.07f * k; c.rate = 5.0f; c.armSwing = 0.0f; c.legSwing = 0.0f;
    }

    void BuildCreatures(CreatureSet& set)
    {
        const MyColor white(255, 255, 255, 255);
        // ---------------------------------------------------------------- GRUNT: a green, horn-helmeted club-swinging brute
        {
            Creature& c = set.grunt;
            const MyColor skin(96, 140, 70, 255), skinD(70, 106, 52, 255), iron(124, 128, 142, 255), leather(106, 74, 46, 255), cloth(156, 40, 38, 255),
                          bone(238, 232, 206, 255), wood(116, 82, 48, 255), gold(240, 200, 70, 255), eyeC(255, 220, 40, 255);
            Mesh& b = c.body;
            AddTaper(b, MyVector(0.0f, 0.36f, 0.0f), 0.17f, 0.15f, MyVector(0.0f, 0.46f, 0.0f), 0.19f, 0.17f, leather);                 // hips
            AddTaper(b, MyVector(0.01f, 0.46f, 0.0f), 0.19f, 0.17f, MyVector(0.0f, 0.74f, 0.0f), 0.25f, 0.22f, skin);                   // barrel chest
            AddBoxFaces(b, MyVector(0.19f, 0.43f, 0.0f), MyVector(0.02f, 0.03f, 0.04f), 1.0f, true, gold);                               // belt buckle
            AddBoxFaces(b, MyVector(0.17f, 0.29f, 0.0f), MyVector(0.015f, 0.12f, 0.11f), 1.0f, true, cloth);                             // loincloth
            AddBoxFaces(b, MyVector(-0.21f, 0.62f, 0.0f), MyVector(0.02f, 0.14f, 0.14f), 1.0f, true, iron);                              // back plate
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = 0.30f * static_cast<float>(s);
                AddTaper(b, MyVector(0.0f, 0.78f, z), 0.10f, 0.10f, MyVector(0.0f, 0.85f, z), 0.07f, 0.07f, iron);                       // pauldron
                AddBar(b, MyVector(0.0f, 0.85f, z), MyVector(0.0f, 0.98f, z * 1.03f), 0.035f, 0.006f, iron);                             // shoulder spike
                AddBar(b, MyVector(0.20f, 0.84f, 0.06f * static_cast<float>(s)), MyVector(0.23f, 0.94f, 0.06f * static_cast<float>(s)), 0.018f, 0.006f, bone);   // tusk
                AddBar(b, MyVector(0.07f, 1.03f, 0.12f * static_cast<float>(s)), MyVector(0.05f, 1.10f, 0.19f * static_cast<float>(s)), 0.03f, 0.02f, bone);   // horn
                AddBar(b, MyVector(0.05f, 1.10f, 0.19f * static_cast<float>(s)), MyVector(0.06f, 1.22f, 0.17f * static_cast<float>(s)), 0.02f, 0.005f, bone);
                AddBoxFaces(b, MyVector(0.195f, 0.96f, 0.06f * static_cast<float>(s)), MyVector(0.01f, 0.02f, 0.03f), 1.0f, true, eyeC);    // glowing eyes
                MarkGlow(b, 6, eyeC);
            }
            AddTaper(b, MyVector(0.03f, 0.74f, 0.0f), 0.09f, 0.09f, MyVector(0.05f, 0.82f, 0.0f), 0.08f, 0.08f, skinD);                  // neck
            AddBoxFaces(b, MyVector(0.09f, 0.92f, 0.0f), MyVector(0.11f, 0.10f, 0.10f), 1.0f, true, skin);                               // head
            AddBoxFaces(b, MyVector(0.13f, 0.845f, 0.0f), MyVector(0.09f, 0.035f, 0.09f), 1.0f, true, skinD);                            // jaw
            AddBoxFaces(b, MyVector(0.20f, 0.91f, 0.0f), MyVector(0.025f, 0.03f, 0.03f), 1.0f, true, skinD);                             // nose
            AddBoxFaces(b, MyVector(0.195f, 0.995f, 0.0f), MyVector(0.012f, 0.014f, 0.095f), 1.0f, true, skinD);                         // brow
            AddTaper(b, MyVector(0.07f, 1.00f, 0.0f), 0.125f, 0.125f, MyVector(0.07f, 1.10f, 0.0f), 0.07f, 0.07f, iron);                 // helmet
            c.armY = 0.77f; c.armZ = 0.31f; c.legY = 0.46f; c.legZ = 0.12f;
            c.armSwing = 40.0f; c.legSwing = 38.0f; c.rate = 8.0f; c.bob = 0.05f;
            for (int s = -1; s <= 1; s += 2)
            {
                Mesh& l = (s > 0) ? c.legR : c.legL;
                AddTaper(l, MyVector(0.0f, 0.0f, 0.0f), 0.085f, 0.075f, MyVector(0.01f, -0.22f, 0.0f), 0.065f, 0.06f, leather);           // thigh
                AddTaper(l, MyVector(0.01f, -0.22f, 0.0f), 0.06f, 0.055f, MyVector(0.02f, -0.34f, 0.0f), 0.055f, 0.05f, skinD);           // shin
                AddBoxFaces(l, MyVector(0.04f, -0.21f, 0.0f), MyVector(0.04f, 0.04f, 0.07f), 1.0f, true, iron);                          // knee guard
                AddBoxFaces(l, MyVector(0.06f, -0.40f, 0.0f), MyVector(0.10f, 0.045f, 0.065f), 1.0f, true, leather);                     // boot
                Mesh& a = (s > 0) ? c.armR : c.armL;
                AddTaper(a, MyVector(0.0f, 0.0f, 0.0f), 0.065f, 0.065f, MyVector(0.01f, -0.20f, 0.0f), 0.055f, 0.055f, skin);            // upper arm
                AddTaper(a, MyVector(0.01f, -0.20f, 0.0f), 0.055f, 0.055f, MyVector(0.03f, -0.36f, 0.0f), 0.05f, 0.05f, skinD);          // forearm
                AddBoxFaces(a, MyVector(0.02f, -0.30f, 0.0f), MyVector(0.062f, 0.05f, 0.062f), 1.0f, true, iron);                        // bracer
                AddBoxFaces(a, MyVector(0.04f, -0.40f, 0.0f), MyVector(0.055f, 0.045f, 0.055f), 1.0f, true, skin);                       // fist
                if (s > 0)                                                                                                                  // the right hand swings a spiked club
                {
                    AddBar(a, MyVector(0.04f, -0.40f, 0.0f), MyVector(0.16f, -0.18f, 0.0f), 0.025f, 0.035f, wood);
                    AddBar(a, MyVector(0.16f, -0.18f, 0.0f), MyVector(0.27f, 0.02f, 0.0f), 0.04f, 0.075f, wood, 0.9f);
                    AddBar(a, MyVector(0.27f, 0.02f, 0.0f), MyVector(0.27f, 0.15f, 0.0f), 0.025f, 0.005f, iron);
                    AddBar(a, MyVector(0.33f, 0.04f, 0.0f), MyVector(0.43f, 0.07f, 0.0f), 0.025f, 0.005f, iron);
                    AddBar(a, MyVector(0.27f, 0.02f, 0.07f), MyVector(0.28f, 0.05f, 0.16f), 0.022f, 0.005f, iron);
                    AddBar(a, MyVector(0.27f, 0.02f, -0.07f), MyVector(0.28f, 0.05f, -0.16f), 0.022f, 0.005f, iron);
                }
                else AddBar(a, MyVector(0.02f, -0.30f, 0.06f), MyVector(0.02f, -0.27f, 0.14f), 0.02f, 0.005f, iron);                       // spiked bracer
            }
        }
        // ---------------------------------------------------------------- GHOST: a ragged, hovering spectre with hollow eyes and clawed arms
        {
            Creature& c = set.ghost;
            const MyColor sheet(196, 214, 246, 255), sheetD(168, 190, 232, 255), claw(236, 244, 255, 255), black(4, 6, 14, 255), cyan(90, 240, 255, 255);
            Mesh& b = c.body;
            static const float ys[7] = { 0.00f, 0.22f, 0.46f, 0.70f, 0.90f, 1.04f, 1.18f };
            static const float rr[7] = { 0.26f, 0.30f, 0.31f, 0.29f, 0.23f, 0.19f, 0.10f };
            static const float cs[8] = { 1.0f, 0.7071f, 0.0f, -0.7071f, -1.0f, -0.7071f, 0.0f, 0.7071f }, sn[8] = { 0.0f, 0.7071f, 1.0f, 0.7071f, 0.0f, -0.7071f, -1.0f, -0.7071f };
            for (int r = 0; r < 7; ++r)
                for (int k = 0; k < 8; ++k)
                    b.verts.push_back(MyVector(rr[r] * cs[k], ys[r] + ((r == 0 && (k & 1)) ? 0.10f : 0.0f), rr[r] * 0.95f * sn[k]));   // the hem is ragged: every other point is lifted
            const MyVector core(0.0f, 0.6f, 0.0f);
            for (int r = 0; r < 6; ++r)
                for (int k = 0; k < 8; ++k)
                {
                    const int n = (k + 1) & 7, a0 = r * 8, a1 = (r + 1) * 8;
                    AddFaceAuto(b, { a0 + k, a0 + n, a1 + n, a1 + k }, core, 0.78f + 0.04f * static_cast<float>(r), true, (r < 2) ? sheetD : sheet);
                }
            AddFaceAuto(b, { 48, 49, 50, 51, 52, 53, 54, 55 }, core, 1.0f, true, sheet);
            for (int s = -1; s <= 1; s += 2)
            {
                AddBoxFaces(b, MyVector(0.19f, 1.05f, 0.075f * static_cast<float>(s)), MyVector(0.025f, 0.05f, 0.035f), 1.0f, true, black);        // hollow eye sockets
                AddBoxFaces(b, MyVector(0.215f, 1.05f, 0.075f * static_cast<float>(s)), MyVector(0.006f, 0.02f, 0.018f), 1.0f, true, cyan);      // cold glowing pupils
                MarkGlow(b, 6, cyan);
            }
            AddBoxFaces(b, MyVector(0.225f, 0.93f, 0.0f), MyVector(0.012f, 0.065f, 0.04f), 1.0f, true, black);                                    // wailing mouth
            c.armY = 0.80f; c.armZ = 0.27f; c.armRest = 78.0f; c.armSwing = 14.0f; c.rate = 3.0f; c.bob = 0.05f; c.hover = 0.16f;
            for (int s = -1; s <= 1; s += 2)
            {
                Mesh& a = (s > 0) ? c.armR : c.armL;
                AddBar(a, MyVector(0.0f, 0.0f, 0.0f), MyVector(0.02f, -0.18f, 0.0f), 0.055f, 0.045f, sheet);
                AddBar(a, MyVector(0.02f, -0.18f, 0.0f), MyVector(0.03f, -0.34f, 0.0f), 0.045f, 0.03f, sheetD);
                for (int f = -1; f <= 1; ++f)
                    AddBar(a, MyVector(0.03f, -0.34f, 0.03f * static_cast<float>(f)), MyVector(0.05f, -0.50f, 0.05f * static_cast<float>(f)), 0.014f, 0.004f, claw);   // claws
            }
        }
        // ---------------------------------------------------------------- DEMON: a horned, winged fiend with a pitchfork
        {
            Creature& c = set.demon;
            const MyColor red(176, 38, 36, 255), redD(122, 24, 28, 255), bone(228, 216, 184, 255), eyeC(255, 196, 40, 255), steel(190, 196, 212, 255),
                          wood(100, 74, 52, 255), skin(106, 20, 46, 255), hoof(54, 32, 32, 255);
            Mesh& b = c.body;
            AddTaper(b, MyVector(0.0f, 0.40f, 0.0f), 0.12f, 0.11f, MyVector(0.0f, 0.62f, 0.0f), 0.15f, 0.14f, red);                      // abdomen
            AddTaper(b, MyVector(0.0f, 0.62f, 0.0f), 0.15f, 0.14f, MyVector(0.0f, 0.90f, 0.0f), 0.21f, 0.27f, red);                      // chest
            for (int k = 0; k < 3; ++k) AddBoxFaces(b, MyVector(0.185f, 0.70f + 0.07f * static_cast<float>(k), 0.0f), MyVector(0.012f, 0.012f, 0.12f), 1.0f, true, redD);   // ribs
            AddTaper(b, MyVector(0.02f, 0.88f, 0.0f), 0.07f, 0.08f, MyVector(0.06f, 0.96f, 0.0f), 0.07f, 0.08f, red);                    // neck
            AddBoxFaces(b, MyVector(0.10f, 1.04f, 0.0f), MyVector(0.11f, 0.10f, 0.10f), 1.0f, true, red);                                // head
            AddBar(b, MyVector(0.17f, 1.02f, 0.0f), MyVector(0.31f, 1.0f, 0.0f), 0.07f, 0.045f, redD);                                   // snout
            AddBoxFaces(b, MyVector(0.205f, 1.10f, 0.0f), MyVector(0.012f, 0.012f, 0.09f), 1.0f, true, redD);                            // brow ridge
            AddBoxFaces(b, MyVector(-0.68f, 0.29f, 0.0f), MyVector(0.06f, 0.012f, 0.045f), 1.0f, true, redD);                            // tail spade
            AddBar(b, MyVector(-0.12f, 0.44f, 0.0f), MyVector(-0.32f, 0.34f, 0.0f), 0.05f, 0.035f, red);                                 // tail
            AddBar(b, MyVector(-0.32f, 0.34f, 0.0f), MyVector(-0.52f, 0.26f, 0.0f), 0.035f, 0.025f, red);
            AddBar(b, MyVector(-0.52f, 0.26f, 0.0f), MyVector(-0.62f, 0.28f, 0.0f), 0.022f, 0.02f, red);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                AddBar(b, MyVector(0.0f, 0.88f, 0.25f * z), MyVector(-0.02f, 1.00f, 0.31f * z), 0.04f, 0.008f, bone);                    // shoulder spikes
                AddBar(b, MyVector(0.27f, 0.99f, 0.035f * z), MyVector(0.28f, 0.91f, 0.035f * z), 0.014f, 0.004f, bone);                 // fangs
                AddBar(b, MyVector(0.06f, 1.12f, 0.07f * z), MyVector(0.00f, 1.22f, 0.12f * z), 0.03f, 0.022f, bone);                    // horns, two segments each
                AddBar(b, MyVector(0.00f, 1.22f, 0.12f * z), MyVector(-0.07f, 1.32f, 0.10f * z), 0.022f, 0.006f, bone);
                AddBoxFaces(b, MyVector(0.205f, 1.07f, 0.06f * z), MyVector(0.01f, 0.018f, 0.03f), 1.0f, true, eyeC);                    // burning eyes
                MarkGlow(b, 6, eyeC);
            }
            c.armY = 0.86f; c.armZ = 0.27f; c.legY = 0.46f; c.legZ = 0.10f; c.wingX = -0.08f; c.wingY = 0.86f; c.wingZ = 0.12f;
            c.armSwing = 28.0f; c.legSwing = 30.0f; c.rate = 7.0f; c.bob = 0.04f; c.flap = 28.0f;
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                Mesh& l = (s > 0) ? c.legR : c.legL;
                AddBar(l, MyVector(0.0f, 0.0f, 0.0f), MyVector(0.02f, -0.20f, 0.0f), 0.075f, 0.055f, red);                               // thigh
                AddBar(l, MyVector(0.02f, -0.20f, 0.0f), MyVector(-0.03f, -0.34f, 0.0f), 0.05f, 0.035f, red);                            // shin (digitigrade)
                AddBar(l, MyVector(-0.03f, -0.34f, 0.0f), MyVector(0.05f, -0.42f, 0.0f), 0.035f, 0.03f, redD);
                AddBoxFaces(l, MyVector(0.07f, -0.435f, 0.0f), MyVector(0.06f, 0.025f, 0.045f), 1.0f, true, hoof);
                Mesh& a = (s > 0) ? c.armR : c.armL;
                AddBar(a, MyVector(0.0f, 0.0f, 0.0f), MyVector(0.02f, -0.20f, 0.0f), 0.06f, 0.05f, red);
                AddBar(a, MyVector(0.02f, -0.20f, 0.0f), MyVector(0.05f, -0.36f, 0.0f), 0.05f, 0.04f, red);
                for (int f = -1; f <= 1; ++f)
                    AddBar(a, MyVector(0.05f, -0.36f, 0.025f * static_cast<float>(f)), MyVector(0.10f, -0.45f, 0.03f * static_cast<float>(f)), 0.012f, 0.003f, bone);   // claws
                if (s > 0)                                                                                                                  // the right hand holds a pitchfork
                {
                    AddBar(a, MyVector(0.05f, -0.40f, 0.0f), MyVector(0.07f, 0.55f, 0.0f), 0.02f, 0.018f, wood);
                    AddBar(a, MyVector(0.07f, 0.55f, -0.07f), MyVector(0.07f, 0.55f, 0.07f), 0.016f, 0.016f, steel);
                    AddBar(a, MyVector(0.07f, 0.55f, -0.07f), MyVector(0.07f, 0.70f, -0.07f), 0.014f, 0.004f, steel);
                    AddBar(a, MyVector(0.07f, 0.55f, 0.0f), MyVector(0.07f, 0.76f, 0.0f), 0.014f, 0.004f, steel);
                    AddBar(a, MyVector(0.07f, 0.55f, 0.07f), MyVector(0.07f, 0.70f, 0.07f), 0.014f, 0.004f, steel);
                }
                Mesh& w = (s > 0) ? c.wingR : c.wingL;                                                                                       // bat wing: bones + membrane
                AddBar(w, MyVector(0.0f, 0.0f, 0.04f * z), MyVector(-0.04f, 0.26f, 0.50f * z), 0.03f, 0.02f, redD);
                AddBar(w, MyVector(-0.04f, 0.26f, 0.50f * z), MyVector(-0.30f, 0.16f, 0.92f * z), 0.016f, 0.006f, redD);
                AddBar(w, MyVector(-0.04f, 0.26f, 0.50f * z), MyVector(-0.40f, -0.06f, 0.76f * z), 0.016f, 0.006f, redD);
                AddBar(w, MyVector(-0.04f, 0.26f, 0.50f * z), MyVector(-0.26f, -0.22f, 0.48f * z), 0.016f, 0.006f, redD);
                AddMembrane(w, { MyVector(0.0f, 0.0f, 0.04f * z), MyVector(-0.04f, 0.26f, 0.50f * z), MyVector(-0.30f, 0.16f, 0.92f * z), MyVector(-0.40f, -0.06f, 0.76f * z),
                                 MyVector(-0.26f, -0.22f, 0.48f * z), MyVector(-0.06f, -0.18f, 0.12f * z) }, skin, 0.9f);
            }
        }
        // ---------------------------------------------------------------- DEATH: a tall hooded dark human, empty black face, red eyes, scythe
        {
            Creature& c = set.death;
            const MyColor robe(62, 44, 94, 255), robeD(42, 30, 66, 255), bone(214, 208, 190, 255), steel(190, 196, 216, 255), black(4, 4, 8, 255),
                          red(255, 30, 30, 255), wood(92, 72, 62, 255);
            Mesh& b = c.body;
            static const float ys[7] = { 0.00f, 0.30f, 0.62f, 0.84f, 0.95f, 1.05f, 1.18f };
            static const float rx[7] = { 0.30f, 0.27f, 0.22f, 0.17f, 0.12f, 0.17f, 0.09f }, rz[7] = { 0.30f, 0.27f, 0.21f, 0.26f, 0.12f, 0.16f, 0.09f };
            static const float cs[8] = { 1.0f, 0.7071f, 0.0f, -0.7071f, -1.0f, -0.7071f, 0.0f, 0.7071f }, sn[8] = { 0.0f, 0.7071f, 1.0f, 0.7071f, 0.0f, -0.7071f, -1.0f, -0.7071f };
            for (int r = 0; r < 7; ++r)
                for (int k = 0; k < 8; ++k)
                    b.verts.push_back(MyVector(rx[r] * cs[k], ys[r] + ((r == 0 && (k & 1)) ? 0.12f : 0.0f), rz[r] * sn[k]));          // ragged hem
            const MyVector core(0.0f, 0.6f, 0.0f);
            for (int r = 0; r < 6; ++r)
                for (int k = 0; k < 8; ++k)
                {
                    const int n = (k + 1) & 7, a0 = r * 8, a1 = (r + 1) * 8;
                    AddFaceAuto(b, { a0 + k, a0 + n, a1 + n, a1 + k }, core, 0.8f + 0.04f * static_cast<float>(r), true, (r == 3 || r == 4) ? robeD : robe);
                }
            AddFaceAuto(b, { 48, 49, 50, 51, 52, 53, 54, 55 }, core, 1.0f, true, robeD);
            AddBoxFaces(b, MyVector(0.165f, 1.06f, 0.0f), MyVector(0.012f, 0.085f, 0.085f), 1.0f, true, black);                                    // the void under the hood
            for (int s = -1; s <= 1; s += 2)
            {
                AddBoxFaces(b, MyVector(0.185f, 1.08f, 0.045f * static_cast<float>(s)), MyVector(0.01f, 0.02f, 0.025f), 1.0f, true, red);       // burning eyes
                MarkGlow(b, 6, red);
            }
            AddBoxFaces(b, MyVector(0.0f, 0.60f, 0.0f), MyVector(0.23f, 0.025f, 0.225f), 1.0f, true, robeD);                                      // belt
            AddBoxFaces(b, MyVector(0.235f, 0.56f, -0.09f), MyVector(0.03f, 0.035f, 0.03f), 1.0f, true, bone);                                    // a skull on the belt
            AddBar(b, MyVector(0.22f, 0.58f, 0.10f), MyVector(0.24f, 0.35f, 0.12f), 0.015f, 0.01f, bone);                                         // rope end
            c.armY = 0.84f; c.armZ = 0.26f; c.armRest = 26.0f; c.armSwing = 9.0f; c.rate = 5.0f; c.bob = 0.03f;
            for (int s = -1; s <= 1; s += 2)
            {
                Mesh& a = (s > 0) ? c.armR : c.armL;
                AddTaper(a, MyVector(0.0f, 0.0f, 0.0f), 0.09f, 0.09f, MyVector(0.03f, -0.22f, 0.0f), 0.12f, 0.12f, robe);                         // flared sleeve
                AddBoxFaces(a, MyVector(0.04f, -0.27f, 0.0f), MyVector(0.035f, 0.04f, 0.025f), 1.0f, true, bone);                                  // bony hand
                for (int f = -1; f <= 1; ++f)
                    AddBar(a, MyVector(0.05f, -0.30f, 0.02f * static_cast<float>(f)), MyVector(0.09f, -0.40f, 0.025f * static_cast<float>(f)), 0.008f, 0.003f, bone);
                if (s > 0)                                                                                                                          // the scythe
                {
                    AddBar(a, MyVector(0.05f, -0.30f, 0.0f), MyVector(0.10f, 0.95f, 0.0f), 0.022f, 0.018f, wood);
                    AddBar(a, MyVector(0.10f, 0.95f, 0.0f), MyVector(0.28f, 1.03f, 0.0f), 0.04f, 0.03f, steel);
                    AddBar(a, MyVector(0.28f, 1.03f, 0.0f), MyVector(0.44f, 0.99f, 0.0f), 0.03f, 0.02f, steel);
                    AddBar(a, MyVector(0.44f, 0.99f, 0.0f), MyVector(0.54f, 0.88f, 0.0f), 0.02f, 0.006f, steel);
                }
            }
        }
        // ---------------------------------------------------------------- SKELETON: a bone warrior with a sword and a wooden shield
        {
            Creature& c = set.skel;
            const MyColor bone(226, 220, 198, 255), boneD(188, 182, 160, 255), steel(176, 182, 198, 255), wood(104, 76, 50, 255), black(6, 8, 10, 255), glow(120, 255, 150, 255), cloth(96, 44, 44, 255);
            Mesh& b = c.body;
            AddBar(b, MyVector(0.0f, 0.46f, 0.0f), MyVector(0.0f, 0.84f, 0.0f), 0.022f, 0.02f, bone);                                   // spine
            for (int k = 0; k < 5; ++k)                                                                                                 // ribs
            {
                const float y = 0.58f + 0.065f * static_cast<float>(k);
                AddBar(b, MyVector(0.02f, y, -0.14f + 0.01f * static_cast<float>(k)), MyVector(0.02f, y, 0.14f - 0.01f * static_cast<float>(k)), 0.014f, 0.014f, boneD);
            }
            AddBar(b, MyVector(0.10f, 0.58f, 0.0f), MyVector(0.10f, 0.84f, 0.0f), 0.016f, 0.016f, bone);                                // sternum
            AddTaper(b, MyVector(0.0f, 0.38f, 0.0f), 0.07f, 0.13f, MyVector(0.0f, 0.48f, 0.0f), 0.06f, 0.10f, boneD);                  // pelvis
            AddBar(b, MyVector(0.0f, 0.83f, -0.20f), MyVector(0.0f, 0.83f, 0.20f), 0.022f, 0.022f, bone);                              // shoulders
            AddTaper(b, MyVector(0.0f, 0.38f, 0.0f), 0.12f, 0.16f, MyVector(0.0f, 0.18f, 0.0f), 0.15f, 0.20f, cloth, 0.9f);            // ragged loincloth
            AddBoxFaces(b, MyVector(0.05f, 0.97f, 0.0f), MyVector(0.085f, 0.09f, 0.075f), 1.0f, true, bone);                           // skull
            AddBoxFaces(b, MyVector(0.09f, 0.865f, 0.0f), MyVector(0.06f, 0.025f, 0.06f), 1.0f, true, boneD);                          // jaw
            AddBar(b, MyVector(0.15f, 0.87f, -0.04f), MyVector(0.15f, 0.87f, 0.04f), 0.008f, 0.008f, bone);                           // teeth
            for (int s = -1; s <= 1; s += 2)
            {
                AddBoxFaces(b, MyVector(0.135f, 0.985f, 0.04f * static_cast<float>(s)), MyVector(0.012f, 0.025f, 0.02f), 1.0f, true, black);        // eye sockets
                AddBoxFaces(b, MyVector(0.143f, 0.985f, 0.04f * static_cast<float>(s)), MyVector(0.006f, 0.012f, 0.012f), 1.0f, true, glow);        // green pinpoints
                MarkGlow(b, 6, glow);
            }
            c.armY = 0.83f; c.armZ = 0.21f; c.legY = 0.42f; c.legZ = 0.09f; c.armSwing = 34.0f; c.legSwing = 34.0f; c.rate = 7.0f; c.bob = 0.03f;
            for (int s = -1; s <= 1; s += 2)
            {
                Mesh& a = (s > 0) ? c.armR : c.armL;
                AddBar(a, MyVector(0.0f, 0.0f, 0.0f), MyVector(0.01f, -0.17f, 0.0f), 0.02f, 0.018f, bone);
                AddBar(a, MyVector(0.01f, -0.17f, 0.0f), MyVector(0.03f, -0.34f, 0.0f), 0.018f, 0.016f, bone);
                AddBoxFaces(a, MyVector(0.04f, -0.37f, 0.0f), MyVector(0.03f, 0.03f, 0.025f), 1.0f, true, boneD);
                if (s > 0)                                                                                                              // the sword
                {
                    AddBar(a, MyVector(0.04f, -0.40f, 0.0f), MyVector(0.07f, -0.22f, 0.0f), 0.02f, 0.02f, wood);
                    AddBar(a, MyVector(0.07f, -0.22f, -0.07f), MyVector(0.07f, -0.22f, 0.07f), 0.016f, 0.016f, steel);
                    AddBar(a, MyVector(0.08f, -0.20f, 0.0f), MyVector(0.13f, 0.40f, 0.0f), 0.024f, 0.006f, steel);
                }
                else                                                                                                                    // the shield, with an iron boss
                {
                    AddTaper(a, MyVector(0.09f, -0.42f, 0.0f), 0.02f, 0.12f, MyVector(0.09f, -0.10f, 0.0f), 0.02f, 0.15f, wood);
                    AddBoxFaces(a, MyVector(0.115f, -0.26f, 0.0f), MyVector(0.02f, 0.04f, 0.04f), 1.0f, true, steel);
                }
                Mesh& l = (s > 0) ? c.legR : c.legL;
                AddBar(l, MyVector(0.0f, 0.0f, 0.0f), MyVector(0.02f, -0.20f, 0.0f), 0.024f, 0.02f, bone);
                AddBar(l, MyVector(0.02f, -0.20f, 0.0f), MyVector(0.0f, -0.40f, 0.0f), 0.02f, 0.017f, bone);
                AddBoxFaces(l, MyVector(0.06f, -0.42f, 0.0f), MyVector(0.07f, 0.015f, 0.03f), 1.0f, true, boneD);
            }
        }
        // ---------------------------------------------------------------- SPIDER: a fast, low, eight-legged crawler with a red hourglass
        {
            Creature& c = set.spider;
            const MyColor dark(40, 28, 54, 255), leg(60, 44, 74, 255), mark(210, 30, 40, 255), eye(255, 50, 70, 255), steel(190, 196, 212, 255);
            Mesh& b = c.body;
            AddTaper(b, MyVector(-0.22f, 0.10f, 0.0f), 0.12f, 0.10f, MyVector(-0.22f, 0.20f, 0.0f), 0.22f, 0.18f, dark);                // abdomen
            AddTaper(b, MyVector(-0.22f, 0.20f, 0.0f), 0.22f, 0.18f, MyVector(-0.22f, 0.30f, 0.0f), 0.10f, 0.09f, dark, 1.05f);
            AddBoxFaces(b, MyVector(-0.22f, 0.305f, 0.0f), MyVector(0.07f, 0.008f, 0.03f), 1.0f, true, mark);  MarkGlow(b, 6, mark);      // hourglass mark
            AddTaper(b, MyVector(0.08f, 0.10f, 0.0f), 0.12f, 0.11f, MyVector(0.08f, 0.24f, 0.0f), 0.09f, 0.09f, dark);                  // head and thorax
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                AddBoxFaces(b, MyVector(0.165f, 0.20f, 0.03f * z), MyVector(0.01f, 0.018f, 0.018f), 1.0f, true, eye);  MarkGlow(b, 6, eye);       // four eyes
                AddBoxFaces(b, MyVector(0.165f, 0.15f, 0.07f * z), MyVector(0.01f, 0.014f, 0.014f), 1.0f, true, eye);  MarkGlow(b, 6, eye);
                AddBar(b, MyVector(0.17f, 0.14f, 0.03f * z), MyVector(0.23f, 0.05f, 0.02f * z), 0.014f, 0.004f, steel);                  // fangs
                AddBar(b, MyVector(0.15f, 0.15f, 0.08f * z), MyVector(0.25f, 0.12f, 0.07f * z), 0.012f, 0.008f, leg);                    // palps
                for (int i = 0; i < 4; ++i)                                                                                              // eight legs: two bones each
                {
                    const float bx = 0.12f - 0.08f * static_cast<float>(i), fan = (1.5f - static_cast<float>(i)) * 0.07f;
                    AddBar(b, MyVector(bx, 0.16f, 0.08f * z), MyVector(bx + fan * 0.5f, 0.34f, 0.30f * z), 0.016f, 0.013f, leg);
                    AddBar(b, MyVector(bx + fan * 0.5f, 0.34f, 0.30f * z), MyVector(bx + fan, 0.0f, 0.48f * z), 0.013f, 0.006f, leg);
                }
            }
            c.armSwing = 0.0f; c.legSwing = 0.0f; c.rate = 15.0f; c.bob = 0.03f;                                                        // it scuttles: a fast little bounce
        }
        // ---------------------------------------------------------------- SORCERER: a robed caster with a pointed hat and a glowing staff
        {
            Creature& c = set.sorc;
            const MyColor robe(74, 42, 146, 255), robeD(52, 30, 108, 255), trim(240, 200, 80, 255), skin(236, 200, 170, 255), beard(236, 236, 242, 255), wood(98, 72, 48, 255),
                          orb(120, 225, 255, 255), eyeC(120, 235, 255, 255);
            Mesh& b = c.body;
            AddTaper(b, MyVector(0.0f, 0.0f, 0.0f), 0.26f, 0.24f, MyVector(0.0f, 0.46f, 0.0f), 0.20f, 0.18f, robe);                     // robe skirt
            AddTaper(b, MyVector(0.0f, 0.46f, 0.0f), 0.20f, 0.18f, MyVector(0.0f, 0.86f, 0.0f), 0.22f, 0.25f, robe, 1.05f);             // robe body, wide shoulders
            AddBoxFaces(b, MyVector(0.0f, 0.50f, 0.0f), MyVector(0.205f, 0.02f, 0.19f), 1.0f, true, trim);                              // golden belt
            AddBoxFaces(b, MyVector(0.0f, 0.02f, 0.0f), MyVector(0.265f, 0.015f, 0.245f), 1.0f, true, trim);                            // golden hem
            AddBoxFaces(b, MyVector(0.03f, 1.0f, 0.0f), MyVector(0.09f, 0.10f, 0.085f), 1.0f, true, skin);                              // head
            AddTaper(b, MyVector(0.09f, 0.94f, 0.0f), 0.035f, 0.07f, MyVector(0.09f, 0.70f, 0.0f), 0.02f, 0.035f, beard);               // long white beard
            AddTaper(b, MyVector(0.0f, 1.07f, 0.0f), 0.23f, 0.23f, MyVector(0.0f, 1.12f, 0.0f), 0.17f, 0.17f, robeD);                   // hat brim
            AddTaper(b, MyVector(0.0f, 1.12f, 0.0f), 0.17f, 0.17f, MyVector(-0.05f, 1.56f, 0.0f), 0.015f, 0.015f, robeD);               // hat cone
            AddBoxFaces(b, MyVector(0.0f, 1.14f, 0.0f), MyVector(0.175f, 0.02f, 0.175f), 1.0f, true, trim);                             // hat band
            for (int s = -1; s <= 1; s += 2)
            {
                AddBoxFaces(b, MyVector(0.115f, 1.02f, 0.035f * static_cast<float>(s)), MyVector(0.01f, 0.02f, 0.02f), 1.0f, true, eyeC);  MarkGlow(b, 6, eyeC);
            }
            c.armY = 0.84f; c.armZ = 0.25f; c.armRest = 20.0f; c.armSwing = 10.0f; c.rate = 4.0f; c.bob = 0.04f; c.hover = 0.12f;
            for (int s = -1; s <= 1; s += 2)
            {
                Mesh& a = (s > 0) ? c.armR : c.armL;
                AddTaper(a, MyVector(0.0f, 0.0f, 0.0f), 0.09f, 0.09f, MyVector(0.03f, -0.22f, 0.0f), 0.12f, 0.12f, robe);               // flared sleeve
                AddBoxFaces(a, MyVector(0.04f, -0.27f, 0.0f), MyVector(0.035f, 0.04f, 0.03f), 1.0f, true, skin);                        // hand
                if (s > 0)                                                                                                              // the staff, topped by a glowing orb in a claw of prongs
                {
                    AddBar(a, MyVector(0.05f, -0.34f, 0.0f), MyVector(0.10f, 0.80f, 0.0f), 0.02f, 0.018f, wood);
                    AddBoxFaces(a, MyVector(0.10f, 0.90f, 0.0f), MyVector(0.06f, 0.06f, 0.06f), 1.0f, true, orb);  MarkGlow(a, 6, orb);
                    for (int p = -1; p <= 1; p += 2) AddBar(a, MyVector(0.10f, 0.78f, 0.0f), MyVector(0.10f, 0.96f, 0.07f * static_cast<float>(p)), 0.012f, 0.008f, trim);
                }
            }
        }
        // ================================================================ THE SECOND BATCH (extra Gauntlet / Dungeon monsters) ==================================================
        // ---------------------------------------------------------------- TROLL: a hulking, hunched green-grey brute with tusks and a tree-trunk club
        {
            Creature& c = set.troll; float hy;
            const MyColor skin(112, 134, 92, 255), skinD(82, 102, 70, 255), cloth(120, 84, 50, 255), metal(86, 80, 74, 255), bone(238, 232, 206, 255), wood(104, 74, 44, 255), eyeC(255, 90, 40, 255);
            CrHumanoid(c, skin, skinD, cloth, metal, 1.30f, 1.08f, 0.95f, hy);
            Mesh& b = c.body;
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                AddBar(b, MyVector(0.17f, hy - 0.06f, 0.06f * z), MyVector(0.21f, hy + 0.06f, 0.07f * z), 0.022f, 0.006f, bone);                                       // tusks
                AddBoxFaces(b, MyVector(0.195f, hy + 0.03f, 0.06f * z), MyVector(0.012f, 0.02f, 0.03f), 1.0f, true, eyeC);  MarkGlow(b, 6, eyeC);                      // glowing eyes
                AddBar(b, MyVector(0.0f, 0.86f, 0.23f * z), MyVector(-0.02f, 0.99f, 0.27f * z), 0.04f, 0.01f, skinD);                                                   // shoulder wart-spikes
            }
            AddBoxFaces(b, MyVector(0.18f, hy + 0.09f, 0.0f), MyVector(0.012f, 0.014f, 0.10f), 1.0f, true, skinD);                                                     // heavy brow
            AddBoxFaces(b, MyVector(0.0f, 0.60f, 0.0f), MyVector(0.20f, 0.05f, 0.30f), 1.0f, true, metal);                                                             // studded belt
            c.armRest = 0.0f; c.rate = 6.0f; c.armSwing = 32.0f; c.legSwing = 30.0f; c.bob = 0.06f;
            AddBar(c.armR, MyVector(0.04f, -0.40f, 0.0f), MyVector(0.20f, -0.12f, 0.0f), 0.03f, 0.04f, wood);                                                           // the club
            AddBar(c.armR, MyVector(0.20f, -0.12f, 0.0f), MyVector(0.34f, 0.16f, 0.0f), 0.05f, 0.10f, wood, 0.9f);
            AddBar(c.armR, MyVector(0.30f, 0.10f, 0.08f), MyVector(0.30f, 0.12f, 0.17f), 0.02f, 0.005f, metal);
            AddBar(c.armR, MyVector(0.30f, 0.10f, -0.08f), MyVector(0.30f, 0.12f, -0.17f), 0.02f, 0.005f, metal);
        }
        // ---------------------------------------------------------------- BAT: a fast, erratic cave bat (no limbs; big wings)
        {
            Creature& c = set.bat;
            const MyColor fur(70, 56, 80, 255), furD(48, 38, 58, 255), membrane(86, 60, 96, 255), bone(150, 130, 160, 255), eyeC(255, 60, 60, 255), tooth(245, 245, 235, 255);
            Mesh& b = c.body;
            AddTaper(b, MyVector(-0.10f, 0.0f, 0.0f), 0.05f, 0.06f, MyVector(0.04f, 0.0f, 0.0f), 0.09f, 0.10f, furD);                                                   // body
            AddTaper(b, MyVector(0.04f, 0.0f, 0.0f), 0.09f, 0.10f, MyVector(0.14f, 0.01f, 0.0f), 0.06f, 0.07f, fur);                                                   // head
            AddBar(b, MyVector(0.16f, 0.0f, 0.0f), MyVector(0.21f, -0.01f, 0.0f), 0.025f, 0.01f, furD);                                                                // snout
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                AddBar(b, MyVector(0.10f, 0.04f, 0.04f * z), MyVector(0.09f, 0.14f, 0.07f * z), 0.025f, 0.004f, fur);                                                   // ears
                AddBoxFaces(b, MyVector(0.165f, 0.03f, 0.03f * z), MyVector(0.01f, 0.012f, 0.012f), 1.0f, true, eyeC);  MarkGlow(b, 6, eyeC);                          // red eyes
                AddBar(b, MyVector(0.18f, -0.03f, 0.012f * z), MyVector(0.185f, -0.07f, 0.012f * z), 0.006f, 0.001f, tooth);                                            // fangs
            }
            c.hover = 0.62f; c.flap = 55.0f; c.rate = 12.0f; c.bob = 0.03f; c.wingX = -0.02f; c.wingY = 0.02f; c.wingZ = 0.05f;
            CrWings(c, 0.62f, membrane, bone);
        }
        // ---------------------------------------------------------------- GOBLIN ARCHER: a small, hooded, pointy-eared sniper with a bow and a quiver
        {
            Creature& c = set.archer; float hy;
            const MyColor skin(120, 164, 74, 255), skinD(88, 126, 54, 255), cloth(88, 66, 44, 255), hood(60, 96, 56, 255), metal(110, 96, 80, 255), wood(130, 92, 52, 255), eyeC(255, 230, 60, 255), bone(238, 232, 206, 255);
            CrHumanoid(c, skin, skinD, cloth, metal, 0.80f, 0.86f, 0.95f, hy);
            Mesh& b = c.body;
            AddTaper(b, MyVector(0.05f, hy - 0.02f, 0.0f), 0.13f, 0.13f, MyVector(0.04f, hy + 0.14f, 0.0f), 0.09f, 0.09f, hood);                                          // hood
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                AddBar(b, MyVector(0.07f, hy, 0.10f * z), MyVector(0.04f, hy + 0.06f, 0.24f * z), 0.025f, 0.004f, skin);                                                // long ears
                AddBoxFaces(b, MyVector(0.185f, hy + 0.01f, 0.05f * z), MyVector(0.01f, 0.014f, 0.02f), 1.0f, true, eyeC);  MarkGlow(b, 6, eyeC);
            }
            AddBar(b, MyVector(0.19f, hy - 0.03f, 0.0f), MyVector(0.23f, hy - 0.05f, 0.0f), 0.02f, 0.006f, skinD);                                                      // nose
            AddBar(b, MyVector(-0.20f, 0.60f, 0.10f), MyVector(-0.30f, 0.86f, 0.14f), 0.04f, 0.035f, wood);                                                             // quiver with arrows
            for (int k = -1; k <= 1; ++k) AddBar(b, MyVector(-0.30f, 0.86f, 0.14f), MyVector(-0.32f, 0.97f, 0.14f + 0.03f * static_cast<float>(k)), 0.008f, 0.004f, bone);
            c.armRest = 40.0f; c.rate = 10.0f;
            AddBar(c.armL, MyVector(0.04f, -0.30f, 0.0f), MyVector(0.20f, -0.12f, 0.0f), 0.015f, 0.015f, wood);                                                         // the bow (a bent bar)
            AddBar(c.armL, MyVector(0.20f, -0.12f, 0.0f), MyVector(0.30f, 0.06f, 0.0f), 0.015f, 0.01f, wood);
            AddBar(c.armL, MyVector(0.04f, -0.30f, 0.0f), MyVector(0.30f, 0.06f, 0.0f), 0.004f, 0.004f, bone);                                                          // the string
        }
        // ---------------------------------------------------------------- SLIME and SLIMELET: green jelly that splits when it is killed
        { Creature& c = set.slime;    CrBlob(c, 1.15f, MyColor(80, 210, 96, 255),  MyColor(52, 150, 70, 255),  MyColor(20, 40, 20, 255)); }
        { Creature& c = set.slimelet; CrBlob(c, 0.60f, MyColor(120, 230, 130, 255), MyColor(80, 180, 96, 255), MyColor(20, 40, 20, 255)); c.rate = 8.0f; }
        // ---------------------------------------------------------------- GOLEM: a vast stone figure with moss and a glowing rune in its chest
        {
            Creature& c = set.golem; float hy;
            const MyColor skin(136, 132, 126, 255), skinD(98, 96, 92, 255), cloth(88, 116, 74, 255), metal(74, 72, 70, 255), rune(90, 230, 255, 255);
            CrHumanoid(c, skin, skinD, cloth, metal, 1.55f, 1.12f, 0.80f, hy);
            Mesh& b = c.body;
            AddBoxFaces(b, MyVector(0.25f, 0.72f, 0.0f), MyVector(0.02f, 0.10f, 0.10f), 1.0f, true, rune);  MarkGlow(b, 6, rune);                                          // chest rune
            AddBoxFaces(b, MyVector(0.26f, 0.72f, 0.0f), MyVector(0.012f, 0.04f, 0.04f), 1.0f, true, MyColor(220, 255, 255, 255));  MarkGlow(b, 6, MyColor(220, 255, 255, 255));
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                AddBoxFaces(b, MyVector(0.0f, 0.95f, 0.36f * z), MyVector(0.14f, 0.09f, 0.12f), 1.0f, true, skinD);                                                     // boulder shoulders
                AddBoxFaces(b, MyVector(0.185f, hy + 0.01f, 0.05f * z), MyVector(0.012f, 0.015f, 0.025f), 1.0f, true, rune);  MarkGlow(b, 6, rune);                      // eyes
                AddBoxFaces(b, MyVector(0.05f, 0.88f, 0.20f * z), MyVector(0.10f, 0.02f, 0.08f), 1.0f, true, cloth);                                                     // moss
            }
            c.rate = 5.0f; c.armSwing = 22.0f; c.legSwing = 20.0f; c.bob = 0.03f;
        }
        // ---------------------------------------------------------------- WEREWOLF: a lean, furred wolf-man with a snout, ears and raking claws
        {
            Creature& c = set.werewolf; float hy;
            const MyColor fur(124, 96, 68, 255), furD(86, 64, 46, 255), cloth(70, 56, 46, 255), metal(60, 50, 44, 255), claw(232, 226, 210, 255), eyeC(255, 40, 30, 255), tooth(250, 250, 240, 255);
            CrHumanoid(c, fur, furD, cloth, metal, 1.0f, 1.04f, 0.90f, hy);
            Mesh& b = c.body;
            AddTaper(b, MyVector(0.13f, hy - 0.03f, 0.0f), 0.07f, 0.08f, MyVector(0.30f, hy - 0.06f, 0.0f), 0.04f, 0.05f, furD);                                         // snout
            AddBoxFaces(b, MyVector(0.31f, hy - 0.05f, 0.0f), MyVector(0.012f, 0.012f, 0.02f), 1.0f, true, MyColor(10, 10, 12, 255));                                    // nose
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                AddBar(b, MyVector(0.05f, hy + 0.08f, 0.06f * z), MyVector(0.02f, hy + 0.24f, 0.10f * z), 0.04f, 0.006f, fur);                                           // pointed ears
                AddBoxFaces(b, MyVector(0.19f, hy + 0.025f, 0.05f * z), MyVector(0.012f, 0.014f, 0.022f), 1.0f, true, eyeC);  MarkGlow(b, 6, eyeC);
                AddBar(b, MyVector(0.24f, hy - 0.10f, 0.025f * z), MyVector(0.25f, hy - 0.15f, 0.025f * z), 0.008f, 0.002f, tooth);                                      // fangs
            }
            AddBar(b, MyVector(-0.20f, 0.46f, 0.0f), MyVector(-0.42f, 0.34f, 0.0f), 0.05f, 0.02f, furD);                                                                // bushy tail
            c.rate = 12.0f; c.armSwing = 55.0f; c.legSwing = 52.0f; c.bob = 0.07f;
            for (int s = -1; s <= 1; s += 2)
            {
                Mesh& a = (s > 0) ? c.armR : c.armL;
                for (int f = -1; f <= 1; ++f) AddBar(a, MyVector(0.07f, -0.40f, 0.03f * static_cast<float>(f)), MyVector(0.14f, -0.50f, 0.04f * static_cast<float>(f)), 0.012f, 0.002f, claw);   // claws
            }
        }
        // ---------------------------------------------------------------- NECROMANCER: a bone-white skull face in a black hooded robe, with a skull staff
        {
            Creature& c = set.necro; float hy;
            const MyColor robe(40, 34, 56, 255), robeD(26, 22, 38, 255), trim(120, 220, 140, 255), bone(232, 228, 208, 255), black(4, 4, 8, 255), eyeC(110, 255, 140, 255), wood(86, 68, 58, 255);
            CrHumanoid(c, robe, robeD, robe, robeD, 1.0f, 1.02f, 0.85f, hy);
            Mesh& b = c.body;
            AddTaper(b, MyVector(0.0f, 0.00f, 0.0f), 0.30f, 0.30f, MyVector(0.0f, 0.46f, 0.0f), 0.20f, 0.19f, robe);                                                    // long robe hides the legs
            AddTaper(b, MyVector(0.0f, 0.46f, 0.0f), 0.20f, 0.19f, MyVector(0.0f, 0.62f, 0.0f), 0.24f, 0.22f, robeD);
            AddBoxFaces(b, MyVector(0.15f, hy, 0.0f), MyVector(0.04f, 0.085f, 0.075f), 1.0f, true, bone);                                                               // skull face
            AddBoxFaces(b, MyVector(0.19f, hy - 0.03f, 0.0f), MyVector(0.012f, 0.025f, 0.05f), 1.0f, true, black);                                                      // jaw gap
            AddTaper(b, MyVector(0.05f, hy - 0.05f, 0.0f), 0.14f, 0.14f, MyVector(0.04f, hy + 0.17f, 0.0f), 0.08f, 0.08f, robeD);                                         // hood
            AddBoxFaces(b, MyVector(0.0f, 0.66f, 0.0f), MyVector(0.26f, 0.015f, 0.24f), 1.0f, true, trim);                                                              // glowing trim
            for (int s = -1; s <= 1; s += 2)
            {
                AddBoxFaces(b, MyVector(0.195f, hy + 0.02f, 0.035f * static_cast<float>(s)), MyVector(0.01f, 0.018f, 0.018f), 1.0f, true, eyeC);  MarkGlow(b, 6, eyeC);
            }
            c.legSwing = 0.0f; c.rate = 7.0f; c.hover = 0.04f; c.bob = 0.03f; c.armRest = 30.0f;
            AddBar(c.armR, MyVector(0.05f, -0.34f, 0.0f), MyVector(0.10f, 0.78f, 0.0f), 0.02f, 0.018f, wood);                                                           // the skull staff
            AddBoxFaces(c.armR, MyVector(0.10f, 0.88f, 0.0f), MyVector(0.07f, 0.07f, 0.06f), 1.0f, true, bone);
            AddBoxFaces(c.armR, MyVector(0.17f, 0.88f, 0.0f), MyVector(0.012f, 0.02f, 0.045f), 1.0f, true, eyeC);  MarkGlow(c.armR, 6, eyeC);
        }
        // ---------------------------------------------------------------- IMP: a tiny winged red devil with horns and a barbed tail
        {
            Creature& c = set.imp; float hy;
            const MyColor skin(206, 56, 48, 255), skinD(150, 36, 34, 255), cloth(80, 30, 40, 255), metal(60, 40, 40, 255), horn(240, 230, 200, 255), eyeC(255, 240, 80, 255), membrane(150, 30, 60, 255);
            CrHumanoid(c, skin, skinD, cloth, metal, 0.62f, 0.62f, 1.15f, hy);
            Mesh& b = c.body;
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                AddBar(b, MyVector(0.05f, hy + 0.08f, 0.06f * z), MyVector(0.02f, hy + 0.20f, 0.10f * z), 0.025f, 0.004f, horn);                                          // horns
                AddBoxFaces(b, MyVector(0.115f, hy + 0.02f, 0.045f * z), MyVector(0.01f, 0.016f, 0.02f), 1.0f, true, eyeC);  MarkGlow(b, 6, eyeC);
            }
            AddBar(b, MyVector(-0.12f, 0.28f, 0.0f), MyVector(-0.30f, 0.20f, 0.0f), 0.02f, 0.012f, skinD);                                                              // tail with a barb
            AddBar(b, MyVector(-0.30f, 0.20f, 0.0f), MyVector(-0.38f, 0.30f, 0.0f), 0.03f, 0.002f, skinD);
            c.hover = 0.26f; c.flap = 32.0f; c.rate = 10.0f; c.bob = 0.04f; c.wingX = -0.10f; c.wingY = 0.42f; c.wingZ = 0.07f; c.armSwing = 20.0f; c.legSwing = 14.0f;
            CrWings(c, 0.50f, membrane, skinD);
        }
        // ---------------------------------------------------------------- CYCLOPS: a towering one-eyed giant carrying a boulder
        {
            Creature& c = set.cyclops; float hy;
            const MyColor skin(176, 146, 114, 255), skinD(134, 108, 84, 255), cloth(100, 70, 44, 255), metal(84, 76, 66, 255), rock(124, 120, 114, 255), eyeW(250, 250, 244, 255), eyeR(240, 40, 30, 255), bone(238, 232, 206, 255);
            CrHumanoid(c, skin, skinD, cloth, metal, 1.35f, 1.18f, 1.05f, hy);
            Mesh& b = c.body;
            AddBoxFaces(b, MyVector(0.20f, hy + 0.01f, 0.0f), MyVector(0.02f, 0.045f, 0.06f), 1.0f, true, eyeW);                                                        // the single eye
            AddBoxFaces(b, MyVector(0.225f, hy + 0.01f, 0.0f), MyVector(0.012f, 0.028f, 0.028f), 1.0f, true, eyeR);  MarkGlow(b, 6, eyeR);
            AddBoxFaces(b, MyVector(0.195f, hy + 0.085f, 0.0f), MyVector(0.012f, 0.014f, 0.09f), 1.0f, true, skinD);                                                    // brow ridge
            AddBoxFaces(b, MyVector(0.185f, hy - 0.07f, 0.0f), MyVector(0.02f, 0.012f, 0.07f), 1.0f, true, bone);                                                       // grin
            AddBoxFaces(b, MyVector(0.0f, 0.52f, 0.0f), MyVector(0.21f, 0.06f, 0.31f), 1.0f, true, cloth);                                                              // hide kilt
            c.rate = 6.5f; c.armSwing = 30.0f; c.legSwing = 28.0f; c.bob = 0.05f;
            AddBoxFaces(c.armR, MyVector(0.14f, -0.30f, 0.0f), MyVector(0.13f, 0.12f, 0.13f), 1.0f, true, rock);                                                        // the boulder in its fist
            AddBoxFaces(c.armR, MyVector(0.15f, -0.24f, 0.05f), MyVector(0.09f, 0.09f, 0.09f), 0.85f, true, rock);
        }
        for (Creature* c : { &set.grunt, &set.ghost, &set.demon, &set.death, &set.skel, &set.spider, &set.sorc, &set.troll, &set.bat, &set.archer, &set.slime, &set.slimelet, &set.golem, &set.werewolf, &set.necro, &set.imp, &set.cyclops })
            for (Mesh* m : { &c->body, &c->armL, &c->armR, &c->legL, &c->legR, &c->wingL, &c->wingR })
                if (!m->verts.empty()) FinalizeMesh(*m);
    }

    // ================================================================================================
    //  THE FLEET - fifteen hand-modelled spacecraft for the shoot-'em-ups
    //  Every craft points along +X with its wings along +/-Z and "up" = +Y, about one unit long.  Hull panels take the instance colour
    //  (so a game can tint a craft by team / kind); glass, metal, weapons and engine glow are fixed colours.  Engines are lit self-glow
    //  bars with metal nozzles.  Built from bars, tapers and thin double-sided wing panels, once at start-up.
    //      0 Dart      needle interceptor        5 Cruiser   armoured capital ship     10 Phantom   stealth flying wing
    //      1 Manta     flying-wing ray           6 Carrier   hangar ship (pod bearer)  11 Saber     twin-boom strike craft
    //      2 Lancer    forward-swept gunship     7 Dreadnought  boss battleship        12 Hornet    compact canard fighter
    //      3 Scarab    armoured beetle bomber    8 Drone     ringed sentry pod         13 Raven     bat-wing raider
    //      4 Wasp      segmented stinger         9 Falcon    four-wing heavy fighter   14 Starhawk  the player's fighter
    // ================================================================================================

    void FleetBar(Mesh& m, const MyVector& a, const MyVector& b, float ra, float rb, const MyColor& col, bool fixed, float tone)
    {
        const MyVector d = b - a;
        const float len = d.Magnitude();
        if (len < 1.0e-4f) return;
        const MyVector dn = d / len;
        const MyVector ref = (MyMath::Abs(dn.y) < 0.9f) ? MyVector::Up() : MyVector::Right();
        const MyVector u = dn.Cross(ref).Normalized(), v = dn.Cross(u).Normalized();
        const int base = static_cast<int>(m.verts.size());
        static const float su[4] = { 1.0f, -1.0f, -1.0f, 1.0f }, sv[4] = { 1.0f, 1.0f, -1.0f, -1.0f };
        for (int k = 0; k < 4; ++k) m.verts.push_back(a + u * (su[k] * ra) + v * (sv[k] * ra));
        for (int k = 0; k < 4; ++k) m.verts.push_back(b + u * (su[k] * rb) + v * (sv[k] * rb));
        const MyVector mid = (a + b) * 0.5f;
        for (int k = 0; k < 4; ++k)
        {
            const int n = (k + 1) & 3;
            AddFaceAuto(m, { base + k, base + n, base + 4 + n, base + 4 + k }, mid, tone, fixed, col);
        }
        AddFaceAuto(m, { base, base + 1, base + 2, base + 3 }, mid, tone * 0.8f, fixed, col);
        AddFaceAuto(m, { base + 4, base + 5, base + 6, base + 7 }, mid, tone, fixed, col);
    }
    // A flat wing / fin panel seen from both sides (convex outline, 3-5 points).
    void FleetPanel(Mesh& m, std::initializer_list<MyVector> pts, const MyColor& col, bool fixed, float tone)
    {
        std::vector<int> idx;
        MyVector centroid;
        for (const MyVector& p : pts) { m.verts.push_back(p); idx.push_back(static_cast<int>(m.verts.size()) - 1); centroid += p; }
        centroid = centroid / static_cast<float>(idx.size());
        const MyVector n = (m.verts[idx[1]] - m.verts[idx[0]]).Cross(m.verts[idx[2]] - m.verts[idx[0]]).Normalized();
        AddFaceVec(m, idx, centroid - n, tone, fixed, col);
        AddFaceVec(m, idx, centroid + n, tone * 0.78f, fixed, col);
    }
    // A tapering slab: bottom rectangle at c0 (half sizes hx0, hz0) to the top rectangle at c1 (hx1, hz1), along Y.
    void FleetSlab(Mesh& m, const MyVector& c0, float hx0, float hz0, const MyVector& c1, float hx1, float hz1, const MyColor& col, bool fixed, float tone)
    {
        const int base = static_cast<int>(m.verts.size());
        static const float sx[4] = { -1.0f, 1.0f, 1.0f, -1.0f }, sz[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
        for (int k = 0; k < 4; ++k) m.verts.push_back(MyVector(c0.x + sx[k] * hx0, c0.y, c0.z + sz[k] * hz0));
        for (int k = 0; k < 4; ++k) m.verts.push_back(MyVector(c1.x + sx[k] * hx1, c1.y, c1.z + sz[k] * hz1));
        const MyVector mid = (c0 + c1) * 0.5f;
        for (int k = 0; k < 4; ++k)
        {
            const int n = (k + 1) & 3;
            AddFaceAuto(m, { base + k, base + n, base + 4 + n, base + 4 + k }, mid, tone, fixed, col);
        }
        AddFaceAuto(m, { base, base + 1, base + 2, base + 3 }, mid, tone * 0.8f, fixed, col);
        AddFaceAuto(m, { base + 4, base + 5, base + 6, base + 7 }, mid, tone, fixed, col);
    }

    static std::vector<FleetEngine> g_fleetEngines[kFlCount];                              // filled by BuildFleet's engine() helper
    const std::vector<FleetEngine>& FleetEngines(int fleetId)
    {
        static const std::vector<FleetEngine> none;
        return (fleetId >= 0 && fleetId < static_cast<int>(kFlCount)) ? g_fleetEngines[fleetId] : none;
    }
    void BuildFleet(Mesh* fleet)
    {
        for (std::vector<FleetEngine>& e : g_fleetEngines) e.clear();
        const MyColor steel(150, 156, 172, 255), dark(58, 62, 78, 255), glass(110, 215, 255, 255), flame(120, 210, 255, 255), flameR(255, 150, 70, 255), noz(96, 100, 116, 255),
                      gun(70, 74, 90, 255), lamp(255, 240, 150, 255), eye(255, 50, 50, 255), gold(240, 200, 80, 255), none(0, 0, 0, 255);
        // an engine: a glowing exhaust bar behind a metal nozzle
        auto engine = [&](Mesh& m, float x, float y, float z, float r, float len, const MyColor& fl)
        {
            { const int id = static_cast<int>(&m - fleet); if (id >= 0 && id < static_cast<int>(kFlCount)) { FleetEngine fe; fe.pos = MyVector(x, y, z); fe.r = r; fe.len = len; g_fleetEngines[id].push_back(fe); } }       // remember the nozzle
            FleetBar(m, MyVector(x, y, z), MyVector(x - len, y, z), r, r * 0.45f, fl, true);
            MarkGlow(m, 6, fl);
            FleetBar(m, MyVector(x + 0.05f, y, z), MyVector(x, y, z), r * 1.25f, r * 1.1f, noz, true);
        };
        // ---------------------------------------------------------------- 0 DART: a needle interceptor with delta wings and twin engines
        {
            Mesh& m = fleet[kFlDart];
            FleetBar(m, MyVector(-0.42f, 0.0f, 0.0f), MyVector(0.58f, 0.0f, 0.0f), 0.065f, 0.012f, none, false);
            FleetBar(m, MyVector(0.02f, 0.05f, 0.0f), MyVector(0.30f, 0.045f, 0.0f), 0.04f, 0.018f, glass, true);
            FleetPanel(m, { MyVector(-0.30f, 0.04f, 0.0f), MyVector(-0.46f, 0.25f, 0.0f), MyVector(-0.48f, 0.04f, 0.0f) }, none, false);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetPanel(m, { MyVector(0.22f, 0.0f, 0.04f * z), MyVector(-0.40f, 0.0f, 0.52f * z), MyVector(-0.46f, 0.0f, 0.10f * z) }, none, false, 0.92f);
                FleetBar(m, MyVector(0.22f, 0.0f, 0.04f * z), MyVector(-0.40f, 0.0f, 0.52f * z), 0.012f, 0.012f, steel, true);
                FleetBar(m, MyVector(0.04f, 0.0f, 0.27f * z), MyVector(0.46f, 0.0f, 0.27f * z), 0.014f, 0.008f, gun, true);                  // wing cannons
                engine(m, -0.44f, 0.0f, 0.09f * z, 0.045f, 0.20f, flame);
            }
        }
        // ---------------------------------------------------------------- 1 MANTA: a flying-wing ray with a hump cockpit and a tail spike
        {
            Mesh& m = fleet[kFlManta];
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetPanel(m, { MyVector(0.50f, 0.0f, 0.0f), MyVector(0.05f, 0.0f, 0.70f * z), MyVector(-0.42f, 0.0f, 0.36f * z), MyVector(-0.30f, 0.0f, 0.0f) }, none, false, 0.95f);
                FleetBar(m, MyVector(0.50f, 0.0f, 0.0f), MyVector(0.05f, 0.0f, 0.70f * z), 0.016f, 0.016f, steel, true);
                FleetBar(m, MyVector(0.05f, 0.0f, 0.70f * z), MyVector(-0.42f, 0.0f, 0.36f * z), 0.014f, 0.014f, steel, true);
                FleetBar(m, MyVector(0.10f, 0.02f, 0.30f * z), MyVector(0.40f, 0.02f, 0.30f * z), 0.02f, 0.012f, gun, true);                  // under-wing guns
                engine(m, -0.30f, 0.03f, 0.16f * z, 0.05f, 0.22f, flameR);
            }
            FleetSlab(m, MyVector(0.0f, 0.0f, 0.0f), 0.34f, 0.14f, MyVector(0.0f, 0.12f, 0.0f), 0.22f, 0.09f, none, false);                    // hump
            FleetBar(m, MyVector(0.16f, 0.10f, 0.0f), MyVector(0.36f, 0.06f, 0.0f), 0.05f, 0.03f, glass, true);                                // canopy
            FleetBar(m, MyVector(-0.30f, 0.0f, 0.0f), MyVector(-0.80f, 0.0f, 0.0f), 0.035f, 0.005f, steel, true);                                // tail spike
        }
        // ---------------------------------------------------------------- 2 LANCER: a long forward-swept gunship with twin cannons
        {
            Mesh& m = fleet[kFlLancer];
            FleetBar(m, MyVector(-0.55f, 0.0f, 0.0f), MyVector(0.62f, 0.0f, 0.0f), 0.08f, 0.02f, none, false);
            FleetBar(m, MyVector(-0.05f, 0.07f, 0.0f), MyVector(0.30f, 0.05f, 0.0f), 0.06f, 0.03f, glass, true);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetPanel(m, { MyVector(0.04f, 0.0f, 0.07f * z), MyVector(0.30f, 0.0f, 0.52f * z), MyVector(0.16f, 0.0f, 0.54f * z), MyVector(-0.22f, 0.0f, 0.09f * z) }, none, false, 0.94f);
                FleetBar(m, MyVector(0.10f, 0.0f, 0.46f * z), MyVector(0.80f, 0.0f, 0.46f * z), 0.028f, 0.014f, gun, true);                  // long tip cannons
                FleetBar(m, MyVector(0.28f, 0.0f, 0.5f * z), MyVector(0.28f, 0.0f, 0.46f * z), 0.05f, 0.04f, steel, true);
                FleetBar(m, MyVector(-0.30f, 0.0f, 0.10f * z), MyVector(-0.52f, 0.16f, 0.22f * z), 0.02f, 0.012f, steel, true);              // tail boom
                engine(m, -0.54f, 0.0f, 0.06f * z, 0.05f, 0.24f, flame);
            }
            FleetPanel(m, { MyVector(-0.30f, 0.05f, 0.0f), MyVector(-0.50f, 0.28f, 0.0f), MyVector(-0.56f, 0.05f, 0.0f) }, none, false);
        }
        // ---------------------------------------------------------------- 3 SCARAB: an armoured beetle bomber with mandibles and four engine pods
        {
            Mesh& m = fleet[kFlScarab];
            FleetSlab(m, MyVector(0.0f, -0.05f, 0.0f), 0.38f, 0.32f, MyVector(0.0f, 0.15f, 0.0f), 0.30f, 0.26f, none, false);                   // shell
            FleetSlab(m, MyVector(0.0f, 0.15f, 0.0f), 0.30f, 0.26f, MyVector(0.0f, 0.20f, 0.0f), 0.18f, 0.15f, none, false, 1.1f);               // shell crown
            FleetBar(m, MyVector(-0.34f, 0.17f, 0.0f), MyVector(0.30f, 0.17f, 0.0f), 0.012f, 0.012f, dark, true);                               // the seam down the back
            for (int k = 0; k < 3; ++k) FleetBar(m, MyVector(-0.20f + 0.18f * static_cast<float>(k), 0.16f, -0.28f), MyVector(-0.20f + 0.18f * static_cast<float>(k), 0.16f, 0.28f), 0.010f, 0.010f, dark, true);
            FleetSlab(m, MyVector(0.40f, 0.0f, 0.0f), 0.10f, 0.14f, MyVector(0.40f, 0.10f, 0.0f), 0.08f, 0.12f, none, false, 0.9f);               // head
            FleetBar(m, MyVector(0.44f, 0.07f, 0.06f), MyVector(0.52f, 0.07f, 0.06f), 0.02f, 0.02f, eye, true);  MarkGlow(m, 6, eye);
            FleetBar(m, MyVector(0.44f, 0.07f, -0.06f), MyVector(0.52f, 0.07f, -0.06f), 0.02f, 0.02f, eye, true);  MarkGlow(m, 6, eye);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetBar(m, MyVector(0.46f, 0.0f, 0.10f * z), MyVector(0.72f, 0.0f, 0.22f * z), 0.04f, 0.022f, steel, true);                // mandibles
                FleetBar(m, MyVector(0.72f, 0.0f, 0.22f * z), MyVector(0.84f, 0.0f, 0.08f * z), 0.022f, 0.006f, steel, true);
                FleetBar(m, MyVector(-0.05f, -0.02f, 0.34f * z), MyVector(-0.12f, -0.02f, 0.56f * z), 0.04f, 0.03f, dark, true);            // outrigger arms
                engine(m, -0.36f, 0.04f, 0.14f * z, 0.05f, 0.18f, flameR);
                engine(m, -0.12f, -0.02f, 0.56f * z, 0.04f, 0.16f, flameR);
            }
        }
        // ---------------------------------------------------------------- 4 WASP: head, thorax and a long striped abdomen with a stinger
        {
            Mesh& m = fleet[kFlWasp];
            FleetSlab(m, MyVector(0.42f, -0.08f, 0.0f), 0.10f, 0.10f, MyVector(0.42f, 0.10f, 0.0f), 0.09f, 0.09f, none, false);                 // head
            FleetBar(m, MyVector(0.20f, 0.0f, 0.0f), MyVector(0.02f, 0.0f, 0.0f), 0.11f, 0.12f, none, false);                                   // thorax
            FleetBar(m, MyVector(0.0f, 0.0f, 0.0f), MyVector(-0.55f, -0.02f, 0.0f), 0.09f, 0.05f, none, false, 0.9f);                           // abdomen
            for (int k = 0; k < 4; ++k)
            {
                const float x = -0.08f - 0.12f * static_cast<float>(k);
                FleetBar(m, MyVector(x, 0.0f, 0.0f), MyVector(x - 0.045f, 0.0f, 0.0f), 0.095f - 0.012f * static_cast<float>(k), 0.095f - 0.012f * static_cast<float>(k), dark, true);   // stripes
            }
            FleetBar(m, MyVector(-0.55f, -0.02f, 0.0f), MyVector(-0.82f, -0.02f, 0.0f), 0.03f, 0.004f, steel, true);                            // stinger
            FleetBar(m, MyVector(0.46f, 0.04f, 0.05f), MyVector(0.50f, 0.04f, 0.05f), 0.025f, 0.025f, eye, true);  MarkGlow(m, 6, eye);
            FleetBar(m, MyVector(0.46f, 0.04f, -0.05f), MyVector(0.50f, 0.04f, -0.05f), 0.025f, 0.025f, eye, true);  MarkGlow(m, 6, eye);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetPanel(m, { MyVector(0.14f, 0.07f, 0.08f * z), MyVector(0.04f, 0.10f, 0.62f * z), MyVector(-0.22f, 0.08f, 0.50f * z), MyVector(-0.08f, 0.07f, 0.08f * z) }, glass, true, 0.9f);   // fore wings
                FleetPanel(m, { MyVector(-0.06f, 0.06f, 0.07f * z), MyVector(-0.20f, 0.08f, 0.40f * z), MyVector(-0.42f, 0.07f, 0.28f * z), MyVector(-0.18f, 0.06f, 0.07f * z) }, glass, true, 0.8f); // hind wings
                FleetBar(m, MyVector(0.20f, -0.04f, 0.06f * z), MyVector(0.46f, -0.12f, 0.14f * z), 0.014f, 0.006f, steel, true);          // forelegs / guns
            }
            engine(m, -0.06f, 0.0f, 0.0f, 0.03f, 0.10f, flameR);
        }
        // ---------------------------------------------------------------- 5 CRUISER: an armoured capital ship with a bridge, turrets and an engine bank
        {
            Mesh& m = fleet[kFlCruiser];
            FleetBar(m, MyVector(-0.70f, 0.0f, 0.0f), MyVector(0.30f, 0.0f, 0.0f), 0.16f, 0.14f, none, false);                                  // main hull
            FleetBar(m, MyVector(0.30f, 0.0f, 0.0f), MyVector(0.74f, 0.0f, 0.0f), 0.14f, 0.025f, none, false, 0.95f);                           // armoured prow
            FleetSlab(m, MyVector(-0.20f, 0.14f, 0.0f), 0.20f, 0.10f, MyVector(-0.10f, 0.28f, 0.0f), 0.12f, 0.07f, none, false, 1.05f);          // bridge tower
            FleetBar(m, MyVector(-0.12f, 0.29f, 0.0f), MyVector(-0.04f, 0.29f, 0.0f), 0.04f, 0.04f, glass, true);
            for (int k = 0; k < 3; ++k)                                                                                                         // dorsal turrets
            {
                const float x = 0.25f - 0.28f * static_cast<float>(k);
                FleetSlab(m, MyVector(x, 0.13f, 0.0f), 0.06f, 0.06f, MyVector(x, 0.19f, 0.0f), 0.045f, 0.045f, gun, true);
                FleetBar(m, MyVector(x, 0.17f, 0.0f), MyVector(x + 0.14f, 0.17f, 0.0f), 0.012f, 0.008f, steel, true);
            }
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetPanel(m, { MyVector(0.0f, 0.0f, 0.15f * z), MyVector(-0.30f, 0.0f, 0.44f * z), MyVector(-0.62f, 0.0f, 0.40f * z), MyVector(-0.62f, 0.0f, 0.15f * z) }, none, false, 0.92f);   // sponsons
                FleetBar(m, MyVector(-0.10f, 0.02f, 0.40f * z), MyVector(0.28f, 0.02f, 0.40f * z), 0.02f, 0.012f, gun, true);
                FleetBar(m, MyVector(0.10f, 0.0f, 0.16f * z), MyVector(0.10f, 0.05f, 0.16f * z), 0.02f, 0.02f, lamp, true);  MarkGlow(m, 6, lamp);
                engine(m, -0.70f, 0.04f, 0.10f * z, 0.055f, 0.22f, flame);
                engine(m, -0.70f, -0.06f, 0.10f * z, 0.045f, 0.18f, flame);
            }
        }
        // ---------------------------------------------------------------- 6 CARRIER: a wide hangar ship with a glowing pod bay (the ships that carry power-ups)
        {
            Mesh& m = fleet[kFlCarrier];
            FleetBar(m, MyVector(-0.55f, 0.0f, 0.0f), MyVector(0.55f, 0.0f, 0.0f), 0.13f, 0.06f, none, false);
            FleetSlab(m, MyVector(0.0f, 0.12f, 0.0f), 0.40f, 0.20f, MyVector(0.0f, 0.20f, 0.0f), 0.30f, 0.14f, none, false, 1.05f);              // flight deck
            FleetBar(m, MyVector(-0.10f, 0.22f, 0.0f), MyVector(0.14f, 0.22f, 0.0f), 0.07f, 0.07f, gold, true);  MarkGlow(m, 6, gold);        // glowing pod bay
            for (int k = -1; k <= 1; ++k) FleetBar(m, MyVector(0.30f, 0.205f, 0.07f * static_cast<float>(k)), MyVector(0.46f, 0.205f, 0.07f * static_cast<float>(k)), 0.02f, 0.02f, dark, true);   // hangar slots
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetPanel(m, { MyVector(0.35f, 0.0f, 0.10f * z), MyVector(-0.05f, 0.0f, 0.78f * z), MyVector(-0.40f, 0.0f, 0.70f * z), MyVector(-0.45f, 0.0f, 0.12f * z) }, none, false, 0.93f);
                FleetBar(m, MyVector(0.10f, 0.0f, 0.74f * z), MyVector(-0.35f, 0.0f, 0.70f * z), 0.07f, 0.065f, steel, true);                // outrigger pods
                FleetBar(m, MyVector(0.16f, 0.02f, 0.74f * z), MyVector(0.34f, 0.02f, 0.74f * z), 0.02f, 0.012f, gun, true);
                engine(m, -0.35f, 0.0f, 0.70f * z, 0.06f, 0.22f, flame);
                engine(m, -0.55f, 0.0f, 0.06f * z, 0.05f, 0.20f, flame);
            }
        }
        // ---------------------------------------------------------------- 7 DREADNOUGHT: a boss battleship: stacked armour decks, turret batteries, six engines
        {
            Mesh& m = fleet[kFlDread];
            FleetSlab(m, MyVector(0.0f, -0.06f, 0.0f), 0.78f, 0.34f, MyVector(0.0f, 0.06f, 0.0f), 0.72f, 0.30f, none, false, 0.9f);              // lower deck
            FleetSlab(m, MyVector(-0.04f, 0.06f, 0.0f), 0.62f, 0.26f, MyVector(-0.04f, 0.17f, 0.0f), 0.54f, 0.21f, none, false);                 // upper deck
            FleetBar(m, MyVector(0.72f, 0.0f, 0.0f), MyVector(0.98f, 0.0f, 0.0f), 0.2f, 0.03f, none, false, 0.95f);                              // prow
            FleetSlab(m, MyVector(-0.20f, 0.17f, 0.0f), 0.22f, 0.12f, MyVector(-0.14f, 0.34f, 0.0f), 0.13f, 0.08f, none, false, 1.05f);          // bridge
            FleetBar(m, MyVector(-0.14f, 0.34f, -0.05f), MyVector(-0.14f, 0.34f, 0.05f), 0.035f, 0.035f, glass, true);
            for (int k = 0; k < 4; ++k)                                                                                                         // main batteries
                for (int s = -1; s <= 1; s += 2)
                {
                    const float x = 0.36f - 0.24f * static_cast<float>(k), z = 0.13f * static_cast<float>(s);
                    FleetSlab(m, MyVector(x, 0.17f, z), 0.07f, 0.06f, MyVector(x, 0.24f, z), 0.05f, 0.045f, gun, true);
                    FleetBar(m, MyVector(x, 0.215f, z), MyVector(x + 0.22f, 0.215f, z), 0.014f, 0.01f, steel, true);
                }
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetPanel(m, { MyVector(0.30f, 0.0f, 0.30f * z), MyVector(-0.20f, 0.0f, 0.62f * z), MyVector(-0.74f, 0.0f, 0.55f * z), MyVector(-0.74f, 0.0f, 0.30f * z) }, none, false, 0.92f);
                FleetBar(m, MyVector(0.10f, 0.0f, 0.58f * z), MyVector(0.50f, 0.0f, 0.46f * z), 0.025f, 0.015f, gun, true);                  // broadside guns
                FleetBar(m, MyVector(0.30f, 0.06f, 0.32f * z), MyVector(0.30f, 0.11f, 0.32f * z), 0.02f, 0.02f, lamp, true);  MarkGlow(m, 6, lamp);
                engine(m, -0.78f, 0.0f, 0.12f * z, 0.07f, 0.26f, flame);
                engine(m, -0.78f, 0.0f, 0.30f * z, 0.06f, 0.22f, flame);
                engine(m, -0.74f, 0.0f, 0.52f * z, 0.05f, 0.20f, flame);
            }
        }
        // ---------------------------------------------------------------- 8 DRONE: a ringed sentry pod with a red eye and four antennae
        {
            Mesh& m = fleet[kFlDrone];
            FleetSlab(m, MyVector(0.0f, -0.16f, 0.0f), 0.12f, 0.12f, MyVector(0.0f, 0.0f, 0.0f), 0.2f, 0.2f, none, false);                       // core: a lens-shaped block
            FleetSlab(m, MyVector(0.0f, 0.0f, 0.0f), 0.2f, 0.2f, MyVector(0.0f, 0.16f, 0.0f), 0.12f, 0.12f, none, false);
            for (int k = 0; k < 8; ++k)                                                                                                         // the ring
            {
                float s0, c0, s1, c1;
                FAST_SINCOS_DEG(static_cast<float>(k) * 45.0f, s0, c0);
                FAST_SINCOS_DEG(static_cast<float>(k + 1) * 45.0f, s1, c1);
                FleetBar(m, MyVector(0.34f * c0, 0.0f, 0.34f * s0), MyVector(0.34f * c1, 0.0f, 0.34f * s1), 0.03f, 0.03f, steel, true);
            }
            for (int k = 0; k < 4; ++k)                                                                                                         // spokes + antennae
            {
                float s0, c0;
                FAST_SINCOS_DEG(static_cast<float>(k) * 90.0f + 45.0f, s0, c0);
                FleetBar(m, MyVector(0.18f * c0, 0.0f, 0.18f * s0), MyVector(0.34f * c0, 0.0f, 0.34f * s0), 0.018f, 0.018f, dark, true);
                FleetBar(m, MyVector(0.34f * c0, 0.0f, 0.34f * s0), MyVector(0.46f * c0, 0.10f, 0.46f * s0), 0.014f, 0.004f, steel, true);
            }
            FleetBar(m, MyVector(0.16f, 0.0f, 0.0f), MyVector(0.26f, 0.0f, 0.0f), 0.06f, 0.05f, eye, true);  MarkGlow(m, 6, eye);               // eye
        }
        // ---------------------------------------------------------------- 9 FALCON: a heavy fighter with four swept wings (X-wing layout)
        {
            Mesh& m = fleet[kFlFalcon];
            FleetBar(m, MyVector(-0.40f, 0.0f, 0.0f), MyVector(0.62f, 0.0f, 0.0f), 0.085f, 0.015f, none, false);
            FleetBar(m, MyVector(0.0f, 0.075f, 0.0f), MyVector(0.28f, 0.06f, 0.0f), 0.05f, 0.025f, glass, true);
            for (int s = -1; s <= 1; s += 2)
                for (int u = -1; u <= 1; u += 2)
                {
                    const float z = static_cast<float>(s), y = static_cast<float>(u);
                    FleetPanel(m, { MyVector(0.10f, 0.02f * y, 0.07f * z), MyVector(-0.34f, 0.30f * y, 0.58f * z), MyVector(-0.46f, 0.30f * y, 0.50f * z), MyVector(-0.30f, 0.02f * y, 0.08f * z) }, none, false, 0.94f);
                    FleetBar(m, MyVector(-0.16f, 0.16f * y, 0.33f * z), MyVector(0.40f, 0.16f * y, 0.33f * z), 0.016f, 0.008f, gun, true);      // laser cannons on the tips
                    FleetBar(m, MyVector(-0.34f, 0.30f * y, 0.58f * z), MyVector(-0.46f, 0.30f * y, 0.50f * z), 0.02f, 0.02f, steel, true);
                    engine(m, -0.40f, 0.06f * y, 0.12f * z, 0.04f, 0.18f, flame);
                }
        }
        // ---------------------------------------------------------------- 10 PHANTOM: a faceted stealth flying wing
        {
            Mesh& m = fleet[kFlPhantom];
            const MyVector nose(0.62f, 0.02f, 0.0f), tail(-0.30f, 0.02f, 0.0f);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetPanel(m, { nose, MyVector(-0.12f, 0.02f, 0.52f * z), MyVector(-0.50f, 0.02f, 0.30f * z), tail }, none, false, 1.0f);       // upper facets
                FleetPanel(m, { nose, MyVector(-0.12f, 0.0f, 0.52f * z), MyVector(-0.50f, 0.0f, 0.30f * z), MyVector(-0.30f, 0.0f, 0.0f) }, none, false, 0.7f);    // lower
                FleetBar(m, nose, MyVector(-0.12f, 0.02f, 0.52f * z), 0.012f, 0.012f, steel, true);
                FleetBar(m, MyVector(-0.20f, 0.01f, 0.30f * z), MyVector(-0.46f, 0.01f, 0.24f * z), 0.02f, 0.02f, flameR, true);  MarkGlow(m, 6, flameR);   // glowing exhaust slits
            }
            FleetSlab(m, MyVector(0.0f, 0.02f, 0.0f), 0.30f, 0.07f, MyVector(0.0f, 0.10f, 0.0f), 0.12f, 0.04f, none, false, 1.1f);               // spine
            FleetBar(m, MyVector(0.14f, 0.10f, 0.0f), MyVector(0.34f, 0.05f, 0.0f), 0.035f, 0.02f, glass, true);
        }
        // ---------------------------------------------------------------- 11 SABER: a twin-boom strike craft
        {
            Mesh& m = fleet[kFlSaber];
            FleetBar(m, MyVector(-0.25f, 0.0f, 0.0f), MyVector(0.58f, 0.0f, 0.0f), 0.10f, 0.02f, none, false);                                  // central pod
            FleetBar(m, MyVector(0.05f, 0.09f, 0.0f), MyVector(0.30f, 0.07f, 0.0f), 0.05f, 0.03f, glass, true);
            FleetBar(m, MyVector(0.40f, 0.0f, 0.0f), MyVector(0.74f, 0.0f, 0.0f), 0.025f, 0.01f, gun, true);                                    // nose cannon
            FleetPanel(m, { MyVector(0.20f, 0.0f, 0.06f), MyVector(0.0f, 0.0f, 0.34f), MyVector(0.0f, 0.0f, -0.34f), MyVector(0.20f, 0.0f, -0.06f) }, none, false, 0.95f);   // forward wing
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetBar(m, MyVector(0.10f, 0.0f, 0.34f * z), MyVector(-0.62f, 0.0f, 0.34f * z), 0.05f, 0.04f, none, false, 0.95f);            // booms
                FleetPanel(m, { MyVector(-0.40f, 0.04f, 0.34f * z), MyVector(-0.58f, 0.30f, 0.34f * z), MyVector(-0.64f, 0.04f, 0.34f * z) }, none, false);   // tail fins
                FleetBar(m, MyVector(0.10f, 0.0f, 0.34f * z), MyVector(0.52f, 0.0f, 0.34f * z), 0.02f, 0.01f, gun, true);                       // boom guns
                engine(m, -0.62f, 0.0f, 0.34f * z, 0.05f, 0.22f, flame);
            }
            FleetPanel(m, { MyVector(-0.55f, 0.0f, 0.34f), MyVector(-0.55f, 0.0f, -0.34f), MyVector(-0.66f, 0.0f, -0.34f), MyVector(-0.66f, 0.0f, 0.34f) }, none, false, 0.9f);   // tail plane
        }
        // ---------------------------------------------------------------- 12 HORNET: a compact fighter with canards and a single big engine
        {
            Mesh& m = fleet[kFlHornet];
            FleetBar(m, MyVector(-0.34f, 0.0f, 0.0f), MyVector(0.46f, 0.0f, 0.0f), 0.10f, 0.03f, none, false);
            FleetBar(m, MyVector(0.0f, 0.085f, 0.0f), MyVector(0.24f, 0.07f, 0.0f), 0.055f, 0.03f, glass, true);
            FleetBar(m, MyVector(0.40f, 0.0f, 0.0f), MyVector(0.66f, 0.0f, 0.0f), 0.02f, 0.008f, gun, true);                                    // nose cannon
            FleetPanel(m, { MyVector(-0.28f, 0.05f, 0.0f), MyVector(-0.42f, 0.24f, 0.0f), MyVector(-0.46f, 0.05f, 0.0f) }, none, false);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetPanel(m, { MyVector(0.32f, 0.0f, 0.06f * z), MyVector(0.22f, 0.0f, 0.24f * z), MyVector(0.12f, 0.0f, 0.22f * z), MyVector(0.14f, 0.0f, 0.07f * z) }, none, false, 1.0f);   // canards
                FleetPanel(m, { MyVector(0.04f, 0.0f, 0.09f * z), MyVector(-0.34f, 0.0f, 0.46f * z), MyVector(-0.44f, 0.0f, 0.40f * z), MyVector(-0.28f, 0.0f, 0.10f * z) }, none, false, 0.93f);
                FleetBar(m, MyVector(-0.10f, 0.0f, 0.30f * z), MyVector(0.20f, 0.0f, 0.30f * z), 0.014f, 0.008f, gun, true);
                FleetBar(m, MyVector(-0.34f, 0.0f, 0.46f * z), MyVector(-0.40f, 0.0f, 0.46f * z), 0.02f, 0.02f, lamp, true);  MarkGlow(m, 6, lamp);      // wingtip lights
            }
            engine(m, -0.36f, 0.0f, 0.0f, 0.075f, 0.28f, flameR);
        }
        // ---------------------------------------------------------------- 13 RAVEN: a bat-wing raider with talons
        {
            Mesh& m = fleet[kFlRaven];
            FleetBar(m, MyVector(-0.34f, 0.0f, 0.0f), MyVector(0.40f, 0.0f, 0.0f), 0.09f, 0.04f, none, false);
            FleetSlab(m, MyVector(0.38f, -0.02f, 0.0f), 0.08f, 0.08f, MyVector(0.38f, 0.10f, 0.0f), 0.06f, 0.06f, none, false, 1.05f);          // head
            FleetBar(m, MyVector(0.44f, 0.06f, 0.04f), MyVector(0.50f, 0.06f, 0.04f), 0.02f, 0.02f, eye, true);  MarkGlow(m, 6, eye);
            FleetBar(m, MyVector(0.44f, 0.06f, -0.04f), MyVector(0.50f, 0.06f, -0.04f), 0.02f, 0.02f, eye, true);  MarkGlow(m, 6, eye);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetPanel(m, { MyVector(0.20f, 0.0f, 0.06f * z), MyVector(0.28f, 0.0f, 0.46f * z), MyVector(0.0f, 0.0f, 0.56f * z), MyVector(-0.14f, 0.0f, 0.12f * z) }, none, false, 1.0f);   // inner wing
                FleetPanel(m, { MyVector(0.28f, 0.0f, 0.46f * z), MyVector(-0.02f, 0.0f, 0.88f * z), MyVector(-0.30f, 0.0f, 0.60f * z), MyVector(0.0f, 0.0f, 0.56f * z) }, none, false, 0.9f);   // outer wing
                FleetPanel(m, { MyVector(-0.14f, 0.0f, 0.12f * z), MyVector(0.0f, 0.0f, 0.56f * z), MyVector(-0.30f, 0.0f, 0.60f * z), MyVector(-0.34f, 0.0f, 0.14f * z) }, none, false, 0.85f);
                FleetBar(m, MyVector(0.28f, 0.0f, 0.46f * z), MyVector(-0.02f, 0.0f, 0.88f * z), 0.014f, 0.01f, steel, true);
                FleetBar(m, MyVector(0.30f, -0.04f, 0.08f * z), MyVector(0.52f, -0.08f, 0.14f * z), 0.016f, 0.004f, steel, true);              // talons
                FleetBar(m, MyVector(0.10f, 0.02f, 0.30f * z), MyVector(0.40f, 0.02f, 0.34f * z), 0.014f, 0.008f, gun, true);
                engine(m, -0.34f, 0.0f, 0.07f * z, 0.045f, 0.2f, flameR);
            }
        }
        // ---------------------------------------------------------------- 14 STARHAWK: the player's fighter: sleek, twin canopy, swept wings, angled fins, cyan engines
        {
            Mesh& m = fleet[kFlStarhawk];
            FleetBar(m, MyVector(-0.46f, 0.0f, 0.0f), MyVector(0.62f, 0.0f, 0.0f), 0.085f, 0.012f, none, false);
            FleetBar(m, MyVector(-0.02f, 0.075f, 0.0f), MyVector(0.30f, 0.06f, 0.0f), 0.05f, 0.025f, glass, true);                              // canopy
            FleetSlab(m, MyVector(-0.30f, 0.0f, 0.0f), 0.12f, 0.10f, MyVector(-0.30f, 0.08f, 0.0f), 0.10f, 0.08f, none, false, 1.05f);           // spine
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetPanel(m, { MyVector(0.28f, 0.0f, 0.06f * z), MyVector(-0.34f, 0.0f, 0.60f * z), MyVector(-0.46f, 0.0f, 0.56f * z), MyVector(-0.30f, 0.0f, 0.10f * z) }, none, false, 0.95f);
                FleetBar(m, MyVector(0.28f, 0.0f, 0.06f * z), MyVector(-0.34f, 0.0f, 0.60f * z), 0.014f, 0.014f, steel, true);
                FleetBar(m, MyVector(0.06f, 0.0f, 0.32f * z), MyVector(0.58f, 0.0f, 0.32f * z), 0.02f, 0.01f, gun, true);                        // wing cannons
                FleetPanel(m, { MyVector(-0.28f, 0.04f, 0.10f * z), MyVector(-0.44f, 0.26f, 0.22f * z), MyVector(-0.48f, 0.04f, 0.12f * z) }, none, false);   // canted fins
                FleetBar(m, MyVector(-0.34f, 0.0f, 0.60f * z), MyVector(-0.40f, 0.0f, 0.60f * z), 0.02f, 0.02f, lamp, true);  MarkGlow(m, 6, lamp);   // wingtip nav lights
                engine(m, -0.46f, 0.0f, 0.10f * z, 0.055f, 0.24f, flame);
            }
        }
        // ================================================================================================================
        //  FINE DETAIL PASS.  Every craft above gets panel lines, cockpit framing, intakes, hardpoint missiles, muzzle brakes,
        //  antennae, window rows, running lights (green to port, red to starboard, white / amber elsewhere) and similar small parts.
        //  All of it is built from the same bars, so it adds geometry but no new drawing code; parts that project to under a pixel
        //  are dropped by the renderer, so the detail only costs anything when a craft is close enough to show it.
        // ================================================================================================================
        const MyColor seam(24, 28, 40, 255), navG(70, 255, 130, 255), navR(255, 70, 70, 255), white(232, 236, 245, 255), orange(255, 140, 50, 255), amber(255, 190, 70, 255);
        auto line = [&](Mesh& m, const MyVector& a, const MyVector& b) { FleetBar(m, a, b, 0.006f, 0.006f, seam, true, 0.9f); };                       // a panel seam
        auto light = [&](Mesh& m, const MyVector& p, const MyColor& c) { FleetBar(m, p, p + MyVector(0.02f, 0.0f, 0.0f), 0.013f, 0.01f, c, true); MarkGlow(m, 6, c); };
        auto missile = [&](Mesh& m, float x, float y, float z, float len)                                                                              // pylon + missile with a warhead cone
        {
            FleetBar(m, MyVector(x + len * 0.45f, y + 0.035f, z), MyVector(x + len * 0.45f, y, z), 0.008f, 0.008f, steel, true);
            FleetBar(m, MyVector(x, y, z), MyVector(x + len, y, z), 0.016f, 0.016f, white, true);
            FleetBar(m, MyVector(x + len, y, z), MyVector(x + len + 0.05f, y, z), 0.016f, 0.003f, orange, true);
            FleetBar(m, MyVector(x, y, z), MyVector(x - 0.04f, y, z), 0.014f, 0.014f, dark, true);
        };
        auto muzzle = [&](Mesh& m, const MyVector& tip, float r) { FleetBar(m, tip - MyVector(0.04f, 0.0f, 0.0f), tip, r * 1.7f, r * 1.7f, dark, true); };     // a muzzle brake
        auto frame = [&](Mesh& m, float x0, float x1, float y, float zHalf)                                                                            // cockpit bows across the canopy
        {
            for (int k = 0; k < 3; ++k)
            {
                const float x = x0 + (x1 - x0) * static_cast<float>(k) / 2.0f;
                FleetBar(m, MyVector(x, y, -zHalf), MyVector(x, y, zHalf), 0.006f, 0.006f, seam, true);
            }
        };
        auto intake = [&](Mesh& m, float x, float y, float z, float len, float r) { FleetBar(m, MyVector(x, y, z), MyVector(x + len, y, z), r, r * 0.7f, dark, true); };
        auto windows = [&](Mesh& m, float x0, float x1, float y, float z, int n)                                                                       // a row of lit portholes
        {
            for (int i = 0; i < n; ++i)
            {
                const float x = x0 + (x1 - x0) * static_cast<float>(i) / static_cast<float>(n - 1);
                FleetBar(m, MyVector(x, y, z), MyVector(x + 0.016f, y, z), 0.007f, 0.007f, amber, true);
                MarkGlow(m, 6, amber);
            }
        };
        auto mast = [&](Mesh& m, const MyVector& base, const MyVector& tip, const MyColor& c) { FleetBar(m, base, tip, 0.007f, 0.003f, steel, true); light(m, tip, c); };

        {   // Dart
            Mesh& m = fleet[kFlDart];
            frame(m, 0.06f, 0.26f, 0.088f, 0.034f);
            line(m, MyVector(-0.40f, 0.066f, 0.0f), MyVector(0.02f, 0.062f, 0.0f));
            for (int k = 0; k < 4; ++k) { const float x = -0.34f + 0.12f * static_cast<float>(k); line(m, MyVector(x, 0.066f, -0.05f), MyVector(x, 0.066f, 0.05f)); }
            FleetBar(m, MyVector(0.58f, 0.0f, 0.0f), MyVector(0.76f, 0.0f, 0.0f), 0.006f, 0.002f, steel, true);                                         // pitot probe
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                intake(m, -0.02f, -0.012f, 0.075f * z, 0.18f, 0.03f);
                missile(m, -0.02f, -0.03f, 0.40f * z, 0.20f);
                line(m, MyVector(-0.28f, 0.002f, 0.28f * z), MyVector(-0.42f, 0.002f, 0.46f * z));                                                     // flap hinges
                line(m, MyVector(0.0f, 0.002f, 0.16f * z), MyVector(-0.22f, 0.002f, 0.30f * z));
                light(m, MyVector(-0.42f, 0.0f, 0.52f * z), (s > 0) ? navR : navG);
                muzzle(m, MyVector(0.46f, 0.0f, 0.27f * z), 0.008f);
            }
            light(m, MyVector(-0.47f, 0.24f, 0.0f), white);
        }
        {   // Manta
            Mesh& m = fleet[kFlManta];
            frame(m, 0.18f, 0.34f, 0.118f, 0.038f);
            FleetSlab(m, MyVector(-0.05f, 0.115f, 0.0f), 0.015f, 0.015f, MyVector(-0.05f, 0.15f, 0.0f), 0.075f, 0.075f, steel, true);                  // sensor dish
            FleetBar(m, MyVector(-0.05f, 0.15f, 0.0f), MyVector(-0.05f, 0.20f, 0.0f), 0.006f, 0.006f, steel, true);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                for (int k = 1; k <= 4; ++k)                                                                                                            // wing ribs, leading to trailing edge
                {
                    const float t = 0.2f * static_cast<float>(k);
                    const MyVector le(0.50f + (0.05f - 0.50f) * t, 0.004f, 0.70f * z * t), te(-0.30f + (-0.42f + 0.30f) * t, 0.004f, 0.36f * z * t);
                    line(m, le, te);
                }
                light(m, MyVector(0.05f, 0.0f, 0.70f * z), (s > 0) ? navR : navG);
                muzzle(m, MyVector(0.40f, 0.02f, 0.30f * z), 0.02f);
                intake(m, 0.22f, 0.03f, 0.10f * z, 0.14f, 0.035f);
                FleetBar(m, MyVector(-0.24f, 0.0f, 0.16f * z), MyVector(-0.28f, 0.0f, 0.16f * z), 0.06f, 0.06f, dark, true);                          // engine bell rings
            }
            for (int k = 0; k < 3; ++k) FleetBar(m, MyVector(-0.46f - 0.12f * static_cast<float>(k), 0.0f, 0.0f), MyVector(-0.48f - 0.12f * static_cast<float>(k), 0.0f, 0.0f), 0.045f - 0.01f * static_cast<float>(k), 0.045f - 0.01f * static_cast<float>(k), steel, true);
            light(m, MyVector(-0.78f, 0.0f, 0.0f), white);
        }
        {   // Lancer
            Mesh& m = fleet[kFlLancer];
            frame(m, -0.02f, 0.28f, 0.112f, 0.052f);
            for (int k = 0; k < 6; ++k) { const float x = -0.45f + 0.13f * static_cast<float>(k); line(m, MyVector(x, 0.078f, -0.07f), MyVector(x, 0.078f, 0.07f)); }
            line(m, MyVector(-0.50f, 0.08f, 0.0f), MyVector(0.40f, 0.04f, 0.0f));
            mast(m, MyVector(-0.40f, 0.07f, 0.0f), MyVector(-0.52f, 0.30f, 0.0f), white);                                                                // radar mast
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                muzzle(m, MyVector(0.80f, 0.0f, 0.46f * z), 0.014f);
                missile(m, 0.0f, -0.045f, 0.30f * z, 0.22f);
                FleetBar(m, MyVector(0.0f, 0.0f, 0.08f * z), MyVector(-0.22f, 0.0f, 0.10f * z), 0.04f, 0.035f, steel, true);                          // wing root fairings
                intake(m, -0.14f, 0.0f, 0.095f * z, 0.2f, 0.035f);
                light(m, MyVector(0.16f, 0.0f, 0.54f * z), (s > 0) ? navR : navG);
                line(m, MyVector(0.10f, 0.002f, 0.12f * z), MyVector(0.22f, 0.002f, 0.48f * z));
                line(m, MyVector(-0.06f, 0.002f, 0.10f * z), MyVector(0.10f, 0.002f, 0.50f * z));
            }
        }
        {   // Scarab
            Mesh& m = fleet[kFlScarab];
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                line(m, MyVector(0.0f, 0.205f, 0.0f), MyVector(0.30f, 0.152f, 0.24f * z));                                                              // radial plate seams
                line(m, MyVector(0.0f, 0.205f, 0.0f), MyVector(-0.30f, 0.152f, 0.24f * z));
                mast(m, MyVector(0.46f, 0.08f, 0.05f * z), MyVector(0.70f, 0.22f, 0.14f * z), (s > 0) ? navR : navG);                                   // antennae
                FleetBar(m, MyVector(0.72f, 0.0f, 0.22f * z), MyVector(0.70f, 0.0f, 0.12f * z), 0.014f, 0.006f, steel, true);                           // mandible teeth
                FleetBar(m, MyVector(0.64f, 0.0f, 0.18f * z), MyVector(0.62f, 0.0f, 0.10f * z), 0.014f, 0.006f, steel, true);
                FleetSlab(m, MyVector(-0.12f, -0.03f, 0.56f * z), 0.04f, 0.04f, MyVector(-0.12f, 0.03f, 0.56f * z), 0.03f, 0.03f, steel, true);          // joint caps
                intake(m, 0.10f, 0.12f, 0.20f * z, 0.14f, 0.02f);
            }
            FleetBar(m, MyVector(-0.12f, 0.205f, 0.0f), MyVector(0.10f, 0.205f, 0.0f), 0.03f, 0.03f, flameR, true);  MarkGlow(m, 6, flameR);        // glowing vent on the back
            light(m, MyVector(-0.40f, 0.1f, 0.0f), white);
        }
        {   // Wasp
            Mesh& m = fleet[kFlWasp];
            for (int k = 0; k < 3; ++k) { const float x = -0.40f - 0.05f * static_cast<float>(k) * 2.0f; FleetBar(m, MyVector(x, -0.01f, 0.0f), MyVector(x - 0.03f, -0.01f, 0.0f), 0.062f - 0.012f * static_cast<float>(k), 0.062f - 0.012f * static_cast<float>(k), dark, true); }
            FleetBar(m, MyVector(-0.82f, -0.02f, 0.0f), MyVector(-0.86f, -0.02f, 0.0f), 0.012f, 0.008f, flameR, true);  MarkGlow(m, 6, flameR);   // stinger tip glows
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                mast(m, MyVector(0.46f, 0.10f, 0.03f * z), MyVector(0.66f, 0.22f, 0.10f * z), (s > 0) ? navR : navG);
                line(m, MyVector(0.14f, 0.075f, 0.08f * z), MyVector(0.04f, 0.10f, 0.62f * z));                                                         // wing veins
                line(m, MyVector(0.0f, 0.08f, 0.20f * z), MyVector(-0.22f, 0.08f, 0.50f * z));
                line(m, MyVector(-0.06f, 0.07f, 0.07f * z), MyVector(-0.42f, 0.07f, 0.28f * z));
                line(m, MyVector(-0.12f, 0.07f, 0.14f * z), MyVector(-0.30f, 0.07f, 0.30f * z));
                for (int k = 0; k < 3; ++k)                                                                                                             // three pairs of legs
                    FleetBar(m, MyVector(0.14f - 0.09f * static_cast<float>(k), -0.08f, 0.08f * z), MyVector(0.10f - 0.09f * static_cast<float>(k), -0.20f, 0.20f * z), 0.012f, 0.006f, steel, true);
                FleetSlab(m, MyVector(0.40f, 0.02f, 0.085f * z), 0.022f, 0.022f, MyVector(0.40f, 0.07f, 0.085f * z), 0.03f, 0.03f, eye, true);          // compound eye
            }
        }
        {   // Cruiser
            Mesh& m = fleet[kFlCruiser];
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                windows(m, -0.58f, 0.18f, 0.03f, 0.158f * z, 6);
                windows(m, -0.58f, 0.18f, -0.04f, 0.158f * z, 6);
                for (int k = 0; k < 2; ++k)                                                                                                             // point-defence turrets on the sponsons
                {
                    const float x = -0.30f + 0.28f * static_cast<float>(k);
                    FleetSlab(m, MyVector(x, 0.0f, 0.32f * z), 0.04f, 0.04f, MyVector(x, 0.045f, 0.32f * z), 0.03f, 0.03f, gun, true);
                    FleetBar(m, MyVector(x, 0.03f, 0.32f * z), MyVector(x + 0.09f, 0.03f, 0.32f * z), 0.007f, 0.005f, steel, true);
                }
                light(m, MyVector(0.10f, 0.0f, 0.42f * z), (s > 0) ? navR : navG);
            }
            for (int k = 0; k < 6; ++k) { const float x = -0.60f + 0.14f * static_cast<float>(k); line(m, MyVector(x, 0.158f, -0.12f), MyVector(x, 0.158f, 0.12f)); }     // hull plating seams
            line(m, MyVector(-0.65f, 0.16f, 0.0f), MyVector(0.30f, 0.16f, 0.0f));
            FleetSlab(m, MyVector(-0.15f, 0.30f, 0.0f), 0.01f, 0.01f, MyVector(-0.15f, 0.36f, 0.0f), 0.08f, 0.08f, steel, true);                       // radar dish on the bridge
            mast(m, MyVector(-0.15f, 0.30f, 0.0f), MyVector(-0.15f, 0.44f, 0.0f), white);
            light(m, MyVector(0.72f, 0.0f, 0.0f), amber);
        }
        {   // Carrier
            Mesh& m = fleet[kFlCarrier];
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                for (int k = 0; k < 6; ++k) light(m, MyVector(-0.30f + 0.12f * static_cast<float>(k), 0.205f, 0.135f * z), (k & 1) ? amber : white);       // deck-edge lights
                light(m, MyVector(-0.40f, 0.0f, 0.70f * z), (s > 0) ? navR : navG);
                FleetBar(m, MyVector(0.22f, 0.205f, 0.07f * z), MyVector(0.40f, 0.205f, 0.07f * z), 0.008f, 0.008f, lamp, true);  MarkGlow(m, 6, lamp);  // lit hangar slots
            }
            FleetSlab(m, MyVector(-0.34f, 0.20f, 0.12f), 0.07f, 0.04f, MyVector(-0.34f, 0.33f, 0.12f), 0.05f, 0.03f, none, false, 1.05f);                 // control tower
            FleetBar(m, MyVector(-0.30f, 0.30f, 0.12f), MyVector(-0.24f, 0.30f, 0.12f), 0.012f, 0.012f, glass, true);
            mast(m, MyVector(-0.34f, 0.33f, 0.12f), MyVector(-0.36f, 0.46f, 0.12f), white);
            for (int k = 0; k < 5; ++k) { const float x = -0.45f + 0.18f * static_cast<float>(k); line(m, MyVector(x, 0.133f, -0.1f), MyVector(x, 0.133f, 0.1f)); }
            line(m, MyVector(-0.50f, 0.135f, 0.0f), MyVector(0.45f, 0.07f, 0.0f));
        }
        {   // Dreadnought
            Mesh& m = fleet[kFlDread];
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                windows(m, -0.62f, 0.60f, 0.0f, 0.336f * z, 5);
                windows(m, -0.50f, 0.46f, 0.11f, 0.262f * z, 4);
                FleetSlab(m, MyVector(0.40f, 0.17f, 0.20f * z), 0.04f, 0.03f, MyVector(0.40f, 0.22f, 0.20f * z), 0.03f, 0.022f, gun, true);               // point-defence mounts
                FleetBar(m, MyVector(0.40f, 0.20f, 0.20f * z), MyVector(0.50f, 0.20f, 0.20f * z), 0.006f, 0.004f, steel, true);
                light(m, MyVector(-0.74f, 0.0f, 0.60f * z), (s > 0) ? navR : navG);
            }
            mast(m, MyVector(-0.14f, 0.34f, 0.0f), MyVector(-0.14f, 0.52f, 0.0f), white);
            mast(m, MyVector(-0.20f, 0.30f, 0.08f), MyVector(-0.24f, 0.44f, 0.12f), amber);
            for (int k = 0; k < 5; ++k) { const float x = -0.55f + 0.2f * static_cast<float>(k); line(m, MyVector(x, 0.172f, -0.18f), MyVector(x, 0.172f, 0.18f)); }
        }
        {   // Drone
            Mesh& m = fleet[kFlDrone];
            for (int k = 0; k < 8; ++k)
            {
                float s0, c0;
                FAST_SINCOS_DEG(static_cast<float>(k) * 45.0f, s0, c0);
                light(m, MyVector(0.34f * c0 - 0.01f, 0.03f, 0.34f * s0), (k & 1) ? amber : white);                                                    // beacons round the ring
            }
            for (int k = 0; k < 4; ++k)
            {
                float s0, c0;
                FAST_SINCOS_DEG(static_cast<float>(k) * 90.0f, s0, c0);
                line(m, MyVector(0.04f * c0, 0.165f, 0.04f * s0), MyVector(0.11f * c0, 0.0f, 0.11f * s0));                                              // core panel seams
                FleetBar(m, MyVector(0.0f, 0.16f, 0.0f), MyVector(0.0f, 0.30f, 0.0f), 0.006f, 0.003f, steel, true);
            }
            mast(m, MyVector(0.0f, 0.16f, 0.0f), MyVector(0.0f, 0.32f, 0.0f), eye);
            mast(m, MyVector(0.0f, -0.16f, 0.0f), MyVector(0.0f, -0.30f, 0.0f), navG);
        }
        {   // Falcon
            Mesh& m = fleet[kFlFalcon];
            frame(m, 0.02f, 0.26f, 0.105f, 0.046f);
            line(m, MyVector(-0.38f, 0.088f, 0.0f), MyVector(0.0f, 0.082f, 0.0f));
            for (int k = 0; k < 4; ++k) { const float x = -0.32f + 0.1f * static_cast<float>(k); line(m, MyVector(x, 0.088f, -0.06f), MyVector(x, 0.088f, 0.06f)); }
            for (int s = -1; s <= 1; s += 2)
                for (int u = -1; u <= 1; u += 2)
                {
                    const float z = static_cast<float>(s), y = static_cast<float>(u);
                    muzzle(m, MyVector(0.40f, 0.16f * y, 0.33f * z), 0.008f);
                    FleetBar(m, MyVector(0.0f, 0.04f * y, 0.09f * z), MyVector(-0.20f, 0.12f * y, 0.30f * z), 0.02f, 0.016f, steel, true);              // wing root struts
                    light(m, MyVector(-0.48f, 0.30f * y, 0.50f * z), (s > 0) ? navR : navG);
                    line(m, MyVector(0.0f, 0.06f * y, 0.12f * z), MyVector(-0.30f, 0.26f * y, 0.52f * z));
                }
            intake(m, -0.05f, 0.0f, 0.095f, 0.2f, 0.035f);
            intake(m, -0.05f, 0.0f, -0.095f, 0.2f, 0.035f);
            FleetBar(m, MyVector(0.62f, 0.0f, 0.0f), MyVector(0.78f, 0.0f, 0.0f), 0.006f, 0.002f, steel, true);
        }
        {   // Phantom
            Mesh& m = fleet[kFlPhantom];
            frame(m, 0.14f, 0.34f, 0.128f, 0.03f);
            FleetBar(m, MyVector(0.62f, 0.02f, 0.0f), MyVector(0.78f, 0.02f, 0.0f), 0.006f, 0.002f, steel, true);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                line(m, MyVector(0.30f, 0.026f, 0.0f), MyVector(-0.30f, 0.026f, 0.30f * z));                                                            // facet edges
                line(m, MyVector(0.0f, 0.026f, 0.0f), MyVector(-0.50f, 0.026f, 0.30f * z));
                line(m, MyVector(0.40f, 0.026f, 0.10f * z), MyVector(-0.12f, 0.026f, 0.52f * z));
                intake(m, 0.10f, 0.05f, 0.075f * z, 0.16f, 0.025f);
                light(m, MyVector(-0.14f, 0.02f, 0.52f * z), (s > 0) ? navR : navG);
            }
        }
        {   // Saber
            Mesh& m = fleet[kFlSaber];
            frame(m, 0.04f, 0.28f, 0.115f, 0.05f);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                for (int k = 0; k < 5; ++k) { const float x = 0.0f - 0.14f * static_cast<float>(k); FleetBar(m, MyVector(x, 0.0f, 0.34f * z), MyVector(x - 0.02f, 0.0f, 0.34f * z), 0.058f, 0.058f, dark, true); }   // boom ribs
                missile(m, 0.04f, -0.035f, 0.20f * z, 0.20f);
                muzzle(m, MyVector(0.52f, 0.0f, 0.34f * z), 0.01f);
                light(m, MyVector(-0.62f, 0.30f, 0.34f * z), (s > 0) ? navR : navG);
                line(m, MyVector(0.18f, 0.002f, 0.10f * z), MyVector(0.0f, 0.002f, 0.30f * z));
            }
            muzzle(m, MyVector(0.74f, 0.0f, 0.0f), 0.01f);
            line(m, MyVector(-0.22f, 0.10f, 0.0f), MyVector(0.45f, 0.03f, 0.0f));
        }
        {   // Hornet
            Mesh& m = fleet[kFlHornet];
            frame(m, 0.02f, 0.22f, 0.115f, 0.05f);
            line(m, MyVector(-0.32f, 0.10f, 0.0f), MyVector(0.35f, 0.04f, 0.0f));
            for (int k = 0; k < 4; ++k) { const float x = -0.26f + 0.12f * static_cast<float>(k); line(m, MyVector(x, 0.1f, -0.06f), MyVector(x, 0.1f, 0.06f)); }
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                intake(m, 0.02f, -0.02f, 0.105f * z, 0.2f, 0.035f);
                missile(m, -0.20f, -0.02f, 0.40f * z, 0.2f);
                light(m, MyVector(0.24f, 0.0f, 0.24f * z), (s > 0) ? navR : navG);
                line(m, MyVector(0.04f, 0.002f, 0.12f * z), MyVector(-0.34f, 0.002f, 0.44f * z));
            }
            FleetBar(m, MyVector(0.66f, 0.0f, 0.0f), MyVector(0.80f, 0.0f, 0.0f), 0.006f, 0.002f, steel, true);
            light(m, MyVector(-0.44f, 0.22f, 0.0f), white);
        }
        {   // Raven
            Mesh& m = fleet[kFlRaven];
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                for (int k = 0; k < 5; ++k)                                                                                                             // feather lines across both wing sections
                {
                    const float t = 0.2f * static_cast<float>(k + 1);
                    line(m, MyVector(0.20f + (0.28f - 0.20f) * t, 0.004f, (0.06f + 0.40f * t) * z), MyVector(-0.14f + (-0.30f + 0.14f) * t, 0.004f, (0.12f + 0.48f * t) * z));
                }
                line(m, MyVector(0.28f, 0.004f, 0.46f * z), MyVector(-0.30f, 0.004f, 0.60f * z));
                FleetBar(m, MyVector(0.34f, -0.05f, 0.10f * z), MyVector(0.54f, -0.10f, 0.18f * z), 0.012f, 0.003f, steel, true);                        // extra talons
                FleetBar(m, MyVector(0.28f, -0.04f, 0.06f * z), MyVector(0.46f, -0.08f, 0.10f * z), 0.012f, 0.003f, steel, true);
                light(m, MyVector(-0.04f, 0.0f, 0.88f * z), (s > 0) ? navR : navG);
                intake(m, 0.18f, 0.02f, 0.06f * z, 0.14f, 0.025f);
            }
            FleetBar(m, MyVector(0.36f, 0.10f, 0.0f), MyVector(0.20f, 0.20f, 0.0f), 0.016f, 0.004f, steel, true);                                       // head crest
            line(m, MyVector(-0.30f, 0.09f, 0.0f), MyVector(0.30f, 0.05f, 0.0f));
        }
        {   // Starhawk
            Mesh& m = fleet[kFlStarhawk];
            frame(m, 0.0f, 0.28f, 0.10f, 0.046f);
            FleetBar(m, MyVector(-0.02f, 0.062f, 0.045f), MyVector(0.30f, 0.05f, 0.026f), 0.008f, 0.006f, steel, true);                                 // canopy rails
            FleetBar(m, MyVector(-0.02f, 0.062f, -0.045f), MyVector(0.30f, 0.05f, -0.026f), 0.008f, 0.006f, steel, true);
            line(m, MyVector(-0.44f, 0.09f, 0.0f), MyVector(-0.02f, 0.085f, 0.0f));
            for (int k = 0; k < 4; ++k) { const float x = -0.38f + 0.1f * static_cast<float>(k); line(m, MyVector(x, 0.088f, -0.06f), MyVector(x, 0.088f, 0.06f)); }
            FleetPanel(m, { MyVector(-0.30f, -0.03f, 0.0f), MyVector(-0.42f, -0.18f, 0.0f), MyVector(-0.46f, -0.03f, 0.0f) }, none, false);           // ventral fin
            FleetBar(m, MyVector(0.62f, 0.0f, 0.0f), MyVector(0.78f, 0.0f, 0.0f), 0.006f, 0.002f, steel, true);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                intake(m, 0.0f, -0.015f, 0.085f * z, 0.2f, 0.03f);
                missile(m, 0.0f, -0.035f, 0.42f * z, 0.22f);
                muzzle(m, MyVector(0.58f, 0.0f, 0.32f * z), 0.01f);
                light(m, MyVector(-0.50f, 0.0f, 0.56f * z), (s > 0) ? navR : navG);
                line(m, MyVector(0.10f, 0.002f, 0.10f * z), MyVector(-0.30f, 0.002f, 0.56f * z));
                line(m, MyVector(0.0f, 0.002f, 0.10f * z), MyVector(-0.40f, 0.002f, 0.52f * z));
                FleetBar(m, MyVector(-0.48f, 0.0f, 0.10f * z), MyVector(-0.50f, 0.0f, 0.10f * z), 0.07f, 0.07f, steel, true);                           // engine bell rims
                light(m, MyVector(-0.46f, 0.26f, 0.22f * z), white);
            }
        }
        // ================================================================================================================
        //  THE SPECIALISTS - ships that fight with something other than a plain bullet - plus the player's Spartan and the mine.
        //  Same conventions as above (nose +X, wings +/-Z, up +Y, hull = instance colour, accents fixed + glowing).
        //      15 Lasher   charged laser beam      20 Hunter   homing missiles        25 Torpedo  heavy torpedo boat
        //      16 Pulsar   expanding shock-wave    21 Swarmer  micro-missile swarm    26 Spartan  the player's fighter
        //      17 Miner    drifting mines          22 Railer   railgun                27 Mine     proximity mine
        //      18 Seeder   heat-seeking mines      23 Flakker  flak spread cannons
        //                                          24 Spitter  burst plasma
        // ================================================================================================================
        const MyColor laserC(255, 70, 70, 255), plasmaC(120, 255, 130, 255), pulseC(140, 205, 255, 255), mineC(255, 120, 40, 255), railC(120, 175, 255, 255), coil(70, 110, 220, 255);
        // a ring of n bars round the X axis at x (emitter hoops, rail coils, focusing rings)
        auto hoop = [&](Mesh& m, float x, float rr, int n, float th, const MyColor& c, bool glow)
        {
            for (int k = 0; k < n; ++k)
            {
                float s0, c0, s1, c1;
                FAST_SINCOS_DEG(360.0f * static_cast<float>(k) / static_cast<float>(n), s0, c0);
                FAST_SINCOS_DEG(360.0f * static_cast<float>(k + 1) / static_cast<float>(n), s1, c1);
                FleetBar(m, MyVector(x, rr * c0, rr * s0), MyVector(x, rr * c1, rr * s1), th, th, c, true);
                if (glow) MarkGlow(m, 6, c);
            }
        };
        {   // 15 LASHER: a long gunship with a big focusing lens in the nose and capacitor pods charging along the wings
            Mesh& m = fleet[kFlLasher];
            FleetBar(m, MyVector(-0.50f, 0.0f, 0.0f), MyVector(0.34f, 0.0f, 0.0f), 0.11f, 0.075f, none, false);
            FleetBar(m, MyVector(0.34f, 0.0f, 0.0f), MyVector(0.58f, 0.0f, 0.0f), 0.075f, 0.05f, steel, true);                                  // lens housing
            hoop(m, 0.50f, 0.085f, 6, 0.012f, dark, false);                                                                                     // focusing ring
            FleetBar(m, MyVector(0.58f, 0.0f, 0.0f), MyVector(0.66f, 0.0f, 0.0f), 0.045f, 0.045f, laserC, true);  MarkGlow(m, 6, laserC);        // the lens
            FleetBar(m, MyVector(-0.05f, 0.09f, 0.0f), MyVector(0.24f, 0.075f, 0.0f), 0.055f, 0.03f, glass, true);
            FleetPanel(m, { MyVector(-0.32f, 0.08f, 0.0f), MyVector(-0.50f, 0.30f, 0.0f), MyVector(-0.56f, 0.08f, 0.0f) }, none, false);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetPanel(m, { MyVector(0.16f, 0.0f, 0.09f * z), MyVector(-0.34f, 0.0f, 0.52f * z), MyVector(-0.50f, 0.0f, 0.44f * z), MyVector(-0.42f, 0.0f, 0.10f * z) }, none, false, 0.94f);
                FleetBar(m, MyVector(0.10f, 0.0f, 0.30f * z), MyVector(-0.34f, 0.0f, 0.30f * z), 0.055f, 0.05f, steel, true);                  // capacitor pods
                FleetBar(m, MyVector(0.06f, 0.056f, 0.30f * z), MyVector(-0.30f, 0.056f, 0.30f * z), 0.012f, 0.012f, laserC, true);  MarkGlow(m, 6, laserC);   // charge strips
                FleetBar(m, MyVector(0.10f, 0.0f, 0.30f * z), MyVector(0.34f, 0.0f, 0.30f * z), 0.03f, 0.012f, gun, true);
                engine(m, -0.50f, 0.0f, 0.09f * z, 0.05f, 0.22f, flameR);
                light(m, MyVector(-0.44f, 0.0f, 0.47f * z), (s > 0) ? navR : navG);
            }
        }
        {   // 16 PULSAR: three glowing emitter hoops round the nose, braced by spokes - it throws expanding rings
            Mesh& m = fleet[kFlPulsar];
            FleetBar(m, MyVector(-0.48f, 0.0f, 0.0f), MyVector(0.20f, 0.0f, 0.0f), 0.10f, 0.13f, none, false);
            FleetBar(m, MyVector(0.20f, 0.0f, 0.0f), MyVector(0.36f, 0.0f, 0.0f), 0.13f, 0.07f, none, false, 0.9f);
            hoop(m, 0.40f, 0.28f, 12, 0.016f, pulseC, true);
            hoop(m, 0.54f, 0.19f, 10, 0.014f, pulseC, true);
            hoop(m, 0.64f, 0.11f, 8, 0.012f, white, true);
            for (int k = 0; k < 4; ++k)
            {
                float s0, c0;
                FAST_SINCOS_DEG(45.0f + 90.0f * static_cast<float>(k), s0, c0);
                FleetBar(m, MyVector(0.28f, 0.09f * c0, 0.09f * s0), MyVector(0.40f, 0.28f * c0, 0.28f * s0), 0.018f, 0.014f, steel, true);
            }
            FleetBar(m, MyVector(0.36f, 0.0f, 0.0f), MyVector(0.72f, 0.0f, 0.0f), 0.03f, 0.012f, pulseC, true);  MarkGlow(m, 6, pulseC);
            FleetBar(m, MyVector(-0.10f, 0.12f, 0.0f), MyVector(0.14f, 0.12f, 0.0f), 0.04f, 0.03f, glass, true);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetPanel(m, { MyVector(0.0f, 0.0f, 0.11f * z), MyVector(-0.36f, 0.0f, 0.46f * z), MyVector(-0.48f, 0.0f, 0.40f * z), MyVector(-0.42f, 0.0f, 0.12f * z) }, none, false, 0.93f);
                engine(m, -0.48f, 0.0f, 0.09f * z, 0.05f, 0.20f, flame);
                light(m, MyVector(-0.46f, 0.0f, 0.40f * z), (s > 0) ? navR : navG);
            }
        }
        {   // 17 MINER: a broad, flat minelayer with an open drop bay and mines racked underneath
            Mesh& m = fleet[kFlMiner];
            FleetSlab(m, MyVector(0.0f, -0.07f, 0.0f), 0.50f, 0.30f, MyVector(0.0f, 0.07f, 0.0f), 0.42f, 0.24f, none, false);
            FleetBar(m, MyVector(0.46f, 0.0f, 0.0f), MyVector(0.74f, 0.0f, 0.0f), 0.13f, 0.02f, none, false, 0.95f);
            FleetBar(m, MyVector(0.18f, 0.09f, 0.0f), MyVector(0.46f, 0.07f, 0.0f), 0.06f, 0.03f, glass, true);
            FleetSlab(m, MyVector(-0.42f, -0.075f, 0.0f), 0.06f, 0.20f, MyVector(-0.42f, -0.14f, 0.0f), 0.05f, 0.18f, dark, true);                // the open drop bay
            for (int k = 0; k < 5; ++k)                                                                                                         // mines in the rack
            {
                const float x = -0.34f + 0.17f * static_cast<float>(k), z = (k & 1) ? 0.10f : -0.10f;
                FleetBar(m, MyVector(x, -0.09f, z), MyVector(x, -0.22f, z), 0.045f, 0.045f, steel, true);
                FleetBar(m, MyVector(x, -0.22f, z), MyVector(x, -0.255f, z), 0.022f, 0.022f, mineC, true);  MarkGlow(m, 6, mineC);
            }
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetBar(m, MyVector(0.10f, 0.0f, 0.28f * z), MyVector(0.10f, 0.0f, 0.46f * z), 0.04f, 0.03f, steel, true);
                FleetBar(m, MyVector(0.28f, 0.0f, 0.50f * z), MyVector(-0.32f, 0.0f, 0.50f * z), 0.06f, 0.055f, none, false, 0.9f);              // outrigger pods
                engine(m, -0.32f, 0.0f, 0.50f * z, 0.05f, 0.20f, flameR);
                engine(m, -0.50f, 0.02f, 0.14f * z, 0.055f, 0.22f, flameR);
                windows(m, -0.30f, 0.30f, 0.01f, 0.272f * z, 5);
                light(m, MyVector(0.28f, 0.0f, 0.52f * z), (s > 0) ? navR : navG);
            }
        }
        {   // 18 SEEDER: a six-armed pod ship; each arm ends in a glowing heat-seeker mine
            Mesh& m = fleet[kFlSeeder];
            FleetSlab(m, MyVector(0.0f, -0.14f, 0.0f), 0.10f, 0.10f, MyVector(0.0f, 0.0f, 0.0f), 0.18f, 0.18f, none, false);
            FleetSlab(m, MyVector(0.0f, 0.0f, 0.0f), 0.18f, 0.18f, MyVector(0.0f, 0.14f, 0.0f), 0.10f, 0.10f, none, false);
            FleetBar(m, MyVector(0.14f, 0.0f, 0.0f), MyVector(0.32f, 0.0f, 0.0f), 0.07f, 0.05f, eye, true);  MarkGlow(m, 6, eye);               // the sensor eye
            FleetBar(m, MyVector(-0.20f, 0.0f, 0.0f), MyVector(-0.50f, 0.0f, 0.0f), 0.09f, 0.05f, none, false, 0.9f);
            hoop(m, -0.05f, 0.14f, 8, 0.012f, steel, false);
            for (int k = 0; k < 6; ++k)
            {
                float s0, c0;
                FAST_SINCOS_DEG(60.0f * static_cast<float>(k), s0, c0);
                FleetBar(m, MyVector(-0.05f, 0.12f * c0, 0.12f * s0), MyVector(-0.16f, 0.40f * c0, 0.40f * s0), 0.02f, 0.02f, steel, true);
                FleetBar(m, MyVector(-0.16f, 0.40f * c0, 0.40f * s0), MyVector(-0.32f, 0.42f * c0, 0.42f * s0), 0.065f, 0.045f, none, false);       // the mine pod
                FleetBar(m, MyVector(-0.32f, 0.42f * c0, 0.42f * s0), MyVector(-0.36f, 0.43f * c0, 0.43f * s0), 0.03f, 0.03f, mineC, true);  MarkGlow(m, 6, mineC);
            }
            engine(m, -0.50f, 0.0f, 0.0f, 0.07f, 0.26f, flameR);
        }
        {   // 19 HUNTER: a missile frigate: launch tubes along the flanks and a rack of missiles on every wing
            Mesh& m = fleet[kFlHunter];
            FleetBar(m, MyVector(-0.55f, 0.0f, 0.0f), MyVector(0.30f, 0.0f, 0.0f), 0.12f, 0.10f, none, false);
            FleetBar(m, MyVector(0.30f, 0.0f, 0.0f), MyVector(0.72f, 0.0f, 0.0f), 0.10f, 0.015f, none, false, 0.95f);
            FleetBar(m, MyVector(0.0f, 0.10f, 0.0f), MyVector(0.26f, 0.09f, 0.0f), 0.05f, 0.03f, glass, true);
            mast(m, MyVector(-0.30f, 0.11f, 0.0f), MyVector(-0.36f, 0.30f, 0.0f), white);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetPanel(m, { MyVector(0.20f, 0.0f, 0.10f * z), MyVector(-0.10f, 0.0f, 0.52f * z), MyVector(-0.40f, 0.0f, 0.48f * z), MyVector(-0.42f, 0.0f, 0.12f * z) }, none, false, 0.93f);
                for (int k = 0; k < 3; ++k)
                {
                    const float y = -0.05f + 0.06f * static_cast<float>(k);
                    FleetBar(m, MyVector(-0.02f, y, 0.125f * z), MyVector(0.34f, y, 0.125f * z), 0.026f, 0.026f, dark, true);                    // launch tubes
                    light(m, MyVector(0.34f, y, 0.125f * z), orange);
                    missile(m, 0.04f, -0.035f, (0.22f + 0.11f * static_cast<float>(k)) * z, 0.30f);                                              // wing-rack missiles
                }
                engine(m, -0.55f, 0.04f, 0.08f * z, 0.06f, 0.24f, flame);
                light(m, MyVector(-0.40f, 0.0f, 0.50f * z), (s > 0) ? navR : navG);
            }
        }
        {   // 20 SWARMER: a block of micro-missile tubes on its back
            Mesh& m = fleet[kFlSwarmer];
            FleetBar(m, MyVector(-0.45f, 0.0f, 0.0f), MyVector(0.40f, 0.0f, 0.0f), 0.10f, 0.06f, none, false);
            FleetSlab(m, MyVector(-0.05f, 0.08f, 0.0f), 0.34f, 0.20f, MyVector(-0.05f, 0.18f, 0.0f), 0.30f, 0.17f, steel, true);               // launcher block
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 3; ++j)
                {
                    const float x = -0.26f + 0.16f * static_cast<float>(i), z = (static_cast<float>(j) - 1.0f) * 0.10f;
                    FleetBar(m, MyVector(x, 0.18f, z), MyVector(x + 0.035f, 0.19f, z), 0.034f, 0.034f, dark, true);
                    FleetBar(m, MyVector(x + 0.035f, 0.19f, z), MyVector(x + 0.05f, 0.195f, z), 0.02f, 0.02f, orange, true);  MarkGlow(m, 6, orange);
                }
            FleetBar(m, MyVector(0.14f, 0.08f, 0.0f), MyVector(0.34f, 0.06f, 0.0f), 0.05f, 0.03f, glass, true);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetPanel(m, { MyVector(0.10f, 0.0f, 0.09f * z), MyVector(-0.30f, 0.0f, 0.42f * z), MyVector(-0.46f, 0.0f, 0.36f * z), MyVector(-0.36f, 0.0f, 0.10f * z) }, none, false, 0.94f);
                engine(m, -0.45f, 0.0f, 0.13f * z, 0.05f, 0.22f, flameR);
                light(m, MyVector(-0.42f, 0.0f, 0.38f * z), (s > 0) ? navR : navG);
            }
        }
        {   // 21 RAILER: a very long twin-rail gun (about 1.4 units) with glowing coils
            Mesh& m = fleet[kFlRailer];
            FleetBar(m, MyVector(-0.55f, 0.0f, 0.0f), MyVector(-0.10f, 0.0f, 0.0f), 0.12f, 0.09f, none, false);
            FleetBar(m, MyVector(-0.30f, 0.10f, 0.0f), MyVector(-0.05f, 0.09f, 0.0f), 0.05f, 0.03f, glass, true);
            FleetBar(m, MyVector(-0.10f, 0.0f, 0.0f), MyVector(0.0f, 0.0f, 0.0f), 0.10f, 0.10f, dark, true);                                  // breech
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetBar(m, MyVector(0.0f, 0.0f, 0.07f * z), MyVector(0.95f, 0.0f, 0.07f * z), 0.02f, 0.014f, steel, true);                    // the twin rails
                FleetPanel(m, { MyVector(-0.30f, 0.0f, 0.10f * z), MyVector(-0.50f, 0.0f, 0.40f * z), MyVector(-0.58f, 0.0f, 0.36f * z), MyVector(-0.54f, 0.0f, 0.10f * z) }, none, false, 0.93f);
                engine(m, -0.55f, 0.0f, 0.09f * z, 0.055f, 0.24f, flame);
                light(m, MyVector(-0.52f, 0.0f, 0.38f * z), (s > 0) ? navR : navG);
            }
            for (int k = 0; k < 7; ++k) hoop(m, 0.08f + 0.13f * static_cast<float>(k), 0.085f, 6, 0.011f, coil, true);                          // accelerator coils
            FleetBar(m, MyVector(0.95f, 0.0f, 0.0f), MyVector(1.03f, 0.0f, 0.0f), 0.035f, 0.03f, railC, true);  MarkGlow(m, 6, railC);        // muzzle glow
        }
        {   // 22 FLAKKER: a stubby, armoured flak boat with a six-barrel cluster
            Mesh& m = fleet[kFlFlakker];
            FleetBar(m, MyVector(-0.40f, 0.0f, 0.0f), MyVector(0.38f, 0.0f, 0.0f), 0.14f, 0.09f, none, false);
            FleetSlab(m, MyVector(0.0f, 0.10f, 0.0f), 0.20f, 0.17f, MyVector(0.0f, 0.18f, 0.0f), 0.16f, 0.14f, steel, true);                    // turret drum
            for (int k = 0; k < 6; ++k)
            {
                float s0, c0;
                FAST_SINCOS_DEG(60.0f * static_cast<float>(k), s0, c0);
                FleetBar(m, MyVector(0.0f, 0.21f + 0.04f * c0, 0.04f * s0), MyVector(0.36f, 0.21f + 0.07f * c0, 0.10f * s0), 0.016f, 0.012f, gun, true);
                muzzle(m, MyVector(0.36f, 0.21f + 0.07f * c0, 0.10f * s0), 0.012f);
            }
            FleetBar(m, MyVector(0.12f, 0.0f, 0.0f), MyVector(0.30f, 0.07f, 0.0f), 0.05f, 0.03f, glass, true);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetPanel(m, { MyVector(0.30f, 0.0f, 0.10f * z), MyVector(0.05f, 0.0f, 0.50f * z), MyVector(-0.20f, 0.0f, 0.48f * z), MyVector(-0.30f, 0.0f, 0.12f * z) }, none, false, 0.92f);   // armour plates
                FleetBar(m, MyVector(0.10f, 0.02f, 0.40f * z), MyVector(0.38f, 0.02f, 0.40f * z), 0.02f, 0.012f, gun, true);
                engine(m, -0.40f, 0.0f, 0.10f * z, 0.06f, 0.22f, flameR);
                light(m, MyVector(-0.20f, 0.0f, 0.49f * z), (s > 0) ? navR : navG);
            }
        }
        {   // 23 SPITTER: an organic plasma boat with a glowing sac and three barrels between curved prongs
            Mesh& m = fleet[kFlSpitter];
            FleetBar(m, MyVector(-0.45f, 0.0f, 0.0f), MyVector(0.10f, 0.0f, 0.0f), 0.08f, 0.11f, none, false);
            FleetSlab(m, MyVector(0.22f, -0.14f, 0.0f), 0.10f, 0.10f, MyVector(0.22f, 0.0f, 0.0f), 0.16f, 0.16f, plasmaC, true);  MarkGlow(m, 6, plasmaC);   // plasma sac
            FleetSlab(m, MyVector(0.22f, 0.0f, 0.0f), 0.16f, 0.16f, MyVector(0.22f, 0.14f, 0.0f), 0.10f, 0.10f, plasmaC, true);  MarkGlow(m, 6, plasmaC);
            for (int k = -1; k <= 1; ++k) FleetBar(m, MyVector(0.34f, 0.04f * static_cast<float>(k), 0.04f * static_cast<float>(k * k)), MyVector(0.66f, 0.03f * static_cast<float>(k), 0.0f), 0.03f, 0.015f, gun, true);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetBar(m, MyVector(0.10f, 0.0f, 0.07f * z), MyVector(0.60f, 0.0f, 0.22f * z), 0.035f, 0.012f, steel, true);                   // prongs
                FleetBar(m, MyVector(0.60f, 0.0f, 0.22f * z), MyVector(0.74f, 0.0f, 0.05f * z), 0.012f, 0.004f, plasmaC, true);  MarkGlow(m, 6, plasmaC);
                FleetPanel(m, { MyVector(0.0f, 0.0f, 0.08f * z), MyVector(-0.30f, 0.0f, 0.50f * z), MyVector(-0.42f, 0.0f, 0.30f * z), MyVector(-0.36f, 0.0f, 0.08f * z) }, none, false, 0.93f);
                engine(m, -0.45f, 0.0f, 0.07f * z, 0.045f, 0.20f, flameR);
                light(m, MyVector(-0.40f, 0.0f, 0.31f * z), (s > 0) ? navR : navG);
            }
            FleetBar(m, MyVector(-0.10f, 0.08f, 0.0f), MyVector(0.04f, 0.10f, 0.0f), 0.035f, 0.025f, glass, true);
        }
        {   // 24 TORPEDO BOAT: a heavy hull carrying one big torpedo slung underneath
            Mesh& m = fleet[kFlTorpedo];
            FleetBar(m, MyVector(-0.55f, 0.0f, 0.0f), MyVector(0.35f, 0.0f, 0.0f), 0.17f, 0.13f, none, false);
            FleetBar(m, MyVector(0.35f, 0.0f, 0.0f), MyVector(0.62f, 0.0f, 0.0f), 0.13f, 0.03f, none, false, 0.95f);
            FleetBar(m, MyVector(-0.02f, 0.15f, 0.0f), MyVector(0.22f, 0.13f, 0.0f), 0.06f, 0.04f, glass, true);
            FleetBar(m, MyVector(-0.35f, -0.24f, 0.0f), MyVector(0.55f, -0.24f, 0.0f), 0.085f, 0.085f, steel, true);                           // the torpedo
            FleetBar(m, MyVector(0.55f, -0.24f, 0.0f), MyVector(0.82f, -0.24f, 0.0f), 0.085f, 0.01f, orange, true);  MarkGlow(m, 6, orange);   // glowing warhead
            FleetBar(m, MyVector(0.10f, -0.24f, 0.0f), MyVector(0.15f, -0.24f, 0.0f), 0.095f, 0.095f, dark, true);
            FleetBar(m, MyVector(-0.10f, -0.13f, 0.0f), MyVector(-0.10f, -0.21f, 0.0f), 0.03f, 0.03f, steel, true);                             // pylons
            FleetBar(m, MyVector(0.30f, -0.12f, 0.0f), MyVector(0.30f, -0.21f, 0.0f), 0.03f, 0.03f, steel, true);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetPanel(m, { MyVector(0.30f, 0.0f, 0.14f * z), MyVector(0.0f, 0.0f, 0.52f * z), MyVector(-0.40f, 0.0f, 0.50f * z), MyVector(-0.50f, 0.0f, 0.16f * z) }, none, false, 0.92f);
                FleetBar(m, MyVector(0.0f, 0.0f, 0.50f * z), MyVector(0.30f, 0.0f, 0.40f * z), 0.025f, 0.015f, gun, true);
                engine(m, -0.55f, 0.04f, 0.13f * z, 0.07f, 0.26f, flame);
                light(m, MyVector(-0.40f, 0.0f, 0.51f * z), (s > 0) ? navR : navG);
            }
        }
        {   // 25 SPARTAN: the player's fighter - layered armoured fuselage, dorsal spine, framed canopy, big intakes, double-delta wings with strakes, canted twin fins,
            //             wing-root cannons, missile racks and two large engines
            Mesh& m = fleet[kFlSpartan];
            FleetBar(m, MyVector(0.22f, 0.0f, 0.0f), MyVector(0.80f, 0.0f, 0.0f), 0.10f, 0.012f, none, false, 0.97f);                           // needle nose
            FleetBar(m, MyVector(-0.14f, 0.0f, 0.0f), MyVector(0.22f, 0.0f, 0.0f), 0.135f, 0.10f, none, false, 1.0f);                           // forward body
            FleetBar(m, MyVector(-0.55f, 0.0f, 0.0f), MyVector(-0.14f, 0.0f, 0.0f), 0.12f, 0.135f, none, false, 0.88f);                         // aft body
            FleetSlab(m, MyVector(-0.30f, 0.10f, 0.0f), 0.26f, 0.07f, MyVector(-0.30f, 0.17f, 0.0f), 0.20f, 0.045f, none, false, 1.08f);        // dorsal spine
            FleetBar(m, MyVector(0.0f, 0.105f, 0.0f), MyVector(0.40f, 0.075f, 0.0f), 0.075f, 0.035f, glass, true);                              // canopy
            frame(m, 0.04f, 0.34f, 0.135f, 0.062f);
            FleetBar(m, MyVector(0.0f, 0.12f, 0.06f), MyVector(0.40f, 0.088f, 0.03f), 0.008f, 0.006f, steel, true);                             // canopy rails
            FleetBar(m, MyVector(0.0f, 0.12f, -0.06f), MyVector(0.40f, 0.088f, -0.03f), 0.008f, 0.006f, steel, true);
            FleetPanel(m, { MyVector(-0.20f, -0.12f, 0.0f), MyVector(-0.40f, -0.26f, 0.0f), MyVector(-0.52f, -0.12f, 0.0f) }, none, false);      // ventral keel
            mast(m, MyVector(-0.50f, 0.17f, 0.0f), MyVector(-0.60f, 0.36f, 0.0f), white);
            line(m, MyVector(-0.52f, 0.14f, 0.0f), MyVector(0.0f, 0.132f, 0.0f));
            for (int k = 0; k < 5; ++k) { const float x = -0.46f + 0.13f * static_cast<float>(k); line(m, MyVector(x, 0.14f, -0.08f), MyVector(x, 0.14f, 0.08f)); }
            FleetBar(m, MyVector(0.80f, 0.0f, 0.0f), MyVector(0.94f, 0.0f, 0.0f), 0.006f, 0.002f, steel, true);                                  // pitot probe
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetPanel(m, { MyVector(0.34f, 0.0f, 0.10f * z), MyVector(-0.30f, 0.0f, 0.84f * z), MyVector(-0.54f, 0.0f, 0.78f * z), MyVector(-0.46f, 0.0f, 0.12f * z) }, none, false, 0.95f);   // main delta
                FleetPanel(m, { MyVector(0.46f, 0.0f, 0.07f * z), MyVector(0.30f, 0.0f, 0.32f * z), MyVector(0.14f, 0.0f, 0.27f * z), MyVector(0.20f, 0.0f, 0.08f * z) }, none, false, 1.03f);  // strake
                FleetBar(m, MyVector(0.34f, 0.0f, 0.10f * z), MyVector(-0.30f, 0.0f, 0.84f * z), 0.016f, 0.016f, steel, true);                  // leading edge
                FleetBar(m, MyVector(-0.30f, 0.0f, 0.84f * z), MyVector(-0.54f, 0.0f, 0.78f * z), 0.014f, 0.014f, steel, true);                 // wingtip
                line(m, MyVector(-0.20f, 0.003f, 0.18f * z), MyVector(-0.44f, 0.003f, 0.74f * z));                                              // flap and aileron hinges
                line(m, MyVector(-0.34f, 0.003f, 0.14f * z), MyVector(-0.50f, 0.003f, 0.76f * z));
                line(m, MyVector(0.10f, 0.003f, 0.16f * z), MyVector(-0.20f, 0.003f, 0.62f * z));
                FleetBar(m, MyVector(0.10f, 0.0f, 0.30f * z), MyVector(0.62f, 0.0f, 0.30f * z), 0.022f, 0.01f, gun, true);                      // wing-root cannons
                muzzle(m, MyVector(0.62f, 0.0f, 0.30f * z), 0.01f);
                intake(m, -0.04f, -0.01f, 0.16f * z, 0.30f, 0.058f);                                                                            // big intakes
                FleetBar(m, MyVector(-0.04f, -0.01f, 0.16f * z), MyVector(-0.02f, -0.01f, 0.16f * z), 0.07f, 0.07f, steel, true);               // intake lips
                missile(m, 0.0f, -0.025f, 0.56f * z, 0.28f);                                                                                    // missile racks under the wings
                missile(m, 0.04f, -0.025f, 0.70f * z, 0.24f);
                FleetPanel(m, { MyVector(-0.30f, 0.08f, 0.14f * z), MyVector(-0.50f, 0.40f, 0.30f * z), MyVector(-0.58f, 0.38f, 0.28f * z), MyVector(-0.56f, 0.08f, 0.14f * z) }, none, false, 1.0f);   // canted fin
                light(m, MyVector(-0.52f, 0.40f, 0.30f * z), white);
                light(m, MyVector(-0.50f, 0.0f, 0.79f * z), (s > 0) ? navR : navG);
                FleetBar(m, MyVector(-0.55f, 0.0f, 0.16f * z), MyVector(-0.30f, 0.0f, 0.16f * z), 0.075f, 0.085f, steel, true, 0.9f);         // engine nacelle
                engine(m, -0.55f, 0.0f, 0.16f * z, 0.075f, 0.30f, flame);
                FleetBar(m, MyVector(-0.56f, 0.0f, 0.16f * z), MyVector(-0.58f, 0.0f, 0.16f * z), 0.095f, 0.095f, steel, true);                 // exhaust rim
            }
        }
        {   // 26 MINE: a dark core with six long spikes, eight short ones, a glowing belt and glowing spike tips
            Mesh& m = fleet[kFlMine];
            AddBoxFaces(m, MyVector(0.0f, 0.0f, 0.0f), MyVector(0.22f, 0.22f, 0.22f), 1.0f, false, none);
            FleetBar(m, MyVector(-0.05f, 0.0f, 0.0f), MyVector(0.05f, 0.0f, 0.0f), 0.26f, 0.26f, mineC, true);  MarkGlow(m, 6, mineC);          // the glowing belt
            for (int ax = 0; ax < 3; ++ax)
                for (int sg = -1; sg <= 1; sg += 2)
                {
                    const float d = static_cast<float>(sg);
                    const MyVector u = (ax == 0) ? MyVector(d, 0.0f, 0.0f) : ((ax == 1) ? MyVector(0.0f, d, 0.0f) : MyVector(0.0f, 0.0f, d));
                    FleetBar(m, u * 0.20f, u * 0.62f, 0.085f, 0.008f, steel, true);
                    FleetBar(m, u * 0.60f, u * 0.68f, 0.02f, 0.012f, laserC, true);  MarkGlow(m, 6, laserC);
                }
            for (int k = 0; k < 8; ++k)
            {
                const MyVector u((k & 1) ? 1.0f : -1.0f, (k & 2) ? 1.0f : -1.0f, (k & 4) ? 1.0f : -1.0f);
                const MyVector un = u * 0.57735f;
                FleetBar(m, un * 0.18f, un * 0.46f, 0.055f, 0.006f, steel, true);
            }
        }
        // ================================================================================================
        //  THE SECOND FLEET: twelve more ships, each built round one of the firing techniques the first fleet does not have (spiral pin-wheels, nova rings, gatling streams, weaving
        //  shots, mine rings, homing rings, trident volleys, crossfire missiles) - and the last four carry two or three of them.  Nose along +X, wings along +/-Z, up = +Y, about one
        //  unit long.  Armour plates are fixed dark metal; lamps, emitters and engine exhausts glow.
        // ================================================================================================
        const MyColor nTeal(60, 225, 235, 255), nViolet(170, 110, 255, 255), nLime(150, 255, 90, 255), nHot(255, 120, 60, 255), nPlate(86, 94, 118, 255), nPlateD(44, 48, 62, 255),
                      nAmber(255, 190, 70, 255), nMagenta(255, 80, 200, 255), nBone(214, 206, 188, 255), nCrimson(190, 40, 52, 255);
        auto nRadial = [](float x, float a, float r) { return MyVector(x, r * std::cos(a), r * std::sin(a)); };         // a point round the X axis
        const float nKPi2 = 6.28318530718f;
        // ---------------------------------------------------------------- VORTEX: a pin-wheel gunship - a hub with four swept, glowing vanes (fires spiral pin-wheels)
        {
            Mesh& m = fleet[kFlVortex];
            FleetBar(m, MyVector(-0.16f, 0.0f, 0.0f), MyVector(0.30f, 0.0f, 0.0f), 0.15f, 0.07f, nPlate, true, 1.0f);
            FleetBar(m, MyVector(0.28f, 0.0f, 0.0f), MyVector(0.58f, 0.0f, 0.0f), 0.07f, 0.008f, steel, true, 1.0f);
            FleetBar(m, MyVector(0.04f, 0.0f, 0.0f), MyVector(0.12f, 0.0f, 0.0f), 0.19f, 0.19f, nTeal, true);  MarkGlow(m, 6, nTeal);
            for (int k = 0; k < 4; ++k)
            {
                const float a = nKPi2 * static_cast<float>(k) / 4.0f;
                FleetPanel(m, { nRadial(0.10f, a, 0.14f), nRadial(-0.30f, a + 0.55f, 0.56f), nRadial(-0.40f, a + 0.50f, 0.50f), nRadial(-0.10f, a + 0.05f, 0.15f) }, none, false, 0.95f);
                FleetBar(m, nRadial(0.10f, a, 0.14f), nRadial(-0.30f, a + 0.55f, 0.56f), 0.016f, 0.016f, steel, true);
                FleetBar(m, nRadial(-0.28f, a + 0.55f, 0.54f), nRadial(-0.34f, a + 0.52f, 0.52f), 0.03f, 0.03f, nTeal, true);  MarkGlow(m, 6, nTeal);          // glowing vane tips
            }
            FleetBar(m, MyVector(0.20f, 0.07f, 0.0f), MyVector(0.40f, 0.05f, 0.0f), 0.04f, 0.02f, glass, true);                                          // cockpit
            engine(m, -0.18f, 0.0f, 0.0f, 0.10f, 0.30f, flame);
        }
        // ---------------------------------------------------------------- NOVA: a spiked power-core ship - a glowing sphere in a cage with six emitter spikes (fires nova rings)
        {
            Mesh& m = fleet[kFlNova];
            AddBoxFaces(m, MyVector(0.0f, 0.0f, 0.0f), MyVector(0.17f, 0.17f, 0.17f), 1.0f, true, nMagenta);  MarkGlow(m, 6 * 1, nMagenta);
            AddBoxFaces(m, MyVector(0.0f, 0.0f, 0.0f), MyVector(0.13f, 0.13f, 0.13f), 1.0f, true, MyColor(255, 230, 250, 255));  MarkGlow(m, 6, MyColor(255, 230, 250, 255));
            for (int k = 0; k < 3; ++k)                                                                                                                   // the cage: three crossed rings of armour
            {
                const float a = nKPi2 * static_cast<float>(k) / 6.0f;
                for (int s = 0; s < 8; ++s)
                {
                    const float t0 = nKPi2 * static_cast<float>(s) / 8.0f, t1 = nKPi2 * static_cast<float>(s + 1) / 8.0f;
                    const MyVector p0(0.34f * std::cos(t0), 0.34f * std::sin(t0) * std::cos(a), 0.34f * std::sin(t0) * std::sin(a)), p1(0.34f * std::cos(t1), 0.34f * std::sin(t1) * std::cos(a), 0.34f * std::sin(t1) * std::sin(a));
                    FleetBar(m, p0, p1, 0.028f, 0.028f, nPlate, true);
                }
            }
            for (int ax = 0; ax < 3; ++ax)
                for (int sg = -1; sg <= 1; sg += 2)
                {
                    const float d = static_cast<float>(sg);
                    const MyVector u = (ax == 0) ? MyVector(d, 0.0f, 0.0f) : ((ax == 1) ? MyVector(0.0f, d, 0.0f) : MyVector(0.0f, 0.0f, d));
                    FleetBar(m, u * 0.34f, u * 0.58f, 0.05f, 0.014f, steel, true);
                    FleetBar(m, u * 0.55f, u * 0.62f, 0.025f, 0.012f, nMagenta, true);  MarkGlow(m, 6, nMagenta);
                }
            engine(m, -0.34f, 0.0f, 0.0f, 0.07f, 0.24f, flameR);
        }
        // ---------------------------------------------------------------- GATLING: a heavy gunship with a six-barrel rotary cannon (fires long streams)
        {
            Mesh& m = fleet[kFlGatling];
            FleetBar(m, MyVector(-0.40f, 0.0f, 0.0f), MyVector(0.14f, 0.0f, 0.0f), 0.16f, 0.13f, nPlate, true, 1.0f);
            FleetBar(m, MyVector(-0.30f, 0.10f, 0.0f), MyVector(0.02f, 0.13f, 0.0f), 0.09f, 0.06f, nPlateD, true);                                         // dorsal armour
            FleetBar(m, MyVector(0.04f, 0.10f, 0.0f), MyVector(0.20f, 0.09f, 0.0f), 0.05f, 0.03f, glass, true);                                          // cockpit
            FleetBar(m, MyVector(0.12f, 0.0f, 0.0f), MyVector(0.20f, 0.0f, 0.0f), 0.115f, 0.115f, dark, true);                                           // gun housing
            for (int k = 0; k < 6; ++k)                                                                                                                   // the barrel cluster
            {
                const float a = nKPi2 * static_cast<float>(k) / 6.0f;
                FleetBar(m, nRadial(0.16f, a, 0.07f), nRadial(0.62f, a, 0.07f), 0.022f, 0.02f, (k & 1) ? gun : steel, true);
                FleetBar(m, nRadial(0.60f, a, 0.07f), nRadial(0.64f, a, 0.07f), 0.014f, 0.01f, nAmber, true);  MarkGlow(m, 6, nAmber);                         // glowing muzzles
            }
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetPanel(m, { MyVector(0.0f, -0.02f, 0.14f * z), MyVector(-0.34f, -0.02f, 0.46f * z), MyVector(-0.46f, -0.02f, 0.34f * z), MyVector(-0.40f, -0.02f, 0.14f * z) }, none, false, 0.9f);
                FleetBar(m, MyVector(-0.30f, -0.02f, 0.30f * z), MyVector(-0.34f, 0.0f, 0.46f * z), 0.04f, 0.03f, dark, true);                              // ammo drums
                engine(m, -0.42f, 0.0f, 0.10f * z, 0.06f, 0.26f, flameR);
            }
        }
        // ---------------------------------------------------------------- SERPENT: a long segmented body, each segment smaller, with finned sides (fires weaving shots)
        {
            Mesh& m = fleet[kFlSerpent];
            static const float sx[6] = { 0.44f, 0.26f, 0.08f, -0.10f, -0.28f, -0.44f }, sr[6] = { 0.07f, 0.13f, 0.12f, 0.10f, 0.075f, 0.05f };
            for (int i = 0; i < 6; ++i)
            {
                FleetBar(m, MyVector(sx[i] + 0.07f, 0.0f, 0.0f), MyVector(sx[i] - 0.07f, 0.0f, 0.0f), sr[i], sr[i] * 0.9f, (i & 1) ? nPlate : nPlateD, true, 1.0f);
                FleetBar(m, MyVector(sx[i], 0.0f, 0.0f), MyVector(sx[i] - 0.015f, 0.0f, 0.0f), sr[i] * 1.12f, sr[i] * 1.12f, nLime, true);  MarkGlow(m, 6, nLime);       // glowing joints
                if (i >= 1 && i <= 4)
                    for (int s = -1; s <= 1; s += 2)
                        FleetPanel(m, { MyVector(sx[i] + 0.05f, 0.0f, sr[i] * static_cast<float>(s)), MyVector(sx[i] - 0.04f, 0.0f, (sr[i] + 0.16f) * static_cast<float>(s)), MyVector(sx[i] - 0.08f, 0.0f, sr[i] * static_cast<float>(s)) }, none, false, 0.9f);
            }
            FleetBar(m, MyVector(0.44f, 0.0f, 0.0f), MyVector(0.62f, 0.0f, 0.0f), 0.05f, 0.01f, steel, true);                                            // snout
            FleetBar(m, MyVector(0.40f, 0.06f, 0.04f), MyVector(0.45f, 0.06f, 0.04f), 0.02f, 0.02f, eye, true);  MarkGlow(m, 6, eye);                    // eyes
            FleetBar(m, MyVector(0.40f, 0.06f, -0.04f), MyVector(0.45f, 0.06f, -0.04f), 0.02f, 0.02f, eye, true);  MarkGlow(m, 6, eye);
            engine(m, -0.46f, 0.0f, 0.0f, 0.045f, 0.22f, nLime);
        }
        // ---------------------------------------------------------------- SOWER: a broad seeder barge ringed by mine bays (lays rings of mines)
        {
            Mesh& m = fleet[kFlSower];
            FleetBar(m, MyVector(-0.40f, 0.0f, 0.0f), MyVector(0.40f, 0.0f, 0.0f), 0.14f, 0.09f, nPlate, true, 1.0f);
            for (int s = 0; s < 8; ++s)                                                                                                                   // the bay ring: eight pods round the hull, each with a mine showing
            {
                const float a = nKPi2 * static_cast<float>(s) / 8.0f;
                FleetBar(m, nRadial(0.12f, a, 0.26f), nRadial(-0.12f, a, 0.26f), 0.065f, 0.065f, nPlateD, true);
                FleetBar(m, nRadial(0.12f, a, 0.26f), nRadial(0.17f, a, 0.26f), 0.045f, 0.03f, nHot, true);  MarkGlow(m, 6, nHot);
                FleetBar(m, nRadial(0.0f, a, 0.14f), nRadial(0.0f, a, 0.22f), 0.02f, 0.02f, steel, true);                                                  // spokes
            }
            FleetBar(m, MyVector(0.30f, 0.10f, 0.0f), MyVector(0.42f, 0.08f, 0.0f), 0.04f, 0.025f, glass, true);
            engine(m, -0.40f, 0.0f, 0.0f, 0.10f, 0.28f, nHot);
        }
        // ---------------------------------------------------------------- REAPER: a crescent raider - swept-forward blade wings with glowing edges (fires homing rings)
        {
            Mesh& m = fleet[kFlReaper];
            FleetBar(m, MyVector(-0.30f, 0.0f, 0.0f), MyVector(0.40f, 0.0f, 0.0f), 0.08f, 0.02f, nPlateD, true, 1.0f);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetPanel(m, { MyVector(-0.20f, 0.0f, 0.06f * z), MyVector(0.10f, 0.0f, 0.60f * z), MyVector(0.34f, 0.0f, 0.64f * z), MyVector(-0.02f, 0.0f, 0.30f * z), MyVector(-0.34f, 0.0f, 0.14f * z) }, none, false, 0.9f);
                FleetBar(m, MyVector(0.10f, 0.0f, 0.60f * z), MyVector(0.34f, 0.0f, 0.64f * z), 0.02f, 0.012f, nCrimson, true);  MarkGlow(m, 6, nCrimson);       // the glowing blade edge
                FleetBar(m, MyVector(-0.16f, 0.0f, 0.10f * z), MyVector(0.10f, 0.0f, 0.58f * z), 0.014f, 0.014f, steel, true);
                FleetBar(m, MyVector(-0.04f, 0.03f, 0.30f * z), MyVector(0.06f, 0.03f, 0.30f * z), 0.03f, 0.03f, nViolet, true);  MarkGlow(m, 6, nViolet);       // ring launchers
                engine(m, -0.30f, 0.0f, 0.08f * z, 0.045f, 0.22f, nViolet);
            }
            FleetBar(m, MyVector(0.04f, 0.06f, 0.0f), MyVector(0.24f, 0.04f, 0.0f), 0.04f, 0.02f, glass, true);
        }
        // ---------------------------------------------------------------- TRIDENT: three long forward barrels on a flat hull (fires fan volleys of railgun slugs)
        {
            Mesh& m = fleet[kFlTrident];
            FleetBar(m, MyVector(-0.40f, 0.0f, 0.0f), MyVector(0.12f, 0.0f, 0.0f), 0.13f, 0.10f, nPlate, true, 1.0f);
            for (int k = -1; k <= 1; ++k)
            {
                const float z = 0.13f * static_cast<float>(k);
                FleetBar(m, MyVector(0.06f, 0.0f, z), MyVector(0.62f, 0.0f, z * 1.4f), 0.035f, 0.022f, (k == 0) ? steel : gun, true);
                FleetBar(m, MyVector(0.58f, 0.0f, z * 1.4f), MyVector(0.64f, 0.0f, z * 1.4f), 0.026f, 0.016f, nTeal, true);  MarkGlow(m, 6, nTeal);
                FleetBar(m, MyVector(0.20f, 0.0f, z), MyVector(0.30f, 0.0f, z), 0.05f, 0.05f, dark, true);                                               // coupling
            }
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetPanel(m, { MyVector(0.0f, -0.02f, 0.12f * z), MyVector(-0.30f, -0.02f, 0.44f * z), MyVector(-0.44f, -0.02f, 0.30f * z), MyVector(-0.38f, -0.02f, 0.12f * z) }, none, false, 0.9f);
                engine(m, -0.42f, 0.0f, 0.08f * z, 0.055f, 0.26f, flame);
            }
            FleetBar(m, MyVector(-0.10f, 0.09f, 0.0f), MyVector(0.04f, 0.08f, 0.0f), 0.045f, 0.03f, glass, true);
        }
        // ---------------------------------------------------------------- CROSSFIRE: a twin-hull catamaran slung with missile pods on its outer sides (fires crossing missiles)
        {
            Mesh& m = fleet[kFlCrossfire];
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetBar(m, MyVector(-0.40f, 0.0f, 0.22f * z), MyVector(0.46f, 0.0f, 0.22f * z), 0.08f, 0.02f, nPlate, true, 1.0f);
                FleetBar(m, MyVector(-0.20f, 0.0f, 0.36f * z), MyVector(0.10f, 0.0f, 0.36f * z), 0.07f, 0.07f, nPlateD, true);                              // missile pods
                for (int k = 0; k < 3; ++k) FleetBar(m, MyVector(0.10f, 0.03f * static_cast<float>(k - 1), 0.36f * z + 0.02f * static_cast<float>(k - 1)), MyVector(0.14f, 0.03f * static_cast<float>(k - 1), 0.36f * z), 0.022f, 0.022f, nHot, true);
                FleetBar(m, MyVector(-0.10f, 0.0f, 0.22f * z), MyVector(-0.20f, 0.0f, 0.36f * z), 0.02f, 0.02f, steel, true);
                engine(m, -0.40f, 0.0f, 0.22f * z, 0.06f, 0.26f, flameR);
            }
            FleetBar(m, MyVector(-0.20f, 0.0f, -0.22f), MyVector(-0.20f, 0.0f, 0.22f), 0.06f, 0.06f, dark, true);                                       // the bridge between the hulls
            FleetBar(m, MyVector(0.0f, 0.06f, -0.12f), MyVector(0.0f, 0.06f, 0.12f), 0.05f, 0.05f, nPlate, true);
            FleetBar(m, MyVector(0.08f, 0.10f, 0.0f), MyVector(0.20f, 0.09f, 0.0f), 0.04f, 0.025f, glass, true);
        }
        // ---------------------------------------------------------------- AEGIS: a heavy armoured cruiser with shield plates, turrets and a bow emitter (bolt flak, shock-waves and missiles)
        {
            Mesh& m = fleet[kFlAegis];
            FleetBar(m, MyVector(-0.44f, 0.0f, 0.0f), MyVector(0.44f, 0.0f, 0.0f), 0.17f, 0.07f, nPlate, true, 1.0f);
            FleetBar(m, MyVector(-0.30f, 0.12f, 0.0f), MyVector(0.20f, 0.10f, 0.0f), 0.10f, 0.06f, nPlateD, true);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetSlab(m, MyVector(0.0f, -0.10f, 0.16f * z), 0.30f, 0.03f, MyVector(0.0f, 0.12f, 0.30f * z), 0.26f, 0.025f, steel, true, 0.95f);        // the big side shield plates
                FleetBar(m, MyVector(-0.14f, 0.06f, 0.22f * z), MyVector(0.16f, 0.06f, 0.22f * z), 0.014f, 0.014f, nTeal, true);  MarkGlow(m, 6, nTeal);        // shield rims
                FleetBar(m, MyVector(0.0f, 0.17f, 0.10f * z), MyVector(0.20f, 0.19f, 0.10f * z), 0.045f, 0.03f, gun, true);                                // turrets
                engine(m, -0.46f, 0.0f, 0.10f * z, 0.07f, 0.28f, flame);
            }
            FleetBar(m, MyVector(0.40f, 0.0f, 0.0f), MyVector(0.56f, 0.0f, 0.0f), 0.05f, 0.012f, nViolet, true);  MarkGlow(m, 6, nViolet);                    // the bow emitter
            FleetBar(m, MyVector(0.10f, 0.19f, 0.0f), MyVector(0.28f, 0.17f, 0.0f), 0.045f, 0.03f, glass, true);
        }
        // ---------------------------------------------------------------- TEMPEST: a stealth delta with a rotating emitter dome (spiral pin-wheels and nova rings)
        {
            Mesh& m = fleet[kFlTempest];
            FleetBar(m, MyVector(-0.34f, 0.0f, 0.0f), MyVector(0.52f, 0.0f, 0.0f), 0.09f, 0.012f, nPlateD, true, 1.0f);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetPanel(m, { MyVector(0.30f, 0.0f, 0.03f * z), MyVector(-0.32f, 0.0f, 0.62f * z), MyVector(-0.46f, 0.0f, 0.46f * z), MyVector(-0.42f, 0.0f, 0.10f * z) }, none, false, 0.9f);
                FleetBar(m, MyVector(0.30f, 0.0f, 0.03f * z), MyVector(-0.32f, 0.0f, 0.62f * z), 0.014f, 0.014f, nViolet, true);  MarkGlow(m, 6, nViolet);
                FleetBar(m, MyVector(-0.20f, 0.02f, 0.24f * z), MyVector(-0.10f, 0.02f, 0.24f * z), 0.035f, 0.035f, nTeal, true);  MarkGlow(m, 6, nTeal);
                engine(m, -0.42f, 0.0f, 0.06f * z, 0.05f, 0.24f, nViolet);
            }
            for (int k = 0; k < 6; ++k)                                                                                                                   // the emitter dome: a ring of six glowing studs
            {
                const float a = nKPi2 * static_cast<float>(k) / 6.0f;
                FleetBar(m, MyVector(0.0f + 0.07f * std::cos(a), 0.09f, 0.07f * std::sin(a)), MyVector(0.0f + 0.07f * std::cos(a), 0.15f, 0.07f * std::sin(a)), 0.02f, 0.014f, nTeal, true);  MarkGlow(m, 6, nTeal);
            }
            FleetBar(m, MyVector(0.14f, 0.06f, 0.0f), MyVector(0.34f, 0.04f, 0.0f), 0.04f, 0.015f, glass, true);
        }
        // ---------------------------------------------------------------- WARDEN: a dreadnought-class battleship, four weapon systems (gatling streams, torpedoes, shock-waves)
        {
            Mesh& m = fleet[kFlWarden];
            FleetBar(m, MyVector(-0.46f, 0.0f, 0.0f), MyVector(0.46f, 0.0f, 0.0f), 0.19f, 0.09f, nPlate, true, 1.0f);
            FleetBar(m, MyVector(-0.34f, 0.15f, 0.0f), MyVector(0.14f, 0.13f, 0.0f), 0.12f, 0.08f, nPlateD, true);
            FleetBar(m, MyVector(0.10f, 0.20f, 0.0f), MyVector(0.30f, 0.18f, 0.0f), 0.06f, 0.04f, glass, true);                                          // command bridge
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetSlab(m, MyVector(-0.10f, -0.06f, 0.18f * z), 0.34f, 0.04f, MyVector(-0.10f, 0.08f, 0.36f * z), 0.30f, 0.03f, steel, true, 0.95f);        // armour wings
                for (int k = 0; k < 3; ++k) FleetBar(m, MyVector(0.10f, 0.02f, 0.30f * z + 0.03f * static_cast<float>(k - 1)), MyVector(0.50f, 0.02f, 0.30f * z + 0.03f * static_cast<float>(k - 1)), 0.016f, 0.012f, gun, true);   // gatling cluster
                FleetBar(m, MyVector(0.0f, -0.10f, 0.14f * z), MyVector(0.30f, -0.10f, 0.14f * z), 0.05f, 0.035f, nHot, true);  MarkGlow(m, 6, nHot);      // torpedo tubes
                FleetBar(m, MyVector(-0.20f, 0.18f, 0.12f * z), MyVector(-0.04f, 0.19f, 0.12f * z), 0.04f, 0.03f, nMagenta, true);  MarkGlow(m, 6, nMagenta);  // shock-wave emitters
                engine(m, -0.48f, 0.04f, 0.12f * z, 0.08f, 0.30f, flameR);
                engine(m, -0.48f, -0.07f, 0.16f * z, 0.06f, 0.24f, flameR);
            }
            FleetBar(m, MyVector(0.44f, 0.0f, 0.0f), MyVector(0.60f, 0.0f, 0.0f), 0.06f, 0.012f, steel, true);
        }
        // ---------------------------------------------------------------- SPECTRE: a sleek assassin, trident rails plus weaving shots plus a laser (a three-way hunter)
        {
            Mesh& m = fleet[kFlSpectre];
            FleetBar(m, MyVector(-0.36f, 0.0f, 0.0f), MyVector(0.54f, 0.0f, 0.0f), 0.08f, 0.012f, nCrimson, true, 1.0f);
            FleetBar(m, MyVector(0.04f, 0.06f, 0.0f), MyVector(0.30f, 0.04f, 0.0f), 0.035f, 0.014f, glass, true);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetPanel(m, { MyVector(0.20f, 0.0f, 0.04f * z), MyVector(-0.20f, 0.0f, 0.36f * z), MyVector(-0.44f, 0.0f, 0.40f * z), MyVector(-0.40f, 0.0f, 0.10f * z) }, none, false, 0.9f);
                FleetBar(m, MyVector(-0.20f, 0.0f, 0.36f * z), MyVector(0.30f, 0.0f, 0.36f * z), 0.02f, 0.012f, steel, true);                               // wing rails
                FleetBar(m, MyVector(0.28f, 0.0f, 0.36f * z), MyVector(0.34f, 0.0f, 0.36f * z), 0.016f, 0.01f, nTeal, true);  MarkGlow(m, 6, nTeal);
                FleetPanel(m, { MyVector(-0.30f, 0.0f, 0.06f * z), MyVector(-0.46f, 0.20f, 0.10f * z), MyVector(-0.48f, 0.0f, 0.08f * z) }, none, false, 0.9f);     // canted tail fins
                engine(m, -0.40f, 0.0f, 0.06f * z, 0.05f, 0.26f, nHot);
            }
            FleetBar(m, MyVector(0.42f, 0.0f, 0.0f), MyVector(0.62f, 0.0f, 0.0f), 0.02f, 0.006f, eye, true);  MarkGlow(m, 6, eye);                       // the laser lens in the nose
        }
        // ================================================================================================
        //  THE THIRD FLEET: sixteen more ships.  Ten are built round a new firing technique (flower, double helix, curtain with a gap, sniper, shotgun, barrage, lattice, ripple rings,
        //  starburst, hailstorm); the last six carry two or three of them.  Same conventions as the second fleet (nose +X, wings +/-Z, about one unit long).
        // ================================================================================================
        const MyColor n3Gold(255, 214, 90, 255), n3Ice(170, 225, 255, 255), n3Red(255, 70, 70, 255), n3Jade(90, 235, 160, 255), n3Plate(96, 100, 124, 255), n3Dark(40, 44, 58, 255);
        // ---------------------------------------------------------------- BLOSSOM: five petal-wings round a glowing bud (flower pattern)
        {
            Mesh& m = fleet[kFlBlossom];
            FleetBar(m, MyVector(-0.20f, 0.0f, 0.0f), MyVector(0.28f, 0.0f, 0.0f), 0.11f, 0.06f, n3Plate, true, 1.0f);
            FleetBar(m, MyVector(0.24f, 0.0f, 0.0f), MyVector(0.40f, 0.0f, 0.0f), 0.07f, 0.04f, nMagenta, true);  MarkGlow(m, 6, nMagenta);
            for (int k = 0; k < 5; ++k)
            {
                const float a = nKPi2 * static_cast<float>(k) / 5.0f;
                FleetPanel(m, { nRadial(0.12f, a - 0.34f, 0.15f), nRadial(-0.30f, a, 0.60f), nRadial(0.12f, a + 0.34f, 0.15f) }, none, false, 0.95f);
                FleetBar(m, nRadial(0.10f, a - 0.30f, 0.16f), nRadial(-0.30f, a, 0.60f), 0.012f, 0.012f, steel, true);
                FleetBar(m, nRadial(-0.28f, a, 0.58f), nRadial(-0.33f, a, 0.62f), 0.03f, 0.03f, nMagenta, true);  MarkGlow(m, 6, nMagenta);
            }
            engine(m, -0.20f, 0.0f, 0.0f, 0.08f, 0.26f, flameR);
        }
        // ---------------------------------------------------------------- HELIX: two glowing strands wound round a core (double-helix pattern)
        {
            Mesh& m = fleet[kFlHelix];
            FleetBar(m, MyVector(-0.40f, 0.0f, 0.0f), MyVector(0.50f, 0.0f, 0.0f), 0.05f, 0.012f, n3Plate, true, 1.0f);
            for (int s = 0; s < 2; ++s)
                for (int i = 0; i < 9; ++i)
                {
                    const float x0 = -0.38f + 0.092f * static_cast<float>(i), x1 = x0 + 0.092f, a0 = 0.85f * static_cast<float>(i) + 3.14159f * static_cast<float>(s), a1 = a0 + 0.85f;
                    FleetBar(m, nRadial(x0, a0, 0.17f), nRadial(x1, a1, 0.17f), 0.022f, 0.022f, (s == 0) ? nTeal : nViolet, true);  MarkGlow(m, 6, (s == 0) ? nTeal : nViolet);
                    if (i % 3 == 0) FleetBar(m, MyVector(x0, 0.0f, 0.0f), nRadial(x0, a0, 0.17f), 0.014f, 0.014f, steel, true);
                }
            FleetBar(m, MyVector(0.10f, 0.05f, 0.0f), MyVector(0.30f, 0.04f, 0.0f), 0.035f, 0.02f, glass, true);
            engine(m, -0.40f, 0.0f, 0.0f, 0.06f, 0.26f, nTeal);
        }
        // ---------------------------------------------------------------- WARPER: a wide emitter wing with a row of glowing studs (fires a curtain of bolts with a gap)
        {
            Mesh& m = fleet[kFlWarper];
            FleetBar(m, MyVector(-0.10f, 0.0f, -0.62f), MyVector(-0.10f, 0.0f, 0.62f), 0.06f, 0.06f, n3Plate, true, 1.0f);
            for (int k = -3; k <= 3; ++k) { const float z = 0.17f * static_cast<float>(k); FleetBar(m, MyVector(-0.06f, 0.0f, z), MyVector(0.10f, 0.0f, z), 0.03f, 0.022f, nLime, true);  MarkGlow(m, 6, nLime); }
            FleetBar(m, MyVector(-0.40f, 0.0f, 0.0f), MyVector(0.40f, 0.0f, 0.0f), 0.09f, 0.03f, nPlate, true, 1.0f);
            for (int s = -1; s <= 1; s += 2) { const float z = static_cast<float>(s); FleetPanel(m, { MyVector(0.0f, 0.0f, 0.2f * z), MyVector(-0.30f, 0.0f, 0.62f * z), MyVector(-0.42f, 0.0f, 0.36f * z) }, none, false, 0.9f); engine(m, -0.40f, 0.0f, 0.4f * z, 0.05f, 0.22f, nLime); }
            FleetBar(m, MyVector(0.14f, 0.06f, 0.0f), MyVector(0.30f, 0.05f, 0.0f), 0.035f, 0.02f, glass, true);
        }
        // ---------------------------------------------------------------- LONGBOW: a needle with a very long barrel and a scope (sniper shots)
        {
            Mesh& m = fleet[kFlLongbow];
            FleetBar(m, MyVector(-0.46f, 0.0f, 0.0f), MyVector(0.10f, 0.0f, 0.0f), 0.09f, 0.06f, n3Plate, true, 1.0f);
            FleetBar(m, MyVector(0.06f, 0.0f, 0.0f), MyVector(0.86f, 0.0f, 0.0f), 0.035f, 0.016f, steel, true);
            FleetBar(m, MyVector(0.84f, 0.0f, 0.0f), MyVector(0.92f, 0.0f, 0.0f), 0.022f, 0.012f, n3Red, true);  MarkGlow(m, 6, n3Red);
            FleetBar(m, MyVector(-0.10f, 0.09f, 0.0f), MyVector(0.20f, 0.09f, 0.0f), 0.03f, 0.02f, glass, true);                                          // scope
            for (int k = 0; k < 4; ++k) { const float a = nKPi2 * static_cast<float>(k) / 4.0f + 0.785f; FleetPanel(m, { nRadial(-0.18f, a, 0.09f), nRadial(-0.44f, a, 0.34f), nRadial(-0.48f, a, 0.10f) }, none, false, 0.9f); }
            engine(m, -0.46f, 0.0f, 0.0f, 0.06f, 0.30f, flame);
        }
        // ---------------------------------------------------------------- MAULER: a stubby brute with two fat short barrels (shotgun blasts)
        {
            Mesh& m = fleet[kFlMauler];
            FleetBar(m, MyVector(-0.36f, 0.0f, 0.0f), MyVector(0.22f, 0.0f, 0.0f), 0.24f, 0.16f, n3Plate, true, 1.0f);
            FleetBar(m, MyVector(-0.20f, 0.14f, 0.0f), MyVector(0.10f, 0.15f, 0.0f), 0.10f, 0.07f, n3Dark, true);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = 0.15f * static_cast<float>(s);
                FleetBar(m, MyVector(0.10f, -0.02f, z), MyVector(0.42f, -0.02f, z), 0.075f, 0.065f, gun, true);
                FleetBar(m, MyVector(0.40f, -0.02f, z), MyVector(0.46f, -0.02f, z), 0.06f, 0.05f, nHot, true);  MarkGlow(m, 6, nHot);
                FleetPanel(m, { MyVector(-0.10f, 0.0f, 0.22f * static_cast<float>(s)), MyVector(-0.32f, 0.0f, 0.50f * static_cast<float>(s)), MyVector(-0.40f, 0.0f, 0.30f * static_cast<float>(s)) }, none, false, 0.9f);
                engine(m, -0.38f, 0.0f, 0.12f * static_cast<float>(s), 0.07f, 0.26f, flameR);
            }
        }
        // ---------------------------------------------------------------- MONSOON: a rain-cloud of launch tubes (barrage: a spray of bolts in a wide cone)
        {
            Mesh& m = fleet[kFlMonsoon];
            FleetBar(m, MyVector(-0.40f, 0.0f, 0.0f), MyVector(0.36f, 0.0f, 0.0f), 0.15f, 0.08f, n3Plate, true, 1.0f);
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 4; ++c)
                {
                    const float x = -0.26f + 0.17f * static_cast<float>(c), a = nKPi2 * (static_cast<float>(r) + 0.5f * static_cast<float>(c & 1)) / 3.0f;
                    FleetBar(m, nRadial(x, a, 0.17f), nRadial(x + 0.13f, a, 0.21f), 0.032f, 0.026f, gun, true);
                    FleetBar(m, nRadial(x + 0.12f, a, 0.20f), nRadial(x + 0.15f, a, 0.22f), 0.022f, 0.016f, nTeal, true);  MarkGlow(m, 6, nTeal);
                }
            FleetBar(m, MyVector(0.20f, 0.07f, 0.0f), MyVector(0.34f, 0.06f, 0.0f), 0.035f, 0.02f, glass, true);
            engine(m, -0.40f, 0.0f, 0.0f, 0.09f, 0.28f, flame);
        }
        // ---------------------------------------------------------------- MATRIX: a cube frame round a glowing core (lattice: a grid of bolts)
        {
            Mesh& m = fleet[kFlMatrix];
            const float h = 0.27f;
            for (int i = 0; i < 4; ++i)
            {
                const float y = (i & 1) ? h : -h, z = (i & 2) ? h : -h;
                FleetBar(m, MyVector(-h, y, z), MyVector(h, y, z), 0.032f, 0.032f, n3Plate, true);                                                       // four edges along X
                const float x = (i & 1) ? h : -h, y2 = (i & 2) ? h : -h;
                FleetBar(m, MyVector(x, y2, -h), MyVector(x, y2, h), 0.032f, 0.032f, n3Plate, true);                                                    // four along Z
                const float x2 = (i & 1) ? h : -h, z2 = (i & 2) ? h : -h;
                FleetBar(m, MyVector(x2, -h, z2), MyVector(x2, h, z2), 0.032f, 0.032f, n3Plate, true);                                                  // four along Y
            }
            AddBoxFaces(m, MyVector(0.0f, 0.0f, 0.0f), MyVector(0.13f, 0.13f, 0.13f), 1.0f, true, nTeal);  MarkGlow(m, 6, nTeal);
            for (int i = 0; i < 8; ++i) { const MyVector c((i & 1) ? h : -h, (i & 2) ? h : -h, (i & 4) ? h : -h); AddBoxFaces(m, c, MyVector(0.04f, 0.04f, 0.04f), 1.0f, true, nViolet);  MarkGlow(m, 6, nViolet); }
            FleetBar(m, MyVector(h, 0.0f, 0.0f), MyVector(0.58f, 0.0f, 0.0f), 0.04f, 0.008f, steel, true);
            engine(m, -h, 0.0f, 0.0f, 0.07f, 0.24f, nTeal);
        }
        // ---------------------------------------------------------------- SONAR: concentric glowing rings on a spine (ripple: rings of bolts)
        {
            Mesh& m = fleet[kFlSonar];
            FleetBar(m, MyVector(-0.40f, 0.0f, 0.0f), MyVector(0.50f, 0.0f, 0.0f), 0.045f, 0.012f, n3Plate, true, 1.0f);
            for (int r = 0; r < 3; ++r)
            {
                const float x = -0.24f + 0.2f * static_cast<float>(r), rad = 0.16f + 0.13f * static_cast<float>(r);
                for (int s = 0; s < 10; ++s) { FleetBar(m, nRadial(x, nKPi2 * static_cast<float>(s) / 10.0f, rad), nRadial(x, nKPi2 * static_cast<float>(s + 1) / 10.0f, rad), 0.02f, 0.02f, n3Ice, true);  MarkGlow(m, 6, n3Ice); }
                for (int k = 0; k < 4; ++k) FleetBar(m, MyVector(x, 0.0f, 0.0f), nRadial(x, nKPi2 * static_cast<float>(k) / 4.0f, rad), 0.012f, 0.012f, steel, true);
            }
            engine(m, -0.40f, 0.0f, 0.0f, 0.05f, 0.24f, nTeal);
        }
        // ---------------------------------------------------------------- STARBURST: a five-pointed star hull with glowing tips (starburst: five lines of bolts)
        {
            Mesh& m = fleet[kFlStarburst];
            AddBoxFaces(m, MyVector(0.0f, 0.0f, 0.0f), MyVector(0.13f, 0.13f, 0.13f), 1.0f, true, n3Gold);  MarkGlow(m, 6, n3Gold);
            for (int k = 0; k < 5; ++k)
            {
                const float a = nKPi2 * static_cast<float>(k) / 5.0f + 0.31f;
                FleetBar(m, nRadial(0.0f, a, 0.10f), nRadial(0.0f, a, 0.62f), 0.075f, 0.012f, n3Plate, true, 1.0f);
                FleetPanel(m, { nRadial(0.05f, a, 0.10f), nRadial(0.14f, a, 0.5f), nRadial(-0.14f, a, 0.5f), nRadial(-0.05f, a, 0.10f) }, none, false, 0.9f);
                FleetBar(m, nRadial(0.0f, a, 0.60f), nRadial(0.0f, a, 0.66f), 0.03f, 0.02f, n3Gold, true);  MarkGlow(m, 6, n3Gold);
            }
            FleetBar(m, MyVector(0.12f, 0.0f, 0.0f), MyVector(0.30f, 0.0f, 0.0f), 0.05f, 0.012f, steel, true);
            engine(m, -0.14f, 0.0f, 0.0f, 0.08f, 0.26f, flameR);
        }
        // ---------------------------------------------------------------- HAILER: a flat disc ringed by a turret crown (hailstorm: a wild spray)
        {
            Mesh& m = fleet[kFlHailer];
            for (int r = 0; r < 2; ++r)
                for (int s = 0; s < 12; ++s) FleetBar(m, nRadial(-0.04f + 0.06f * static_cast<float>(r), nKPi2 * static_cast<float>(s) / 12.0f, 0.34f - 0.1f * static_cast<float>(r)), nRadial(-0.04f + 0.06f * static_cast<float>(r), nKPi2 * static_cast<float>(s + 1) / 12.0f, 0.34f - 0.1f * static_cast<float>(r)), 0.05f, 0.05f, (r == 0) ? n3Plate : n3Dark, true);
            for (int s = 0; s < 8; ++s) { const float a = nKPi2 * static_cast<float>(s) / 8.0f; FleetBar(m, nRadial(0.04f, a, 0.30f), nRadial(0.26f, a, 0.34f), 0.03f, 0.024f, gun, true);  FleetBar(m, nRadial(0.25f, a, 0.335f), nRadial(0.29f, a, 0.345f), 0.02f, 0.014f, nHot, true);  MarkGlow(m, 6, nHot); }
            FleetBar(m, MyVector(-0.10f, 0.0f, 0.0f), MyVector(0.14f, 0.0f, 0.0f), 0.12f, 0.07f, n3Plate, true, 1.0f);
            AddBoxFaces(m, MyVector(0.06f, 0.10f, 0.0f), MyVector(0.05f, 0.04f, 0.05f), 1.0f, true, glass);
            engine(m, -0.12f, 0.0f, 0.0f, 0.09f, 0.26f, flameR);
        }
        // ---------------------------------------------------------------- MARAUDER: a brute that does shotgun blasts, curtains of bolts and missiles
        {
            Mesh& m = fleet[kFlMarauder];
            FleetBar(m, MyVector(-0.38f, 0.0f, 0.0f), MyVector(0.30f, 0.0f, 0.0f), 0.2f, 0.1f, n3Plate, true, 1.0f);
            FleetBar(m, MyVector(-0.04f, 0.0f, -0.55f), MyVector(-0.04f, 0.0f, 0.55f), 0.05f, 0.05f, n3Dark, true);
            for (int k = -2; k <= 2; ++k) { FleetBar(m, MyVector(0.0f, 0.0f, 0.22f * static_cast<float>(k)), MyVector(0.10f, 0.0f, 0.22f * static_cast<float>(k)), 0.03f, 0.022f, nLime, true);  MarkGlow(m, 6, nLime); }
            for (int s = -1; s <= 1; s += 2) { const float z = static_cast<float>(s); FleetBar(m, MyVector(0.10f, -0.04f, 0.14f * z), MyVector(0.46f, -0.04f, 0.14f * z), 0.06f, 0.05f, gun, true);  FleetBar(m, MyVector(-0.20f, 0.12f, 0.30f * z), MyVector(0.06f, 0.12f, 0.30f * z), 0.055f, 0.055f, n3Dark, true);  FleetBar(m, MyVector(0.04f, 0.12f, 0.30f * z), MyVector(0.10f, 0.12f, 0.30f * z), 0.035f, 0.03f, nHot, true);  MarkGlow(m, 6, nHot);  engine(m, -0.40f, 0.0f, 0.12f * z, 0.07f, 0.26f, flameR); }
        }
        // ---------------------------------------------------------------- ORACLE: a halo ship (flower, ripples and a laser)
        {
            Mesh& m = fleet[kFlOracle];
            for (int s = 0; s < 14; ++s) { FleetBar(m, nRadial(-0.02f, nKPi2 * static_cast<float>(s) / 14.0f, 0.42f), nRadial(-0.02f, nKPi2 * static_cast<float>(s + 1) / 14.0f, 0.42f), 0.03f, 0.03f, nViolet, true);  MarkGlow(m, 6, nViolet); }
            FleetBar(m, MyVector(-0.30f, 0.0f, 0.0f), MyVector(0.30f, 0.0f, 0.0f), 0.1f, 0.05f, n3Plate, true, 1.0f);
            for (int k = 0; k < 4; ++k) FleetBar(m, MyVector(-0.02f, 0.0f, 0.0f), nRadial(-0.02f, nKPi2 * static_cast<float>(k) / 4.0f, 0.40f), 0.018f, 0.018f, steel, true);
            AddBoxFaces(m, MyVector(0.12f, 0.0f, 0.0f), MyVector(0.07f, 0.07f, 0.07f), 1.0f, true, n3Red);  MarkGlow(m, 6, n3Red);
            FleetBar(m, MyVector(0.30f, 0.0f, 0.0f), MyVector(0.56f, 0.0f, 0.0f), 0.03f, 0.008f, steel, true);
            engine(m, -0.30f, 0.0f, 0.0f, 0.07f, 0.26f, nViolet);
        }
        // ---------------------------------------------------------------- LEVIATHAN: a long armoured warship with side batteries (barrage, torpedoes, novas)
        {
            Mesh& m = fleet[kFlLeviathan];
            for (int i = 0; i < 8; ++i)
            {
                const float x = 0.46f - 0.13f * static_cast<float>(i), r = 0.10f + 0.07f * std::sin(3.14159f * (static_cast<float>(i) + 0.5f) / 8.0f);
                FleetBar(m, MyVector(x, 0.0f, 0.0f), MyVector(x - 0.12f, 0.0f, 0.0f), r, r * 0.95f, (i & 1) ? n3Plate : n3Dark, true, 1.0f);
                if (i % 2 == 1) for (int s = -1; s <= 1; s += 2) { FleetBar(m, MyVector(x - 0.02f, 0.02f, r * static_cast<float>(s)), MyVector(x - 0.10f, 0.02f, (r + 0.13f) * static_cast<float>(s)), 0.035f, 0.03f, gun, true);  FleetBar(m, MyVector(x - 0.09f, 0.02f, (r + 0.12f) * static_cast<float>(s)), MyVector(x - 0.11f, 0.02f, (r + 0.14f) * static_cast<float>(s)), 0.022f, 0.016f, nHot, true);  MarkGlow(m, 6, nHot); }
            }
            FleetBar(m, MyVector(0.40f, 0.0f, 0.0f), MyVector(0.62f, 0.0f, 0.0f), 0.07f, 0.012f, steel, true);
            FleetBar(m, MyVector(0.22f, 0.13f, 0.0f), MyVector(0.36f, 0.12f, 0.0f), 0.05f, 0.03f, glass, true);
            for (int s = -1; s <= 1; s += 2) engine(m, -0.56f, 0.0f, 0.10f * static_cast<float>(s), 0.07f, 0.28f, flameR);
        }
        // ---------------------------------------------------------------- PHOENIX: swept firebird wings with glowing feather edges (starburst, helix and shock-waves)
        {
            Mesh& m = fleet[kFlPhoenix];
            FleetBar(m, MyVector(-0.34f, 0.0f, 0.0f), MyVector(0.50f, 0.0f, 0.0f), 0.08f, 0.012f, n3Red, true, 1.0f);
            FleetBar(m, MyVector(0.06f, 0.05f, 0.0f), MyVector(0.26f, 0.04f, 0.0f), 0.035f, 0.016f, glass, true);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetPanel(m, { MyVector(0.20f, 0.0f, 0.05f * z), MyVector(-0.46f, 0.0f, 0.66f * z), MyVector(-0.52f, 0.0f, 0.30f * z), MyVector(-0.30f, 0.0f, 0.10f * z) }, none, false, 0.9f);
                for (int f = 0; f < 4; ++f) { const float t = 0.25f * static_cast<float>(f + 1); FleetBar(m, MyVector(0.12f - 0.5f * t, 0.0f, (0.10f + 0.50f * t) * z), MyVector(-0.30f - 0.25f * t, 0.0f, (0.20f + 0.52f * t) * z), 0.016f, 0.01f, n3Gold, true);  MarkGlow(m, 6, n3Gold); }
                engine(m, -0.32f, 0.0f, 0.06f * z, 0.05f, 0.26f, nHot);
            }
            for (int k = -2; k <= 2; ++k) FleetBar(m, MyVector(-0.34f, 0.0f, 0.0f), MyVector(-0.62f, 0.02f * static_cast<float>(k), 0.07f * static_cast<float>(k)), 0.014f, 0.008f, n3Gold, true);       // tail fan
        }
        // ---------------------------------------------------------------- COLOSSUS: a heavy gun platform - a huge sniper barrel and barrage racks (sniper, barrage, missiles)
        {
            Mesh& m = fleet[kFlColossus];
            FleetBar(m, MyVector(-0.40f, 0.0f, 0.0f), MyVector(0.22f, 0.0f, 0.0f), 0.24f, 0.17f, n3Plate, true, 1.0f);
            FleetBar(m, MyVector(0.0f, 0.18f, 0.0f), MyVector(0.80f, 0.18f, 0.0f), 0.05f, 0.03f, steel, true);
            FleetBar(m, MyVector(0.78f, 0.18f, 0.0f), MyVector(0.86f, 0.18f, 0.0f), 0.032f, 0.02f, n3Red, true);  MarkGlow(m, 6, n3Red);
            FleetBar(m, MyVector(-0.10f, 0.14f, 0.0f), MyVector(0.10f, 0.14f, 0.0f), 0.12f, 0.12f, n3Dark, true);
            for (int s = -1; s <= 1; s += 2) for (int k = 0; k < 4; ++k) { const float z = (0.20f + 0.05f * static_cast<float>(k & 1)) * static_cast<float>(s), x = -0.20f + 0.12f * static_cast<float>(k); FleetBar(m, MyVector(x, -0.02f, z), MyVector(x + 0.09f, 0.04f * static_cast<float>(k - 1), z + 0.04f * static_cast<float>(s)), 0.03f, 0.024f, gun, true); }
            for (int s = -1; s <= 1; s += 2) engine(m, -0.42f, 0.0f, 0.14f * static_cast<float>(s), 0.09f, 0.28f, flameR);
        }
        // ---------------------------------------------------------------- OVERLORD: a crowned flagship - a tower, a ring of prongs and wide wings (flowers, lattices and hailstorms)
        {
            Mesh& m = fleet[kFlOverlord];
            FleetBar(m, MyVector(-0.44f, 0.0f, 0.0f), MyVector(0.40f, 0.0f, 0.0f), 0.15f, 0.09f, n3Plate, true, 1.0f);
            FleetBar(m, MyVector(-0.20f, 0.14f, 0.0f), MyVector(0.10f, 0.30f, 0.0f), 0.07f, 0.04f, n3Dark, true);
            for (int k = 0; k < 6; ++k) { const float a = nKPi2 * static_cast<float>(k) / 6.0f; FleetBar(m, MyVector(0.10f, 0.30f, 0.0f) + nRadial(0.0f, a, 0.06f), MyVector(0.10f, 0.30f, 0.0f) + nRadial(0.10f, a, 0.14f), 0.02f, 0.01f, n3Gold, true);  MarkGlow(m, 6, n3Gold); }
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetSlab(m, MyVector(-0.10f, -0.05f, 0.16f * z), 0.36f, 0.035f, MyVector(-0.10f, 0.10f, 0.50f * z), 0.30f, 0.03f, steel, true, 0.95f);
                for (int k = 0; k < 3; ++k) { FleetBar(m, MyVector(0.0f - 0.10f * static_cast<float>(k), 0.0f, 0.30f * z), MyVector(0.14f - 0.10f * static_cast<float>(k), 0.0f, 0.30f * z), 0.03f, 0.024f, nMagenta, true);  MarkGlow(m, 6, nMagenta); }
                engine(m, -0.46f, 0.0f, 0.10f * z, 0.08f, 0.30f, flameR);
            }
            FleetBar(m, MyVector(0.38f, 0.0f, 0.0f), MyVector(0.62f, 0.0f, 0.0f), 0.05f, 0.01f, steel, true);
        }
        // ================================================================ THE FOURTH FLEET (12 ships) AND SIX BOSS CRAFT: each silhouette hints at its firing pattern ============================
        const MyColor f4Ember(255, 140, 50, 255), f4Cyan(90, 235, 255, 255), f4Lime(170, 255, 90, 255), f4Violet(190, 120, 255, 255), f4Bone(220, 214, 196, 255);
        // ---- BANSHEE: a spindle with three swept fins (rotating spiral arms)
        {
            Mesh& m = fleet[kFlBanshee];
            FleetBar(m, MyVector(-0.46f, 0.0f, 0.0f), MyVector(0.52f, 0.0f, 0.0f), 0.075f, 0.012f, none, false);
            for (int k = 0; k < 3; ++k) { const float a = nKPi2 * static_cast<float>(k) / 3.0f; FleetBar(m, nRadial(0.10f, a, 0.04f), nRadial(-0.40f, a + 0.7f, 0.30f), 0.022f, 0.008f, steel, true); FleetBar(m, nRadial(-0.40f, a + 0.7f, 0.30f), nRadial(-0.46f, a + 0.7f, 0.30f), 0.03f, 0.03f, f4Cyan, true); MarkGlow(m, 6, f4Cyan); }
            FleetBar(m, MyVector(0.08f, 0.05f, 0.0f), MyVector(0.28f, 0.04f, 0.0f), 0.04f, 0.02f, glass, true);
            engine(m, -0.48f, 0.0f, 0.0f, 0.06f, 0.24f, flame);
        }
        // ---- RIPPER: a broad blade wing with a saw-tooth leading edge (a converging wall of bolts)
        {
            Mesh& m = fleet[kFlRipper];
            FleetBar(m, MyVector(-0.40f, 0.0f, 0.0f), MyVector(0.40f, 0.0f, 0.0f), 0.08f, 0.03f, none, false);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetSlab(m, MyVector(-0.20f, 0.0f, 0.06f * z), 0.20f, 0.02f, MyVector(-0.10f, 0.0f, 0.62f * z), 0.12f, 0.02f, none, false, 0.95f);
                for (int k = 0; k < 4; ++k) FleetPanel(m, { MyVector(0.0f, 0.0f, (0.12f + 0.12f * static_cast<float>(k)) * z), MyVector(0.20f - 0.03f * static_cast<float>(k), 0.0f, (0.18f + 0.12f * static_cast<float>(k)) * z), MyVector(-0.04f, 0.0f, (0.20f + 0.12f * static_cast<float>(k)) * z) }, steel, true, 1.0f);
                FleetBar(m, MyVector(0.0f, 0.0f, 0.58f * z), MyVector(0.30f, 0.0f, 0.58f * z), 0.02f, 0.012f, gun, true);
                engine(m, -0.42f, 0.0f, 0.08f * z, 0.05f, 0.22f, flameR);
            }
        }
        // ---- WIDOW: a spider body with six legs fanned forward (a wide fan of bolts)
        {
            Mesh& m = fleet[kFlWidow];
            FleetSlab(m, MyVector(-0.10f, -0.04f, 0.0f), 0.20f, 0.16f, MyVector(-0.10f, 0.10f, 0.0f), 0.14f, 0.11f, none, false);
            FleetBar(m, MyVector(0.10f, 0.0f, 0.0f), MyVector(0.30f, 0.02f, 0.0f), 0.07f, 0.05f, none, false, 0.9f);
            for (int k = 0; k < 3; ++k) for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s), x = 0.05f - 0.14f * static_cast<float>(k);
                FleetBar(m, MyVector(x, 0.0f, 0.10f * z), MyVector(x + 0.34f, -0.04f, 0.40f * z), 0.022f, 0.012f, steel, true);
                FleetBar(m, MyVector(x + 0.34f, -0.04f, 0.40f * z), MyVector(x + 0.44f, -0.04f, 0.40f * z), 0.02f, 0.01f, f4Lime, true);  MarkGlow(m, 6, f4Lime);
            }
            FleetBar(m, MyVector(0.30f, 0.04f, 0.04f), MyVector(0.36f, 0.04f, 0.04f), 0.02f, 0.02f, eye, true);  MarkGlow(m, 6, eye);
            FleetBar(m, MyVector(0.30f, 0.04f, -0.04f), MyVector(0.36f, 0.04f, -0.04f), 0.02f, 0.02f, eye, true);  MarkGlow(m, 6, eye);
            engine(m, -0.30f, 0.0f, 0.0f, 0.07f, 0.24f, flame);
        }
        // ---- COMET: a glowing head and a long thin tail (a fast bolt trailed by slower ones)
        {
            Mesh& m = fleet[kFlComet];
            FleetBar(m, MyVector(0.10f, 0.0f, 0.0f), MyVector(0.46f, 0.0f, 0.0f), 0.13f, 0.02f, none, false);
            FleetBar(m, MyVector(0.20f, 0.0f, 0.0f), MyVector(0.38f, 0.0f, 0.0f), 0.08f, 0.04f, f4Ember, true);  MarkGlow(m, 6, f4Ember);
            for (int k = -2; k <= 2; ++k) FleetBar(m, MyVector(0.10f, 0.0f, 0.0f), MyVector(-0.70f, 0.05f * static_cast<float>(k), 0.07f * static_cast<float>(k)), 0.05f - 0.01f * std::fabs(static_cast<float>(k)), 0.004f, (k & 1) ? f4Ember : steel, true);
            for (int s = -1; s <= 1; s += 2) { const float z = static_cast<float>(s); FleetPanel(m, { MyVector(0.10f, 0.0f, 0.10f * z), MyVector(-0.10f, 0.0f, 0.34f * z), MyVector(-0.22f, 0.0f, 0.08f * z) }, none, false, 0.92f); }
        }
        // ---- LANCET: a chevron arrowhead (a V of bolts)
        {
            Mesh& m = fleet[kFlLancet];
            FleetBar(m, MyVector(-0.30f, 0.0f, 0.0f), MyVector(0.58f, 0.0f, 0.0f), 0.07f, 0.01f, none, false);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetPanel(m, { MyVector(0.50f, 0.0f, 0.02f * z), MyVector(-0.34f, 0.0f, 0.54f * z), MyVector(-0.20f, 0.0f, 0.54f * z), MyVector(-0.04f, 0.0f, 0.10f * z) }, none, false, 0.95f);
                FleetBar(m, MyVector(0.50f, 0.0f, 0.02f * z), MyVector(-0.34f, 0.0f, 0.54f * z), 0.014f, 0.014f, steel, true);
                FleetBar(m, MyVector(-0.30f, 0.0f, 0.50f * z), MyVector(-0.04f, 0.0f, 0.50f * z), 0.02f, 0.012f, gun, true);
                engine(m, -0.30f, 0.0f, 0.06f * z, 0.045f, 0.2f, flame);
            }
            FleetBar(m, MyVector(0.0f, 0.04f, 0.0f), MyVector(0.22f, 0.03f, 0.0f), 0.04f, 0.02f, glass, true);
        }
        // ---- GORGON: a serpent of five linked segments (a weaving stream of bolts)
        {
            Mesh& m = fleet[kFlGorgon];
            for (int k = 0; k < 5; ++k)
            {
                const float x = 0.34f - 0.20f * static_cast<float>(k), z = 0.07f * std::sin(static_cast<float>(k) * 1.4f), r = 0.095f - 0.011f * static_cast<float>(k);
                FleetBar(m, MyVector(x, 0.0f, z), MyVector(x - 0.17f, 0.0f, 0.07f * std::sin(static_cast<float>(k + 1) * 1.4f)), r, r - 0.01f, (k & 1) ? f4Lime : none, (k & 1) != 0);
            }
            FleetSlab(m, MyVector(0.34f, -0.06f, 0.0f), 0.07f, 0.09f, MyVector(0.46f, 0.0f, 0.0f), 0.03f, 0.04f, none, false);
            FleetBar(m, MyVector(0.40f, 0.05f, 0.04f), MyVector(0.44f, 0.05f, 0.04f), 0.016f, 0.016f, eye, true);  MarkGlow(m, 6, eye);
            FleetBar(m, MyVector(0.40f, 0.05f, -0.04f), MyVector(0.44f, 0.05f, -0.04f), 0.016f, 0.016f, eye, true);  MarkGlow(m, 6, eye);
            engine(m, -0.66f, 0.0f, 0.0f, 0.05f, 0.2f, flame);
        }
        // ---- MANTIS: two huge forward claws (a pincer of bolts)
        {
            Mesh& m = fleet[kFlMantis];
            FleetSlab(m, MyVector(-0.20f, -0.05f, 0.0f), 0.22f, 0.14f, MyVector(0.10f, 0.05f, 0.0f), 0.14f, 0.09f, none, false);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetBar(m, MyVector(0.0f, 0.0f, 0.10f * z), MyVector(0.30f, 0.0f, 0.34f * z), 0.05f, 0.035f, steel, true);
                FleetBar(m, MyVector(0.30f, 0.0f, 0.34f * z), MyVector(0.56f, 0.0f, 0.12f * z), 0.035f, 0.006f, f4Bone, true);
                FleetBar(m, MyVector(0.30f, 0.0f, 0.34f * z), MyVector(0.52f, 0.0f, 0.26f * z), 0.02f, 0.006f, f4Bone, true);
                engine(m, -0.40f, 0.0f, 0.10f * z, 0.06f, 0.24f, flameR);
            }
            FleetBar(m, MyVector(0.08f, 0.06f, 0.0f), MyVector(0.20f, 0.05f, 0.0f), 0.05f, 0.03f, glass, true);
        }
        // ---- CINDER: a ragged lump with spikes (a burst of random shrapnel)
        {
            Mesh& m = fleet[kFlCinder];
            FleetSlab(m, MyVector(0.0f, -0.10f, 0.0f), 0.30f, 0.26f, MyVector(0.0f, 0.12f, 0.0f), 0.22f, 0.18f, none, false);
            for (int k = 0; k < 9; ++k) { const float a = nKPi2 * static_cast<float>(k) / 9.0f; FleetBar(m, nRadial(0.0f, a, 0.18f), nRadial(0.10f * std::sin(static_cast<float>(k) * 2.3f), a, 0.34f + 0.06f * static_cast<float>(k % 3)), 0.04f, 0.005f, (k % 3 == 0) ? f4Ember : steel, true); }
            FleetBar(m, MyVector(0.20f, 0.0f, 0.0f), MyVector(0.30f, 0.0f, 0.0f), 0.06f, 0.04f, f4Ember, true);  MarkGlow(m, 6, f4Ember);
            engine(m, -0.30f, 0.0f, 0.0f, 0.08f, 0.26f, flameR);
        }
        // ---- GLAIVE: a disc with eight spokes (a compass burst of bolts)
        {
            Mesh& m = fleet[kFlGlaive];
            FleetBar(m, MyVector(-0.06f, 0.0f, 0.0f), MyVector(0.06f, 0.0f, 0.0f), 0.30f, 0.30f, none, false);
            for (int k = 0; k < 8; ++k) { const float a = nKPi2 * static_cast<float>(k) / 8.0f; FleetBar(m, nRadial(0.0f, a, 0.28f), nRadial(0.0f, a, 0.50f), 0.03f, 0.012f, (k & 1) ? f4Cyan : steel, true); if (k & 1) MarkGlow(m, 6, f4Cyan); }
            FleetBar(m, MyVector(0.04f, 0.0f, 0.0f), MyVector(0.20f, 0.0f, 0.0f), 0.10f, 0.05f, glass, true);
        }
        // ---- ECLIPSE: a dark core held inside a ring (rings of slow and fast bolts)
        {
            Mesh& m = fleet[kFlEclipse];
            FleetBar(m, MyVector(-0.16f, 0.0f, 0.0f), MyVector(0.16f, 0.0f, 0.0f), 0.17f, 0.17f, n3Dark, true);
            for (int k = 0; k < 10; ++k) { const float a0 = nKPi2 * static_cast<float>(k) / 10.0f, a1 = nKPi2 * static_cast<float>(k + 1) / 10.0f; FleetBar(m, nRadial(0.0f, a0, 0.40f), nRadial(0.0f, a1, 0.40f), 0.04f, 0.04f, (k & 1) ? f4Violet : steel, true); if (k & 1) MarkGlow(m, 6, f4Violet); }
            for (int k = 0; k < 4; ++k) { const float a = nKPi2 * static_cast<float>(k) / 4.0f + 0.4f; FleetBar(m, nRadial(0.0f, a, 0.17f), nRadial(0.0f, a, 0.38f), 0.02f, 0.02f, steel, true); }
            FleetBar(m, MyVector(0.12f, 0.0f, 0.0f), MyVector(0.22f, 0.0f, 0.0f), 0.06f, 0.03f, f4Violet, true);  MarkGlow(m, 6, f4Violet);
        }
        // ---- REVENANT: a skull-prowed hunter (spiral, pincer and missiles)
        {
            Mesh& m = fleet[kFlRevenant];
            FleetSlab(m, MyVector(-0.20f, -0.06f, 0.0f), 0.28f, 0.20f, MyVector(0.18f, 0.06f, 0.0f), 0.18f, 0.13f, none, false);
            FleetSlab(m, MyVector(0.18f, -0.04f, 0.0f), 0.13f, 0.12f, MyVector(0.40f, 0.04f, 0.0f), 0.06f, 0.07f, f4Bone, true);
            FleetBar(m, MyVector(0.34f, 0.05f, 0.05f), MyVector(0.40f, 0.05f, 0.05f), 0.03f, 0.03f, eye, true);  MarkGlow(m, 6, eye);
            FleetBar(m, MyVector(0.34f, 0.05f, -0.05f), MyVector(0.40f, 0.05f, -0.05f), 0.03f, 0.03f, eye, true);  MarkGlow(m, 6, eye);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetPanel(m, { MyVector(0.10f, 0.0f, 0.12f * z), MyVector(-0.34f, 0.0f, 0.52f * z), MyVector(-0.40f, 0.0f, 0.10f * z) }, none, false, 0.9f);
                FleetBar(m, MyVector(0.0f, -0.04f, 0.34f * z), MyVector(0.30f, -0.04f, 0.34f * z), 0.03f, 0.02f, gun, true);
                engine(m, -0.42f, 0.0f, 0.10f * z, 0.07f, 0.26f, flameR);
            }
        }
        // ---- TALON: a hooked predator (comet streams, a wall and a rail)
        {
            Mesh& m = fleet[kFlTalon];
            FleetBar(m, MyVector(-0.44f, 0.0f, 0.0f), MyVector(0.40f, 0.0f, 0.0f), 0.08f, 0.02f, none, false);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                FleetBar(m, MyVector(0.0f, 0.0f, 0.06f * z), MyVector(0.20f, 0.0f, 0.40f * z), 0.04f, 0.03f, steel, true);
                FleetBar(m, MyVector(0.20f, 0.0f, 0.40f * z), MyVector(0.52f, 0.0f, 0.30f * z), 0.03f, 0.008f, f4Ember, true);  MarkGlow(m, 6, f4Ember);
                FleetPanel(m, { MyVector(0.0f, 0.0f, 0.06f * z), MyVector(-0.40f, 0.0f, 0.30f * z), MyVector(-0.42f, 0.0f, 0.06f * z) }, none, false, 0.94f);
                engine(m, -0.46f, 0.0f, 0.06f * z, 0.05f, 0.22f, flame);
            }
            FleetBar(m, MyVector(0.0f, 0.05f, 0.0f), MyVector(0.20f, 0.04f, 0.0f), 0.045f, 0.02f, glass, true);
            FleetBar(m, MyVector(0.38f, 0.0f, 0.0f), MyVector(0.64f, 0.0f, 0.0f), 0.025f, 0.008f, f4Cyan, true);
        }
        // ================================================================ BOSS CRAFT (drawn smaller in the arenas) ================================================================
        // ---- BEHEMOTH: a slab-sided warship with turret rows
        {
            Mesh& m = fleet[kFlBehemoth];
            FleetSlab(m, MyVector(-0.30f, -0.10f, 0.0f), 0.60f, 0.34f, MyVector(0.20f, 0.12f, 0.0f), 0.40f, 0.26f, n3Plate, true);
            FleetSlab(m, MyVector(0.20f, 0.0f, 0.0f), 0.30f, 0.22f, MyVector(0.62f, 0.0f, 0.0f), 0.06f, 0.08f, none, false);
            for (int s = -1; s <= 1; s += 2) for (int k = 0; k < 4; ++k)
            {
                const float z = (0.26f + 0.04f * static_cast<float>(k & 1)) * static_cast<float>(s), x = -0.50f + 0.20f * static_cast<float>(k);
                FleetBar(m, MyVector(x, 0.12f, z), MyVector(x + 0.18f, 0.12f, z), 0.06f, 0.05f, steel, true);
                FleetBar(m, MyVector(x + 0.16f, 0.12f, z), MyVector(x + 0.34f, 0.12f, z), 0.02f, 0.016f, f4Ember, true);  MarkGlow(m, 6, f4Ember);
            }
            FleetBar(m, MyVector(-0.10f, 0.18f, 0.0f), MyVector(0.10f, 0.30f, 0.0f), 0.10f, 0.07f, n3Dark, true);
            FleetBar(m, MyVector(0.0f, 0.28f, 0.0f), MyVector(0.20f, 0.28f, 0.0f), 0.05f, 0.03f, glass, true);
            for (int s = -1; s <= 1; s += 2) engine(m, -0.62f, 0.0f, 0.18f * static_cast<float>(s), 0.12f, 0.32f, flameR);
        }
        // ---- HYDRA: five necks, each ending in a glowing head
        {
            Mesh& m = fleet[kFlHydra];
            FleetBar(m, MyVector(-0.50f, 0.0f, 0.0f), MyVector(0.0f, 0.0f, 0.0f), 0.26f, 0.20f, n3Plate, true);
            for (int k = -2; k <= 2; ++k)
            {
                const float z = 0.15f * static_cast<float>(k), a = 0.16f * static_cast<float>(k), yy = 0.04f * static_cast<float>(k & 1);
                FleetBar(m, MyVector(0.0f, 0.0f, 0.5f * z), MyVector(0.30f, yy, 0.5f * z + 0.20f * a), 0.07f, 0.05f, none, false);
                FleetBar(m, MyVector(0.30f, yy, 0.5f * z + 0.20f * a), MyVector(0.62f, 0.0f, z + 0.30f * a), 0.05f, 0.03f, none, false, 0.9f);
                FleetBar(m, MyVector(0.58f, 0.0f, z + 0.28f * a), MyVector(0.70f, 0.0f, z + 0.30f * a), 0.045f, 0.02f, eye, true);  MarkGlow(m, 6, eye);
            }
            for (int s = -1; s <= 1; s += 2) engine(m, -0.52f, 0.0f, 0.16f * static_cast<float>(s), 0.10f, 0.30f, flameR);
        }
        // ---- JUGGERNAUT: an armoured wedge with side batteries
        {
            Mesh& m = fleet[kFlJuggernaut];
            FleetSlab(m, MyVector(-0.40f, -0.12f, 0.0f), 0.34f, 0.40f, MyVector(0.30f, 0.06f, 0.0f), 0.30f, 0.10f, n3Plate, true);
            FleetBar(m, MyVector(0.30f, 0.0f, 0.0f), MyVector(0.74f, 0.0f, 0.0f), 0.10f, 0.012f, steel, true);
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = static_cast<float>(s);
                for (int k = 0; k < 3; ++k)
                {
                    const float x0 = -0.30f + 0.20f * static_cast<float>(k);
                    FleetBar(m, MyVector(x0, 0.0f, 0.34f * z), MyVector(x0 + 0.30f, 0.0f, 0.34f * z), 0.07f, 0.06f, n3Dark, true);
                    FleetBar(m, MyVector(x0 + 0.30f, 0.0f, 0.34f * z), MyVector(x0 + 0.44f, 0.0f, 0.34f * z), 0.025f, 0.02f, n3Red, true);  MarkGlow(m, 6, n3Red);
                }
                engine(m, -0.74f, 0.0f, 0.16f * z, 0.10f, 0.30f, flameR);
            }
        }
        // ---- SERAPH: a bright angel with six fanned wings
        {
            Mesh& m = fleet[kFlSeraph];
            FleetBar(m, MyVector(-0.36f, 0.0f, 0.0f), MyVector(0.50f, 0.0f, 0.0f), 0.10f, 0.02f, none, false);
            FleetBar(m, MyVector(0.10f, 0.06f, 0.0f), MyVector(0.30f, 0.05f, 0.0f), 0.05f, 0.03f, glass, true);
            for (int s = -1; s <= 1; s += 2) for (int k = 0; k < 3; ++k)
            {
                const float z = static_cast<float>(s), fk = static_cast<float>(k);
                FleetPanel(m, { MyVector(0.0f, 0.0f, 0.06f * z), MyVector(-0.20f - 0.18f * fk, 0.04f * fk, (0.40f + 0.22f * fk) * z), MyVector(-0.30f - 0.18f * fk, 0.04f * fk, (0.18f + 0.10f * fk) * z) }, (k == 1) ? f4Bone : none, k == 1, 1.0f);
            }
            for (int k = 0; k < 6; ++k) { const float a = nKPi2 * static_cast<float>(k) / 6.0f; FleetBar(m, nRadial(-0.34f, a, 0.04f), nRadial(-0.50f, a, 0.14f), 0.016f, 0.01f, n3Gold, true);  MarkGlow(m, 6, n3Gold); }
            engine(m, -0.40f, 0.0f, 0.0f, 0.08f, 0.30f, flame);
        }
        // ---- KRAKEN: a dome with eight trailing tentacles
        {
            Mesh& m = fleet[kFlKraken];
            FleetSlab(m, MyVector(0.0f, -0.10f, 0.0f), 0.34f, 0.34f, MyVector(0.0f, 0.18f, 0.0f), 0.20f, 0.20f, none, false);
            FleetBar(m, MyVector(0.20f, 0.04f, 0.10f), MyVector(0.30f, 0.04f, 0.10f), 0.05f, 0.04f, eye, true);  MarkGlow(m, 6, eye);
            FleetBar(m, MyVector(0.20f, 0.04f, -0.10f), MyVector(0.30f, 0.04f, -0.10f), 0.05f, 0.04f, eye, true);  MarkGlow(m, 6, eye);
            for (int k = 0; k < 8; ++k)
            {
                const float a = nKPi2 * static_cast<float>(k) / 8.0f;
                const MyVector p0 = nRadial(-0.10f, a, 0.22f), p1 = nRadial(-0.40f, a + 0.5f, 0.40f), p2 = nRadial(-0.70f, a + 1.0f, 0.34f);
                FleetBar(m, p0, p1, 0.06f, 0.035f, (k & 1) ? f4Violet : none, (k & 1) != 0);
                FleetBar(m, p1, p2, 0.035f, 0.008f, (k & 1) ? f4Violet : none, (k & 1) != 0);
            }
        }
        // ---- ANNIHILATOR: a cross-shaped war station round a glowing core
        {
            Mesh& m = fleet[kFlAnnihilator];
            FleetBar(m, MyVector(-0.55f, 0.0f, 0.0f), MyVector(0.55f, 0.0f, 0.0f), 0.12f, 0.12f, n3Plate, true);
            FleetBar(m, MyVector(0.0f, 0.0f, -0.55f), MyVector(0.0f, 0.0f, 0.55f), 0.12f, 0.12f, n3Plate, true);
            FleetBar(m, MyVector(-0.10f, 0.0f, 0.0f), MyVector(0.10f, 0.0f, 0.0f), 0.24f, 0.24f, n3Dark, true);
            FleetBar(m, MyVector(0.0f, 0.0f, 0.0f), MyVector(0.14f, 0.0f, 0.0f), 0.14f, 0.10f, n3Red, true);  MarkGlow(m, 6, n3Red);
            for (int k = 0; k < 4; ++k)
            {
                const MyVector tip = (k & 1) ? MyVector(0.0f, 0.0f, (k == 1) ? 0.55f : -0.55f) : MyVector((k == 0) ? 0.55f : -0.55f, 0.0f, 0.0f);
                FleetBar(m, tip, tip * 1.18f, 0.09f, 0.04f, f4Ember, true);  MarkGlow(m, 6, f4Ember);
            }
            for (int k = 0; k < 4; ++k) engine(m, -0.58f, 0.0f, -0.18f + 0.12f * static_cast<float>(k), 0.05f, 0.24f, flameR);
        }
        for (int i = 0; i < kFlCount; ++i) FinalizeMesh(fleet[i]);
    }

    Mesh BuildRoundBombMesh()
    {
        Mesh m;
        const int N = 10;
        float cs[N], sn[N];
        for (int i = 0; i < N; ++i) FAST_SINCOS_DEG(360.0f * static_cast<float>(i) / static_cast<float>(N), sn[i], cs[i]);
        auto V = [&](float x, float y, float z) { m.verts.push_back(MyVector(x, y, z)); return static_cast<int>(m.verts.size()) - 1; };
        static const float kY[6] = { -0.44f, -0.30f, -0.10f, 0.12f, 0.32f, 0.45f };
        int ring[6][N];
        for (int s = 0; s < 6; ++s) { const float r = std::sqrt(0.25f - kY[s] * kY[s]); for (int i = 0; i < N; ++i) ring[s][i] = V(r * cs[i], kY[s], r * sn[i]); }
        const int bot = V(0.0f, -0.5f, 0.0f), top = V(0.0f, 0.5f, 0.0f);
        const MyVector core(0.0f, 0.0f, 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const int j = (i + 1) % N;
            AddFaceAuto(m, { ring[0][i], ring[0][j], bot }, core, 0.78f);
            for (int s = 0; s < 5; ++s) AddFaceAuto(m, { ring[s][i], ring[s][j], ring[s + 1][j], ring[s + 1][i] }, core, (s == 2 || s == 3) ? 1.0f : 0.9f, false, MyColor());
            AddFaceAuto(m, { ring[5][i], ring[5][j], top }, core, 0.95f);
        }
        const MyColor steel(150, 156, 172, 255), brown(120, 84, 50, 255), spark(255, 200, 90, 255), glint(232, 238, 255, 255);
        FleetBar(m, MyVector(0.0f, 0.42f, 0.0f), MyVector(0.0f, 0.62f, 0.0f), 0.13f, 0.11f, steel, true);                           // the collar
        FleetBar(m, MyVector(0.0f, 0.62f, 0.0f), MyVector(0.10f, 0.76f, 0.0f), 0.035f, 0.03f, brown, true);                         // the fuse, in two bent pieces
        FleetBar(m, MyVector(0.10f, 0.76f, 0.0f), MyVector(0.22f, 0.84f, 0.0f), 0.03f, 0.026f, brown, true);
        FleetBar(m, MyVector(0.22f, 0.84f, 0.0f), MyVector(0.27f, 0.86f, 0.0f), 0.026f, 0.02f, spark, true);  MarkGlow(m, 6, spark);  // the glowing end
        const int g0 = V(-0.30f, 0.16f, -0.405f), g1 = V(-0.12f, 0.16f, -0.425f), g2 = V(-0.12f, 0.36f, -0.405f), g3 = V(-0.30f, 0.36f, -0.385f);
        AddFaceAuto(m, { g0, g1, g2, g3 }, MyVector(-0.2f, 0.26f, 0.0f), 1.0f, true, glint);                                          // a glint of light (the camera looks in along +Z)
        FinalizeMesh(m);
        return m;
    }

    // A ground-based HEAT-SEEKER LAUNCHER (Project X): a tracked armoured chassis with road wheels, a rotating drum, and a rack of three tilted launch tubes each loaded with a
    // red-tipped missile.  Local +Y is up, the model fits roughly a 1.6 x 1.5 box standing on y = 0 and faces the player (left, -X) - the tubes lean that way.  Built from FleetBar
    // pieces only (cheap, flat-shaded) so it adds a few dozen polygons per launcher.
    Mesh BuildRocketLauncherMesh()
    {
        Mesh m;
        const MyColor hull(112, 118, 134, 255), dark(52, 56, 70, 255), olive(96, 106, 84, 255), steel(170, 176, 192, 255), warn(255, 190, 40, 255), red(235, 64, 56, 255), white(232, 236, 245, 255);
        FleetBar(m, MyVector(-0.70f, 0.14f, 0.0f), MyVector(0.70f, 0.14f, 0.0f), 0.22f, 0.22f, dark, true);                         // track skirts
        FleetBar(m, MyVector(-0.62f, 0.14f, 0.26f), MyVector(0.62f, 0.14f, 0.26f), 0.16f, 0.16f, dark, true);
        FleetBar(m, MyVector(-0.62f, 0.14f, -0.26f), MyVector(0.62f, 0.14f, -0.26f), 0.16f, 0.16f, dark, true);
        for (int k = 0; k < 5; ++k)                                                                                                    // road wheels
        {
            const float x = -0.56f + 0.28f * static_cast<float>(k);
            FleetBar(m, MyVector(x, 0.14f, 0.34f), MyVector(x, 0.14f, 0.40f), 0.12f, 0.12f, steel, true);
            FleetBar(m, MyVector(x, 0.14f, -0.34f), MyVector(x, 0.14f, -0.40f), 0.12f, 0.12f, steel, true);
        }
        FleetBar(m, MyVector(-0.55f, 0.36f, 0.0f), MyVector(0.55f, 0.36f, 0.0f), 0.26f, 0.22f, hull, true);                         // hull with a sloped glacis plate
        FleetBar(m, MyVector(-0.72f, 0.32f, 0.0f), MyVector(-0.50f, 0.38f, 0.0f), 0.14f, 0.20f, olive, true);
        FleetBar(m, MyVector(-0.20f, 0.55f, 0.0f), MyVector(0.20f, 0.55f, 0.0f), 0.30f, 0.30f, olive, true);                        // turret drum
        FleetBar(m, MyVector(-0.20f, 0.60f, 0.0f), MyVector(0.20f, 0.60f, 0.0f), 0.34f, 0.34f, hull, true);
        FleetBar(m, MyVector(0.45f, 0.50f, 0.0f), MyVector(0.62f, 0.50f, 0.0f), 0.10f, 0.10f, warn, true);                          // hazard stripe on the rear deck
        // the rack: three tubes leaning forward, with a missile nose showing in each
        for (int k = -1; k <= 1; ++k)
        {
            const float z = 0.20f * static_cast<float>(k);
            FleetBar(m, MyVector(0.08f, 0.62f, z), MyVector(-0.34f, 1.30f, z), 0.085f, 0.085f, steel, true);                         // tube
            FleetBar(m, MyVector(-0.28f, 1.20f, z), MyVector(-0.34f, 1.30f, z), 0.10f, 0.10f, dark, true);                           // muzzle ring
            FleetBar(m, MyVector(-0.32f, 1.27f, z), MyVector(-0.40f, 1.40f, z), 0.06f, 0.012f, red, true);                           // warhead
            MarkGlow(m, 6, red);
        }
        FleetBar(m, MyVector(0.10f, 0.78f, -0.26f), MyVector(0.10f, 0.78f, 0.26f), 0.05f, 0.05f, dark, true);                       // cross-brace holding the rack
        FleetBar(m, MyVector(0.36f, 0.62f, 0.0f), MyVector(0.36f, 1.10f, 0.0f), 0.015f, 0.015f, white, true);                        // seeker antenna
        FleetBar(m, MyVector(0.36f, 1.10f, 0.0f), MyVector(0.36f, 1.16f, 0.0f), 0.04f, 0.04f, red, true);   MarkGlow(m, 6, red);
        FinalizeMesh(m);
        return m;
    }

    // ================================================================================================
    //  MORPHEOUS ADD-ONS - the real hardware that bolts onto the player's fighter as it is built up (one mesh per stage, both sides in each)
    // ================================================================================================
    void BuildMorphAddons(Mesh* a)
    {
        const MyColor steel(190, 196, 212, 255), dark(58, 62, 78, 255), gun(78, 82, 98, 255), none(0, 0, 0, 255), glass(110, 215, 255, 255), orange(255, 140, 50, 255), white(232, 236, 245, 255),
                      noz(96, 100, 116, 255), flame(120, 210, 255, 255), seam(24, 28, 40, 255), lamp(255, 240, 150, 255), navG(70, 255, 130, 255), navR(255, 70, 70, 255), cyan(110, 220, 255, 255);
        auto light = [&](Mesh& m, const MyVector& p, const MyColor& c) { FleetBar(m, p, p + MyVector(0.03f, 0.0f, 0.0f), 0.022f, 0.016f, c, true); MarkGlow(m, 6, c); };
        auto ringX = [&](Mesh& m, float x, float y, float z, float rr, int n, float th, const MyColor& c, bool glow)       // a hoop round a barrel / nacelle (axis +X)
        {
            for (int k = 0; k < n; ++k)
            {
                float s0, c0, s1, c1;
                FAST_SINCOS_DEG(360.0f * static_cast<float>(k) / static_cast<float>(n), s0, c0);
                FAST_SINCOS_DEG(360.0f * static_cast<float>(k + 1) / static_cast<float>(n), s1, c1);
                FleetBar(m, MyVector(x, y + rr * c0, z + rr * s0), MyVector(x, y + rr * c1, z + rr * s1), th, th, c, true);
                if (glow) MarkGlow(m, 6, c);
            }
        };
        auto missile = [&](Mesh& m, float x, float y, float z, float len)                                                  // body, warhead, tail ring and four small fins
        {
            FleetBar(m, MyVector(x, y, z), MyVector(x + len, y, z), 0.034f, 0.034f, white, true);
            FleetBar(m, MyVector(x + len, y, z), MyVector(x + len + 0.12f, y, z), 0.034f, 0.005f, orange, true);
            FleetBar(m, MyVector(x, y, z), MyVector(x - 0.04f, y, z), 0.03f, 0.03f, dark, true);
            FleetBar(m, MyVector(x + len * 0.55f, y, z), MyVector(x + len * 0.58f, y, z), 0.04f, 0.04f, dark, true);       // a dark band
            FleetPanel(m, { MyVector(x + 0.10f, y, z), MyVector(x - 0.05f, y, z + 0.11f), MyVector(x - 0.05f, y, z - 0.11f) }, white, true, 0.9f);
            FleetPanel(m, { MyVector(x + 0.10f, y, z), MyVector(x - 0.05f, y + 0.11f, z), MyVector(x - 0.05f, y - 0.11f, z) }, white, true, 0.8f);
        };
        auto barrel = [&](Mesh& m, const MyVector& from, const MyVector& to, float r)                                      // a gun barrel with a cooling jacket and a muzzle brake
        {
            FleetBar(m, from, to, r, r * 0.8f, gun, true);
            FleetBar(m, from + (to - from) * 0.15f, from + (to - from) * 0.55f, r * 1.6f, r * 1.5f, steel, true);          // jacket
            FleetBar(m, to - (to - from) * 0.12f, to, r * 1.9f, r * 1.9f, dark, true);                                     // muzzle brake
        };
        for (int i = 0; i < kMaCount; ++i) a[i] = Mesh();
        // ---- 0 WING PODS: a streamlined pod under each wing on a pylon, with an intake, a nav light and a small nozzle
        {
            Mesh& m = a[kMaWingPods];
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = 0.62f * static_cast<float>(s);
                FleetBar(m, MyVector(0.30f, -0.09f, z), MyVector(-0.45f, -0.09f, z), 0.085f, 0.060f, none, false);
                FleetBar(m, MyVector(0.30f, -0.09f, z), MyVector(0.42f, -0.09f, z), 0.060f, 0.040f, none, false, 0.9f);   // nose
                FleetBar(m, MyVector(0.20f, -0.09f, z), MyVector(0.30f, -0.09f, z), 0.090f, 0.088f, dark, true);          // intake lip
                FleetBar(m, MyVector(0.0f, -0.01f, z), MyVector(0.0f, -0.09f, z), 0.03f, 0.03f, steel, true);              // pylon
                FleetBar(m, MyVector(-0.20f, -0.01f, z), MyVector(-0.20f, -0.09f, z), 0.03f, 0.03f, steel, true);
                FleetBar(m, MyVector(-0.45f, -0.09f, z), MyVector(-0.50f, -0.09f, z), 0.05f, 0.045f, noz, true);
                light(m, MyVector(0.40f, -0.09f, z), (s > 0) ? navR : navG);
                for (int k = 0; k < 3; ++k) FleetBar(m, MyVector(0.10f - 0.17f * static_cast<float>(k), -0.17f, z - 0.005f), MyVector(0.10f - 0.17f * static_cast<float>(k) - 0.01f, -0.17f, z - 0.005f), 0.0f + 0.004f, 0.004f, seam, true);
            }
        }
        // ---- 1 TWIN GUNS: two cannons on receivers either side of the nose, long jacketed barrels and muzzle brakes
        {
            Mesh& m = a[kMaTwinGuns];
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = 0.45f * static_cast<float>(s);
                FleetSlab(m, MyVector(0.12f, 0.02f, z), 0.16f, 0.07f, MyVector(0.12f, 0.11f, z), 0.13f, 0.055f, gun, true);                                       // receiver
                barrel(m, MyVector(0.24f, 0.07f, z), MyVector(1.00f, 0.07f, z), 0.034f);
                FleetBar(m, MyVector(0.16f, 0.11f, z), MyVector(0.16f, 0.15f, z), 0.018f, 0.018f, steel, true);                                                // ammo feed
            }
        }
        // ---- 2 ENGINE NACELLES: a large nacelle either side behind the wings: intake ring, armoured body, exhaust nozzle with a glowing core
        {
            Mesh& m = a[kMaNacelles];
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = 0.50f * static_cast<float>(s);
                FleetBar(m, MyVector(-0.28f, -0.10f, z), MyVector(-0.92f, -0.10f, z), 0.125f, 0.10f, none, false);
                ringX(m, -0.28f, -0.10f, z, 0.13f, 8, 0.016f, dark, false);
                FleetBar(m, MyVector(-0.92f, -0.10f, z), MyVector(-0.98f, -0.10f, z), 0.115f, 0.10f, noz, true);                                               // nozzle
                FleetBar(m, MyVector(-0.98f, -0.10f, z), MyVector(-1.10f, -0.10f, z), 0.07f, 0.03f, flame, true);  MarkGlow(m, 6, flame);                     // glowing core
                FleetBar(m, MyVector(-0.30f, -0.03f, z), MyVector(-0.55f, -0.03f, z), 0.05f, 0.05f, steel, true);                                              // pylon strut
                for (int k = 0; k < 3; ++k) FleetBar(m, MyVector(-0.45f - 0.14f * static_cast<float>(k), -0.10f, z), MyVector(-0.47f - 0.14f * static_cast<float>(k), -0.10f, z), 0.13f, 0.13f, seam, true, 0.8f);   // panel bands
            }
        }
        // ---- 3 SIDE GUNS: a strut out of each wing carrying a swivel turret with twin barrels pointing outward
        {
            Mesh& m = a[kMaSideGuns];
            for (int s = -1; s <= 1; s += 2)
            {
                const float d = static_cast<float>(s);
                FleetBar(m, MyVector(-0.10f, 0.0f, 0.78f * d), MyVector(-0.10f, 0.03f, 1.22f * d), 0.04f, 0.035f, steel, true);
                FleetSlab(m, MyVector(-0.10f, 0.0f, 1.24f * d), 0.13f, 0.12f, MyVector(-0.10f, 0.10f, 1.24f * d), 0.10f, 0.09f, gun, true);                       // turret
                for (int k = -1; k <= 1; k += 2) barrel(m, MyVector(-0.10f + 0.05f * static_cast<float>(k), 0.06f, 1.26f * d), MyVector(-0.10f + 0.05f * static_cast<float>(k), 0.06f, 1.72f * d), 0.024f);
                FleetBar(m, MyVector(-0.10f, 0.10f, 1.24f * d), MyVector(-0.10f, 0.14f, 1.24f * d), 0.03f, 0.02f, glass, true);                                  // sight
            }
        }
        // ---- 4 REAR GUNS: tail booms with a gun housing and a long rearward barrel each (a third centre barrel comes with stage 9 via the same mesh)
        {
            Mesh& m = a[kMaRearGuns];
            for (int s = -1; s <= 1; s += 2)
            {
                const float z = 0.25f * static_cast<float>(s);
                FleetBar(m, MyVector(-0.60f, 0.03f, z), MyVector(-1.05f, 0.03f, z), 0.045f, 0.040f, steel, true);                                                // boom
                FleetSlab(m, MyVector(-1.05f, 0.0f, z), 0.12f, 0.07f, MyVector(-1.05f, 0.10f, z), 0.10f, 0.06f, gun, true);                                      // housing
                barrel(m, MyVector(-1.15f, 0.05f, z), MyVector(-1.70f, 0.05f, z), 0.030f);
                light(m, MyVector(-1.12f, 0.12f, z), (s > 0) ? navR : navG);
            }
            barrel(m, MyVector(-0.80f, 0.12f, 0.0f), MyVector(-1.55f, 0.12f, 0.0f), 0.026f);                                                                     // centre barrel
        }
        // ---- 5 MISSILE PODS: under-wing racks with a pylon and a row of missiles
        {
            Mesh& m = a[kMaMissiles];
            for (int s = -1; s <= 1; s += 2)
            {
                const float d = static_cast<float>(s);
                FleetBar(m, MyVector(0.35f, -0.16f, 0.62f * d), MyVector(-0.10f, -0.16f, 0.62f * d), 0.06f, 0.055f, steel, true);                               // rack
                FleetBar(m, MyVector(0.10f, -0.04f, 0.62f * d), MyVector(0.10f, -0.16f, 0.62f * d), 0.03f, 0.03f, steel, true);
                for (int k = 0; k < 3; ++k) missile(m, 0.0f, -0.22f, (0.50f + 0.12f * static_cast<float>(k)) * d, 0.62f);
            }
        }
        // ---- 6 NOSE ARMOUR: a wedge prow with an armoured ridge and a glowing sensor window
        {
            Mesh& m = a[kMaNoseArmor];
            FleetSlab(m, MyVector(0.62f, -0.04f, 0.0f), 0.26f, 0.22f, MyVector(0.62f, 0.10f, 0.0f), 0.20f, 0.16f, none, false, 1.05f);
            FleetBar(m, MyVector(0.62f, 0.02f, 0.0f), MyVector(1.12f, 0.02f, 0.0f), 0.10f, 0.02f, none, false, 0.95f);
            FleetBar(m, MyVector(0.45f, 0.13f, 0.0f), MyVector(1.00f, 0.08f, 0.0f), 0.025f, 0.012f, steel, true);                                              // ridge
            FleetBar(m, MyVector(0.80f, 0.045f, 0.0f), MyVector(0.90f, 0.04f, 0.0f), 0.04f, 0.03f, glass, true);  MarkGlow(m, 6, glass);                      // sensor
            for (int s = -1; s <= 1; s += 2) FleetBar(m, MyVector(0.45f, 0.0f, 0.20f * static_cast<float>(s)), MyVector(0.95f, 0.0f, 0.06f * static_cast<float>(s)), 0.02f, 0.012f, steel, true);
        }
        // ---- 7 LASER EMITTER: a long focusing barrel on the nose with glowing rings and a lens
        {
            Mesh& m = a[kMaLaser];
            FleetBar(m, MyVector(0.95f, 0.02f, 0.0f), MyVector(1.42f, 0.02f, 0.0f), 0.055f, 0.04f, steel, true);
            for (int k = 0; k < 4; ++k) ringX(m, 1.02f + 0.10f * static_cast<float>(k), 0.02f, 0.0f, 0.075f, 6, 0.012f, cyan, true);
            FleetBar(m, MyVector(1.42f, 0.02f, 0.0f), MyVector(1.50f, 0.02f, 0.0f), 0.045f, 0.045f, cyan, true);  MarkGlow(m, 6, cyan);
            FleetBar(m, MyVector(0.70f, 0.14f, 0.0f), MyVector(1.30f, 0.09f, 0.0f), 0.018f, 0.014f, dark, true);                                              // coolant line
            FleetSlab(m, MyVector(0.90f, 0.02f, 0.0f), 0.10f, 0.07f, MyVector(0.90f, 0.12f, 0.0f), 0.08f, 0.05f, gun, true);                                  // capacitor housing
        }
        // ---- 8 OUTER WINGS: swept wing extensions with a leading-edge spar, ribs, wingtip cannons and nav lights
        {
            Mesh& m = a[kMaOuterWings];
            for (int s = -1; s <= 1; s += 2)
            {
                const float d = static_cast<float>(s);
                FleetPanel(m, { MyVector(0.0f, 0.0f, 0.84f * d), MyVector(-0.30f, 0.0f, 2.05f * d), MyVector(-0.55f, 0.0f, 1.95f * d), MyVector(-0.46f, 0.0f, 0.84f * d) }, none, false, 0.95f);
                FleetBar(m, MyVector(0.0f, 0.0f, 0.84f * d), MyVector(-0.30f, 0.0f, 2.05f * d), 0.022f, 0.016f, steel, true);
                for (int k = 1; k <= 4; ++k)
                {
                    const float u = 0.2f * static_cast<float>(k);
                    FleetBar(m, MyVector(-0.04f * u, 0.004f, (0.84f + 1.21f * u) * d), MyVector(-0.46f - 0.09f * u, 0.004f, (0.84f + 1.11f * u) * d), 0.006f, 0.006f, seam, true, 0.9f);
                }
                FleetBar(m, MyVector(-0.10f, 0.0f, 1.90f * d), MyVector(0.45f, 0.0f, 1.90f * d), 0.024f, 0.012f, gun, true);                                      // wingtip cannon
                light(m, MyVector(-0.32f, 0.0f, 2.04f * d), (s > 0) ? navR : navG);
            }
        }
        // ---- 9 DORSAL: a tail fin, armour plates along the flanks and extra missile racks
        {
            Mesh& m = a[kMaDorsal];
            FleetPanel(m, { MyVector(-0.20f, 0.10f, 0.0f), MyVector(-0.62f, 0.42f, 0.0f), MyVector(-0.74f, 0.40f, 0.0f), MyVector(-0.70f, 0.10f, 0.0f) }, none, false, 1.05f);
            FleetBar(m, MyVector(-0.62f, 0.42f, 0.0f), MyVector(-0.62f, 0.50f, 0.0f), 0.01f, 0.004f, steel, true);
            light(m, MyVector(-0.62f, 0.50f, 0.0f), white);
            for (int s = -1; s <= 1; s += 2)
            {
                const float d = static_cast<float>(s);
                FleetSlab(m, MyVector(0.20f, 0.0f, 1.05f * d), 0.55f, 0.05f, MyVector(0.20f, 0.10f, 1.05f * d), 0.50f, 0.04f, none, false, 0.95f);              // flank armour
                FleetBar(m, MyVector(0.0f, 0.0f, 0.90f * d), MyVector(0.0f, 0.0f, 1.05f * d), 0.02f, 0.02f, steel, true);
                missile(m, 0.0f, -0.10f, 0.95f * d, 0.50f);
            }
        }
        // ---- 10 DRONE: one option drone (drawn twice, orbiting the ship): a core with a glowing eye, a ring of struts and two stubby wings
        {
            Mesh& m = a[kMaDrone];
            FleetSlab(m, MyVector(0.0f, -0.10f, 0.0f), 0.06f, 0.06f, MyVector(0.0f, 0.0f, 0.0f), 0.12f, 0.12f, none, false);
            FleetSlab(m, MyVector(0.0f, 0.0f, 0.0f), 0.12f, 0.12f, MyVector(0.0f, 0.10f, 0.0f), 0.06f, 0.06f, none, false, 1.1f);
            for (int k = 0; k < 8; ++k)
            {
                float s0, c0, s1, c1;
                FAST_SINCOS_DEG(45.0f * static_cast<float>(k), s0, c0); FAST_SINCOS_DEG(45.0f * static_cast<float>(k + 1), s1, c1);
                FleetBar(m, MyVector(0.20f * c0, 0.0f, 0.20f * s0), MyVector(0.20f * c1, 0.0f, 0.20f * s1), 0.016f, 0.016f, steel, true);
            }
            FleetBar(m, MyVector(0.10f, 0.0f, 0.0f), MyVector(0.20f, 0.0f, 0.0f), 0.04f, 0.035f, lamp, true);  MarkGlow(m, 6, lamp);
            FleetBar(m, MyVector(0.0f, 0.0f, 0.12f), MyVector(0.0f, 0.0f, 0.30f), 0.012f, 0.012f, gun, true);
            FleetBar(m, MyVector(0.0f, 0.0f, -0.12f), MyVector(0.0f, 0.0f, -0.30f), 0.012f, 0.012f, gun, true);
        }
        for (int i = 0; i < kMaCount; ++i) FinalizeMesh(a[i]);
    }

    // ================================================================================================
    //  THE STARBASE - a space station that the game turns about its vertical axis.  Units: about 1 in radius, y up.
    // ================================================================================================
    // Saturn's ring system as one mesh, in units of the planet's radius (1.0): the faint C ring, the bright B ring in five bands, the A ring - with the dark CASSINI DIVISION (1.95 to
    // 2.03) and the thin ENCKE GAP (2.20 to 2.215) left empty so the stars show through.  Tilted like Gyrospace's rings.  Each band is double sided (back-face culling would otherwise
    // hide the underside) and painted with fixed colours.  48 segments x 10 bands x 2 sides.
    Mesh BuildSaturnRingsMesh()
    {
        Mesh m;
        struct Band { float r0, r1; int cr, cg, cb; };
        static const Band kBands[] = {
            { 1.24f, 1.38f, 104, 92, 78 }, { 1.38f, 1.53f, 128, 114, 96 },
            { 1.53f, 1.62f, 214, 192, 152 }, { 1.62f, 1.72f, 238, 218, 178 }, { 1.72f, 1.80f, 200, 178, 140 }, { 1.80f, 1.88f, 232, 212, 172 }, { 1.88f, 1.95f, 208, 186, 148 },
            { 2.03f, 2.12f, 196, 174, 138 }, { 2.12f, 2.20f, 182, 160, 126 }, { 2.215f, 2.27f, 190, 168, 132 } };
        const MyQuaternion tilt = MyQuaternion::AngleAxis(24.0f, MyVector::Right()) * MyQuaternion::AngleAxis(-12.0f, MyVector::Forward());
        const MyVector nrm = tilt * MyVector(0.0f, 1.0f, 0.0f);
        constexpr int kSeg = 48;
        for (const Band& b : kBands)
            for (int k = 0; k < kSeg; ++k)
            {
                float s0, c0, s1, c1;
                FAST_SINCOS_DEG(static_cast<float>(k) * (360.0f / kSeg), s0, c0);
                FAST_SINCOS_DEG(static_cast<float>(k + 1) * (360.0f / kSeg), s1, c1);
                const int base = static_cast<int>(m.verts.size());
                m.verts.push_back(tilt * MyVector(b.r0 * c0, 0.0f, b.r0 * s0)); m.verts.push_back(tilt * MyVector(b.r1 * c0, 0.0f, b.r1 * s0));
                m.verts.push_back(tilt * MyVector(b.r1 * c1, 0.0f, b.r1 * s1)); m.verts.push_back(tilt * MyVector(b.r0 * c1, 0.0f, b.r0 * s1));
                const float grain = 0.94f + 0.06f * static_cast<float>((k * 7 + static_cast<int>(b.r0 * 100.0f)) % 3);       // a little variation along the band
                const MyColor col(static_cast<uint8_t>(b.cr * grain), static_cast<uint8_t>(b.cg * grain), static_cast<uint8_t>(b.cb * grain), 255);
                const MyVector mid = (m.verts[base] + m.verts[base + 2]) * 0.5f;
                AddFaceAuto(m, { base, base + 1, base + 2, base + 3 }, mid - nrm, 1.0f, true, col);                          // the top face (interior point below)
                AddFaceAuto(m, { base + 3, base + 2, base + 1, base }, mid + nrm, 0.8f, true, col);                          // the underside, a little darker
            }
        m.radius = 2.3f;
        FinalizeMesh(m);
        return m;
    }

    Mesh BuildStarbaseMesh()
    {
        Mesh m;
        const MyColor steel(150, 156, 172, 255), dark(58, 62, 78, 255), none(0, 0, 0, 255), glass(110, 215, 255, 255), amber(255, 190, 70, 255), white(232, 236, 245, 255),
                      solar(35, 70, 170, 255), solarL(90, 140, 235, 255), navG(70, 255, 130, 255), navR(255, 70, 70, 255), seam(24, 28, 40, 255), noz(96, 100, 116, 255), flame(120, 210, 255, 255);
        // a 12-sided drum from y0 (radius r0) to y1 (radius r1) with both caps
        auto drum = [&](float y0, float y1, float r0, float r1, const MyColor& c, bool fixed, float tone)
        {
            const int n = 12, base = static_cast<int>(m.verts.size());
            for (int k = 0; k < n; ++k) { float s, cc; FAST_SINCOS_DEG(360.0f * static_cast<float>(k) / static_cast<float>(n), s, cc); m.verts.push_back(MyVector(r0 * cc, y0, r0 * s)); }
            for (int k = 0; k < n; ++k) { float s, cc; FAST_SINCOS_DEG(360.0f * static_cast<float>(k) / static_cast<float>(n), s, cc); m.verts.push_back(MyVector(r1 * cc, y1, r1 * s)); }
            const MyVector mid(0.0f, (y0 + y1) * 0.5f, 0.0f);
            std::vector<int> bot, top;
            for (int k = 0; k < n; ++k)
            {
                const int nx = (k + 1) % n;
                AddFaceAuto(m, { base + k, base + nx, base + n + nx, base + n + k }, mid, tone * (0.88f + 0.12f * static_cast<float>(k & 1)), fixed, c);   // alternate facets read as armour plates
                bot.push_back(base + k); top.push_back(base + n + k);
            }
            AddFaceVec(m, bot, mid, tone * 0.8f, fixed, c);
            AddFaceVec(m, top, mid, tone, fixed, c);
        };
        auto light = [&](const MyVector& p, const MyColor& c) { FleetBar(m, p, p + MyVector(0.0f, 0.03f, 0.0f), 0.018f, 0.014f, c, true); MarkGlow(m, 6, c); };
        // ---- the core: lower engineering pod, main drum, shoulder cone, cap, mast and dish
        drum(-0.46f, -0.30f, 0.12f, 0.20f, none, false, 0.85f);
        drum(-0.30f, 0.30f, 0.20f, 0.20f, none, false, 1.0f);
        drum(0.30f, 0.46f, 0.20f, 0.10f, none, false, 1.05f);
        drum(0.46f, 0.52f, 0.10f, 0.05f, steel, true, 1.0f);
        drum(-0.58f, -0.46f, 0.12f, 0.14f, noz, true, 0.9f);                                                                                     // engineering nozzle ring
        FleetBar(m, MyVector(0.0f, -0.58f, 0.0f), MyVector(0.0f, -0.64f, 0.0f), 0.09f, 0.09f, flame, true);  MarkGlow(m, 6, flame);               // its glow
        FleetBar(m, MyVector(0.0f, 0.52f, 0.0f), MyVector(0.0f, 0.86f, 0.0f), 0.012f, 0.005f, steel, true);
        light(MyVector(0.0f, 0.86f, 0.0f), navR);
        FleetSlab(m, MyVector(0.07f, 0.56f, 0.0f), 0.005f, 0.005f, MyVector(0.07f, 0.66f, 0.0f), 0.09f, 0.09f, steel, true);                      // sensor dish
        for (int k = 0; k < 3; ++k) drum(-0.20f + 0.20f * static_cast<float>(k) - 0.012f, -0.20f + 0.20f * static_cast<float>(k) + 0.012f, 0.206f, 0.206f, seam, true, 0.8f);   // armour seams (thin bands)
        // ---- the habitat ring: sixteen segments on four spokes, with a lit window band and docking pylons
        const float R = 0.62f;
        for (int k = 0; k < 16; ++k)
        {
            float s0, c0, s1, c1;
            FAST_SINCOS_DEG(22.5f * static_cast<float>(k), s0, c0); FAST_SINCOS_DEG(22.5f * static_cast<float>(k + 1), s1, c1);
            FleetBar(m, MyVector(R * c0, 0.0f, R * s0), MyVector(R * c1, 0.0f, R * s1), 0.075f, 0.075f, none, false, (k & 1) ? 0.92f : 1.04f);
            const float rm = R + 0.078f;
            float sm, cm; FAST_SINCOS_DEG(22.5f * (static_cast<float>(k) + 0.5f), sm, cm);
            FleetBar(m, MyVector(rm * cm, 0.012f, rm * sm), MyVector(rm * cm * 1.004f, 0.012f, rm * sm * 1.004f), 0.014f, 0.014f, amber, true);    // porthole
            MarkGlow(m, 6, amber);
            FleetBar(m, MyVector(R * c0, 0.078f, R * s0), MyVector(R * c1, 0.078f, R * s1), 0.016f, 0.016f, seam, true, 0.9f);                      // top strip
        }
        for (int k = 0; k < 4; ++k)
        {
            float s0, c0;
            FAST_SINCOS_DEG(45.0f + 90.0f * static_cast<float>(k), s0, c0);
            FleetBar(m, MyVector(0.20f * c0, 0.0f, 0.20f * s0), MyVector(R * c0, 0.0f, R * s0), 0.04f, 0.04f, steel, true);                          // spokes
            FleetBar(m, MyVector(0.10f * c0, 0.30f, 0.10f * s0), MyVector(R * c0, 0.04f, R * s0), 0.008f, 0.008f, dark, true);                       // cables
            FAST_SINCOS_DEG(90.0f * static_cast<float>(k), s0, c0);
            FleetBar(m, MyVector((R + 0.075f) * c0, 0.0f, (R + 0.075f) * s0), MyVector((R + 0.20f) * c0, 0.0f, (R + 0.20f) * s0), 0.05f, 0.04f, steel, true);   // docking pylon
            light(MyVector((R + 0.20f) * c0, 0.04f, (R + 0.20f) * s0), (k & 1) ? navR : navG);
        }
        // ---- two solar arrays on booms, above the ring plane (flat panels with a grid of cells)
        for (int s = -1; s <= 1; s += 2)
        {
            const float z = static_cast<float>(s);
            FleetBar(m, MyVector(0.0f, 0.40f, 0.0f), MyVector(0.0f, 0.40f, 1.0f * z), 0.02f, 0.02f, steel, true);
            FleetPanel(m, { MyVector(-0.20f, 0.42f, 0.34f * z), MyVector(0.20f, 0.42f, 0.34f * z), MyVector(0.20f, 0.42f, 0.98f * z), MyVector(-0.20f, 0.42f, 0.98f * z) }, solar, true, 1.0f);
            for (int k = 1; k < 4; ++k) FleetBar(m, MyVector(-0.20f, 0.425f, (0.34f + 0.16f * static_cast<float>(k)) * z), MyVector(0.20f, 0.425f, (0.34f + 0.16f * static_cast<float>(k)) * z), 0.006f, 0.006f, solarL, true);
            FleetBar(m, MyVector(0.0f, 0.425f, 0.34f * z), MyVector(0.0f, 0.425f, 0.98f * z), 0.006f, 0.006f, solarL, true);
        }
        FinalizeMesh(m);
        return m;
    }

    // One textured planet for Gyrospace.  `mapFile` is an equirectangular raw RGB24 map (128 x 64, 256 x 128 or 512 x 256: the file size says which); `subdiv` is the icosphere
    // level (4 = 5120 faces, 5 = 20480).  The vector renderer cannot texture-map, so each face takes the map colour (bilinearly sampled, so no blocky texels) at its centre
    // (counting double) and corners.  Unmapped (nearly black) texels are replaced by the map's average colour plus a little noise.  What makes a low-resolution map read as a
    // real world on a dense mesh:
    //  * GAS GIANTS (kind 1-4: Neptune, Uranus, Saturn, Jupiter): the map lookup is TURBULENCE-warped (band edges wobble and swirl) and each face gets streaky detail from noise
    //    stretched along the bands; the globe is OBLATE (Jupiter 0.935, Saturn 0.90 polar / equatorial), as the real ones are.  No relief.
    //  * ROCKY WORLDS (kind 0 Pluto, 5 Mars): fractal relief plus a scatter of impact CRATERS (bowl and raised rim, in the geometry, so they light and shade properly); crater
    //    floors are painted a little darker.
    //  * EARTH (kind 6): the sea is flat, the land is raised by ridged mountain noise.
    // Returns false, leaving `m` untouched, if the file is missing or not one of those sizes.
    bool BuildMappedPlanetMesh(Mesh& m, const std::filesystem::path& mapFile, int subdiv, int kind)
    {
        std::ifstream in(mapFile, std::ios::binary | std::ios::ate);
        if (!in) return false;
        const std::streamoff sz = in.tellg();
        int W = 0, H = 0;
        if (sz == 128 * 64 * 3) { W = 128; H = 64; }
        else if (sz == 256 * 128 * 3) { W = 256; H = 128; }
        else if (sz == 512 * 256 * 3) { W = 512; H = 256; }
        else return false;
        std::vector<unsigned char> px(static_cast<size_t>(sz));
        in.seekg(0);
        in.read(reinterpret_cast<char*>(px.data()), static_cast<std::streamsize>(px.size()));
        if (in.gcount() != static_cast<std::streamsize>(px.size())) return false;
        double sr = 0.0, sg = 0.0, sb = 0.0; long cnt = 0;
        for (size_t i = 0; i < px.size(); i += 3)
            if (px[i] + px[i + 1] + px[i + 2] >= 72) { sr += px[i]; sg += px[i + 1]; sb += px[i + 2]; ++cnt; }
        const float ar = cnt ? static_cast<float>(sr / cnt) : 128.0f, ag = cnt ? static_cast<float>(sg / cnt) : 128.0f, ab = cnt ? static_cast<float>(sb / cnt) : 128.0f;

        const bool gas = (kind >= 1 && kind <= 4), dense = subdiv >= 5;
        const MyVector wOff1(4.1f, 9.7f, 2.3f), wOff2(7.7f, 1.9f, 5.3f), fine(13.0f, 29.0f, 3.0f), seed(2.9f, 6.1f + static_cast<float>(kind), 8.8f);

        // One texel, with unmapped ones filled.
        auto texel = [&](int x, int y, float out[3])
        {
            x = ((x % W) + W) % W; y = (std::max)(0, (std::min)(H - 1, y));
            const unsigned char* q = &px[(static_cast<size_t>(y) * W + x) * 3];
            if (q[0] + q[1] + q[2] < 72) { const float k = 0.82f + 0.36f * Hash3(x, y, 7); out[0] = ar * k; out[1] = ag * k; out[2] = ab * k; }
            else { out[0] = q[0]; out[1] = q[1]; out[2] = q[2]; }
        };
        auto sample = [&](const MyVector& d, float out[3])
        {
            const MyVector n = d.Normalized();
            float u = MyMath::Atan2(n.z, n.x) / MyMath::kTwoPi + 0.5f, vv = 0.5f - MyMath::Asin(MyMath::Clamp(n.y, -1.0f, 1.0f)) / MyMath::kPi;
            if (gas)                                                                       // wobble the band edges (a texel is 2.8 degrees on the 128-wide maps)
            {
                u += 0.06f * (Fbm3N(n * 7.0f + wOff1, 4) - 0.47f);
                vv += 0.02f * (Fbm3N(n * 7.0f + wOff2, 4) - 0.47f);
            }
            const float fx = u * static_cast<float>(W) - 0.5f, fy = vv * static_cast<float>(H) - 0.5f;
            const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
            const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
            float a[3], b[3], c[3], e[3];
            texel(x0, y0, a); texel(x0 + 1, y0, b); texel(x0, y0 + 1, c); texel(x0 + 1, y0 + 1, e);
            for (int ch = 0; ch < 3; ++ch)
            {
                const float top = a[ch] + (b[ch] - a[ch]) * tx, bot = c[ch] + (e[ch] - c[ch]) * tx;
                out[ch] = top + (bot - top) * ty;
            }
            if (kind == 4)                                                                 // Jupiter: the GREAT RED SPOT, an oval in the southern hemisphere (22 degrees south) with a pale rim
            {
                const float lat = MyMath::Asin(MyMath::Clamp(n.y, -1.0f, 1.0f)), lon = MyMath::Atan2(n.z, n.x);
                float dlon = lon - 0.9f; if (dlon > MyMath::kPi) dlon -= MyMath::kTwoPi; if (dlon < -MyMath::kPi) dlon += MyMath::kTwoPi;
                const float dl = (lat + 0.384f) / 0.125f, dn = dlon * std::cos(lat) / 0.27f;      // about 7 degrees tall and 15 wide
                const float d = std::sqrt(dl * dl + dn * dn);                                    // 0 at the centre, 1 at the edge of the oval
                if (d < 1.55f)
                {
                    const float body = MyMath::Clamp(1.0f - d, 0.0f, 1.0f), edge = MyMath::Clamp((1.55f - d) / 0.55f, 0.0f, 1.0f);
                    const float rim = (d > 0.85f) ? edge * 0.45f : 0.0f;                         // a pale ring just outside the red
                    const float red[3] = { 176.0f - 24.0f * body, 74.0f - 22.0f * body, 48.0f - 14.0f * body }, pale[3] = { 232.0f, 206.0f, 176.0f };
                    const float kBody = MyMath::Clamp(body * 2.4f, 0.0f, 1.0f);
                    for (int ch = 0; ch < 3; ++ch) { out[ch] += (red[ch] - out[ch]) * kBody; out[ch] += (pale[ch] - out[ch]) * rim; }
                }
            }
        };

        std::vector<MyVector> v;
        std::vector<std::array<int, 3>> tris;
        BuildIcoSphere(subdiv, v, tris);

        // impact craters for the airless worlds: centre direction, angular radius (radians), depth (fraction of the radius)
        struct Crater { MyVector c; float r, depth; };
        Crater cr[20];
        int craters = 0;
        if (kind == 0 || kind == 5)
        {
            unsigned rng = 0x9E3779B9u * static_cast<unsigned>(kind + 1);
            auto rnd = [&]() { rng = rng * 1664525u + 1013904223u; return static_cast<float>((rng >> 8) & 0xFFFFFF) / 16777216.0f; };
            craters = dense ? 20 : 12;
            for (int i = 0; i < craters; ++i)
            {
                const float z = 2.0f * rnd() - 1.0f, a = 6.2831853f * rnd(), s = std::sqrt(MyMath::Max(0.0f, 1.0f - z * z));
                cr[i].c = MyVector(s * std::cos(a), z, s * std::sin(a));
                cr[i].r = 0.07f + 0.23f * rnd() * rnd();                                  // mostly small, a few big basins
                cr[i].depth = 0.010f + 0.022f * rnd();
            }
        }

        const float flat = (kind == 4) ? 0.935f : ((kind == 3) ? 0.90f : ((kind == 1 || kind == 2) ? 0.975f : 1.0f));      // polar / equatorial radius
        std::vector<float> pit(v.size(), 0.0f);                                           // per-vertex crater depth, for darkening the floors
        for (size_t i = 0; i < v.size(); ++i)
        {
            const MyVector d = v[i];
            float r = 1.0f;
            if (kind == 6)                                                                // Earth: raised land, flat sea
            {
                float s[3];
                sample(d, s);
                if (!(s[2] > s[0] + 22.0f && s[2] > s[1]))
                {
                    const float base = MyMath::Max(0.0f, Fbm3N(d * 5.5f + seed, dense ? 6 : 4) - 0.30f);
                    const float ridge = 1.0f - MyMath::Abs(MyMath::Clamp(2.0f * Fbm3N(d * 11.0f + fine, 4) - 1.0f, -1.0f, 1.0f));
                    r += (dense ? 0.060f : 0.045f) * base * (0.65f + 0.70f * ridge);
                }
            }
            else if (!gas)                                                                // Pluto / Mars: rolling relief and craters
            {
                r += ((kind == 5) ? 0.030f : 0.022f) * (Fbm3N(d * 3.2f + seed, dense ? 6 : 4) - 0.47f);
                r += 0.010f * (1.0f - MyMath::Abs(MyMath::Clamp(2.0f * Fbm3N(d * 12.0f + fine, 4) - 1.0f, -1.0f, 1.0f))) - 0.005f;
                for (int k = 0; k < craters; ++k)
                {
                    const float cd = MyMath::Clamp(d.Dot(cr[k].c), -1.0f, 1.0f);
                    if (cd < 0.9f) continue;                                              // farther than ~26 degrees: outside every crater
                    const float t = std::acos(cd) / cr[k].r;                              // 0 at the centre, 1 at the rim
                    if (t < 1.0f) { const float dep = cr[k].depth * (1.0f - t * t); r -= dep; pit[i] += dep; }
                    const float rim = (t - 1.05f) / 0.22f;
                    r += 0.35f * cr[k].depth * std::exp(-rim * rim);                      // the raised lip
                }
            }
            m.verts.push_back(MyVector(d.x * r, d.y * r * flat, d.z * r));
        }

        const float step = gas ? 8.0f : 6.0f;                                              // colour quantisation: faces of one colour batch together
        for (const auto& tr : tris)
        {
            float acc[3] = { 0.0f, 0.0f, 0.0f }, s[3];
            const MyVector centre = ((v[tr[0]] + v[tr[1]] + v[tr[2]]) * (1.0f / 3.0f)).Normalized();
            sample(centre, s);  for (int ch = 0; ch < 3; ++ch) acc[ch] += 2.0f * s[ch];     // the centre counts double
            for (int k = 0; k < 3; ++k) { sample(v[tr[k]], s); for (int ch = 0; ch < 3; ++ch) acc[ch] += s[ch]; }
            // Fine detail: streaks along the bands on a gas giant (noise stretched in latitude), grain elsewhere.
            float detail;
            if (gas) detail = 0.95f + 0.10f * (Fbm3N(MyVector(centre.x * 6.0f, centre.y * 40.0f, centre.z * 6.0f) + fine, 4) - 0.47f) * 2.0f;
            else     detail = 0.92f + 0.16f * Fbm3N(centre * (dense ? 34.0f : 20.0f) + fine, 4);
            if (craters > 0) detail *= MyMath::Clamp(1.0f - 5.0f * (pit[tr[0]] + pit[tr[1]] + pit[tr[2]]) * (1.0f / 3.0f), 0.70f, 1.0f);       // darker crater floors
            auto q = [&](float val) { const int i = static_cast<int>(MyMath::Clamp(val * 0.2f * detail, 0.0f, 255.0f)); return static_cast<uint8_t>(static_cast<int>(i / step) * static_cast<int>(step) + static_cast<int>(step) / 2); };
            AddFaceAuto(m, { tr[0], tr[1], tr[2] }, MyVector(0.0f, 0.0f, 0.0f), 1.0f, true, MyColor(q(acc[0]), q(acc[1]), q(acc[2]), 255));
        }
        FinalizeMesh(m);
        return true;
    }
namespace
{
    struct ShipRng                                                                    // a tiny xorshift generator: BuildProceduralShip is deterministic for a seed
    {
        uint32_t s; explicit ShipRng(uint32_t seed) : s(seed * 2654435761u + 12345u) { if (s == 0) s = 1; }
        uint32_t Next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
        float F() { return static_cast<float>(Next() & 0xFFFFFFu) / 16777216.0f; }
        float R(float a, float b) { return a + (b - a) * F(); }
        int I(int n) { return n > 0 ? static_cast<int>(Next() % static_cast<uint32_t>(n)) : 0; }
    };
}
// ---- Bevelling, strobe points and the procedural ship generator ---------------------------------------------------------------------------------------------------------------------------------
// BevelMesh: a rounded-edge twin of a mesh.  For each face of 3+ vertices that is neither self-lit nor a decal and is not tiny, an inner copy (shrunk toward the centre by `inset`, raised along the normal by
// `lift` x the mesh radius) keeps the face's paint and one chamfer quad per edge joins the original edge to the inner edge; its normal is the face normal tilted toward the outside of the edge, so the lighting
// rolls off round every edge.  The outer vertices are unchanged, so neighbouring faces still meet without gaps.  (Cost: about 4 x the faces: draw it near the camera and the plain mesh far away.)
Mesh BevelMesh(const Mesh& src, float inset, float lift)
{
    Mesh m; m.verts = src.verts; m.radius = src.radius; m.faces.reserve(src.faces.size() * 4);
    const float minArea = src.radius * src.radius * 0.0012f;
    for (const Face& f : src.faces)
    {
        const int n = static_cast<int>(f.idx.size());
        if (n < 3 || f.glow || f.depthBias != 0.0f) { m.faces.push_back(f); continue; }
        MyVector c(0.0f, 0.0f, 0.0f); for (int k : f.idx) c = c + src.verts[k]; c = c * (1.0f / static_cast<float>(n));
        float area = 0.0f; for (int i = 1; i + 1 < n; ++i) area += (src.verts[f.idx[i]] - src.verts[f.idx[0]]).Cross(src.verts[f.idx[i + 1]] - src.verts[f.idx[0]]).Magnitude() * 0.5f;
        if (area < minArea) { m.faces.push_back(f); continue; }
        const int base = static_cast<int>(m.verts.size());
        for (int i = 0; i < n; ++i) m.verts.push_back(c + (src.verts[f.idx[i]] - c) * (1.0f - inset) + f.normal * (lift * src.radius));
        Face inner = f; for (int i = 0; i < n; ++i) inner.idx[i] = base + i; m.faces.push_back(inner);
        for (int i = 0; i < n; ++i)
        {
            const int j = (i + 1) % n; MyVector out = (src.verts[f.idx[i]] + src.verts[f.idx[j]]) * 0.5f - c; out = out - f.normal * out.Dot(f.normal);
            out = out.SqrMagnitude() > 1.0e-10f ? out.Normalized() : f.normal;
            Face q = f; q.idx.assign({ f.idx[i], f.idx[j], base + j, base + i }); q.normal = (f.normal * 0.62f + out * 0.78f).Normalized(); q.tone = f.tone * 0.97f; m.faces.push_back(q);
        }
    }
    return m;
}

// FindStrobePoints: the most port, the most starboard and the highest vertex of a mesh (nose +X, up +Y, wings along Z): where wing-tip and fin lights belong.
void FindStrobePoints(const Mesh& m, MyVector& port, MyVector& starboard, MyVector& top)
{
    port = starboard = top = MyVector(0.0f, 0.0f, 0.0f); if (m.verts.empty()) return; port = starboard = top = m.verts[0];
    for (const MyVector& v : m.verts) { if (v.z < port.z) port = v; if (v.z > starboard.z) starboard = v; if (v.y > top.y) top = v; }
}
// BuildProceduralShip: one of 10 designs (0 needle, 1 delta, 2 twin-boom, 3 wedge, 4 gunboat, 5 carrier, 6 hauler, 7 interceptor, 8 cruiser, 9 saucer) in 4 variants; the seed varies the proportions.  The hull is
// lofted from 10 superelliptic cross-sections (round, not boxy) with a canopy, swept tapered wings with gun barrels, nacelles or cargo pods and glowing engine nozzles; the nozzles are returned in `engines`
// (local space, nose along +X) so a renderer can animate a flame at each.  Hull panels take the instance colour.
void BuildProceduralShip(int design, int variant, unsigned seed, Mesh& m, std::vector<FleetEngine>& eng)
{
    struct P { float len, wid, hgt, noseK, tailF; int wingPairs; float span, chord, sweep, thick; int pods; float podR; int guns; };
    static const P kP[10] = {
        { 1.00f, 0.070f, 0.060f, 1.0f, 0.70f, 1, 0.22f, 0.30f,  0.12f, 0.012f, 0, 0.00f, 2 },     // 0 needle
        { 0.85f, 0.100f, 0.050f, 1.0f, 0.90f, 1, 0.42f, 0.65f,  0.35f, 0.020f, 0, 0.00f, 2 },     // 1 delta
        { 0.80f, 0.090f, 0.070f, 0.9f, 0.80f, 1, 0.34f, 0.25f,  0.00f, 0.015f, 2, 0.04f, 2 },     // 2 twin-boom
        { 0.95f, 0.200f, 0.090f, 0.8f, 1.00f, 1, 0.30f, 0.45f,  0.20f, 0.030f, 0, 0.00f, 2 },     // 3 wedge
        { 1.05f, 0.130f, 0.100f, 0.7f, 0.90f, 2, 0.26f, 0.28f,  0.10f, 0.020f, 2, 0.045f, 4 },    // 4 gunboat
        { 1.10f, 0.260f, 0.120f, 0.55f, 1.0f, 1, 0.20f, 0.55f,  0.00f, 0.040f, 2, 0.06f, 0 },     // 5 carrier
        { 1.00f, 0.180f, 0.150f, 0.6f, 1.00f, 0, 0.00f, 0.00f,  0.00f, 0.000f, 4, 0.08f, 0 },     // 6 hauler (pods are cargo)
        { 0.90f, 0.060f, 0.060f, 1.0f, 0.80f, 1, 0.30f, 0.30f, -0.15f, 0.012f, 2, 0.03f, 2 },     // 7 interceptor (forward-swept)
        { 1.20f, 0.100f, 0.090f, 0.9f, 0.90f, 2, 0.22f, 0.20f,  0.15f, 0.018f, 2, 0.04f, 4 },     // 8 cruiser
        { 0.70f, 0.360f, 0.080f, 0.5f, 1.00f, 0, 0.00f, 0.00f,  0.00f, 0.000f, 0, 0.00f, 0 } };   // 9 saucer
    const int arch = (std::clamp)(design, 0, 9), var = variant & 3; P p = kP[arch]; ShipRng rng(seed);
    p.len *= 0.90f + 0.20f * rng.F(); p.wid *= 0.88f + 0.24f * rng.F(); p.hgt *= 0.90f + 0.20f * rng.F(); p.span *= 0.85f + 0.30f * rng.F(); p.chord *= 0.85f + 0.30f * rng.F(); p.sweep += (rng.F() - 0.5f) * 0.12f;
    if (var == 3 && p.wingPairs < 2 && p.span > 0.0f) p.wingPairs = 2; if (var == 2) p.guns += 2;
    constexpr int NR = 10, M = 10; const float PI2 = 6.2831853f; m = Mesh(); eng.clear();
    const MyColor dark(46, 50, 62, 255), metal(118, 126, 144, 255), glass(40, 120, 170, 255), fire(255, 170, 60, 255);
    auto prof = [&](float u)                                                          // the hull's radius factor along its length: a flared tail swelling to the full section, then a taper to the nose
    {
        if (u < 0.35f) return p.tailF + (1.0f - p.tailF) * (u / 0.35f);
        const float w = (u - 0.35f) / 0.65f; return (std::max)(0.06f, 1.0f - std::pow(w, 1.7f * p.noseK) * 0.94f);
    };
    auto sgnpow = [](float v, float e) { return (v < 0.0f ? -1.0f : 1.0f) * std::pow(std::fabs(v), e); };
    for (int i = 0; i < NR; ++i)                                                      // the rings: superellipse sections (exponent 2 / 0.8 = a rounded box), so the hull has no sharp edges
    {
        const float u = static_cast<float>(i) / static_cast<float>(NR - 1), x = (u - 0.5f) * p.len, f = prof(u);
        for (int j = 0; j < M; ++j) { const float th = PI2 * static_cast<float>(j) / static_cast<float>(M); m.verts.push_back(MyVector(x, p.hgt * f * sgnpow(std::sin(th), 0.8f), p.wid * f * sgnpow(std::cos(th), 0.8f))); }
    }
    for (int i = 0; i + 1 < NR; ++i) for (int j = 0; j < M; ++j)
    {
        const int a = i * M + j, b = i * M + (j + 1) % M, c = (i + 1) * M + (j + 1) % M, d = (i + 1) * M + j; const float mid = std::sin(PI2 * (static_cast<float>(j) + 0.5f) / static_cast<float>(M));
        AddFaceAuto(m, { a, b, c, d }, MyVector(0.5f * (m.verts[a].x + m.verts[d].x), 0.0f, 0.0f), mid > 0.35f ? 1.0f : (mid < -0.35f ? 0.70f : 0.86f));
    }
    { std::vector<int> cap0, cap1; for (int j = 0; j < M; ++j) { cap0.push_back(j); cap1.push_back((NR - 1) * M + j); } AddFaceVec(m, cap0, MyVector(0.0f, 0.0f, 0.0f), 0.5f, true, dark); AddFaceVec(m, cap1, MyVector(0.0f, 0.0f, 0.0f), 0.8f); }
    // the canopy: a glass patch on the top of the hull at about 62 % of its length, lying a hair above it
    { const float xc = (0.62f - 0.5f) * p.len, yt = p.hgt * prof(0.62f) + 0.004f, zw = p.wid * prof(0.62f) * 0.35f; const int b0 = static_cast<int>(m.verts.size());
      m.verts.push_back(MyVector(xc + 0.08f, yt, -zw)); m.verts.push_back(MyVector(xc + 0.08f, yt, zw)); m.verts.push_back(MyVector(xc - 0.07f, yt + 0.012f, zw)); m.verts.push_back(MyVector(xc - 0.07f, yt + 0.012f, -zw));
      AddFaceAuto(m, { b0, b0 + 1, b0 + 2, b0 + 3 }, MyVector(xc, 0.0f, 0.0f), 1.0f, true, glass); m.faces.back().depthBias = 0.01f; }
    // the wings: each pair is a four-sided lofted section (leading edge, top, trailing edge, bottom) from the root (buried in the hull) to a tapered tip, swept back by `sweep`
    for (int w = 0; w < p.wingPairs; ++w) for (int sd = -1; sd <= 1; sd += 2)
    {
        const float sg = static_cast<float>(sd), xr = (w == 0 ? -0.04f : -0.20f) * p.len, span = p.span * (1.0f - 0.25f * static_cast<float>(w)), ch = p.chord * (1.0f - 0.2f * static_cast<float>(w)), tc = ch * 0.35f, th = p.thick, dih = 0.05f * span;
        const float z0 = sg * p.wid * 0.75f, z1 = sg * (p.wid + span), xt = xr - p.sweep * span; const int b0 = static_cast<int>(m.verts.size());
        m.verts.push_back(MyVector(xr + ch * 0.5f, 0.0f, z0)); m.verts.push_back(MyVector(xr, th, z0)); m.verts.push_back(MyVector(xr - ch * 0.5f, 0.0f, z0)); m.verts.push_back(MyVector(xr, -th * 0.6f, z0));
        m.verts.push_back(MyVector(xt + tc * 0.5f, dih, z1)); m.verts.push_back(MyVector(xt, dih + th * 0.6f, z1)); m.verts.push_back(MyVector(xt - tc * 0.5f, dih, z1)); m.verts.push_back(MyVector(xt, dih - th * 0.4f, z1));
        const MyVector in(xr, 0.0f, 0.5f * (z0 + z1));
        AddFaceAuto(m, { b0, b0 + 1, b0 + 5, b0 + 4 }, in, 1.0f); AddFaceAuto(m, { b0 + 1, b0 + 2, b0 + 6, b0 + 5 }, in, 0.92f); AddFaceAuto(m, { b0, b0 + 3, b0 + 7, b0 + 4 }, in, 0.75f); AddFaceAuto(m, { b0 + 3, b0 + 2, b0 + 6, b0 + 7 }, in, 0.68f);
        AddFaceAuto(m, { b0 + 4, b0 + 5, b0 + 6, b0 + 7 }, in, 0.85f);                                                                                              // the tip cap
        if (w == 0 && p.guns > 0 && (p.guns >= 4 || sd > 0 || true)) FleetBar(m, MyVector(xt + tc * 0.5f, dih, z1), MyVector(xt + tc * 0.5f + 0.20f, dih, z1), 0.012f, 0.008f, dark, true);     // a gun barrel on each tip
        if (w == 1 && p.guns >= 4) FleetBar(m, MyVector(xt + tc * 0.5f, dih, z1), MyVector(xt + tc * 0.5f + 0.16f, dih, z1), 0.010f, 0.007f, dark, true);
    }
    // nacelles / cargo pods: long round bars beside the hull (the hauler's four are short and fat, above and below), joined to it by a short strut
    for (int i = 0; i < p.pods; ++i)
    {
        const float sg = (i & 1) ? 1.0f : -1.0f, row = static_cast<float>(i / 2); const bool cargo = arch == 6;
        const float z = sg * (p.wid + p.podR * 1.2f + 0.05f), y = cargo ? (row == 0 ? 0.55f : -0.55f) * p.hgt * 1.4f : 0.0f, xa = cargo ? -0.18f : -0.42f * p.len, xb = cargo ? 0.18f : 0.30f * p.len;
        FleetBar(m, MyVector(xa, y, z), MyVector(xb, y, z), p.podR, p.podR * (cargo ? 0.9f : 0.55f), cargo ? metal : dark, cargo);
        FleetBar(m, MyVector(0.5f * (xa + xb), y * 0.5f, z * 0.45f), MyVector(0.5f * (xa + xb), y, z), 0.014f, 0.012f, metal, true);                  // the strut
        if (!cargo) { const size_t before = m.faces.size(); FleetBar(m, MyVector(xa - 0.01f, y, z), MyVector(xa - 0.07f, y, z), p.podR * 0.8f, p.podR * 0.6f, fire, true); MarkGlow(m, m.faces.size() - before, fire); eng.push_back({ MyVector(xa - 0.07f, y, z), p.podR * 0.8f, 0.18f }); }
    }
    // the main engines at the tail: one or two nozzles with a glowing bar each
    { const int ne = p.wid > 0.15f ? 2 : 1; const float x0 = -0.5f * p.len;
      for (int e = 0; e < ne; ++e) { const float z = ne == 1 ? 0.0f : (e == 0 ? -1.0f : 1.0f) * p.wid * 0.45f, r = (std::min)(0.045f, p.wid * (ne == 1 ? 0.55f : 0.32f) + 0.012f);
          FleetBar(m, MyVector(x0 + 0.04f, 0.0f, z), MyVector(x0 - 0.02f, 0.0f, z), r, r * 1.15f, dark, true);
          const size_t before = m.faces.size(); FleetBar(m, MyVector(x0 - 0.02f, 0.0f, z), MyVector(x0 - 0.07f, 0.0f, z), r * 0.85f, r * 0.55f, fire, true); MarkGlow(m, m.faces.size() - before, fire);
          eng.push_back({ MyVector(x0 - 0.07f, 0.0f, z), r * 0.85f, 0.2f }); } }
    FinalizeMesh(m);
}

}   // namespace MyShapes
