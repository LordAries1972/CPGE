#pragma once

#include "Includes.h"
#include "Renderer.h"
#include "Debug.h"
#include "Vectors.h"
#include "Color.h"

#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
    #include "ConstantBuffer.h"
    using namespace DirectX;
#endif
// On OpenGL/Vulkan, XMFLOAT3 and XMFLOAT4 are aliased to Vector3/Vector4 via Includes.h.
// No DirectX headers required for the OpenGL render pipeline.

enum class LightType {
    DIRECTIONAL,
    POINT,
    SPOT
};

enum class LightAnimMode : int
{
    None = 0,
    Flicker,
    Pulse,
    Strobe
};

//=============================================================================================================
// LightBuffer - Matches layout in ModelPShader.hlsl (register b1)
//=============================================================================================================
constexpr int MAX_LIGHTS = 8;                                     // This must match the Pixel Shader
constexpr int MAX_GLOBAL_LIGHTS = 8;                              // This must match the Pixel Shader

//=============================================================================================================
// Corrected LightStruct - 160 bytes total (Matches ModelPixel.hlsl LightStruct)
// IMPORTANT: Do NOT force 256-byte stride here. HLSL arrays use the HLSL struct size as the stride.
// If CPU stride differs from HLSL stride, lights will be read incorrectly and models can appear black.
//=============================================================================================================
struct LightStruct
{
    XMFLOAT3 position;
    float _pad0;
    XMFLOAT3 direction;
    float _pad1;
    XMFLOAT3 color;
    float _pad2;
    XMFLOAT3 ambient;
    float intensity;
    XMFLOAT3 specularColor;
    float _pad3;

    float range;
    float angle;
    int type;
    int active;

    int animMode;
    float animTimer;
    float animSpeed;
    float baseIntensity;

    float animAmplitude;
    int   castShadows;                          // 1 = this light renders a shadow map (was _pad4; shaders keep it as padding)
    float innerCone;
    float outerCone;

    float lightFalloff;
    float Shiningness;
    float Reflection;
    float _pad5[1];

    // === MANDATORY ADDITION TO MATCH CPU ===
    float _pad6[4]; // Final 16 bytes padding to 160 bytes total
};

struct alignas(16) LightBuffer
{
    int numLights;
    float padding[3];                           // Padding to 16-byte alignment
    LightStruct lights[MAX_LIGHTS];
};

struct alignas(16) GlobalLightBuffer
{
    int numLights;
    float padding[3];                           // Padding to 16-byte alignment
    LightStruct lights[MAX_GLOBAL_LIGHTS];
};

//=============================================================================
// Shadow Mapping - shared by all renderers (DX11 / DX12 / OpenGL / Vulkan)
//=============================================================================
// Budget per frame:
//   1 directional light  -> t8  dirShadowMap     (Texture2D, orthographic)
//   8 spot lights        -> t9  localShadowMaps  (Texture2DArray, 1 slice each)
//   4 point lights       -> t9  localShadowMaps  (6 consecutive slices each: +X -X +Y -Y +Z -Z)
// The caps above are the hard GPU layout limits; config.myConfig.maxSpotShadows /
// maxPointShadows / shadowsEnabled lower them at runtime (Video settings tab).
//
// Matrices held by ShadowFrameData are row-major, row-vector convention
// (clip = float4(worldPos,1) * M) - the same convention as XMMATRIX.
// Upload rule (matches the existing b0 ConstantBuffer handling):
//   HLSL (DX11/DX12) : ShadowPackGPU(..., transposeForHLSL=true)  and mul(v, M) in shader
//   GLSL (GL/Vulkan) : ShadowPackGPU(..., transposeForHLSL=false) and M * v    in shader
//=============================================================================
constexpr int MAX_SPOT_SHADOWS         = 8;
constexpr int MAX_POINT_SHADOWS        = 4;
constexpr int MAX_LOCAL_SHADOW_SLICES  = MAX_SPOT_SHADOWS + MAX_POINT_SHADOWS * 6;   // 32
constexpr int SHADOW_TARGET_DIRECTIONAL = -1;                                         // ShadowView::target for the dir map

