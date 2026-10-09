// ModelPixel.glsl — OpenGL Fragment Shader
// Converted from ModelPixel.hlsl (HLSL 5.0) for OpenGL 3.3+ core profile.
// Full conditional map support, tangent-W bitangent, PCF shadow, gloss map, emissive texture.
// Shader Version: GLSL 330 core
//
// Texture binding slots mirror HLSL register(tN) / register(sN) mapping:
//   t0  diffuseTexture     (sampler2D, binding 0)
//   t1  normalMap          (sampler2D, binding 1)
//   t2  metallicMap        (sampler2D, binding 2)
//   t3  roughnessMap       (sampler2D, binding 3)
//   t4  aoMap              (sampler2D, binding 4)
//   t5  environmentMap     (samplerCube, binding 5)
//   t6  glossMap           (sampler2D, binding 6)
//   t7  emissiveMap        (sampler2D, binding 7)
//   t8  shadowMap          (sampler2DShadow, binding 8)       directional light
//   t9  localShadowMaps    (sampler2DArrayShadow, binding 9)  spot slices + point cube faces
//
// Uniform block binding slots mirror HLSL register(bN):
//   b0  ConstantBuffer     (binding 0)
//   b1  LightBuffer        (binding 1)
//   b2  DebugBuffer        (binding 2)
//   b3  GlobalLightBuffer  (binding 3)
//   b4  MaterialBuffer     (binding 4)
//   b5  EnvBuffer          (binding 5)
//   b6  ShadowBuffer       (binding 6)
//
#version 330 core
#extension GL_ARB_shading_language_420pack : enable    // layout(binding=N) on UBOs — core in 4.2, extension on 3.3

// ── Outputs ──────────────────────────────────────────────────────────────────
out vec4 fragColor;

// ── Inputs from vertex shader ────────────────────────────────────────────────
in vec3 vWorldPosition;
in vec3 vNormal;
in vec2 vTexCoord;
in vec3 vViewDirection;
in vec3 vTangent;
in vec3 vBitangent;

// ── Texture Samplers ─────────────────────────────────────────────────────────
uniform sampler2D       diffuseTexture;     // t0
uniform sampler2D       normalMap;          // t1
uniform sampler2D       metallicMap;        // t2
uniform sampler2D       roughnessMap;       // t3
uniform sampler2D       aoMap;              // t4
uniform samplerCube     environmentMap;     // t5
uniform sampler2D       glossMap;           // t6: gloss/smoothness (roughness = 1 - gloss.r)
uniform sampler2D       emissiveMap;        // t7: emissive texture (multiplied by EmissiveFactor)
uniform sampler2DShadow shadowMap;          // t8: directional shadow depth map (hardware PCF)
uniform sampler2DArrayShadow localShadowMaps; // t9: spot slices + point-light cube faces (hardware PCF)
uniform samplerCube     sceneProbe;         // t10: renderer-owned scene reflection probe (used when the model has no t5 map)
uniform sampler2DArray  planarMap;          // t11: planar mirror renders, one layer per reflection plane (see Lights.h)

#define MAX_LIGHTS        8
#define MAX_GLOBAL_LIGHTS 8
#define PI 3.14159265359

// ── ConstantBuffer (binding 0) ───────────────────────────────────────────────
// MUST match ModelVertex.glsl exactly — field names, types, and padding must be
// identical in every shader stage that declares this block.
layout(std140, binding = 0) uniform ConstantBuffer
{
    mat4  uWorld;
    mat4  uView;
    mat4  uProjection;
    vec3  uCameraPosition;
    float _pad0;
    vec3  uModelScale;
    float _pad1;
};

// ── DebugBuffer (binding 2) ──────────────────────────────────────────────────
// Debug modes:
//   0=Full  1=Normals  2=TextureOnly  3=LightingOnly(diffuse)  4=SpecularOnly
//   5=NoLighting  6=MaterialsOnly  7=ShadowsOnly  8=ReflectionOnly  9=MetallicOnly
layout(std140, binding = 2) uniform DebugBuffer
{
    int   debugMode;
    float _padDB0;
    float _padDB1;
    float _padDB2;
};

