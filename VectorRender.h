// ================================================================================================
//  VectorRender.h  -  the reusable 3D vector render pipeline (CPU transform -> painter's sort -> batched 2D polygon fill)
//
//  This is the engine-level half of what used to live inside the vector demo in main.cpp.  It contains NO game and NO scene code:
//    * MyVectorRender   : camera, projection, mesh submission (lit, back-face culled, level-of-detail), world polygons, drop shadows,
//                         floor, explosion drawing, the quality governor and the final sort + batched fill (Flush).
//    * MyVectorText     : the vector-stroke text routines - the glyph table, animated banners and 7-segment numbers.
//
//  Everything is drawn through Renderer::DrawFilledPolygon / DrawFilledPolygonBatch, so it runs unchanged on DirectX 11, DirectX 12,
//  OpenGL and Vulkan.  Nothing here touches the window, input or sound.
//
//  Dependencies: Vectors.h (MyVector, Mat3, Rect2 ...), VectorShapes.h (Mesh, Shade), Renderer.h, MathPrecalculation.h.
//
//  Per-frame usage (one thread draws; the scratch buffers are static / preallocated so the loop never allocates after the first frame)
//
//      MyVectorRender& vr = MyVectorRender::Instance();
//      vr.UpdateQualityFromConfig(dt);                      // once a frame: applies the Video tab's Vector options live
//      vr.BeginFrame(W, H);                                 // clears the queues, sets the projection
//      vr.ApplyCamera(target, pitchDeg, yawDeg, distance);
//      vr.SubmitMesh(mesh, Mat3::FromQuat(q), scale, pos, colour);      // as many as you like
//      vr.Flush(renderer);                                  // far-to-near sort, then one batched fill per run of equal colour
//
//  Text (any 2D pass, no BeginFrame needed):
//      MyVectorText::DrawBanner(renderer, L"GAME OVER", cx, cy, 48.0f, colour, time, age);
//      MyVectorText::DrawNumber(renderer, score, 6, x, y, 28.0f, colour, time, 0.0f);
// ================================================================================================
#pragma once

#include "Includes.h"                    // global platform / renderer definitions (must come first)
#include "Vectors.h"
#include "AdaptiveQuality.h"
#include "VectorShapes.h"
#include "Color.h"

#include <cstdint>
#include <vector>

class Renderer;                          // Renderer.h: only referenced by pointer / reference here

namespace MyVR
{
    // ---- fixed capacities: everything is preallocated so the render loop never allocates ----------------------------------------
    constexpr int   kMaxPolyVerts   = 32;            // widest face a mesh may have (a 32-gon disc)
    constexpr int   kMaxPolys       = 32768;         // visible faces per frame; anything past this is silently DROPPED (the last submitted goes missing)
    constexpr int   kMaxMeshVerts   = 10300;         // the densest planet mesh (subdiv 5 = 10242 vertices) fits
    constexpr float kLodCullPx      = 1.2f;          // objects that project smaller than this (radius, pixels) are skipped
    constexpr float kLodBillboardPx = 6.0f;          // ... smaller than this become a single hexagon
    // Explosion drawing (SubmitFx / SubmitCubeBooms) stops once this many faces are queued, so a big blast can never push ships, shots and shadows past kMaxPolys,
    // where the overflow is silently dropped (it looks like flicker and like objects vanishing).  The cheap 6-face debris cubes get a little extra room.
    constexpr size_t kFxPolyBudget     = static_cast<size_t>(kMaxPolys) * 3 / 4;
    constexpr size_t kFxCubePolyBudget = kFxPolyBudget + static_cast<size_t>(kMaxPolys) / 16;

    // Painter's-sort depths for the background layers: floor first, then grid, then shadows, then every lit object (view-space z is always far smaller).
    constexpr float kDepthFloor  = 2.0e9f;
    constexpr float kDepthGrid   = 1.9e9f;
    constexpr float kDepthShadow = 1.0e9f;

    struct Poly    { int first; int count; MyColor color; };         // the points live in pts[first .. first + count)
    struct SortKey { float depth; int index; };