enum class ShadowKind : int
{
    None        = 0,
    Directional = 1,
    Spot        = 2,
    Point       = 3
};

// GPU layout of cbuffer ShadowBuffer : register(b6) / layout(std140, binding=6).
// MUST match ModelPixel.hlsl, DX12NativeModelPixel.hlsl, ModelPixel.glsl and the
// Vulkan inline 3D fragment shader exactly.  2368 bytes.
// displayBrightness / displayContrast ride along in b6 because every renderer uploads it
// every frame (even with shadows off); the model pixel shaders apply them to the final colour.
struct alignas(16) ShadowBufferData
{
    float lightViewProj[16];                                // Directional light view-projection
    float shadowBias;                                       // Directional depth bias
    float shadowStrength;                                   // Shadow darkness [0-1]
    float useShadowMap;                                     // 1.0 = t8 holds a valid directional map
    float shadowMapSize;                                    // Directional map resolution (PCF texel size)
    float localViewProj[MAX_LOCAL_SHADOW_SLICES][16];       // Spot slices + point cube faces
    int   lightShadowInfo[MAX_GLOBAL_LIGHTS][4];            // Per global light: x=ShadowKind, y=first slice
    float localBias;                                        // Spot/point depth bias
    float useLocalShadows;                                  // 1.0 = t9 holds valid slices
    float localMapSize;                                     // Local slice resolution (PCF texel size)
    float displayBrightness;                                // config.myConfig.brightness (0.5 - 1.5, 1 = neutral)
    float displayContrast;                                  // config.myConfig.contrast   (0.5 - 1.5, 1 = neutral)
    float reflectionScale;                                  // Scene reflection strength; 0 = scene probe off (see ReflectionFrame)
    float reflectionMaxMip;                                 // Highest mip index of the scene probe cubemap
    float reflectionBlur;                                   // Extra mip bias added to the roughness-driven probe lookup
    float planarParams[4];                                  // x = planar reflection scale (0 = off), y = distortion, z/w = 1/screen width, 1/screen height
    float planarPlanes[4][4];                               // Per planar slice: xyz = plane normal (faces the viewer), w = plane constant d (dot(n,p)+d = 0)
};
static_assert(sizeof(ShadowBufferData) == 2368, "ShadowBufferData must be 2368 bytes to match the shader ShadowBuffer layout");

// One depth-only render target for the shadow pass.
struct ShadowView
{
    float viewProj[16];                                     // Row-major, row-vector
    int   target;                                           // SHADOW_TARGET_DIRECTIONAL or local slice 0..31
    int   kind;                                             // ShadowKind
    float lightPos[3];                                      // Spot/point light position (caster culling)
    float range;                                            // Spot/point range; <= 0 means no range culling
};

// World-space bounding sphere of one model, supplied by the renderer.
struct ShadowCaster
{
    float center[3];
    float radius;
};

// Everything a renderer needs for one frame of shadows.
struct ShadowFrameData
{
    bool  enabled         = false;                          // false = skip the depth pass, shaders see useShadowMap=0
    bool  hasDirectional  = false;
    int   localSlicesUsed = 0;
    int   dirMapSize      = 2048;
    int   localMapSize    = 1024;
    float dirViewProj[16] = {};
    float localViewProj[MAX_LOCAL_SHADOW_SLICES][16] = {};
    int   lightKind[MAX_GLOBAL_LIGHTS]       = {};          // ShadowKind per uploaded global light
    int   lightFirstSlice[MAX_GLOBAL_LIGHTS] = {};
    std::vector<ShadowView> views;                          // Depth passes to render this frame
};

// Resolution helpers (read config.myConfig.shadowQuality).
int  ShadowDirMapSizeFromConfig();
int  ShadowLocalMapSizeFromConfig();