// ── LightStruct (matches CPU LightStruct exactly) ────────────────────────────
struct LightStruct
{
    vec3  position;   float _pad0;
    vec3  direction;  float _pad1;
    vec3  color;      float _pad2;
    vec3  ambient;    float intensity;
    vec3  specularColor; float _pad3;

    float range;
    float angle;
    int   type;
    int   lActive;

    int   animMode;
    float animTimer;
    float animSpeed;
    float baseIntensity;

    float animAmplitude;
    float _pad4;
    float innerCone;
    float outerCone;

    float lightFalloff;
    float Shiningness;
    float Reflection;
    float _pad5;

    // Final 16 bytes of padding to match the CPU LightStruct (160 bytes).
    // MUST be a vec4, not float[4]: std140 gives scalar arrays a 16-byte
    // element stride, which would inflate the struct to 208 bytes and
    // misalign every light after index 0.
    vec4 _pad6;
};

// ── LightBuffer (binding 1) ──────────────────────────────────────────────────
layout(std140, binding = 1) uniform LightBuffer
{
    int        numLights;
    float      _padLB0; float _padLB1; float _padLB2;
    LightStruct lights[MAX_LIGHTS];
};

// ── GlobalLightBuffer (binding 3) ────────────────────────────────────────────
layout(std140, binding = 3) uniform GlobalLightBuffer
{
    int        globalLightCount;
    float      _padGL0; float _padGL1; float _padGL2;
    LightStruct globalLights[MAX_GLOBAL_LIGHTS];
};

// ── MaterialBuffer (binding 4) ───────────────────────────────────────────────
layout(std140, binding = 4) uniform MaterialBuffer
{
    vec3  Ka;             float ReceiveShadows;   // 1.0 = this model is darkened by shadow maps
    vec3  Kd;             float PlanarStrength;
    vec3  Ks;             float PlanarIndex;
    float Ns;
    float Metallic;
    float Roughness;
    float ReflectionStrength;
    float useMetallicMap;
    float useRoughnessMap;
    float useAOMap;
    float useEnvMap;
    vec3  EmissiveFactor; float EmissiveStrength;
    float NormalScale;
    float useDiffuseMap;            // 1.0 = sample t0 * Kd; 0.0 = use Kd directly
    float useGlossMap;              // 1.0 = use t6 gloss map (roughness = 1 - gloss.r)
    float useEmissiveMap;           // 1.0 = use t7 emissive texture (* EmissiveFactor)
};

// ── EnvBuffer (binding 5) ────────────────────────────────────────────────────
layout(std140, binding = 5) uniform EnvBuffer
{
    float envIntensity;
    vec3  envTint;
    float mipLODBias;
    float fresnel0;
    vec2  _padE;
};

// ── ShadowBuffer (binding 6) ─────────────────────────────────────────────────
// MUST match ShadowBufferData in Lights.h (2368 bytes, std140).
#define MAX_LOCAL_SHADOW_SLICES 32
#define SHADOW_KIND_NONE        0
#define SHADOW_KIND_DIRECTIONAL 1
#define SHADOW_KIND_SPOT        2
#define SHADOW_KIND_POINT       3

layout(std140, binding = 6) uniform ShadowBuffer
{
    mat4  lightViewProj;                            // Directional light view-projection matrix
    float shadowBias;                               // Directional depth bias to prevent shadow acne
    float shadowStrength;                           // Shadow darkness multiplier [0-1]
    float useShadowMap;                             // 1.0 = directional map at t8 is active
    float shadowMapSize;                            // Directional map resolution for PCF texel offset
    mat4  localViewProj[MAX_LOCAL_SHADOW_SLICES];   // Spot slices + point cube faces (+X -X +Y -Y +Z -Z)
    ivec4 lightShadowInfo[MAX_GLOBAL_LIGHTS];       // Per global light: x = SHADOW_KIND_*, y = first slice
    float localBias;                                // Spot / point depth bias
    float useLocalShadows;                          // 1.0 = t9 array holds valid slices
    float localMapSize;                             // Slice resolution for PCF texel offset
    float displayBrightness;                        // Video settings brightness (1 = neutral)
    float displayContrast;                          // Video settings contrast   (1 = neutral)
    float reflectionScale;      // Scene reflection strength (0 = scene probe off)
    float reflectionMaxMip;     // Highest mip index of the scene probe cube map at unit 5
    float reflectionBlur;       // Extra mip bias for the scene probe lookup
    vec4  planarParams;         // x = planar scale (0 = off), y = distortion, zw = 1/screen size
    vec4  planarPlanes[4];      // Per planar layer: xyz = plane normal (faces the viewer), w = d
};

