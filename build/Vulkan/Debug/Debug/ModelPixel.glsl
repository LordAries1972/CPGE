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
//   t8  shadowMap          (sampler2DShadow, binding 8)
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
uniform sampler2DShadow shadowMap;          // t8: shadow depth map (hardware PCF)

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
    vec3  Ka;             float _padM1;
    vec3  Kd;             float _padM2;
    vec3  Ks;             float _padM3;
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
layout(std140, binding = 6) uniform ShadowBuffer
{
    mat4  lightViewProj;            // Light view-projection matrix
    float shadowBias;               // Depth bias to prevent shadow acne
    float shadowStrength;           // Shadow darkness multiplier [0-1]
    float useShadowMap;             // 1.0 = shadow map at t8 is active
    float shadowMapSize;            // Shadow map resolution (e.g. 2048.0) for PCF texel offset
};

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

// ── PCF Shadow (sampler2DShadow: hardware PCF on supported drivers) ───────────
// Returns 1.0 = lit, 0.0 = fully shadowed.
float SampleShadow(vec3 worldPos)
{
    if (useShadowMap < 0.5)
        return 1.0;

    vec4 lightClip  = lightViewProj * vec4(worldPos, 1.0);
    vec3 projCoords = lightClip.xyz / lightClip.w;

    // Discard outside shadow frustum
    if (projCoords.z > 1.0 || projCoords.z < 0.0)  return 1.0;
    if (abs(projCoords.x) > 1.0)                    return 1.0;
    if (abs(projCoords.y) > 1.0)                    return 1.0;

    // NDC [-1,1] → texture [0,1]; flip Y for OpenGL clip convention
    vec2 shadowUV;
    shadowUV.x = projCoords.x * 0.5 + 0.5;
    shadowUV.y = projCoords.y * 0.5 + 0.5;   // OpenGL Y is not flipped

    float currentDepth = projCoords.z - shadowBias;

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
    shadow /= 9.0;

    return mix(1.0 - shadowStrength, 1.0, shadow);
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
    if (useEnvMap > 0.5)
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
        if (useEnvMap > 0.5 && debugMode != 5)
            finalColor += envRefl;
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

    for (int gi = 0; gi < globalLightCount; ++gi)
    {
        vec3 ld, ls;
        directLighting += ProcessLight(globalLights[gi], normalWS, V, vWorldPosition,
                                       roughnessValue, metallicValue, albedoColor.rgb, F0,
                                       ld, ls);
        diffuseAccum  += ld;
        specularAccum += ls;
    }

    // === Debug: separate diffuse / specular visualisation
    if (debugMode == 3) { fragColor = vec4(diffuseAccum, albedoColor.a); return; }
    if (debugMode == 4) { fragColor = vec4(specularAccum, albedoColor.a); return; }
    if (debugMode == 6)
    {
        fragColor = vec4(metallicValue, 1.0 - roughnessValue, aoValue, 1.0);
        return;
    }

    // === PCF shadow factor
    float shadowFactor = SampleShadow(vWorldPosition);

    if (debugMode == 7) { fragColor = vec4(vec3(shadowFactor), 1.0); return; }

    // === Combine: material ambient + per-light ambient + direct (shadow-modulated)
    finalColor += lightAmbient * albedoColor.rgb * aoValue
               + directLighting * shadowFactor;

    // === Emissive
    vec3 emissiveTex = (useEmissiveMap > 0.5) ? texture(emissiveMap, vTexCoord).rgb : vec3(1.0);
    vec3 emissive = EmissiveFactor * emissiveTex * EmissiveStrength;
    finalColor += emissive;

    // === Environment reflection
    if (useEnvMap > 0.5)
        finalColor += envRefl;

    // Linear output — matches DX12/DX11 which do not apply tone mapping or
    // gamma correction in the shader (the display / sRGB framebuffer handles it).
    // Manual Reinhard + pow(1/2.2) caused over-brightness vs the DX12 reference.
    fragColor = vec4(finalColor, albedoColor.a);
}
