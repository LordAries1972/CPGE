// Enhanced ModelPShader.hlsl with full conditional map support, tangent-W bitangent,
// PCF shadow mapping, gloss map, emissive texture, and corrected debug modes.

// ── Texture Slots ────────────────────────────────────────────────────────────
Texture2D diffuseTexture : register(t0);                // t0: Albedo / base colour map
Texture2D normalMap      : register(t1);                // t1: Tangent-space normal map
Texture2D metallicMap    : register(t2);                // t2: Metallic map (R channel)
Texture2D roughnessMap   : register(t3);                // t3: Roughness map (G channel)
Texture2D aoMap          : register(t4);                // t4: Ambient occlusion map (R channel)
TextureCube environmentMap : register(t5);              // t5: Environment cube map for reflections
Texture2D glossMap       : register(t6);                // t6: Gloss/smoothness map (roughness = 1 - gloss.r)
Texture2D emissiveMap    : register(t7);                // t7: Emissive texture (multiplied by EmissiveFactor)
Texture2D shadowMap      : register(t8);                // t8: Directional light shadow depth map (PCF)
Texture2DArray localShadowMaps : register(t9);          // t9: Spot slices + point-light cube faces (PCF)
TextureCube sceneProbe   : register(t10);               // t10: Renderer-owned scene reflection probe (used when the model has no t5 map)
Texture2DArray planarMap : register(t11);               // t11: Planar mirror renders, one slice per reflection plane (see Lights.h)

// ── Sampler Slots ────────────────────────────────────────────────────────────
SamplerState             samplerState  : register(s0);  // s0: Standard wrap sampler
SamplerState             envSamplerState : register(s1);// s1: Environment map sampler
SamplerComparisonState   shadowSampler : register(s2);  // s2: PCF comparison sampler (LESS_EQUAL)

#define MAX_LIGHTS        8
#define MAX_GLOBAL_LIGHTS 8
#define PI                3.14159265359f

// ── Constant Buffers ─────────────────────────────────────────────────────────

cbuffer ConstantBuffer : register(b0)
{
    matrix worldMatrix;
    matrix viewMatrix;
    matrix projectionMatrix;
    float3 cameraPosition;
    float  padding;
}

// Debug modes:
//   0=Full  1=Normals  2=TextureOnly  3=LightingOnly(diffuse)  4=SpecularOnly
//   5=NoLighting  6=MaterialsOnly  7=ShadowsOnly  8=ReflectionOnly  9=MetallicOnly
cbuffer DebugBuffer : register(b2)
{
    int    debugMode;
    float3 _padDebug;
}

// ── Light Type Definitions ────────────────────────────────────────────────────
#define LIGHT_TYPE_DIRECTIONAL 0
#define LIGHT_TYPE_POINT       1
#define LIGHT_TYPE_SPOT        2

struct LightStruct
{
    float3 position;
    float  _pad0;
    float3 direction;
    float  _pad1;
    float3 color;
    float  _pad2;
    float3 ambient;
    float  intensity;
    float3 specularColor;
    float  _pad3;

    float range;
    float angle;
    int   type;
    int   active;

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
    float _pad5[1];

    float _pad6[4];                             // Final 16 bytes padding to 160 bytes total
};

cbuffer LightBuffer : register(b1)
{
    int        numLights;
    float3     _pad;
    LightStruct lights[MAX_LIGHTS];
};

cbuffer GlobalLightBuffer : register(b3)
{
    int        globalLightCount;
    float3     _padG;
    LightStruct globalLights[MAX_GLOBAL_LIGHTS];
};