    // Everything that is rebuilt per frame.
    struct FrameState
    {
        MyGeom::Mat3 viewRot;                        // world -> view rotation
        MyGeom::Mat3 worldFromView;                  // view -> world rotation (unprojects screen rays)
        MyVector     camPos;                         // camera position in world space
        MyVector     lightDir;                       // unit vector TOWARD the light, in view space
        float        focal = 1.0f, cx = 0.0f, cy = 0.0f;   // perspective projection (pixels)
        float        w = 0.0f, h = 0.0f;             // client size in pixels
    };

    // The Video tab's "Vector ..." settings turned into per-frame values (see MyVectorRender::UpdateQuality).
    struct Quality
    {
        float lodMul     = 1.0f;       // multiplies the two LOD pixel thresholds: bigger = models turn into dots / vanish sooner
        float particles  = 1.0f;       // share of explosion debris / sparks / smoke that is created (feed it to the pools' SetDensity)
        int   ringStride = 1;          // shock-wave rings are drawn with 24 / ringStride segments (1, 2 or 3)
        bool  shadows    = true;       // drop shadows under models
        bool  fx         = true;       // shock-wave rings, fireballs, smoke on top of the debris cubes
        float detail     = 1.0f;       // the combined 0.1 .. 1 level the values above were derived from (1 = everything)
    };

    // The raw user settings the governor reads (filled from MyConfig by UpdateQualityFromConfig, or by hand for tests / tools).
    struct QualitySettings
    {
        int   quality     = 0;         // 0 Auto (holds targetFps)   1 Low   2 Medium   3 High
        int   targetFps   = 60;        // frame rate the Auto level tries to hold
        float detailDist  = 1.0f;      // 0.5 - 2.0: how far away models keep their detail
        bool  shadows     = true;
        float particles   = 1.0f;      // 0.25 - 1.0
        bool  effects     = true;
    };

    // Scope guards: restore the previous value when they go out of scope.
    class MeshLightScope;
    class MeshFlatScope;
    class MeshAlphaScope;

    // Terrain shadows (side-scrolling games with a column landscape): while `active`, SubmitShadow drops each vertex straight down onto the
    // top of the column under it instead of sliding it along the sun ray onto one flat plane.  No allocation; set by the caller's draw code.
    struct TerrainShadowCtx
    {
        bool  active = false;
        float colW = 0.5f;                                   // width of one terrain column (must equal the width the columns are drawn with)
        float (*top)(float x, float arg) = nullptr;          // height of the terrain at x (the same function the columns use)
        float arg = 0.0f;                                    // passed through to `top`
        float zc = 0.6f, zHalf = 1.3f;                       // z centre of the column tops, and how far a shadow may extend from it
        // > 0: every shadow is drawn at this EXPLICIT painter's depth (use kDepthShadow when the land itself is pinned to the back with drawFirst: the shadows then sit just above it
        // and always behind every object).  0: real view depth with a small bias - right for a land that is depth sorted like everything else.  Real depth lets a spinning ship's
        // faces flip in front of / behind its own shadow from frame to frame, which flickers.
        float depth = 0.0f;
    };

    // The three primitive meshes SubmitFx needs to draw the explosion family: a self-lit sphere (fireballs, smoke, embers, orbs), a cube
    // (shards) and a long thin shape such as an octahedron (sparks, bolts).  Build them once with the MyShapes builders.
    struct FxMeshes
    {
        const MyShapes::Mesh* puff = nullptr;
        const MyShapes::Mesh* cube = nullptr;
        const MyShapes::Mesh* octa = nullptr;
    };

    class MyVectorRender
    {
    public:
        using Mesh = MyShapes::Mesh;

        // One shared instance (the render pipeline is single threaded by design).
        static MyVectorRender& Instance();

        // ---- frame life cycle ---------------------------------------------------------------------------------------------------
        void Reserve();                                      // allocates the queues once (called by BeginFrame as well)
        // Clears the queues and sets the projection for a W x H client area: focal length = h * focalScale pixels (1.05 = a comfortable
        // game view), the projection centre sits centreYFraction of the way down the screen, and the light comes from the up-left, toward the viewer.
        void BeginFrame(float w, float h, float focalScale = 1.05f, float centreYFraction = 0.52f);
        // Sorts far to near and fills every visible face; consecutive polygons of ONE colour become ONE batched fill.
        void Flush(Renderer& r);