// Plans this frame's shadow views.  `lights` MUST be the same vector (same order) the
// renderer uploads to the GlobalLightBuffer - only the first MAX_GLOBAL_LIGHTS are used.
// `casters` are world-space spheres of every model that casts shadows (used to fit the
// directional frustum).  `camPos` centres the directional frustum when the scene is
// larger than config.myConfig.shadowDistance.
void BuildShadowFrame(const std::vector<LightStruct>& lights,
                      const std::vector<ShadowCaster>& casters,
                      const float camPos[3],
                      ShadowFrameData& out);

// Fills the GPU constant block for this frame.
void ShadowPackGPU(const ShadowFrameData& frame, bool transposeForHLSL, ShadowBufferData& out);

// True when `caster` can affect `view` (range test for spot/point, always true for directional).
bool ShadowViewAffectsCaster(const ShadowView& view, const ShadowCaster& caster);

// out = a * b (row-major, row-vector).  `out` may not alias a or b.
void ShadowMat4Mul(const float a[16], const float b[16], float out[16]);

// Builds the world-space sphere for a model.  world is row-major row-vector (XMMATRIX /
// XMFLOAT4X4 byte layout); scale is the per-vertex scale the vertex shader applies BEFORE
// world (pass 1,1,1 when the scale is already baked into world).
void ShadowMakeWorldSphere(const float localCenter[3], float localRadius,
                           const float scale[3], const float world[16],
                           ShadowCaster& out);

//=============================================================================
// Scene Reflections - shared by all renderers (DX11 / DX12 / OpenGL / Vulkan)
//=============================================================================
// A procedural sky/environment cubemap (with a full mip chain for roughness) is built on the
// CPU from the scene lights and uploaded by each renderer into its own cube texture.  Models
// that have no environment map of their own (useEnvMap == 0) sample it from a SEPARATE slot,
// "sceneProbe": t10 (DX11 / DX12), texture unit 10 (OpenGL), set 2 binding 4 (Vulkan).  It is
// bound once per frame by the renderer, so per-model state is untouched.  Strength / blur /
// max mip travel in the ShadowBuffer (b6) tail floats, which every renderer already uploads
// each frame; reflectionScale == 0 means "probe off".
//
// Face order +X -X +Y -Y +Z -Z.  Texel (px,py) of face f maps to a direction with the standard
// cube-map table used by D3D11, D3D12, OpenGL and Vulkan (identical in all four).
// Pixels are RGBA8 (linear values), mip 0 first, tightly packed rows.
//
// Config (all optional keys in GameConfig.cfg, not checksummed):
//   reflectionsEnabled : master switch (live)
//   reflectionQuality  : 0=Low 64px  1=Medium 128px  2=High 256px  (cube size, needs restart)
//   reflectionStrength : 0.0 - 2.0 multiplier (live)
//   reflectionBlur     : 0.0 - 3.0 extra mip bias, softens all reflections (live)
//   reflectionUpdate   : 0=Slow (1 s)  1=Normal (0.25 s)  2=Every frame  - how fast the probe
//                        follows light changes (live)
//=============================================================================
constexpr int REFLECTION_MAX_MIPS = 9;                           // 256 -> 1 texel

struct ReflectionProbe
{
    int      requestedSize = 0;                                  // Optional: size for the FIRST build (renderer whose texture already exists); 0 = from config
    int      size     = 0;                                       // Face edge in texels at mip 0 (0 = never built)
    int      mipCount = 0;
    uint32_t signature = 0;                                      // Hash of the inputs the pixels were built from
    float    sinceBuild = 0.0f;                                  // Seconds since the last rebuild (throttle)
    uint64_t version  = 0;                                       // Incremented on every rebuild; renderers compare to re-upload
    std::vector<uint8_t> data[6][REFLECTION_MAX_MIPS];           // [face][mip] RGBA8
};

// Per-frame state read by the renderers' material setup and by ShadowPackGPU.
struct ReflectionFrame
{
    bool  active  = false;                                       // Scene probe is bound at t5 for models without their own map
    float scale   = 1.0f;                                        // config reflectionStrength
    float maxMip  = 0.0f;
    float blur    = 0.0f;
};
extern ReflectionFrame g_reflectionFrame;