// ── Material Buffer (b4) ──────────────────────────────────────────────────────
// Total: 112 bytes (7 × float4 rows)
cbuffer MaterialBuffer : register(b4)
{
    float3 Ka;                                      // Ambient colour
    float  ReceiveShadows;                          // 1.0 = this model is darkened by shadow maps
    float3 Kd;                                      // Diffuse colour (baseColorFactor RGB, linear)
    float  PlanarStrength;                          // Planar reflection mix for reflector surfaces (0 = none)
    float3 Ks;                                      // Specular colour
    float  PlanarIndex;                             // Slice of planarMap this reflector uses (0-3)
    float  Ns;                                      // Specular exponent (shininess)
    float  Metallic;                                // Base metallic factor [0-1]
    float  Roughness;                               // Base roughness factor [0-1]
    float  ReflectionStrength;                      // Global reflection strength multiplier
    float  useMetallicMap;                          // 1.0 = use metallic map (t2)
    float  useRoughnessMap;                         // 1.0 = use roughness map (t3)
    float  useAOMap;                                // 1.0 = use ambient occlusion map (t4)
    float  useEnvMap;                               // 1.0 = use environment map (t5)
    float3 EmissiveFactor;                          // Emissive colour (RGB)
    float  EmissiveStrength;                        // KHR_materials_emissive_strength multiplier
    float  NormalScale;                             // normalTexture.scale (0 = no normal map)
    float  useDiffuseMap;                           // 1.0 = sample t0 * Kd; 0.0 = use Kd directly
    float  useGlossMap;                             // 1.0 = use gloss map at t6 (roughness = 1 - gloss.r)
    float  useEmissiveMap;                          // 1.0 = use emissive texture at t7 (* EmissiveFactor)
};

// ── Environment Settings Buffer (b5) ─────────────────────────────────────────
cbuffer EnvBuffer : register(b5)
{
    float  envIntensity;                            // Environment map intensity
    float3 envTint;                                 // Environment map tint colour
    float  mipLODBias;                              // Mip level bias for environment sampling
    float  fresnel0;                                // Base Fresnel reflectance at normal incidence (F0)
    float2 _padEnv;
}

// ── Shadow Buffer (b6) ────────────────────────────────────────────────────────
// MUST match ShadowBufferData in Lights.h (2368 bytes).
#define MAX_LOCAL_SHADOW_SLICES 32
#define SHADOW_KIND_NONE        0
#define SHADOW_KIND_DIRECTIONAL 1
#define SHADOW_KIND_SPOT        2
#define SHADOW_KIND_POINT       3

cbuffer ShadowBuffer : register(b6)
{
    float4x4 lightViewProj;                         // Directional light view-projection matrix
    float    shadowBias;                            // Directional depth bias to prevent shadow acne
    float    shadowStrength;                        // Shadow darkness multiplier [0-1]
    float    useShadowMap;                          // 1.0 = directional map at t8 is active
    float    shadowMapSize;                         // Directional map resolution for PCF texel offset
    float4x4 localViewProj[MAX_LOCAL_SHADOW_SLICES];// Spot slices + point cube faces (+X -X +Y -Y +Z -Z)
    int4     lightShadowInfo[MAX_GLOBAL_LIGHTS];    // Per global light: x = SHADOW_KIND_*, y = first slice
    float    localBias;                             // Spot / point depth bias
    float    useLocalShadows;                       // 1.0 = t9 array holds valid slices
    float    localMapSize;                          // Slice resolution for PCF texel offset
    float    displayBrightness;                     // Video settings brightness (1 = neutral)
    float    displayContrast;                       // Video settings contrast   (1 = neutral)
    float    reflectionScale;                       // Scene reflection strength (0 = scene probe off)
    float    reflectionMaxMip;                      // Highest mip index of the scene probe cube map at t5
    float    reflectionBlur;                        // Extra mip bias for the scene probe lookup
    float4   planarParams;                          // x = planar scale (0 = off), y = distortion, zw = 1/screen size
    float4   planarPlanes[4];                       // Per planar slice: xyz = plane normal (faces the viewer), w = d
}