        // ---- camera / projection ------------------------------------------------------------------------------------------------
        void ApplyCamera(const MyVector& target, float pitchDeg, float yawDeg, float dist);
        bool ProjectPoint(const MyVector& world, Vector2& out) const;                  // false when the point is behind the camera
        bool ScreenToPlane(float sx, float sy, const MyVector& n, float d, MyVector& out) const;   // eye ray through a pixel vs the plane n.p = d
        MyGeom::Rect2 FloorField(float planeY) const;        // biggest fully visible rectangle of the floor y = planeY (x0..x1 along X, y0..y1 along Z)
        MyGeom::Rect2 FloorFieldMix(float planeY, float T, float B, float L, float R, float mix) const;   // same with margins (fractions) and a width mix
        MyGeom::Rect2 SideField() const;                     // the vertical plane z = 0 (side-scrolling games)

        // ---- mesh submission ----------------------------------------------------------------------------------------------------
        // Transforms one mesh instance, culls it, back-face culls and lights it, and queues the visible faces.
        //   drawFirst : forces the instance to the back of the painter's order (floor).
        //   keepShape : never swap the model for the distant hexagon (small articulated figures whose limbs are separate meshes).
        void SubmitMesh(const Mesh& mesh, const MyGeom::Mat3& modelRot, const MyVector& scale, const MyVector& pos,
                        const MyColor& base, bool drawFirst = false, bool keepShape = false);
        // For a big CONVEX body (a planet): faces of one colour share a depth so they merge into a few batched fills.  depthBias < 0 draws it later.
        void SubmitMeshGrouped(const Mesh& mesh, const MyGeom::Mat3& modelRot, const MyVector& scale, const MyVector& pos,
                               const MyColor& base, float depthBias = 0.0f);
        // A vector drawn as a filled 3D arrow from `origin` along v (length = |v|); `arrowMesh` is a unit +X arrow (MyShapes::BuildArrowMesh).
        void SubmitArrow(const Mesh& arrowMesh, const MyVector& origin, const MyVector& v, const MyColor& color, float thickness = 1.0f);
        // Lit mesh with its drop shadow.
        void SubmitObject(const Mesh& mesh, const MyQuaternion& q, const MyVector& scale, const MyVector& pos,
                          const MyColor& color, float shadowY, const MyColor& shadowColor, float shadowDepth, float shadowBias = 0.0f);

        // ---- level of detail ----------------------------------------------------------------------------------------------------
        // Picks the most detailed of several versions of one model worth drawing at this on-screen size (tiers go coarsest -> finest).
        const Mesh& PickMeshLod(const Mesh* const* tiers, int topTier, float projectedRadiusPx) const;
        // Planet LOD: Low quality -> mn (1280 faces, if it exists); below High -> lo (5120); High -> hi (20480) when the globe is big enough.
        const Mesh& PlanetLod(const Mesh& hi, const Mesh& lo, const Mesh& mn, const MyVector& pos, float radius) const;

        // ---- world-space polygons, shadows, floor -------------------------------------------------------------------------------
        // depth > 0 : explicit painter's depth.   depth <= 0 : average view depth minus `bias` (nearer = drawn later).  Clipped against the near plane.
        void SubmitWorldPoly(const MyVector* pts, int n, const MyColor& color, float depth, float bias = 0.0f);
        // Planar projected shadow of a convex mesh (only light-facing faces are projected: their union is the silhouette).
        void SubmitShadow(const Mesh& mesh, const MyGeom::Mat3& rot, const MyVector& scale, const MyVector& pos, float planeY,
                          const MyColor& color, float depth, float bias = 0.0f);
        // Shadow of a roughly CONVEX many-faced body (rocks, blobs) as its projected convex hull: one or two polygons instead of one per face.
        void SubmitHullShadow(const Mesh& mesh, const MyGeom::Mat3& rot, const MyVector& scale, const MyVector& pos, float planeY,
                              const MyColor& color, float depth, float bias = 0.0f);
        void SubmitTerrainShadow(const Mesh& mesh, const MyGeom::Mat3& rot, const MyVector& scale, const MyVector& pos, const MyColor& color);
        // A cheap octagonal shadow lying on a surface: `centre` on the surface, ax / az unit directions along it, rx / rz radii.
        void SubmitTopEllipse(const MyVector& centre, const MyVector& ax, const MyVector& az, float rx, float rz, const MyColor& color, float bias);
        // Floor slab + faint grid (top-down games); explicit depths keep it behind the action.
        void SubmitFloor(const MyGeom::Rect2& f, const MyColor& floorCol, const MyColor& gridCol, float spacing);
        static MyVector LightTravel();                                                   // direction the light rays travel (down, away from the sun)
        static MyVector ProjectToPlaneY(const MyVector& p, float planeY);               // slide p along the light ray onto y = planeY