int  ReflectionProbeSizeFromConfig();                            // 64 / 128 / 256 from config reflectionQuality
int  ReflectionMipCount(int size);                               // log2(size) + 1, capped at REFLECTION_MAX_MIPS
inline int ReflectionMipSize(int size, int mip) { int s = size >> mip; return s < 1 ? 1 : s; }

// Rebuilds `probe` when it has never been built, or the lights changed and the throttle allows.
// The cube size is taken from the config at the first build and then kept.  Returns true when
// the pixel data changed and must be (re)uploaded.
// `lights` is the same vector the renderer uploads to the GlobalLightBuffer.
bool ReflectionProbeUpdate(const std::vector<LightStruct>& lights, float deltaTime, ReflectionProbe& probe);

// Publishes g_reflectionFrame for this frame.  `resourceReady` = the renderer owns a valid cube
// texture for `probe`.  Call once per frame BEFORE ShadowPackGPU / material setup.
void ReflectionFrameSet(const ReflectionProbe& probe, bool resourceReady);

//-----------------------------------------------------------------------------
// Live scene capture into the reflection cube (config reflectionLive)
//-----------------------------------------------------------------------------
// The renderers also keep a second cube (the "capture cube") that holds the real scene seen from the
// camera: each captured face is first filled with the matching face of the sky probe, then every model is
// drawn over it with a 90 degree camera, and after the sixth face of a cycle the mip chain is regenerated.
// One face is captured per frame (a cycle takes 6 frames) and a new cycle starts after the interval of
// config reflectionUpdate (1 s / 0.25 s / immediately).  Until the first cycle completes (ready == false)
// the sky cube stays bound at t10.  While a face is being drawn the SKY cube is bound instead, so the capture
// target is never sampled and written at once; planar reflection is switched off for those draws.
struct ReflectionCapture
{
    bool  ready      = false;                            // A full cycle completed: bind the capture cube
    bool  cycling    = false;
    int   nextFace   = 0;                                // 0-5, face order +X -X +Y -Y +Z -Z
    float sinceCycle = 0.0f;
    float origin[3]  = { 0.0f, 0.0f, 0.0f };             // Capture point (camera position when the cycle started)
};
extern ReflectionCapture g_reflectionCapture;

// Advances the schedule.  Returns true when `face` must be captured this frame; lastFace is true on the
// sixth face of a cycle (regenerate mips, then set g_reflectionCapture.ready).
bool ReflectionCaptureNext(float deltaTime, const float cameraPos[3], int& face, bool& lastFace);
void ReflectionCaptureReset();                           // Renderer teardown / live capture switched off

// Camera for one cube face (row-vector float[16], see the Planar helpers).  90 degree fov, aspect 1.
// zeroToOne: projection depth [0,1] (DX, Vulkan) instead of [-1,1] (OpenGL).  flipY: the API's render
// target row order is opposite to the cube table's (OpenGL, Vulkan) - projection Y is negated, which also
// reverses the winding, so cull modes must be off while capturing.
void ReflectionCaptureFaceCamera(int face, const float origin[3], float nearZ, float farZ,
                                 bool zeroToOne, bool flipY, float outView[16], float outProj[16]);

