// ModelPixel.glsl — OpenGL Fragment Shader
// Converted from ModelPixel.hlsl (HLSL 5.0) for OpenGL 3.3+ core profile.
// Compiler: OpenGL / Windows SDK (opengl32.lib + glew)
// Shader Version: GLSL 330 core
//
// Texture binding slots mirror HLSL register(tN) / register(sN) mapping:
//   t0  diffuseTexture   (sampler2D, binding 0)
//   t1  normalMap        (sampler2D, binding 1)
//   t2  metallicMap      (sampler2D, binding 2)
//   t3  roughnessMap     (sampler2D, binding 3)
//   t4  aoMap            (sampler2D, binding 4)
//   t5  environmentMap   (samplerCube, binding 5)
//
// Uniform block binding slots mirror HLSL register(bN):
//   b0  ConstantBuffer   (binding 0)
//   b1  LightBuffer      (binding 1)
//   b2  DebugBuffer      (binding 2)
//   b3  GlobalLightBuffer (binding 3)
//   b4  MaterialBuffer   (binding 4)
//   b5  EnvBuffer        (binding 5)
//
#version 330 core

// ── Outputs ──────────────────────────────────────────────────────────────────
out vec4 fragColor;

// ── Inputs from vertex shader ────────────────────────────────────────────────
in vec3 vWorldPosition;
in vec3 vNormal;
in vec2 vTexCoord;
in vec3 vViewDirection;
in vec3 vTangent;
in vec3 vBitangent;

// ── Texture Samplers (OpenGL combines Texture + Sampler) ─────────────────────
uniform sampler2D   diffuseTexture;     // t0
uniform sampler2D   normalMap;          // t1
uniform sampler2D   metallicMap;        // t2
uniform sampler2D   roughnessMap;       // t3
uniform sampler2D   aoMap;              // t4
uniform samplerCube environmentMap;     // t5

#define MAX_LIGHTS        8
#define MAX_GLOBAL_LIGHTS 8
#define PI 3.14159265359

// ── ConstantBuffer (binding 0) ───────────────────────────────────────────────
layout(std140, binding = 0) uniform ConstantBuffer
{
    mat4  uWorld;
    mat4  uView;
    mat4  uProjection;
    vec3  cameraPosition;
    float _padCB;
};

// ── DebugBuffer (binding 2) ──────────────────────────────────────────────────
// Debug modes:
//   0=Full  1=Normals  2=TextureOnly  3=LightingOnly  4=SpecularOnly
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

    float _pad6[4];
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
    float _padM4;
    float _padM5;
    float _padM6;
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

// ── PBR Helper Functions ─────────────────────────────────────────────────────

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

#define LIGHT_TYPE_DIRECTIONAL 0
#define LIGHT_TYPE_POINT       1
#define LIGHT_TYPE_SPOT        2

vec3 ProcessLight(LightStruct light, vec3 N, vec3 V, vec3 worldPos,
                  float roughness, float metallic, vec3 albedo, vec3 F0)
{
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
            vec3  spotDir    = normalize(-light.direction);
            float spotCos    = dot(spotDir, -L);
            float inner      = cos(light.innerCone);
            float outer      = cos(light.outerCone);
            float spotFall   = smoothstep(outer, inner, spotCos);
            float distFall   = 1.0 / (1.0 + pow(dist, light.lightFalloff));
            attenuation = spotFall * distFall;
        }
    }

    float totalIntensity = max(light.baseIntensity + light.intensity, 0.0);
    attenuation *= totalIntensity;

    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0001)
        return vec3(0.0);

    vec3  H = normalize(V + L);
    float reflAdj = 1.0 + light.Reflection;
    vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0 * reflAdj);
    float NDF = DistributionGGX(N, H, roughness / (1.0 + light.Shiningness));
    float G   = GeometrySmith(N, V, L, roughness);

    vec3 numerator   = NDF * G * F;
    float denominator= 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
    vec3 specular    = numerator / denominator;

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    vec3 diffuseColor  = kD * albedo / PI;
    vec3 specularColor = specular * light.specularColor * reflAdj;

    return (diffuseColor + specularColor) * light.color * NdotL * attenuation;
}