        // ---- explosions ---------------------------------------------------------------------------------------------------------
        // Draws the explosion family (rings, fireballs, sparks, smoke, flak) and the signature blast styles.  The pools store each ring's
        // plane per particle (spawn them with CameraFacingNormal() or Up), so nothing else is needed here.  Honours Quality::fx and ringStride.
        void SubmitFx(const MyExplosionFX& fx, const MyBlastStyles& bs, const FxMeshes& meshes);
        // Draws the cube debris pool; shadowY > -1e8 also drops a shadow for every cube onto that plane.
        void SubmitCubeBooms(const MyCubeExplosion& boom, const Mesh& cube, float shadowY = -1.0e9f);
        // The direction the camera looks along, in world space (the normal a camera-facing ring or blast should use).
        MyVector CameraFacingNormal() const { return m_f.worldFromView.Mul(MyVector(0.0f, 0.0f, 1.0f)); }

        // ---- quality governor ---------------------------------------------------------------------------------------------------
        //   Low / Medium / High : fixed detail 0.25 / 0.6 / 1.0.
        //   Auto                : holds the target frame rate.  Above target for 0.3 s -> detail - 0.1; fine for a while -> probes up by 0.05;
        //                         a failed probe doubles the wait before the next one (up to 60 s) so it settles instead of flickering.
        // Call with raw frame time; resetScene refits Auto after changing scene/workload.
        void UpdateQuality(const QualitySettings& s, float dt, bool resetScene = false);
        void UpdateQualityFromConfig(float dt, bool resetScene = false);              // reads MyConfig::vectorQuality / vectorTargetFps / ... (Configuration.h)
        // Hands the particle share to the explosion pools (takes effect on the next blast).
        void ApplyParticleDensity(MyCubeExplosion& boom, MyExplosionFX& fx, MyBlastStyles& bs) const;

        // ---- state --------------------------------------------------------------------------------------------------------------
        FrameState&       Frame()         { return m_f; }
        const FrameState& Frame() const   { return m_f; }
        const Quality&    GetQuality() const { return m_vq; }
        TerrainShadowCtx& TerrainShadow() { return m_tshadow; }
        // Global brightness multiplier for everything SubmitMesh draws (faces with their own fixed paint ignore the instance colour, this does not).
        // Brightness / flat mode / sun are owned by the LIGHT SYSTEM (lightsManager, Lights.h "VECTOR LIGHTING"); these accessors forward to it (defined in VectorRender.cpp so this
        // header does not need Lights.h) and the scope guards below keep working unchanged.
        void  SetMeshLight(float l);
        float GetMeshLight() const;
        // FLAT mode: every face in the instance colour at full brightness - a plain silhouette (hit outlines).
        void  SetMeshFlat(bool f);
        bool  GetMeshFlat() const;
        // Global opacity (0..1) for everything SubmitMesh draws, fixed-paint faces included: fades a model in or out (a ship appearing in the arena).
        void  SetMeshAlpha(float a) { m_meshAlpha = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a); }
        float GetMeshAlpha() const  { return m_meshAlpha; }
        // Per-frame counters (reset by BeginFrame): fill batches issued by Flush, models drawn as a hexagon, instances / faces skipped.
        int StatBatches() const { return m_statBatches; }
        int StatLod() const     { return m_statLod; }
        int StatCulled() const  { return m_statCulled; }
        size_t QueuedPolys() const { return m_polys.size(); }

    private:
        MyVectorRender() = default;
        bool PolyVisible(const Vector2* p, int n) const;     // rejects offscreen and sub-pixel polygons
        void PushPoly(const Vector2* p, int n, const MyColor& c, float depth);