// Video settings brightness / contrast.  A zeroed ShadowBuffer (not yet uploaded) reads as neutral.
// Clamp to displayable range FIRST: emissive / over-lit pixels sit well above 1.0 and would
// otherwise still clip to white after the adjustment, making the sliders look dead.
vec3 ApplyDisplayAdjust(vec3 c)
{
    float b = (displayBrightness > 0.0) ? displayBrightness : 1.0;
    float k = (displayContrast   > 0.0) ? displayContrast   : 1.0;
    c = clamp(c, 0.0, 1.0);
    c = clamp((c - 0.5) * k + 0.5, 0.0, 1.0);       // contrast around mid-grey
    return c * b;
}

#define LIGHT_TYPE_DIRECTIONAL 0
#define LIGHT_TYPE_POINT       1
#define LIGHT_TYPE_SPOT        2

// ── PBR Helpers ───────────────────────────────────────────────────────────────

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom  = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return a2 / max(denom, 0.001);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

// ── Process Light — returns combined contribution; outDiff/outSpec for debug ─
vec3 ProcessLight(LightStruct light, vec3 N, vec3 V, vec3 worldPos,
                  float roughness, float metallic, vec3 albedo, vec3 F0,
                  out vec3 outDiff, out vec3 outSpec)
{
    outDiff = vec3(0.0);
    outSpec = vec3(0.0);

    if (light.lActive == 0)
        return vec3(0.0);

    vec3  L = vec3(0.0);
    float attenuation = 1.0;

    if (light.type == LIGHT_TYPE_DIRECTIONAL)
    {
        L = normalize(-light.direction);
    }
    else
    {
        vec3  lightVec = light.position - worldPos;
        float dist     = length(lightVec);
        L = normalize(lightVec);

        if (light.type == LIGHT_TYPE_POINT)
        {
            attenuation = clamp(1.0 - dist / light.range, 0.0, 1.0)
                        / (1.0 + dist * dist);
        }
        else if (light.type == LIGHT_TYPE_SPOT)
        {
            vec3  spotDir  = normalize(-light.direction);
            float spotCos  = dot(spotDir, -L);
            float inner    = cos(light.innerCone);
            float outer    = cos(light.outerCone);
            float spotFall = smoothstep(outer, inner, spotCos);
            float distFall = 1.0 / (1.0 + pow(dist, light.lightFalloff));
            attenuation = spotFall * distFall;
        }
    }

    float totalIntensity = max(light.baseIntensity + light.intensity, 0.0);
    attenuation *= totalIntensity;

    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0001)
        return vec3(0.0);

    vec3  H      = normalize(V + L);
    float reflAdj = 1.0 + light.Reflection;
    vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0 * reflAdj);
    float NDF = DistributionGGX(N, H, roughness / (1.0 + light.Shiningness));
    float G   = GeometrySmith(N, V, L, roughness);

    vec3  numerator    = NDF * G * F;
    float denominator  = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
    vec3  specular     = numerator / denominator;

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    vec3 diffuseColor  = kD * albedo / PI;
    vec3 specularColor = specular * light.specularColor * reflAdj;

    outDiff = diffuseColor  * light.color * NdotL * attenuation;
    outSpec = specularColor * light.color * NdotL * attenuation;

    return outDiff + outSpec;
}

// ── PCF Shadow (hardware comparison samplers) ────────────────────────────────
// All helpers return raw visibility: 1.0 = lit, 0.0 = fully shadowed.
// The light matrices produce D3D-style NDC depth in [0,1]; OpenGL writes window
// depth as z * 0.5 + 0.5 (default [-1,1] depth range), so the reference is remapped
// the same way before comparing.  UV: NDC y = -1 is the bottom row in GL (no flip).