// ── Fragment Shader Main ─────────────────────────────────────────────────────
void main()
{
    // === Sample albedo texture
    vec4 albedoColor = texture(diffuseTexture, vTexCoord);
    albedoColor.rgb *= Kd;

    if (debugMode == 2) { fragColor = albedoColor; return; }

    // === Sample PBR maps (GLTF ORM pack: G=roughness, B=metallic, R=AO)
    float metallicValue  = (useMetallicMap  > 0.5) ? texture(metallicMap,  vTexCoord).b : Metallic;
    float roughnessValue = (useRoughnessMap > 0.5) ? texture(roughnessMap, vTexCoord).g : Roughness;
    float aoValue        = (useAOMap        > 0.5) ? texture(aoMap,        vTexCoord).r : 1.0;

    if (debugMode == 9)
    {
        fragColor = vec4(vec3(metallicValue), 1.0);
        return;
    }

    // === Normal mapping
    vec3 normalTS = texture(normalMap, vTexCoord).xyz;
    normalTS = normalTS * 2.0 - 1.0;
    normalTS.y = -normalTS.y;           // DirectX → OpenGL G-channel correction
    normalTS.xy *= NormalScale;

    vec3 N = normalize(vNormal);
    vec3 T = normalize(vTangent);
    vec3 B = normalize(vBitangent);
    mat3 TBN = mat3(T, B, N);
    vec3 normalWS = normalize(TBN * normalTS);

    if (debugMode == 1) { fragColor = vec4(abs(normalWS), 1.0); return; }

    // === View direction
    vec3 V = normalize(cameraPosition - vWorldPosition);

    // === F0 for Fresnel
    vec3 F0 = mix(vec3(fresnel0), albedoColor.rgb * Ks, metallicValue);

    // === Environment reflection
    vec3 reflDir    = reflect(-V, normalWS);
    float roughMip  = roughnessValue * 5.0 + mipLODBias;
    vec3 envRefl    = vec3(0.0);
    if (useEnvMap > 0.5)
    {
        envRefl = textureLod(environmentMap, reflDir, roughMip).rgb;
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

    // === Local lights
    vec3 directLighting = vec3(0.0);
    vec3 lightAmbient   = vec3(0.0);
    for (int i = 0; i < numLights; ++i)
    {
        if (lights[i].lActive != 0)
            lightAmbient += lights[i].ambient;
        directLighting += ProcessLight(lights[i], normalWS, V, vWorldPosition,
                                       roughnessValue, metallicValue, albedoColor.rgb, F0);
    }

    // === Global lights
    for (int gi = 0; gi < globalLightCount; ++gi)
    {
        directLighting += ProcessLight(globalLights[gi], normalWS, V, vWorldPosition,
                                       roughnessValue, metallicValue, albedoColor.rgb, F0);
    }

    // === Combine
    vec3 additionalAmbient = Ka * albedoColor.rgb * aoValue * max(length(lightAmbient), 0.0);
    finalColor += directLighting + additionalAmbient;

    if (debugMode == 3) { fragColor = vec4(directLighting, 1.0); return; }

    // === Emissive
    vec3 emissive = EmissiveFactor * EmissiveStrength;
    finalColor += emissive;

    // === Environment reflection
    if (useEnvMap > 0.5)
        finalColor += envRefl;

    // === Tone mapping (simple Reinhard) + gamma correction
    finalColor = finalColor / (finalColor + vec3(1.0));
    finalColor = pow(clamp(finalColor, 0.0, 1.0), vec3(1.0 / 2.2));

    fragColor = vec4(finalColor, albedoColor.a);
}