        FrameState            m_f;
        Quality               m_vq;
        TerrainShadowCtx      m_tshadow;
        std::vector<Poly>     m_polys;
        std::vector<SortKey>  m_keys;
        std::vector<Vector2>  m_pts;                         // every polygon's projected points, back to back (no per-polygon zero-fill)
        std::vector<const Vector2*> m_batchPts;              // Flush scratch (kept so Flush never allocates)
        std::vector<int>            m_batchCnt;
        float m_meshAlpha = 1.0f;
        AdaptiveQuality m_autoQuality;                      // measured system/scene fit, remembered between frames
        float m_lodDetail = -1.0f;                           // hysteresis keeps small Auto probes from toggling model LOD
        int   m_statBatches = 0, m_statLod = 0, m_statCulled = 0;
        bool  m_shadowsOn = true;                            // shadow switch with hysteresis (see UpdateQuality)
        bool  m_reserved = false;
    };

    // RAII helpers: darken everything (torch-light) or draw plain silhouettes for one scope.
    class MeshLightScope
    {
    public:
        MeshLightScope(MyVectorRender& vr, float light) : m_vr(vr), m_old(vr.GetMeshLight()) { vr.SetMeshLight(light); }
        ~MeshLightScope() { m_vr.SetMeshLight(m_old); }
        MeshLightScope(const MeshLightScope&) = delete;
        MeshLightScope& operator=(const MeshLightScope&) = delete;
    private:
        MyVectorRender& m_vr; float m_old;
    };
    class MeshFlatScope
    {
    public:
        MeshFlatScope(MyVectorRender& vr, bool flat) : m_vr(vr), m_old(vr.GetMeshFlat()) { vr.SetMeshFlat(flat); }
        ~MeshFlatScope() { m_vr.SetMeshFlat(m_old); }
        MeshFlatScope(const MeshFlatScope&) = delete;
        MeshFlatScope& operator=(const MeshFlatScope&) = delete;
    private:
        MyVectorRender& m_vr; bool m_old;
    };

    class MeshAlphaScope
    {
    public:
        MeshAlphaScope(MyVectorRender& vr, float alpha) : m_vr(vr), m_old(vr.GetMeshAlpha()) { vr.SetMeshAlpha(alpha); }
        ~MeshAlphaScope() { m_vr.SetMeshAlpha(m_old); }
        MeshAlphaScope(const MeshAlphaScope&) = delete;
        MeshAlphaScope& operator=(const MeshAlphaScope&) = delete;
    private:
        MyVectorRender& m_vr; float m_old;
    };

    // Hue wheel built from ONE table lookup (hue 0..1, value 0..1).
    MyColor HueColor(float hue01, float value = 1.0f);
}

// ================================================================================================
//  MyVectorText  -  vector-stroke text: every letter is a handful of line strokes in a unit box (x -0.3..0.3, y -0.5..0.5)
// ================================================================================================
namespace MyVectorText
{
    // One glyph: up to 9 strokes, each (x0, y0, x1, y1) in the unit box.
    struct Glyph { int n; float s[12][4]; };

    // The stroke table for one character: 0-9, A-Z and ':' '-'.  Returns nullptr for spaces and unknown characters (callers skip them).
    const Glyph* GetGlyph(wchar_t c);

    // Table sine with an O(1) wrap, for animations that never stop.
    float SinW(float radians);

    // Formatted text through Renderer::DrawMyText (256 characters at most).
    void TextF(Renderer& r, float x, float y, const MyColor& col, float size, const wchar_t* fmt, ...);

    // Animated banner: letters drop in one after another, then bob, swing about their vertical axis, pulse and flicker like a vector monitor.
    // Each stroke is drawn three times (wide dim glow, body colour, thin white-hot core).
    //   cx, cy = centre of the whole word, h = letter height, time = running clock, age = seconds since the banner first appeared.
    void DrawBanner(Renderer& r, const wchar_t* text, float cx, float cy, float h, const MyColor& col, float time, float age);

    // 7-segment number built from stroke quads, swaying in 3D.  `digits` columns (leading zeros are dimmed), pop 0..1 enlarges it when points score.
    void DrawNumber(Renderer& r, int value, int digits, float x, float y, float h, const MyColor& col, float time, float pop);
}
