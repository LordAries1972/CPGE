#include "Includes.h"
#include "Lights.h"
#include <cfloat>
#include <cstring>
#include <cwctype>

Light::Light(const std::string& name, LightStruct myLight) {
    data = myLight;
    data.active = true;
}

void Light::SetPosition(const XMFLOAT3& pos) { data.position = pos; }
void Light::SetDirection(const XMFLOAT3& dir) { data.direction = dir; }
void Light::SetColor(const XMFLOAT3& color) { data.color = color; }
void Light::SetAmbient(const XMFLOAT3& amb) { data.ambient = amb; }
void Light::SetIntensity(float i) { data.intensity = i; }
void Light::SetRange(float r) { data.range = r; }
void Light::SetAngle(float a) { data.angle = a; }
void Light::SetActive(bool state) { data.active = state; }

LightStruct Light::GetStruct() const {
    return data;
}

// -------------------- LightsManager --------------------

void LightsManager::CreateLight(const std::wstring& name, LightStruct type) {
    std::lock_guard<std::mutex> lock(mtx);
    LightStruct light{};
    light = type;
    light.active = true;
    lightMap[name] = light;
}

void LightsManager::UpdateLight(const std::wstring& name, const LightStruct& updatedData) {
    std::lock_guard<std::mutex> lock(mtx);
    if (lightMap.find(name) != lightMap.end()) {
        lightMap[name] = updatedData;
    }
}

bool LightsManager::GetLight(const std::wstring& name, LightStruct& outData) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = lightMap.find(name);
    if (it != lightMap.end()) {
        outData = it->second;
        return true;
    }
    return false;
}

void LightsManager::RemoveLight(const std::wstring& name) {
    std::lock_guard<std::mutex> lock(mtx);
    lightMap.erase(name);
}

std::vector<LightStruct> LightsManager::GetAllLights() {
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<LightStruct> result;
    for (const auto& [_, light] : lightMap) {
        result.push_back(light);
    }
    return result;
}

int LightsManager::GetLightCount() {
    std::lock_guard<std::mutex> lock(mtx);
    return static_cast<int>(lightMap.size());
}

void LightsManager::ClearLights() {
    std::lock_guard<std::mutex> lock(mtx);
    lightMap.clear();
}

void LightsManager::AnimateLights(float deltaTime)
{
    std::lock_guard<std::mutex> lock(mtx);

    for (auto& [name, light] : lightMap)
    {
        // Ensure baseIntensity is valid if unset (Otherwise we will see black objects) if no other lights been used.
        if (light.baseIntensity == 0.0f && light.intensity > 0.0f)
            light.baseIntensity = light.intensity;

        light.animTimer += deltaTime * light.animSpeed;

        switch (light.animMode)
        {
        case int(LightAnimMode::Pulse):
        {
            float pulse = 0.5f * sinf(light.animTimer * 6.28318530718f) + 0.5f; // [0,1]
            light.intensity = light.baseIntensity + (pulse * light.animAmplitude);
            break;
        }
        case int(LightAnimMode::Flicker):
        {
            float jitter = static_cast<float>(rand()) / RAND_MAX; // [0,1]
            light.intensity = light.baseIntensity + (jitter - 0.5f) * light.animAmplitude;
            break;
        }
        case int(LightAnimMode::Strobe):
        {
            float toggle = fmodf(light.animTimer, 1.0f);
            light.intensity = (toggle > 0.5f) ? light.baseIntensity : 0.0f;
            break;
        }
        case int(LightAnimMode::None):
        default:
            // No animation active — leave intensity at its current value.
            // baseIntensity is the baseline for Pulse/Flicker/Strobe only.
            break;
        }

        if (light.animTimer > 10000.0f)
            light.animTimer = 0.0f;
    }
}

//=============================================================================
// Shadow Mapping planner (shared by all renderers)
// Self-contained float math - no DirectXMath / GLM dependency so the same code
// builds for DX11, DX12, OpenGL and Vulkan (Windows and Linux).
// All matrices: row-major, row-vector (clip = v * M), left-handed, depth 0..1.
//=============================================================================
namespace
{
    struct SV3 { float x, y, z; };