// Video settings brightness / contrast.  A zeroed b6 (not yet uploaded) reads as neutral.
// Clamp to displayable range FIRST: emissive / over-lit pixels sit well above 1.0 and would
// otherwise still clip to white after the adjustment, making the sliders look dead.
float3 ApplyDisplayAdjust(float3 c)
{
    float b = (displayBrightness > 0.0f) ? displayBrightness : 1.0f;
    float k = (displayContrast   > 0.0f) ? displayContrast   : 1.0f;
    c = saturate(c);
    c = saturate((c - 0.5f) * k + 0.5f);           // contrast around mid-grey
    return c * b;
}

// ── Pixel Shader Input ────────────────────────────────────────────────────────
struct PS_INPUT
{
    float4 position      : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float3 normal        : TEXCOORD1;
    float2 texCoord      : TEXCOORD2;
    float3 viewDirection : TEXCOORD3;
    float3 tangent       : TEXCOORD4;
    float3 bitangent     : TEXCOORD5;
};

// ── PBR Helper Functions ──────────────────────────────────────────────────────

// Schlick's approximation of Fresnel reflectance
float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

// GGX/Trowbridge-Reitz normal distribution function
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a      = roughness * roughness;
    float a2     = a * a;
    float NdotH  = max(dot(N, H), 0.0f);
    float NdotH2 = NdotH * NdotH;
    float denom  = (NdotH2 * (a2 - 1.0f) + 1.0f);
    denom = PI * denom * denom;
    return a2 / max(denom, 0.001f);
}