//=============================================================================
// Planar Reflections - shared by all renderers (DX11 / DX12 / OpenGL / Vulkan)
//=============================================================================
// Floors / water / mirrors show a real mirrored render of the scene.  A model is a "planar
// reflector" when ModelInfo::planarReflector is set OR its name contains one of the tags
// "_mirror", "_water", "_planar" (case-insensitive) - an artist can tag a mesh in Blender.
//
// Planes: the reflecting surface of each reflector is found from its mesh (the dominant flat
// facing, see ModelComputePlanarLocalPlane) and may have ANY orientation and height.  Reflectors on
// the same plane share one render; up to MAX_PLANAR_PLANES (config planarMaxPlanes) different planes
// are rendered per frame, each into its own slice of a texture array.  A reflector seen from behind
// is skipped.  Each reflector's slice index travels in MaterialBuffer (PlanarIndex).
//
// Per frame, in this order:
//   1. PlanarBeginPlan()                       - clear the plane list
//   2. ModelPlanarRegister() for every reflector  (world matrices of this frame)
//   3. PlanarFrameBegin()                      - decides active / re-render; BEFORE the shadow pass, so
//                                                ShadowPackGPU (b6) already knows the planes this frame
//   4. after the shadow pass: for each plane i, PlanarBuildCamera() and a mirror render of every
//      non-reflector model into slice i (oblique near plane on the plane, x-flipped projection so
//      the mirror's winding flip cancels), then g_planarFrame.hasImage = true
//   5. bind the array as "planarMap" (t11 DX, unit 11 GL, set 2 binding 5 Vulkan)
// Reflector pixels sample slice PlanarIndex with uv = (1 - fragX/W, fragY/H) (the x flip above),
// faded by roughness and a Fresnel-like view term, rippled by the normal-map detail.
//
// Matrix convention for these helpers: raw float[16] in ROW-VECTOR layout (clip = [p 1] * V * P).
// XMMATRIX bytes, and glm::mat4 bytes, are both already in that layout.
//
// Config (optional keys in GameConfig.cfg, not checksummed):
//   planarEnabled    : master switch (live)
//   planarQuality    : 0=640x360  1=960x540  2=1280x720 render target size (needs restart)
//   planarStrength   : 0.0 - 1.0 global multiplier (live)
//   planarDistortion : 0.0 - 1.0 normal-map ripple of the reflection (live)
//   planarUpdate     : 0=every frame  1=every 2nd frame  2=every 3rd frame (live)
//   planarMaxPlanes  : 1 - 4 distinct planes rendered per frame (live)
//=============================================================================
constexpr int MAX_PLANAR_PLANES = 4;

struct PlanarPlane
{
    float n[3] = { 0.0f, 1.0f, 0.0f };                       // Unit normal, pointing to the side the viewer is on
    float d    = 0.0f;                                       // dot(n, p) + d == 0 on the plane
};

struct PlanarFrame
{
    bool        active          = false;                     // Reflectors sample the planar array this frame
    bool        renderThisFrame = false;                     // Re-render the mirror passes this frame
    bool        hasImage        = false;                     // The mirror passes have completed at least once
    int         planeCount      = 0;
    PlanarPlane planes[MAX_PLANAR_PLANES];
    int         frameCounter    = 0;
    uint32_t    renderedSignature = 0;                       // Plane hash at the last re-render
    float       screenW         = 1.0f;
    float       screenH         = 1.0f;
};
extern PlanarFrame g_planarFrame;

int  PlanarWidthFromConfig();                                // 640 / 960 / 1280
int  PlanarHeightFromConfig();                               // 360 / 540 / 720

// True when `name` carries a reflector tag (_mirror / _water / _planar), case-insensitive.
bool PlanarNameIsReflector(const std::wstring& name);

// World plane of a mesh surface given in local space: point pLocal, normal nLocal.  `scale` is applied
// first, then `world` (row-vector, row-major float[16]).  outN is a unit normal.
void PlanarWorldPlane(const float nLocal[3], const float pLocal[3], const float scale[3], const float world[16],
                      float outN[3], float* outD);

// Clears the plane list (start of the per-frame planning).
void PlanarBeginPlan();

// Adds a plane (n unit, d) or merges it into a coplanar one already listed.  Returns the slice index,
// or -1 when planar reflections are off or MAX(config planarMaxPlanes) planes are already in use.
int  PlanarRegisterPlane(const float n[3], float d);

// Builds the mirrored camera for the plane (n, d).  `zeroToOne` = projection depth range is [0,1]
// (DX, Vulkan) instead of [-1,1] (OpenGL).  outCamPos is the reflected eye (use it for shading).
void PlanarBuildCamera(const float view[16], const float proj[16], const float camPos[3],
                       const float n[3], float d, bool zeroToOne,
                       float outView[16], float outProj[16], float outCamPos[3]);

// Per-frame decision after every reflector was registered.  `resourceReady` = the renderer owns a valid
// array target; screenW/H = main back buffer size in pixels.  Sets g_planarFrame (read by ShadowPackGPU and
// the material setup) and returns renderThisFrame.  `active` is true on every frame that has planes, because
// the mirror passes of THIS frame run before any reflector is drawn.
bool PlanarFrameBegin(bool resourceReady, float screenW, float screenH);