    inline SV3   SAdd(SV3 a, SV3 b)      { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    inline SV3   SSub(SV3 a, SV3 b)      { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    inline SV3   SMul(SV3 a, float s)    { return { a.x * s, a.y * s, a.z * s }; }
    inline float SDot(SV3 a, SV3 b)      { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline SV3   SCross(SV3 a, SV3 b)    { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
    inline float SLen(SV3 a)             { return sqrtf(SDot(a, a)); }
    inline SV3   SNorm(SV3 a, SV3 fallback)
    {
        float l = SLen(a);
        return (l > 1e-6f) ? SMul(a, 1.0f / l) : fallback;
    }

    void SIdentity(float m[16])
    {
        for (int i = 0; i < 16; ++i) m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }

    // Picks an up vector that is not parallel to the view direction.
    SV3 SPickUp(SV3 dir)
    {
        return (fabsf(dir.y) > 0.99f) ? SV3{ 0.0f, 0.0f, 1.0f } : SV3{ 0.0f, 1.0f, 0.0f };
    }

    // XMMatrixLookToLH equivalent.
    void SLookToLH(SV3 eye, SV3 dir, SV3 up, float m[16])
    {
        SV3 z = SNorm(dir, { 0.0f, 0.0f, 1.0f });
        SV3 x = SNorm(SCross(up, z), { 1.0f, 0.0f, 0.0f });
        SV3 y = SCross(z, x);
        m[0]  = x.x; m[1]  = y.x; m[2]  = z.x; m[3]  = 0.0f;
        m[4]  = x.y; m[5]  = y.y; m[6]  = z.y; m[7]  = 0.0f;
        m[8]  = x.z; m[9]  = y.z; m[10] = z.z; m[11] = 0.0f;
        m[12] = -SDot(x, eye); m[13] = -SDot(y, eye); m[14] = -SDot(z, eye); m[15] = 1.0f;
    }

    // XMMatrixOrthographicOffCenterLH equivalent.
    void SOrthoOffCenterLH(float l, float r, float b, float t, float n, float f, float m[16])
    {
        SIdentity(m);
        m[0]  = 2.0f / (r - l);
        m[5]  = 2.0f / (t - b);
        m[10] = 1.0f / (f - n);
        m[12] = (l + r) / (l - r);
        m[13] = (t + b) / (b - t);
        m[14] = n / (n - f);
    }

    // XMMatrixPerspectiveLH expressed with the half-angle tangent (aspect 1).
    void SPerspectiveLH(float tanHalfFov, float n, float f, float m[16])
    {
        for (int i = 0; i < 16; ++i) m[i] = 0.0f;
        const float s = 1.0f / tanHalfFov;
        m[0]  = s;
        m[5]  = s;
        m[10] = f / (f - n);
        m[11] = 1.0f;
        m[14] = -n * f / (f - n);
    }

    // Transform a point by a row-major row-vector matrix (w assumed 1, no divide).
    SV3 STransformPoint(SV3 p, const float m[16])
    {
        return { p.x * m[0] + p.y * m[4] + p.z * m[8]  + m[12],
                 p.x * m[1] + p.y * m[5] + p.z * m[9]  + m[13],
                 p.x * m[2] + p.y * m[6] + p.z * m[10] + m[14] };
    }

    void SCopy16(const float* src, float* dst) { for (int i = 0; i < 16; ++i) dst[i] = src[i]; }

    void STranspose16(const float* src, float* dst)
    {
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                dst[c * 4 + r] = src[r * 4 + c];
    }
}

void ShadowMat4Mul(const float a[16], const float b[16], float out[16])
{
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] +
                             a[r * 4 + 1] * b[1 * 4 + c] +
                             a[r * 4 + 2] * b[2 * 4 + c] +
                             a[r * 4 + 3] * b[3 * 4 + c];
}

int ShadowDirMapSizeFromConfig()
{
    switch (config.myConfig.shadowQuality)
    {
        case 0:  return 1024;
        case 2:  return 4096;
        default: return 2048;
    }
}

int ShadowLocalMapSizeFromConfig()
{
    return (config.myConfig.shadowQuality <= 0) ? 512 : 1024;
}

void ShadowMakeWorldSphere(const float localCenter[3], float localRadius,
                           const float scale[3], const float world[16],
                           ShadowCaster& out)
{
    SV3 p = { localCenter[0] * scale[0], localCenter[1] * scale[1], localCenter[2] * scale[2] };
    SV3 w = STransformPoint(p, world);
    out.center[0] = w.x; out.center[1] = w.y; out.center[2] = w.z;

    // Largest axis scale of the per-vertex scale times the largest basis length of world.
    float s = std::max(fabsf(scale[0]), std::max(fabsf(scale[1]), fabsf(scale[2])));
    float r0 = SLen({ world[0], world[1], world[2] });
    float r1 = SLen({ world[4], world[5], world[6] });
    float r2 = SLen({ world[8], world[9], world[10] });
    out.radius = localRadius * s * std::max(r0, std::max(r1, r2));
}

bool ShadowViewAffectsCaster(const ShadowView& view, const ShadowCaster& caster)
{
    if (view.range <= 0.0f) return true;
    SV3 d = { caster.center[0] - view.lightPos[0], caster.center[1] - view.lightPos[1], caster.center[2] - view.lightPos[2] };
    return (SLen(d) - caster.radius) <= view.range;
}

void BuildShadowFrame(const std::vector<LightStruct>& lights,
                      const std::vector<ShadowCaster>& casters,
                      const float camPos[3],
                      ShadowFrameData& out)
{
    out.enabled         = false;
    out.hasDirectional  = false;
    out.localSlicesUsed = 0;
    out.dirMapSize      = ShadowDirMapSizeFromConfig();
    out.localMapSize    = ShadowLocalMapSizeFromConfig();
    out.views.clear();
    for (int i = 0; i < MAX_GLOBAL_LIGHTS; ++i) { out.lightKind[i] = int(ShadowKind::None); out.lightFirstSlice[i] = 0; }

    if (!config.myConfig.shadowsEnabled)
        return;

    const int maxSpot  = std::clamp(config.myConfig.maxSpotShadows,  0, MAX_SPOT_SHADOWS);
    const int maxPoint = std::clamp(config.myConfig.maxPointShadows, 0, MAX_POINT_SHADOWS);
    const float shadowDistance = static_cast<float>(std::clamp(config.myConfig.shadowDistance, 50.0L, 1000.0L));

    int spotCount = 0, pointCount = 0;
    const int lightCount = std::min(static_cast<int>(lights.size()), MAX_GLOBAL_LIGHTS);

    for (int li = 0; li < lightCount; ++li)
    {
        const LightStruct& L = lights[li];
        if (!L.active || !L.castShadows) continue;

        // ------------------------------------------------------------------ Directional
        if (L.type == int(LightType::DIRECTIONAL))
        {
            if (out.hasDirectional) continue;                       // only one directional shadow

            SV3 dir = SNorm({ L.direction.x, L.direction.y, L.direction.z }, { 0.0f, -1.0f, 0.0f });

            // Bounding sphere of all casters; fall back to a camera-centred sphere.
            SV3   centre = { camPos[0], camPos[1], camPos[2] };
            float radius = shadowDistance;
            if (!casters.empty())
            {
                SV3 mn = {  FLT_MAX,  FLT_MAX,  FLT_MAX };
                SV3 mx = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
                for (const ShadowCaster& c : casters)
                {
                    mn.x = std::min(mn.x, c.center[0] - c.radius); mx.x = std::max(mx.x, c.center[0] + c.radius);
                    mn.y = std::min(mn.y, c.center[1] - c.radius); mx.y = std::max(mx.y, c.center[1] + c.radius);
                    mn.z = std::min(mn.z, c.center[2] - c.radius); mx.z = std::max(mx.z, c.center[2] + c.radius);
                }
                SV3 sc = SMul(SAdd(mn, mx), 0.5f);
                float sr = 0.0f;
                for (const ShadowCaster& c : casters)
                    sr = std::max(sr, SLen(SSub({ c.center[0], c.center[1], c.center[2] }, sc)) + c.radius);

                // Scene larger than the configured shadow distance: keep the camera area only.
                if (sr <= shadowDistance) { centre = sc; radius = std::max(sr, 0.01f); }
            }

            // Rotation-only light view (eye at origin) so the frustum can be texel-snapped
            // in light space - prevents shadow edges shimmering as the camera/scene moves.
            float view[16];
            SLookToLH({ 0.0f, 0.0f, 0.0f }, dir, SPickUp(dir), view);
            SV3 lc = STransformPoint(centre, view);

            const float texel = (2.0f * radius) / static_cast<float>(out.dirMapSize);
            lc.x = floorf(lc.x / texel) * texel;
            lc.y = floorf(lc.y / texel) * texel;

            const float depthPad = radius * 1.05f + 0.01f;
            float proj[16];
            SOrthoOffCenterLH(lc.x - radius, lc.x + radius, lc.y - radius, lc.y + radius,
                              lc.z - depthPad, lc.z + depthPad, proj);
            ShadowMat4Mul(view, proj, out.dirViewProj);

            ShadowView sv{};
            SCopy16(out.dirViewProj, sv.viewProj);
            sv.target = SHADOW_TARGET_DIRECTIONAL;
            sv.kind   = int(ShadowKind::Directional);
            sv.range  = 0.0f;
            out.views.push_back(sv);

            out.hasDirectional     = true;
            out.lightKind[li]      = int(ShadowKind::Directional);
            out.lightFirstSlice[li] = 0;
            continue;
        }

        const SV3   pos   = { L.position.x, L.position.y, L.position.z };
        const float range = (L.range > 0.0f) ? L.range : 1000.0f;
        const float nearZ = std::max(0.05f, range * 0.0005f);

        // ------------------------------------------------------------------ Spot
        if (L.type == int(LightType::SPOT))
        {
            if (spotCount >= maxSpot) continue;
            if (out.localSlicesUsed + 1 > MAX_LOCAL_SHADOW_SLICES) continue;

            SV3 dir = SNorm({ L.direction.x, L.direction.y, L.direction.z }, { 0.0f, -1.0f, 0.0f });

            // outerCone is the half-angle in radians (glTF / FBX importers convert to radians).
            float halfAngle = (L.outerCone > 0.0f) ? L.outerCone : 0.785398f;
            halfAngle = std::clamp(halfAngle * 1.05f, 0.087f, 1.48f);      // ~5 deg .. ~85 deg

            float view[16], proj[16];
            SLookToLH(pos, dir, SPickUp(dir), view);
            SPerspectiveLH(tanf(halfAngle), nearZ, range, proj);

            const int slice = out.localSlicesUsed++;
            ShadowMat4Mul(view, proj, out.localViewProj[slice]);

            ShadowView sv{};
            SCopy16(out.localViewProj[slice], sv.viewProj);
            sv.target = slice;
            sv.kind   = int(ShadowKind::Spot);
            sv.lightPos[0] = pos.x; sv.lightPos[1] = pos.y; sv.lightPos[2] = pos.z;
            sv.range  = range;
            out.views.push_back(sv);

            out.lightKind[li]       = int(ShadowKind::Spot);
            out.lightFirstSlice[li] = slice;
            ++spotCount;
            continue;
        }

        // ------------------------------------------------------------------ Point
        if (L.type == int(LightType::POINT))
        {
            if (pointCount >= maxPoint) continue;
            if (out.localSlicesUsed + 6 > MAX_LOCAL_SHADOW_SLICES) continue;

            // Face order MUST match the shaders' major-axis selection: +X -X +Y -Y +Z -Z.
            static const SV3 kDirs[6] = { { 1,0,0 }, { -1,0,0 }, { 0,1,0 }, { 0,-1,0 }, { 0,0,1 }, { 0,0,-1 } };
            static const SV3 kUps[6]  = { { 0,1,0 }, {  0,1,0 }, { 0,0,-1 }, { 0,0,1 }, { 0,1,0 }, { 0,1,0 } };

            // Slightly wider than 90 degrees so 3x3 PCF taps at a face edge stay inside the face.
            const float tanHalf = 1.0f + 4.0f / static_cast<float>(out.localMapSize);

            const int first = out.localSlicesUsed;
            for (int f = 0; f < 6; ++f)
            {
                float view[16], proj[16];
                SLookToLH(pos, kDirs[f], kUps[f], view);
                SPerspectiveLH(tanHalf, nearZ, range, proj);

                const int slice = out.localSlicesUsed++;
                ShadowMat4Mul(view, proj, out.localViewProj[slice]);

                ShadowView sv{};
                SCopy16(out.localViewProj[slice], sv.viewProj);
                sv.target = slice;
                sv.kind   = int(ShadowKind::Point);
                sv.lightPos[0] = pos.x; sv.lightPos[1] = pos.y; sv.lightPos[2] = pos.z;
                sv.range  = range;
                out.views.push_back(sv);
            }

            out.lightKind[li]       = int(ShadowKind::Point);
            out.lightFirstSlice[li] = first;
            ++pointCount;
            continue;
        }
    }

    out.enabled = out.hasDirectional || (out.localSlicesUsed > 0);
}

void ShadowPackGPU(const ShadowFrameData& frame, bool transposeForHLSL, ShadowBufferData& out)
{
    memset(&out, 0, sizeof(out));

    auto put = [transposeForHLSL](const float* src, float* dst)
    {
        if (transposeForHLSL) STranspose16(src, dst);
        else                  SCopy16(src, dst);
    };

    if (frame.enabled && frame.hasDirectional)
        put(frame.dirViewProj, out.lightViewProj);
    else
        SIdentity(out.lightViewProj);

    for (int s = 0; s < MAX_LOCAL_SHADOW_SLICES; ++s)
    {
        if (frame.enabled && s < frame.localSlicesUsed)
            put(frame.localViewProj[s], out.localViewProj[s]);
        else
            SIdentity(out.localViewProj[s]);
    }

    for (int i = 0; i < MAX_GLOBAL_LIGHTS; ++i)
    {
        out.lightShadowInfo[i][0] = frame.enabled ? frame.lightKind[i] : int(ShadowKind::None);
        out.lightShadowInfo[i][1] = frame.lightFirstSlice[i];
    }

    out.shadowBias      = 0.0015f;
    out.shadowStrength  = 0.8f;
    out.useShadowMap    = (frame.enabled && frame.hasDirectional) ? 1.0f : 0.0f;
    out.shadowMapSize   = static_cast<float>(frame.dirMapSize);
    out.localBias       = 0.0005f;
    out.useLocalShadows = (frame.enabled && frame.localSlicesUsed > 0) ? 1.0f : 0.0f;
    out.localMapSize    = static_cast<float>(frame.localMapSize);

    // Video settings: brightness / contrast (applied at the end of the model pixel shaders).
    out.displayBrightness = static_cast<float>(std::clamp(config.myConfig.brightness, 0.5L, 1.5L));
    out.displayContrast   = static_cast<float>(std::clamp(config.myConfig.contrast,   0.5L, 1.5L));

    // Scene reflections: scale == 0 tells the shaders the probe is off.
    out.reflectionScale  = g_reflectionFrame.active ? g_reflectionFrame.scale : 0.0f;
    out.reflectionMaxMip = g_reflectionFrame.maxMip;
    out.reflectionBlur   = g_reflectionFrame.blur;

    // Planar reflections: x == 0 tells the shaders the planar target is off.
    out.planarParams[0] = g_planarFrame.active
        ? static_cast<float>(std::clamp(config.myConfig.planarStrength, 0.0L, 1.0L)) : 0.0f;
    out.planarParams[1] = static_cast<float>(std::clamp(config.myConfig.planarDistortion, 0.0L, 1.0L));
    out.planarParams[2] = 1.0f / std::max(g_planarFrame.screenW, 1.0f);
    out.planarParams[3] = 1.0f / std::max(g_planarFrame.screenH, 1.0f);
    for (int p = 0; p < MAX_PLANAR_PLANES; ++p)
    {
        const bool used = g_planarFrame.active && p < g_planarFrame.planeCount;
        out.planarPlanes[p][0] = used ? g_planarFrame.planes[p].n[0] : 0.0f;
        out.planarPlanes[p][1] = used ? g_planarFrame.planes[p].n[1] : 1.0f;
        out.planarPlanes[p][2] = used ? g_planarFrame.planes[p].n[2] : 0.0f;
        out.planarPlanes[p][3] = used ? g_planarFrame.planes[p].d    : 0.0f;
    }
}

//=============================================================================
// Scene reflections - procedural sky probe (see Lights.h)
//=============================================================================
ReflectionFrame g_reflectionFrame;

int ReflectionProbeSizeFromConfig()
{
    switch (config.myConfig.reflectionQuality)
    {
        case 0:  return 64;
        case 2:  return 256;
        default: return 128;
    }
}

int ReflectionMipCount(int size)
{
    int n = 1;
    while ((size >> n) >= 1 && n < REFLECTION_MAX_MIPS) ++n;
    return n;
}

namespace
{
    // Inputs the sky is built from.  Quantised so light flicker/pulse does not rebuild every frame.
    struct SkyInputs
    {
        float sunDir[3]   = { 0.0f, 1.0f, 0.0f };               // Direction TOWARDS the sun (-light.direction)
        float sunColor[3] = { 0.0f, 0.0f, 0.0f };
        float ambient[3]  = { 0.0f, 0.0f, 0.0f };
        bool  hasSun      = false;
    };

    SkyInputs GatherSkyInputs(const std::vector<LightStruct>& lights)
    {
        SkyInputs in;
        float best = 0.0f;
        for (const LightStruct& l : lights)
        {
            if (!l.active) continue;
            in.ambient[0] += l.ambient.x; in.ambient[1] += l.ambient.y; in.ambient[2] += l.ambient.z;
            if (l.type != 0) continue;                           // directional only (LIGHT_TYPE_DIRECTIONAL)

            const float inten = std::max(l.baseIntensity + l.intensity, 0.0f);
            const float lum   = inten * (l.color.x + l.color.y + l.color.z);
            if (lum <= best) continue;
            best = lum;

            float dx = -l.direction.x, dy = -l.direction.y, dz = -l.direction.z;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (len < 1e-5f) continue;
            in.sunDir[0] = dx / len; in.sunDir[1] = dy / len; in.sunDir[2] = dz / len;
            in.sunColor[0] = l.color.x * inten; in.sunColor[1] = l.color.y * inten; in.sunColor[2] = l.color.z * inten;
            in.hasSun = true;
        }
        for (int i = 0; i < 3; ++i)
        {
            in.ambient[i]  = std::clamp(in.ambient[i], 0.0f, 1.0f);
            in.sunColor[i] = std::clamp(in.sunColor[i], 0.0f, 4.0f);
        }
        return in;
    }

    uint32_t HashSkyInputs(const SkyInputs& in)
    {
        auto q = [](float v, float steps) { return static_cast<uint32_t>(std::lround(std::clamp(v, -8.0f, 8.0f) * steps) + 4096); };
        uint32_t h = 2166136261u;
        auto mix = [&h](uint32_t v) { h = (h ^ v) * 16777619u; };
        mix(in.hasSun ? 1u : 0u);
        for (int i = 0; i < 3; ++i) { mix(q(in.sunDir[i], 32.0f)); mix(q(in.sunColor[i], 16.0f)); mix(q(in.ambient[i], 16.0f)); }
        return h;
    }

    // Direction of the centre of texel (px,py) on `face` - the standard cube-map table.
    void CubeTexelDir(int face, int px, int py, int size, float out[3])
    {
        const float sc = 2.0f * (px + 0.5f) / static_cast<float>(size) - 1.0f;
        const float tc = 2.0f * (py + 0.5f) / static_cast<float>(size) - 1.0f;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        switch (face)
        {
            case 0: x =  1.0f; z = -sc; y = -tc; break;          // +X
            case 1: x = -1.0f; z =  sc; y = -tc; break;          // -X
            case 2: y =  1.0f; x =  sc; z =  tc; break;          // +Y
            case 3: y = -1.0f; x =  sc; z = -tc; break;          // -Y
            case 4: z =  1.0f; x =  sc; y = -tc; break;          // +Z
            default: z = -1.0f; x = -sc; y = -tc; break;         // -Z
        }
        const float inv = 1.0f / std::sqrt(x * x + y * y + z * z);
        out[0] = x * inv; out[1] = y * inv; out[2] = z * inv;
    }

    // Soft studio-style sky: bright horizon, cooler zenith, dark ground, plus a sun glint
    // taken from the strongest directional light.  Linear colour, clamped to 0-1 by the caller.
    void SkyColor(const SkyInputs& in, const float d[3], float out[3])
    {
        const float up = d[1];
        const float amb = 0.35f + 0.5f * (in.ambient[0] + in.ambient[1] + in.ambient[2]) / 3.0f;

        const float zen[3] = { 0.20f, 0.32f, 0.62f };
        const float hor[3] = { 0.70f, 0.76f, 0.86f };
        const float gnd[3] = { 0.10f, 0.10f, 0.12f };

        float t = std::clamp(std::fabs(up), 0.0f, 1.0f);
        t = std::pow(t, 0.6f);
        for (int i = 0; i < 3; ++i)
        {
            const float sky = hor[i] + (zen[i] - hor[i]) * t;
            const float col = (up >= 0.0f) ? sky : (hor[i] * 0.35f + (gnd[i] - hor[i] * 0.35f) * t);
            out[i] = col * amb;
        }

        if (in.hasSun)
        {
            const float sd = std::max(d[0] * in.sunDir[0] + d[1] * in.sunDir[1] + d[2] * in.sunDir[2], 0.0f);
            const float glint = std::pow(sd, 400.0f) * 4.0f + std::pow(sd, 24.0f) * 0.35f + std::pow(sd, 4.0f) * 0.06f;
            for (int i = 0; i < 3; ++i)
                out[i] += in.sunColor[i] * glint;
        }
    }

    void BuildProbePixels(const SkyInputs& in, ReflectionProbe& probe)
    {
        const int size = probe.size;
        for (int f = 0; f < 6; ++f)
        {
            std::vector<uint8_t>& m0 = probe.data[f][0];
            m0.assign(static_cast<size_t>(size) * size * 4, 255);
            for (int py = 0; py < size; ++py)
            {
                for (int px = 0; px < size; ++px)
                {
                    float d[3], c[3];
                    CubeTexelDir(f, px, py, size, d);
                    SkyColor(in, d, c);
                    uint8_t* p = &m0[(static_cast<size_t>(py) * size + px) * 4];
                    for (int i = 0; i < 3; ++i)
                        p[i] = static_cast<uint8_t>(std::lround(std::clamp(c[i], 0.0f, 1.0f) * 255.0f));
                    p[3] = 255;
                }
            }

            // Box-filter mip chain (smooth sky, so per-face filtering is enough).
            for (int mip = 1; mip < probe.mipCount; ++mip)
            {
                const int ps = ReflectionMipSize(size, mip - 1);
                const int ms = ReflectionMipSize(size, mip);
                const std::vector<uint8_t>& src = probe.data[f][mip - 1];
                std::vector<uint8_t>& dst = probe.data[f][mip];
                dst.assign(static_cast<size_t>(ms) * ms * 4, 255);
                for (int y = 0; y < ms; ++y)
                {
                    for (int x = 0; x < ms; ++x)
                    {
                        const int x0 = std::min(x * 2, ps - 1), x1 = std::min(x * 2 + 1, ps - 1);
                        const int y0 = std::min(y * 2, ps - 1), y1 = std::min(y * 2 + 1, ps - 1);
                        for (int ch = 0; ch < 3; ++ch)
                        {
                            const int sum = src[(static_cast<size_t>(y0) * ps + x0) * 4 + ch] + src[(static_cast<size_t>(y0) * ps + x1) * 4 + ch]
                                          + src[(static_cast<size_t>(y1) * ps + x0) * 4 + ch] + src[(static_cast<size_t>(y1) * ps + x1) * 4 + ch];
                            dst[(static_cast<size_t>(y) * ms + x) * 4 + ch] = static_cast<uint8_t>((sum + 2) / 4);
                        }
                    }
                }
            }
        }
    }
}

bool ReflectionProbeUpdate(const std::vector<LightStruct>& lights, float deltaTime, ReflectionProbe& probe)
{
    if (!config.myConfig.reflectionsEnabled)
        return false;

    probe.sinceBuild += std::max(deltaTime, 0.0f);

    // The cube size is fixed at the first build (quality changes apply after a video restart,
    // when the renderer drops its probe + texture and starts again).
    const bool firstBuild = (probe.size == 0);
    const int  size = !firstBuild ? probe.size
                    : (probe.requestedSize > 0 ? probe.requestedSize : ReflectionProbeSizeFromConfig());
    const SkyInputs in = GatherSkyInputs(lights);
    const uint32_t sig = HashSkyInputs(in);

    if (!firstBuild)
    {
        if (sig == probe.signature) return false;
        static const float kInterval[3] = { 1.0f, 0.25f, 0.0f };
        const float interval = kInterval[std::clamp(config.myConfig.reflectionUpdate, 0, 2)];
        if (probe.sinceBuild < interval) return false;
    }

    probe.size       = size;
    probe.mipCount   = ReflectionMipCount(size);
    probe.signature  = sig;
    probe.sinceBuild = 0.0f;
    BuildProbePixels(in, probe);
    ++probe.version;
    return true;
}

void ReflectionFrameSet(const ReflectionProbe& probe, bool resourceReady)
{
    g_reflectionFrame.active = config.myConfig.reflectionsEnabled && resourceReady && probe.size > 0;
    g_reflectionFrame.scale  = static_cast<float>(std::clamp(config.myConfig.reflectionStrength, 0.0L, 2.0L));
    g_reflectionFrame.maxMip = static_cast<float>(std::max(probe.mipCount - 1, 0));
    g_reflectionFrame.blur   = static_cast<float>(std::clamp(config.myConfig.reflectionBlur, 0.0L, 3.0L));
}

//=============================================================================
// Planar reflections (see "Planar Reflections" in Lights.h)
//=============================================================================
PlanarFrame g_planarFrame;

int PlanarWidthFromConfig()
{
    switch (config.myConfig.planarQuality) { case 0: return 640; case 2: return 1280; default: return 960; }
}

int PlanarHeightFromConfig()
{
    switch (config.myConfig.planarQuality) { case 0: return 360; case 2: return 720; default: return 540; }
}

bool PlanarNameIsReflector(const std::wstring& name)
{
    if (name.empty()) return false;
    std::wstring lower = name;
    for (wchar_t& c : lower) c = static_cast<wchar_t>(std::towlower(c));
    return lower.find(L"_mirror") != std::wstring::npos
        || lower.find(L"_water")  != std::wstring::npos
        || lower.find(L"_planar") != std::wstring::npos;
}

namespace
{
    // out = a * b (row-major, row-vector); out must not alias.
    void PMul(const float a[16], const float b[16], float out[16])
    {
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c]
                               + a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
    }

    // General 4x4 inverse (cofactor expansion).  Returns false when singular.
    bool PInverse(const float m[16], float inv[16])
    {
        float t[16];
        t[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
        t[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
        t[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
        t[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
        t[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
        t[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
        t[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
        t[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
        t[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
        t[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
        t[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
        t[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
        t[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
        t[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
        t[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
        t[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];
        const float det = m[0]*t[0] + m[1]*t[4] + m[2]*t[8] + m[3]*t[12];
        if (std::fabs(det) < 1e-12f) return false;
        const float id = 1.0f / det;
        for (int i = 0; i < 16; ++i) inv[i] = t[i] * id;
        return true;
    }

    float PSign(float v) { return v < 0.0f ? -1.0f : 1.0f; }
}

void PlanarWorldPlane(const float nLocal[3], const float pLocal[3], const float scale[3], const float world[16],
                      float outN[3], float* outD)
{
    // Point: (pLocal * scale) * world  (row-vector, w = 1).
    const float sp[3] = { pLocal[0] * scale[0], pLocal[1] * scale[1], pLocal[2] * scale[2] };
    float wp[3];
    for (int c = 0; c < 3; ++c)
        wp[c] = sp[0] * world[0 * 4 + c] + sp[1] * world[1 * 4 + c] + sp[2] * world[2 * 4 + c] + world[3 * 4 + c];

    // Normal: p' = p * M with M = diag(scale) * W3; a plane normal transforms by the inverse transpose.
    // For a row-vector normal n, n' = n * inverse(M)^T, i.e. n'^T = inverse(M) * n^T.
    float M[9];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            M[r * 3 + c] = scale[r] * world[r * 4 + c];
    const float c00 = M[4] * M[8] - M[5] * M[7], c01 = M[5] * M[6] - M[3] * M[8], c02 = M[3] * M[7] - M[4] * M[6];
    const float det = M[0] * c00 + M[1] * c01 + M[2] * c02;
    float n[3] = { nLocal[0], nLocal[1], nLocal[2] };
    if (std::fabs(det) > 1e-12f)
    {
        const float id = 1.0f / det;
        const float inv[9] = {
            c00 * id, (M[2] * M[7] - M[1] * M[8]) * id, (M[1] * M[5] - M[2] * M[4]) * id,
            c01 * id, (M[0] * M[8] - M[2] * M[6]) * id, (M[2] * M[3] - M[0] * M[5]) * id,
            c02 * id, (M[1] * M[6] - M[0] * M[7]) * id, (M[0] * M[4] - M[1] * M[3]) * id };
        for (int i = 0; i < 3; ++i)
            n[i] = inv[i * 3 + 0] * nLocal[0] + inv[i * 3 + 1] * nLocal[1] + inv[i * 3 + 2] * nLocal[2];
    }
    const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    const float il = len > 1e-12f ? 1.0f / len : 0.0f;
    outN[0] = n[0] * il; outN[1] = n[1] * il; outN[2] = n[2] * il;
    *outD = -(outN[0] * wp[0] + outN[1] * wp[1] + outN[2] * wp[2]);
}

void PlanarBeginPlan()
{
    g_planarFrame.planeCount = 0;
}

int PlanarRegisterPlane(const float n[3], float d)
{
    if (!config.myConfig.planarEnabled) return -1;
    const int cap = std::clamp(config.myConfig.planarMaxPlanes, 1, MAX_PLANAR_PLANES);

    for (int i = 0; i < g_planarFrame.planeCount; ++i)                           // coplanar with an existing one?
    {
        const PlanarPlane& p = g_planarFrame.planes[i];
        const float dotN = p.n[0] * n[0] + p.n[1] * n[1] + p.n[2] * n[2];
        if (dotN > 0.9995f && std::fabs(p.d - d) < 0.05f)
            return i;
    }
    if (g_planarFrame.planeCount >= cap) return -1;

    PlanarPlane& np = g_planarFrame.planes[g_planarFrame.planeCount];
    np.n[0] = n[0]; np.n[1] = n[1]; np.n[2] = n[2]; np.d = d;
    return g_planarFrame.planeCount++;
}

void PlanarBuildCamera(const float view[16], const float proj[16], const float camPos[3],
                       const float n[3], float d, bool zeroToOne,
                       float outView[16], float outProj[16], float outCamPos[3])
{
    // Reflection about the plane (n, d), row-vector: p' = p - 2 (n.p + d) n.
    const float mirror[16] = {
        1 - 2 * n[0] * n[0],   -2 * n[0] * n[1],     -2 * n[0] * n[2],    0,
          -2 * n[1] * n[0],  1 - 2 * n[1] * n[1],    -2 * n[1] * n[2],    0,
          -2 * n[2] * n[0],    -2 * n[2] * n[1],   1 - 2 * n[2] * n[2],   0,
          -2 * d * n[0],       -2 * d * n[1],        -2 * d * n[2],       1 };
    PMul(mirror, view, outView);

    const float cd = n[0] * camPos[0] + n[1] * camPos[1] + n[2] * camPos[2] + d;
    outCamPos[0] = camPos[0] - 2.0f * cd * n[0];
    outCamPos[1] = camPos[1] - 2.0f * cd * n[1];
    outCamPos[2] = camPos[2] - 2.0f * cd * n[2];

    std::memcpy(outProj, proj, sizeof(float) * 16);

    // Oblique near plane.  The world plane (n, d) keeps the viewer side (n.p + d >= 0).  In the mirrored
    // view space the plane vector is C = inverse(View') * (n, d)^T  (row-vector convention).
    float invV[16], invP[16];
    if (PInverse(outView, invV) && PInverse(proj, invP))
    {
        const float cw[4] = { n[0], n[1], n[2], d };
        float C[4];
        for (int r = 0; r < 4; ++r)
            C[r] = invV[r * 4 + 0] * cw[0] + invV[r * 4 + 1] * cw[1] + invV[r * 4 + 2] * cw[2] + invV[r * 4 + 3] * cw[3];

        // Far-plane corner on the plane's side: clip corner (sx, sy, 1, 1) -> view space Q = corner * P^-1.
        const float corner[4] = { PSign(C[0]) * PSign(proj[0]), PSign(C[1]) * PSign(proj[5]), 1.0f, 1.0f };
        float Q[4];
        for (int c = 0; c < 4; ++c)
            Q[c] = corner[0] * invP[0 * 4 + c] + corner[1] * invP[1 * 4 + c] + corner[2] * invP[2 * 4 + c] + corner[3] * invP[3 * 4 + c];

        const float cDotQ = C[0] * Q[0] + C[1] * Q[1] + C[2] * Q[2] + C[3] * Q[3];
        const float c3[4] = { proj[3], proj[7], proj[11], proj[15] };               // column 3 (w)
        const float qDotW = Q[0] * c3[0] + Q[1] * c3[1] + Q[2] * c3[2] + Q[3] * c3[3];
        if (std::fabs(cDotQ) > 1e-8f)
        {
            // Column 2 (z) of the projection is replaced so that clip.z == 0 (D3D) or clip.z == -clip.w (GL) on the plane.
            const float s = (zeroToOne ? 1.0f : 2.0f) * qDotW / cDotQ;
            for (int r = 0; r < 4; ++r)
                outProj[r * 4 + 2] = s * C[r] - (zeroToOne ? 0.0f : c3[r]);
        }
    }

    // Flip clip-space X so the mirror's winding reversal cancels (no cull-mode change needed).
    for (int r = 0; r < 4; ++r)
        outProj[r * 4 + 0] = -outProj[r * 4 + 0];
}

bool PlanarFrameBegin(bool resourceReady, float screenW, float screenH)
{
    g_planarFrame.screenW = screenW;
    g_planarFrame.screenH = screenH;

    const bool enabled = config.myConfig.planarEnabled && resourceReady && g_planarFrame.planeCount > 0;
    if (!enabled)
    {
        g_planarFrame.active          = false;
        g_planarFrame.renderThisFrame = false;
        return false;
    }

    // Hash of the planes (quantised) - a changed set forces a re-render even when the update rate would skip.
    uint32_t sig = 2166136261u;
    auto mix = [&sig](float v) { sig = (sig ^ static_cast<uint32_t>(std::lround(std::clamp(v, -4096.0f, 4096.0f) * 64.0f) + 262144)) * 16777619u; };
    mix(static_cast<float>(g_planarFrame.planeCount));
    for (int i = 0; i < g_planarFrame.planeCount; ++i)
    {
        mix(g_planarFrame.planes[i].n[0]); mix(g_planarFrame.planes[i].n[1]); mix(g_planarFrame.planes[i].n[2]);
        mix(g_planarFrame.planes[i].d);
    }

    const int every = std::clamp(config.myConfig.planarUpdate, 0, 2) + 1;
    const bool changed = !g_planarFrame.hasImage || sig != g_planarFrame.renderedSignature;
    g_planarFrame.renderThisFrame = changed || (g_planarFrame.frameCounter % every == 0);
    ++g_planarFrame.frameCounter;
    if (g_planarFrame.renderThisFrame)
        g_planarFrame.renderedSignature = sig;
    g_planarFrame.active = true;
    return g_planarFrame.renderThisFrame;
}

//=============================================================================
// Live scene capture into the reflection cube (see Lights.h)
//=============================================================================
ReflectionCapture g_reflectionCapture;

void ReflectionCaptureReset()
{
    g_reflectionCapture = ReflectionCapture{};
}

bool ReflectionCaptureNext(float deltaTime, const float cameraPos[3], int& face, bool& lastFace)
{
    ReflectionCapture& c = g_reflectionCapture;
    if (!config.myConfig.reflectionsEnabled || !config.myConfig.reflectionLive)
    {
        c.cycling = false;
        c.ready   = false;
        return false;
    }

    if (!c.cycling)
    {
        c.sinceCycle += std::max(deltaTime, 0.0f);
        static const float kInterval[3] = { 1.0f, 0.25f, 0.0f };
        if (c.sinceCycle < kInterval[std::clamp(config.myConfig.reflectionUpdate, 0, 2)])
            return false;
        c.cycling  = true;
        c.nextFace = 0;
        c.origin[0] = cameraPos[0]; c.origin[1] = cameraPos[1]; c.origin[2] = cameraPos[2];
    }

    face     = c.nextFace;
    lastFace = (face == 5);
    ++c.nextFace;
    if (lastFace)
    {
        c.cycling    = false;
        c.sinceCycle = 0.0f;
    }
    return true;
}

void ReflectionCaptureFaceCamera(int face, const float origin[3], float nearZ, float farZ,
                                 bool zeroToOne, bool flipY, float outView[16], float outProj[16])
{
    // Look / up per face - the D3D cube-map convention (matches CubeTexelDir above).
    static const float kLook[6][3] = { {  1, 0, 0 }, { -1, 0, 0 }, { 0,  1, 0 }, { 0, -1, 0 }, { 0, 0,  1 }, { 0, 0, -1 } };
    static const float kUp  [6][3] = { {  0, 1, 0 }, {  0, 1, 0 }, { 0, 0, -1 }, { 0, 0,  1 }, { 0, 1,  0 }, { 0, 1,  0 } };
    const int f = std::clamp(face, 0, 5);
    const float* fw = kLook[f];
    const float* up = kUp[f];

    // LookAtLH, row-vector: r = up x fw, u' = fw x r.
    float r[3]  = { up[1] * fw[2] - up[2] * fw[1], up[2] * fw[0] - up[0] * fw[2], up[0] * fw[1] - up[1] * fw[0] };
    const float rl = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    for (int i = 0; i < 3; ++i) r[i] /= rl;
    const float u[3] = { fw[1] * r[2] - fw[2] * r[1], fw[2] * r[0] - fw[0] * r[2], fw[0] * r[1] - fw[1] * r[0] };

    const float dr = r[0] * origin[0] + r[1] * origin[1] + r[2] * origin[2];
    const float du = u[0] * origin[0] + u[1] * origin[1] + u[2] * origin[2];
    const float df = fw[0] * origin[0] + fw[1] * origin[1] + fw[2] * origin[2];
    const float v[16] = { r[0], u[0], fw[0], 0,
                          r[1], u[1], fw[1], 0,
                          r[2], u[2], fw[2], 0,
                          -dr,  -du,  -df,   1 };
    std::memcpy(outView, v, sizeof(v));

    // 90 degree fov, aspect 1: x and y scale are both 1.
    std::memset(outProj, 0, sizeof(float) * 16);
    outProj[0]  = 1.0f;
    outProj[5]  = flipY ? -1.0f : 1.0f;
    outProj[11] = 1.0f;                                                    // w = z (left-handed)
    if (zeroToOne)
    {
        outProj[10] = farZ / (farZ - nearZ);
        outProj[14] = -nearZ * farZ / (farZ - nearZ);
    }
    else
    {
        outProj[10] = (farZ + nearZ) / (farZ - nearZ);
        outProj[14] = -2.0f * farZ * nearZ / (farZ - nearZ);
    }
}