// Directional light (unit 8).
float SampleDirShadow(vec3 worldPos)
{
    if (useShadowMap < 0.5)
        return 1.0;

    vec4 lightClip  = lightViewProj * vec4(worldPos, 1.0);
    vec3 projCoords = lightClip.xyz / lightClip.w;

    // Discard outside shadow frustum
    if (projCoords.z > 1.0 || projCoords.z < 0.0)  return 1.0;
    if (abs(projCoords.x) > 1.0)                    return 1.0;
    if (abs(projCoords.y) > 1.0)                    return 1.0;

    vec2  shadowUV     = projCoords.xy * 0.5 + 0.5;
    float currentDepth = (projCoords.z - shadowBias) * 0.5 + 0.5;

    // 3x3 PCF kernel
    float shadow    = 0.0;
    float texelSize = 1.0 / max(shadowMapSize, 1.0);
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            // sampler2DShadow: texture() returns 0 or 1 (hardware PCF comparison)
            shadow += texture(shadowMap, vec3(shadowUV + vec2(float(x), float(y)) * texelSize, currentDepth));
        }
    }
    return shadow / 9.0;
}

// Spot light slice or one point-light cube face (unit 9 array layer).
float SampleLocalShadow(int slice, vec3 worldPos)
{
    if (useLocalShadows < 0.5 || slice < 0 || slice >= MAX_LOCAL_SHADOW_SLICES)
        return 1.0;

    vec4 lightClip = localViewProj[slice] * vec4(worldPos, 1.0);
    if (lightClip.w <= 0.0001)                      // Behind the light
        return 1.0;
    vec3 projCoords = lightClip.xyz / lightClip.w;

    if (projCoords.z > 1.0 || projCoords.z < 0.0)  return 1.0;
    if (abs(projCoords.x) > 1.0)                    return 1.0;
    if (abs(projCoords.y) > 1.0)                    return 1.0;

    vec2  shadowUV     = projCoords.xy * 0.5 + 0.5;
    float currentDepth = (projCoords.z - localBias) * 0.5 + 0.5;
    float layer        = float(slice);

    float shadow    = 0.0;
    float texelSize = 1.0 / max(localMapSize, 1.0);
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            vec2 uv = shadowUV + vec2(float(x), float(y)) * texelSize;
            shadow += texture(localShadowMaps, vec4(uv, layer, currentDepth));
        }
    }
    return shadow / 9.0;
}

// Point-light cube face from the light-to-pixel vector.  Order MUST match
// BuildShadowFrame() in Lights.cpp: +X -X +Y -Y +Z -Z.
int PointShadowFace(vec3 v)
{
    vec3 a = abs(v);
    if (a.x >= a.y && a.x >= a.z)
        return (v.x >= 0.0) ? 0 : 1;
    if (a.y >= a.z)
        return (v.y >= 0.0) ? 2 : 3;
    return (v.z >= 0.0) ? 4 : 5;
}

// Final shadow multiplier for global light `li` (1.0 = unshadowed).
float ShadowForGlobalLight(int li, vec3 lightPos, vec3 worldPos)
{
    if (ReceiveShadows < 0.5)
        return 1.0;

    ivec4 info = lightShadowInfo[li];
    float vis  = 1.0;

    if (info.x == SHADOW_KIND_DIRECTIONAL)
        vis = SampleDirShadow(worldPos);
    else if (info.x == SHADOW_KIND_SPOT)
        vis = SampleLocalShadow(info.y, worldPos);
    else if (info.x == SHADOW_KIND_POINT)
        vis = SampleLocalShadow(info.y + PointShadowFace(worldPos - lightPos), worldPos);

    return mix(1.0 - shadowStrength, 1.0, vis);
}