// Smith GGX geometry term
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return NdotV / (NdotV * (1.0f - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

// ── Process Light — returns combined contribution; outDiff and outSpec allow
//    separate debug mode visualisation of diffuse and specular components.
// ─────────────────────────────────────────────────────────────────────────────
float3 ProcessLight(LightStruct light, float3 N, float3 V, float3 worldPos,
                    float roughness, float metallic, float3 albedo, float3 F0,
                    out float3 outDiff, out float3 outSpec)
{
    // Outputs initialised unconditionally — no early returns so FXC can prove
    // outDiff/outSpec are set on every path (suppresses X4000 warning).
    outDiff = (float3) 0;
    outSpec = (float3) 0;

    [branch]
    if (light.active != 0)
    {
        float3 L          = (float3) 0;
        float  attenuation = 1.0f;

        // Light direction and distance attenuation
        if (light.type == LIGHT_TYPE_DIRECTIONAL)
        {
            L = normalize(-light.direction);
        }
        else
        {
            float3 lightVec = light.position - worldPos;
            float  dist     = length(lightVec);
            L = normalize(lightVec);

            if (light.type == LIGHT_TYPE_POINT)
            {
                attenuation = saturate(1.0f - dist / light.range) / (1.0f + dist * dist);
            }
            else if (light.type == LIGHT_TYPE_SPOT)
            {
                float3 spotDir    = normalize(-light.direction);
                float  spotCos    = dot(spotDir, -L);
                float  inner      = cos(light.innerCone);
                float  outer      = cos(light.outerCone);
                float  spotFall   = smoothstep(outer, inner, spotCos);
                float  distFall   = 1.0f / (1.0f + pow(dist, light.lightFalloff));
                attenuation = spotFall * distFall;
            }
        }

        float totalIntensity = max(light.baseIntensity + light.intensity, 0.0f);
        attenuation *= totalIntensity;

        float NdotL = max(dot(N, L), 0.0f);

        [branch]
        if (NdotL > 0.0001f)
        {
            float3 H      = normalize(V + L);
            float  reflAdj = 1.0f + light.Reflection;

            float3 F   = FresnelSchlick(max(dot(H, V), 0.0f), F0 * reflAdj);
            float  NDF = DistributionGGX(N, H, roughness / (1.0f + light.Shiningness));
            float  G   = GeometrySmith(N, V, L, roughness);

            float3 numerator   = NDF * G * F;
            float  denominator = 4.0f * max(dot(N, V), 0.0f) * NdotL + 0.001f;
            float3 specular    = numerator / denominator;

            float3 kS = F;
            float3 kD = (float3(1.0f, 1.0f, 1.0f) - kS) * (1.0f - metallic);

            float3 diffuseColor  = kD * albedo / PI;
            float3 specularColor = specular * light.specularColor * reflAdj;

            outDiff = diffuseColor  * light.color * NdotL * attenuation;
            outSpec = specularColor * light.color * NdotL * attenuation;
        }
    }

    return outDiff + outSpec;
}

// ── PCF Shadow Sampling (3x3 kernel) ─────────────────────────────────────────
// All helpers return raw visibility: 1.0 = fully lit, 0.0 = fully in shadow.
// Values between are produced by the PCF kernel averaging.

// Directional light (t8).
float SampleDirShadow(float3 worldPos)
{
    if (useShadowMap < 0.5f)
        return 1.0f;

    // Transform world position to light clip space
    float4 lightClip   = mul(float4(worldPos, 1.0f), lightViewProj);
    if (abs(lightClip.w) < 0.00001f)                // Invalid clip-space W: treat as lit so bad shadow data cannot divide by ~0
        return 1.0f;
    float3 projCoords  = lightClip.xyz / lightClip.w;

    // Discard samples outside the shadow frustum
    if (projCoords.z > 1.0f || projCoords.z < 0.0f)
        return 1.0f;
    if (projCoords.x < -1.0f || projCoords.x > 1.0f)
        return 1.0f;
    if (projCoords.y < -1.0f || projCoords.y > 1.0f)
        return 1.0f;

    // Remap X,Y from NDC [-1,1] to texture [0,1]; flip Y for DX clip convention
    // float2 initialised with constructor — component-wise assignment triggers X4000 in FXC.
    float2 shadowUV = float2(projCoords.x * 0.5f + 0.5f, projCoords.y * -0.5f + 0.5f);

    float currentDepth = projCoords.z - shadowBias;

    // 3x3 PCF kernel — SampleCmpLevelZero returns 1.0 (lit) or 0.0 (shadowed) per tap
    float shadow    = 0.0f;
    float texelSize = 1.0f / max(shadowMapSize, 1.0f);
    [unroll] for (int x = -1; x <= 1; ++x)
    {
        [unroll] for (int y = -1; y <= 1; ++y)
        {
            shadow += shadowMap.SampleCmpLevelZero(
                          shadowSampler,
                          shadowUV + float2((float)x, (float)y) * texelSize,
                          currentDepth);
        }
    }
    return shadow / 9.0f;
}

// Spot light slice or one point-light cube face (t9 array slice).
float SampleLocalShadow(int slice, float3 worldPos)
{
    if (useLocalShadows < 0.5f || slice < 0 || slice >= MAX_LOCAL_SHADOW_SLICES)
        return 1.0f;

    float4 lightClip = mul(float4(worldPos, 1.0f), localViewProj[slice]);
    if (lightClip.w <= 0.0001f)                     // Behind the light
        return 1.0f;
    float3 projCoords = lightClip.xyz / lightClip.w;

    if (projCoords.z > 1.0f || projCoords.z < 0.0f)
        return 1.0f;
    if (projCoords.x < -1.0f || projCoords.x > 1.0f)
        return 1.0f;
    if (projCoords.y < -1.0f || projCoords.y > 1.0f)
        return 1.0f;

    float2 shadowUV     = float2(projCoords.x * 0.5f + 0.5f, projCoords.y * -0.5f + 0.5f);
    float  currentDepth = projCoords.z - localBias;
    float  layer        = (float)slice;

    float shadow    = 0.0f;
    float texelSize = 1.0f / max(localMapSize, 1.0f);
    [unroll] for (int x = -1; x <= 1; ++x)
    {
        [unroll] for (int y = -1; y <= 1; ++y)
        {
            float2 uv = shadowUV + float2((float)x, (float)y) * texelSize;
            shadow += localShadowMaps.SampleCmpLevelZero(shadowSampler, float3(uv, layer), currentDepth);
        }
    }
    return shadow / 9.0f;
}

// Point-light cube face from the light-to-pixel vector.  Order MUST match
// BuildShadowFrame() in Lights.cpp: +X -X +Y -Y +Z -Z.
int PointShadowFace(float3 v)
{
    float3 a = abs(v);
    if (a.x >= a.y && a.x >= a.z)
        return (v.x >= 0.0f) ? 0 : 1;
    if (a.y >= a.z)
        return (v.y >= 0.0f) ? 2 : 3;
    return (v.z >= 0.0f) ? 4 : 5;
}

// Final shadow multiplier for global light `li` (1.0 = unshadowed).
float ShadowForGlobalLight(int li, float3 lightPos, float3 worldPos)
{
    if (ReceiveShadows < 0.5f)
        return 1.0f;

    int4  info = lightShadowInfo[li];
    float vis  = 1.0f;

    [branch]
    if (info.x == SHADOW_KIND_DIRECTIONAL)
        vis = SampleDirShadow(worldPos);
    else if (info.x == SHADOW_KIND_SPOT)
        vis = SampleLocalShadow(info.y, worldPos);
    else if (info.x == SHADOW_KIND_POINT)
        vis = SampleLocalShadow(info.y + PointShadowFace(worldPos - lightPos), worldPos);

    // vis=1 (lit) -> lerp controls darkness: 0 strength = no shadow, 1 strength = full shadow
    return lerp(1.0f - shadowStrength, 1.0f, vis);
}

// ── Pixel Shader Entry Point ──────────────────────────────────────────────────
float4 main(PS_INPUT input) : SV_TARGET
{
    // === Albedo: conditional diffuse map or direct Kd
    float4 albedoColor;
    if (useDiffuseMap > 0.5f)
        albedoColor = diffuseTexture.Sample(samplerState, input.texCoord) * float4(Kd, 1.0f);
    else
        albedoColor = float4(Kd, 1.0f);

    // === TextureOnly debug mode
    if (debugMode == 2)
        return albedoColor;

    // === Sample PBR material maps
    // GLTF 2.0 ORM texture pack: R=AO(unused here), G=roughness, B=metallic
    float metallicValue  = (useMetallicMap  > 0.5f) ? metallicMap.Sample(samplerState,  input.texCoord).b : Metallic;
    float roughnessValue = (useRoughnessMap > 0.5f) ? roughnessMap.Sample(samplerState, input.texCoord).g : Roughness;

    // Gloss map overrides roughness: roughness = 1 - gloss.r
    if (useGlossMap > 0.5f)
        roughnessValue = 1.0f - glossMap.Sample(samplerState, input.texCoord).r;

    float aoValue = (useAOMap > 0.5f) ? aoMap.Sample(samplerState, input.texCoord).r : 1.0f;

    // === MetallicOnly debug mode
    if (debugMode == 9)
        return float4(metallicValue, metallicValue, metallicValue, 1.0f);

    // === Resolve world-space normal
    // NormalScale <= 0 means no normal map present — use geometry vertex normal directly.
    float3 N = normalize(input.normal);
    float3 normalWS;
    if (NormalScale <= 0.0f)
    {
        // No normal map — use raw geometry normal
        normalWS = N;
    }
    else
    {
        // Sample normal map; apply GLTF NormalScale on XY only (Z is reconstructed by GPU)
        // Flip Y for DirectX-convention maps (Blender default exports DX-convention).
        float3 normalTS = normalMap.Sample(samplerState, input.texCoord).xyz;
        normalTS        = normalTS * 2.0f - 1.0f;       // [0,1] -> [-1,1]
        normalTS.y      = -normalTS.y;                  // DirectX -> OpenGL G-channel correction
        normalTS.xy    *= NormalScale;

        // Reconstruct TBN from interpolated VS outputs.
        // The bitangent already incorporates tangent.w handedness from the vertex shader.
        float3 T = normalize(input.tangent);
        float3 B = normalize(input.bitangent);
        float3x3 TBN = float3x3(T, B, N);
        normalWS = normalize(mul(normalTS, TBN));
    }

    // === NormalsOnly debug mode
    if (debugMode == 1)
        return float4(abs(normalWS), 1.0f);

    // === View direction
    float3 V = normalize(cameraPosition - input.worldPosition);

    // === Base Fresnel value F0 (dielectric 0.04; metal: derived from albedo * Ks)
    float3 F0 = lerp(float3(fresnel0, fresnel0, fresnel0), albedoColor.rgb * Ks, metallicValue);

    // === Environment reflection
    float3 reflectionVector = reflect(-V, normalWS);
    float  roughnessMip     = roughnessValue * 5.0f + mipLODBias;
    float3 envReflection    = (float3) 0;

    // Scene probe: models without their own t5 map reflect the renderer's sky cube (t10).
    // reflectionScale == 0 means the probe is off (Video settings / resource unavailable).
    bool useProbe = (useEnvMap <= 0.5f) && (reflectionScale > 0.0f);

    if (useProbe)
    {
        // Roughness picks the mip.  Roughness-aware Fresnel keeps rough dielectrics from
        // over-reflecting at grazing angles.
        float  probeMip = clamp(roughnessValue * reflectionMaxMip + reflectionBlur, 0.0f, reflectionMaxMip);
        float3 probe    = sceneProbe.SampleLevel(envSamplerState, reflectionVector, probeMip).rgb;
        float  NoV      = saturate(dot(normalWS, V));
        float  gloss    = 1.0f - roughnessValue;
        float3 Fr       = F0 + (max(float3(gloss, gloss, gloss), F0) - F0) * pow(1.0f - NoV, 5.0f);
        envReflection   = probe * envTint * envIntensity * Fr * reflectionScale * aoValue;
    }
    else if (useEnvMap > 0.5f)
    {
        envReflection  = environmentMap.SampleLevel(envSamplerState, reflectionVector, roughnessMip).rgb;
        envReflection *= envTint * envIntensity;
        float3 fresnelFactor = FresnelSchlick(max(dot(normalWS, V), 0.0f), F0);
        envReflection *= fresnelFactor * ReflectionStrength;
    }

    // === ReflectionOnly debug mode
    if (debugMode == 8)
        return float4(envReflection, 1.0f);

    // === Ambient (material Ka * albedo * AO)
    float3 ambient    = Ka * albedoColor.rgb * aoValue;
    float3 finalColor = ambient;

    // === Early exit: NoLighting debug mode or no lights
    if ((numLights == 0 && globalLightCount == 0) || debugMode == 5)
    {
        if ((useEnvMap > 0.5f || useProbe) && debugMode != 5)
            finalColor += envReflection;
        if (debugMode != 5)
            finalColor = ApplyDisplayAdjust(finalColor);
        return float4(finalColor, albedoColor.a);
    }

    // ── Accumulate all lights ─────────────────────────────────────────────────
    float3 diffuseAccum  = (float3) 0;
    float3 specularAccum = (float3) 0;
    float3 directLighting = (float3) 0;
    float3 lightAmbient   = (float3) 0;

    // Local scene lights (b1)
    for (int i = 0; i < numLights; ++i)
    {
        if (lights[i].active)
            lightAmbient += lights[i].ambient;

        float3 ld, ls;
        directLighting += ProcessLight(lights[i], normalWS, V, input.worldPosition,
                                       roughnessValue, metallicValue, albedoColor.rgb, F0,
                                       ld, ls);
        diffuseAccum  += ld;
        specularAccum += ls;
    }

    // Global lights (b3) - each one attenuated by its own shadow map (if it has one).
    float shadowVisMin = 1.0f;                      // For the ShadowsOnly debug view
    [loop]
    for (int gi = 0; gi < globalLightCount; ++gi)
    {
        float3 ld, ls;
        float3 contrib = ProcessLight(globalLights[gi], normalWS, V, input.worldPosition,
                                      roughnessValue, metallicValue, albedoColor.rgb, F0,
                                      ld, ls);
        float  sh      = ShadowForGlobalLight(gi, globalLights[gi].position, input.worldPosition);
        shadowVisMin   = min(shadowVisMin, sh);
        directLighting += contrib * sh;
        diffuseAccum   += ld * sh;
        specularAccum  += ls * sh;
    }

    // === Debug modes: separate diffuse and specular visualisation
    if (debugMode == 3)                             // LightingOnly — diffuse component
        return float4(diffuseAccum, albedoColor.a);

    if (debugMode == 4)                             // SpecularOnly
        return float4(specularAccum, albedoColor.a);

    if (debugMode == 6)                             // MaterialsOnly
    {
        float3 matDebug = float3(metallicValue, 1.0f - roughnessValue, aoValue);
        return float4(matDebug, 1.0f);
    }

    // === ShadowsOnly debug mode (darkest shadow factor across all global lights)
    if (debugMode == 7)
        return float4(shadowVisMin, shadowVisMin, shadowVisMin, 1.0f);

    // === Combine direct lighting (already shadow-attenuated per light) + per-light ambient + material ambient
    finalColor += lightAmbient * albedoColor.rgb * aoValue
               + directLighting;

    // === Environment reflection (excluded from debug modes 3/4/7 above)
    if (useEnvMap > 0.5f || useProbe)
        finalColor += envReflection;

    // === Emissive contribution (additive; independent of lighting and shadow)
    float3 emissiveTex = (useEmissiveMap > 0.5f)
                       ? emissiveMap.Sample(samplerState, input.texCoord).rgb
                       : float3(1.0f, 1.0f, 1.0f);
    finalColor += EmissiveFactor * emissiveTex * EmissiveStrength;

    // === Planar reflection (reflector surfaces only).  The mirror render uses an x-flipped projection,
    // so u is mirrored; v matches the screen because both passes share the same projection Y.
    // PlanarIndex picks the slice of the plane this surface reflects.
    if (PlanarStrength > 0.0f && planarParams.x > 0.0f)
    {
        int    planarSlice = clamp((int)(PlanarIndex + 0.5f), 0, 3);
        float3 planarN     = planarPlanes[planarSlice].xyz;
        float2 planarUV    = input.position.xy * planarParams.zw;
        planarUV.x         = 1.0f - planarUV.x;
        // Ripple: normal-map detail (normalWS vs the geometric normal) along the plane's two tangents.
        float3 planarT1    = normalize(cross(planarN, (abs(planarN.y) < 0.99f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f)));
        float3 planarT2    = cross(planarN, planarT1);
        float3 planarDelta = normalWS - N;
        planarUV          += float2(dot(planarDelta, planarT1), dot(planarDelta, planarT2)) * (planarParams.y * 0.25f);
        planarUV           = saturate(planarUV);
        float3 planarCol   = planarMap.SampleLevel(envSamplerState, float3(planarUV, (float)planarSlice), 0).rgb;
        float  planarNoV   = saturate(dot(normalWS, V));
        float  planarW     = saturate(PlanarStrength * planarParams.x * (1.0f - roughnessValue)
                                      * (0.4f + 0.6f * pow(1.0f - planarNoV, 2.0f)));
        finalColor = lerp(finalColor, planarCol, planarW);
    }

    // === Video settings brightness / contrast
    finalColor = ApplyDisplayAdjust(finalColor);

    // === Final output
    return float4(finalColor, albedoColor.a);
}