//=============================================================================
// Per-Renderer Light Consumption Reference
//=============================================================================
// LightStruct / LightBuffer / GlobalLightBuffer compile on ALL renderers.
// The upload path from CPU to GPU differs per pipeline:
//
//   DX11  — GlobalLightBuffer to b3 (SLOT_GLOBAL_LIGHT_BUFFER) via
//            PSSetConstantBuffers(); LightBuffer to b1 via UpdateModelLighting().
//            AnimateLights(dt) must run in DXRenderFrame BEFORE the upload.
//
//   DX12  — GlobalLightBuffer to root param DX12_ROOT_PARAM_GLOBAL_LIGHT_BUFFER
//            via Map/Unmap on the upload heap (DX12Renderer.cpp).
//            LightBuffer to b1 via UpdateModelLighting() (11on12 context,
//            DX12Models.cpp).  AnimateLights(dt) must run in DX12RenderFrame
//            BEFORE UpdateConstantBuffers().
//
//   OpenGL — GlobalLightBuffer to UBO at GLSL_BINDING_GLOBAL_LIGHT (3) via
//             glBufferSubData + glBindBufferBase (OpenGLRenderer.h/.cpp).
//             Embedded fallback shaders receive per-light data via individual
//             glUniform* calls.  AnimateLights(dt) must run in OpenGLRenderFrame
//             BEFORE GetAllLights().
//
//   Vulkan — GlobalLightBuffer to a per-frame UBO at set=2 binding=0 of the
//             3D pipeline (m_lightUBO[frame], VULKAN_RenderFrame.cpp), same
//             layout and light model as DX b3 / GL binding 3.  AnimateLights(dt)
//             must run in RenderGamePlay BEFORE the UBO upload.
//=============================================================================
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
    // DX path: SLOT_LIGHT_BUFFER (1) and SLOT_GLOBAL_LIGHT_BUFFER (3)
    // are defined in Includes.h and consumed by Models.cpp / DXRenderFrame.cpp /
    // DX12Renderer.cpp / DX12Models.cpp.
#elif defined(__USE_OPENGL__)
    // OpenGL path: GLSL_BINDING_GLOBAL_LIGHT (3) is declared in OpenGLRenderer.h.
    // The UBO is bound via glBindBufferBase and updated per frame in
    // OpenGLRenderFrame.cpp with glBufferSubData before the model draw loop.
#elif defined(__USE_VULKAN__)
    // Vulkan path: GlobalLightBuffer is copied into the per-frame set=2 UBO
    // (VULKAN_RenderFrame.cpp RenderGamePlay); ShadowBufferData into the set=2
    // shadow UBO by RenderShadowPassVK.  LightBuffer (per-model) is CPU-side only.
#endif

//=============================================================================
// Light Class Declaration
//=============================================================================
class Light {
public:
    Light(const std::string& name, LightStruct myLight);

    void SetPosition(const XMFLOAT3& pos);
    void SetDirection(const XMFLOAT3& dir);
    void SetColor(const XMFLOAT3& color);
    void SetAmbient(const XMFLOAT3& amb);
    void SetIntensity(float intensity);
    void SetRange(float range);
    void SetAngle(float angle);
    void SetActive(bool state);

    LightStruct GetStruct() const;

private:
    LightStruct data;
};

class LightsManager {
public:
    void CreateLight(const std::wstring& name, LightStruct type);
    void UpdateLight(const std::wstring& name, const LightStruct& updatedData);
    bool GetLight(const std::wstring& name, LightStruct& outData);
    void RemoveLight(const std::wstring& name);
    void AnimateLights(float deltaTime);
    int  GetLightCount();
    void ClearLights();                                            // Remove all lights — call at the start of each scene load

    std::vector<LightStruct> GetAllLights();

private:
    std::unordered_map<std::wstring, LightStruct> lightMap;
    std::mutex mtx;
};