// ── Fragment Shader Main ─────────────────────────────────────────────────────
void main()
{
    // === Albedo: conditional diffuse map or direct Kd
    vec4 albedoColor;
    if (useDiffuseMap > 0.5)
        albedoColor = texture(diffuseTexture, vTexCoord) * vec4(Kd, 1.0);
    else
        albedoColor = vec4(Kd, 1.0);

    if (debugMode == 2) { fragColor = albedoColor; return; }

    // === Sample PBR maps (GLTF ORM pack: G=roughness, B=metallic)
    float metallicValue  = (useMetallicMap  > 0.5) ? texture(metallicMap,  vTexCoord).b : Metallic;
    float roughnessValue = (useRoughnessMap > 0.5) ? texture(roughnessMap, vTexCoord).g : Roughness;

    // Gloss map overrides roughness: roughness = 1 - gloss.r
    if (useGlossMap > 0.5)
        roughnessValue = 1.0 - texture(glossMap, vTexCoord).r;

    float aoValue = (useAOMap > 0.5) ? texture(aoMap, vTexCoord).r : 1.0;

    if (debugMode == 9) { fragColor = vec4(vec3(metallicValue), 1.0); return; }

    // === Resolve world-space normal
    // NormalScale <= 0 means no normal map — use geometry vertex normal directly.
    vec3 normalWS;
    if (NormalScale <= 0.0)
    {
        normalWS = normalize(vNormal);
    }
    else
    {
        vec3 normalTS = texture(normalMap, vTexCoord).xyz;
        normalTS = normalTS * 2.0 - 1.0;
        normalTS.y = -normalTS.y;           // DirectX → OpenGL G-channel correction
        normalTS.xy *= NormalScale;

        vec3 N = normalize(vNormal);
        vec3 T = normalize(vTangent);
        vec3 B = normalize(vBitangent);     // Already includes tangent.w handedness from VS
        mat3 TBN = mat3(T, B, N);
        normalWS = normalize(TBN * normalTS);
    }

    if (debugMode == 1) { fragColor = vec4(abs(normalWS), 1.0); return; }

    // === View direction
    vec3 V = normalize(uCameraPosition - vWorldPosition);

    // === F0 for Fresnel
    vec3 F0 = mix(vec3(fresnel0), albedoColor.rgb * Ks, metallicValue);

    // === Environment reflection
    vec3 reflDir = reflect(-V, normalWS);
    float roughMip  = roughnessValue * 5.0 + mipLODBias;
    vec3 envRefl = vec3(0.0);
    // Scene probe: models without their own t5 map reflect the renderer's sky cube (unit 10).
    // reflectionScale == 0 means the probe is off (Video settings / resource unavailable).
    bool useProbe = (useEnvMap <= 0.5) && (reflectionScale > 0.0);

    if (useProbe)
    {
        // Roughness picks the mip.  Roughness-aware Fresnel keeps rough dielectrics from
        // over-reflecting at grazing angles.
        float probeMip = clamp(roughnessValue * reflectionMaxMip + reflectionBlur, 0.0, reflectionMaxMip);
        vec3  probe    = textureLod(sceneProbe, reflDir, probeMip).rgb;
        float NoV      = clamp(dot(normalWS, V), 0.0, 1.0);
        float gloss    = 1.0 - roughnessValue;
        vec3  Fr       = F0 + (max(vec3(gloss), F0) - F0) * pow(1.0 - NoV, 5.0);
        envRefl        = probe * envTint * envIntensity * Fr * reflectionScale * aoValue;
    }
    else if (useEnvMap > 0.5)
    {
        envRefl  = textureLod(environmentMap, reflDir, roughMip).rgb;
        envRefl *= envTint * envIntensity;
        vec3 fresnelF = FresnelSchlick(max(dot(normalWS, V), 0.0), F0);
        envRefl *= fresnelF * ReflectionStrength;
    }

    if (debugMode == 8) { fragColor = vec4(envRefl, 1.0); return; }

    // === Ambient
    vec3 ambient    = Ka * albedoColor.rgb * aoValue;
    vec3 finalColor = ambient;

    if ((numLights == 0 && globalLightCount == 0) || debugMode == 5)
    {
        if ((useEnvMap > 0.5 || useProbe) && debugMode != 5)
            finalColor += envRefl;
        if (debugMode != 5)
            finalColor = ApplyDisplayAdjust(finalColor);
        fragColor = vec4(finalColor, albedoColor.a);
        return;
    }

    // === Accumulate lights
    vec3 diffuseAccum  = vec3(0.0);
    vec3 specularAccum = vec3(0.0);
    vec3 directLighting = vec3(0.0);
    vec3 lightAmbient   = vec3(0.0);

    for (int i = 0; i < numLights; ++i)
    {
        if (lights[i].lActive != 0)
            lightAmbient += lights[i].ambient;

        vec3 ld, ls;
        directLighting += ProcessLight(lights[i], normalWS, V, vWorldPosition,
                                       roughnessValue, metallicValue, albedoColor.rgb, F0,
                                       ld, ls);
        diffuseAccum  += ld;
        specularAccum += ls;
    }

    // Global lights - each one attenuated by its own shadow map (if it has one).
    float shadowVisMin = 1.0;                       // For the ShadowsOnly debug view
    for (int gi = 0; gi < globalLightCount; ++gi)
    {
        vec3 ld, ls;
        vec3  contrib = ProcessLight(globalLights[gi], normalWS, V, vWorldPosition,
                                     roughnessValue, metallicValue, albedoColor.rgb, F0,
                                     ld, ls);
        float sh      = ShadowForGlobalLight(gi, globalLights[gi].position, vWorldPosition);
        shadowVisMin  = min(shadowVisMin, sh);
        directLighting += contrib * sh;
        diffuseAccum   += ld * sh;
        specularAccum  += ls * sh;
    }

    // === Debug: separate diffuse / specular visualisation
    if (debugMode == 3) { fragColor = vec4(diffuseAccum, albedoColor.a); return; }
    if (debugMode == 4) { fragColor = vec4(specularAccum, albedoColor.a); return; }
    if (debugMode == 6)
    {
        fragColor = vec4(metallicValue, 1.0 - roughnessValue, aoValue, 1.0);
        return;
    }

    // === ShadowsOnly debug mode (darkest shadow factor across all global lights)
    if (debugMode == 7) { fragColor = vec4(vec3(shadowVisMin), 1.0); return; }

    // === Combine: material ambient + per-light ambient + direct (already shadow-attenuated per light)
    finalColor += lightAmbient * albedoColor.rgb * aoValue
               + directLighting;

    // === Emissive
    vec3 emissiveTex = (useEmissiveMap > 0.5) ? texture(emissiveMap, vTexCoord).rgb : vec3(1.0);
    vec3 emissive = EmissiveFactor * emissiveTex * EmissiveStrength;
    finalColor += emissive;

    // === Environment reflection
    if (useEnvMap > 0.5 || useProbe)
        finalColor += envRefl;

    // Linear output — matches DX12/DX11 which do not apply tone mapping or
    // gamma correction in the shader (the display / sRGB framebuffer handles it).
    // Manual Reinhard + pow(1/2.2) caused over-brightness vs the DX12 reference.
    // === Planar reflection (reflector surfaces only).  The mirror render uses an x-flipped projection,
    // so u is mirrored; gl_FragCoord (origin bottom-left) matches the FBO texture orientation.
    // PlanarIndex picks the layer of the plane this surface reflects.
    if (PlanarStrength > 0.0 && planarParams.x > 0.0)
    {
        int  planarSlice = clamp(int(PlanarIndex + 0.5), 0, 3);
        vec3 planarN     = planarPlanes[planarSlice].xyz;
        vec2 planarUV    = gl_FragCoord.xy * planarParams.zw;
        planarUV.x       = 1.0 - planarUV.x;
        // Ripple: normal-map detail (normalWS vs the geometric normal) along the plane's two tangents.
        vec3 planarT1    = normalize(cross(planarN, (abs(planarN.y) < 0.99) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
        vec3 planarT2    = cross(planarN, planarT1);
        vec3 planarDelta = normalWS - normalize(vNormal);
        planarUV        += vec2(dot(planarDelta, planarT1), dot(planarDelta, planarT2)) * (planarParams.y * 0.25);
        planarUV         = clamp(planarUV, 0.0, 1.0);
        vec3  planarCol  = textureLod(planarMap, vec3(planarUV, float(planarSlice)), 0.0).rgb;
        float planarNoV  = clamp(dot(normalWS, V), 0.0, 1.0);
        float planarW    = clamp(PlanarStrength * planarParams.x * (1.0 - roughnessValue)
                                 * (0.4 + 0.6 * pow(1.0 - planarNoV, 2.0)), 0.0, 1.0);
        finalColor = mix(finalColor, planarCol, planarW);
    }

    finalColor = ApplyDisplayAdjust(finalColor);                // Video settings brightness / contrast
    fragColor = vec4(finalColor, albedoColor.a);
}
