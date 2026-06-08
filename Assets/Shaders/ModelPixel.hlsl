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
Texture2D shadowMap      : register(t8);                // t8: Shadow depth map for PCF

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
    float  pad1;
    float3 Kd;                                      // Diffuse colour (baseColorFactor RGB, linear)
    float  pad2;
    float3 Ks;                                      // Specular colour
    float  pad3;
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
cbuffer ShadowBuffer : register(b6)
{
    float4x4 lightViewProj;                         // Light view-projection matrix
    float    shadowBias;                            // Depth bias to prevent shadow acne
    float    shadowStrength;                        // Shadow darkness multiplier [0-1]
    float    useShadowMap;                          // 1.0 = shadow map at t8 is active
    float    shadowMapSize;                         // Shadow map resolution (e.g. 2048.0) for PCF texel offset
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
    outDiff = (float3) 0;
    outSpec = (float3) 0;

    if (light.active == 0)
        return (float3) 0;

    float3 L = (float3) 0;
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
    if (NdotL <= 0.0001f)
        return (float3) 0;

    float3 H = normalize(V + L);
    float  reflAdj = 1.0f + light.Reflection;

    float3 F   = FresnelSchlick(max(dot(H, V), 0.0f), F0 * reflAdj);
    float  NDF = DistributionGGX(N, H, roughness / (1.0f + light.Shiningness));
    float  G   = GeometrySmith(N, V, L, roughness);

    float3 numerator   = NDF * G * F;
    float  denominator = 4.0f * max(dot(N, V), 0.0f) * max(dot(N, L), 0.0f) + 0.001f;
    float3 specular    = numerator / denominator;

    float3 kS = F;
    float3 kD = (float3(1.0f, 1.0f, 1.0f) - kS) * (1.0f - metallic);

    float3 diffuseColor  = kD * albedo / PI;
    float3 specularColor = specular * light.specularColor * reflAdj;

    outDiff = diffuseColor  * light.color * NdotL * attenuation;
    outSpec = specularColor * light.color * NdotL * attenuation;

    return outDiff + outSpec;
}

// ── PCF Shadow Sampling (3x3 kernel) ─────────────────────────────────────────
// Returns a shadow factor: 1.0 = fully lit, 0.0 = fully in shadow.
// Values between are produced by the PCF kernel averaging.
float SampleShadow(float3 worldPos)
{
    if (useShadowMap < 0.5f)
        return 1.0f;

    // Transform world position to light clip space
    float4 lightClip   = mul(float4(worldPos, 1.0f), lightViewProj);
    float3 projCoords  = lightClip.xyz / lightClip.w;

    // Discard samples outside the shadow frustum
    if (projCoords.z > 1.0f || projCoords.z < 0.0f)
        return 1.0f;
    if (projCoords.x < -1.0f || projCoords.x > 1.0f)
        return 1.0f;
    if (projCoords.y < -1.0f || projCoords.y > 1.0f)
        return 1.0f;

    // Remap X,Y from NDC [-1,1] to texture [0,1]; flip Y for DX clip convention
    float2 shadowUV;
    shadowUV.x = projCoords.x *  0.5f + 0.5f;
    shadowUV.y = projCoords.y * -0.5f + 0.5f;

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
    shadow /= 9.0f;

    // shadow=1 (lit) → lerp controls darkness: 0 strength=no shadow, 1 strength=full shadow
    return lerp(1.0f - shadowStrength, 1.0f, shadow);
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

    if (useEnvMap > 0.5f)
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
        if (useEnvMap > 0.5f && debugMode != 5)
            finalColor += envReflection;
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

    // Global lights (b3)
    for (int gi = 0; gi < globalLightCount; ++gi)
    {
        float3 ld, ls;
        directLighting += ProcessLight(globalLights[gi], normalWS, V, input.worldPosition,
                                       roughnessValue, metallicValue, albedoColor.rgb, F0,
                                       ld, ls);
        diffuseAccum  += ld;
        specularAccum += ls;
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

    // === PCF shadow factor
    float shadowFactor = SampleShadow(input.worldPosition);

    // === ShadowsOnly debug mode
    if (debugMode == 7)
        return float4(shadowFactor, shadowFactor, shadowFactor, 1.0f);

    // === Combine direct lighting (modulated by shadow) + per-light ambient + material ambient
    finalColor += lightAmbient * albedoColor.rgb * aoValue
               + directLighting * shadowFactor;

    // === Environment reflection (excluded from debug modes 3/4/7 above)
    if (useEnvMap > 0.5f)
        finalColor += envReflection;

    // === Emissive contribution (additive; independent of lighting and shadow)
    float3 emissiveTex = (useEmissiveMap > 0.5f)
                       ? emissiveMap.Sample(samplerState, input.texCoord).rgb
                       : float3(1.0f, 1.0f, 1.0f);
    finalColor += EmissiveFactor * emissiveTex * EmissiveStrength;

    // === Final output
    return float4(finalColor, albedoColor.a);
}
