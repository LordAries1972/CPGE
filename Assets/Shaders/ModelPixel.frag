// ModelPixel.frag — Vulkan Fragment Shader (GLSL 450 / SPIR-V source)
// Compiled to SPIR-V via glslangValidator or shaderc.
// Mirrors ModelPixel.glsl (OpenGL) with Vulkan descriptor set/binding layout.
//
// Compile command (shaderc):
//   glslangValidator -V ModelPixel.frag -o ModelPixel.frag.spv
//
// Descriptor layout (set / binding mirrors HLSL register(tN/bN)):
//   set 0, binding 0  — ConstantBuffer (UBO)
//   set 0, binding 1  — LightBuffer    (UBO)
//   set 0, binding 2  — DebugBuffer    (UBO)
//   set 0, binding 3  — GlobalLightBuffer (UBO)
//   set 0, binding 4  — MaterialBuffer (UBO)
//   set 0, binding 5  — EnvBuffer      (UBO)
//   set 1, binding 0  — diffuseTexture (combined image sampler)
//   set 1, binding 1  — normalMap
//   set 1, binding 2  — metallicMap
//   set 1, binding 3  — roughnessMap
//   set 1, binding 4  — aoMap
//   set 1, binding 5  — environmentMap (cubemap)
//
#version 450

// ── Fragment Output ───────────────────────────────────────────────────────────
layout(location = 0) out vec4 fragColor;

// ── Inputs from vertex shader ─────────────────────────────────────────────────
layout(location = 0) in vec3 vWorldPosition;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vTexCoord;
layout(location = 3) in vec3 vViewDirection;
layout(location = 4) in vec3 vTangent;
layout(location = 5) in vec3 vBitangent;

// ── Texture Samplers ──────────────────────────────────────────────────────────
layout(set = 1, binding = 0) uniform sampler2D   diffuseTexture;
layout(set = 1, binding = 1) uniform sampler2D   normalMap;
layout(set = 1, binding = 2) uniform sampler2D   metallicMap;
layout(set = 1, binding = 3) uniform sampler2D   roughnessMap;
layout(set = 1, binding = 4) uniform sampler2D   aoMap;
layout(set = 1, binding = 5) uniform samplerCube environmentMap;

#define MAX_LIGHTS        8
#define MAX_GLOBAL_LIGHTS 8
#define PI 3.14159265359

// ── Uniform Buffers ───────────────────────────────────────────────────────────
layout(set = 0, binding = 0) uniform ConstantBuffer {
    mat4  uWorld;
    mat4  uView;
    mat4  uProjection;
    vec3  cameraPosition;
    float _padCB;
} cb;

layout(set = 0, binding = 2) uniform DebugBuffer {
    int   debugMode;
    float _p0; float _p1; float _p2;
} db;

struct LightStruct {
    vec3  position;   float _pad0;
    vec3  direction;  float _pad1;
    vec3  color;      float _pad2;
    vec3  ambient;    float intensity;
    vec3  specularColor; float _pad3;
    float range;  float angle;  int type;  int lActive;
    int   animMode;  float animTimer;  float animSpeed;  float baseIntensity;
    float animAmplitude;  float _pad4;  float innerCone;  float outerCone;
    float lightFalloff;  float Shiningness;  float Reflection;  float _pad5;
    vec4  _pad6;  // 16-byte tail padding
};

layout(set = 0, binding = 1) uniform LightBuffer {
    int        numLights;
    float _pl0; float _pl1; float _pl2;
    LightStruct lights[MAX_LIGHTS];
} lb;

layout(set = 0, binding = 3) uniform GlobalLightBuffer {
    int        globalLightCount;
    float _pg0; float _pg1; float _pg2;
    LightStruct globalLights[MAX_GLOBAL_LIGHTS];
} glb;

layout(set = 0, binding = 4) uniform MaterialBuffer {
    vec3  Ka;  float _pm1;
    vec3  Kd;  float _pm2;
    vec3  Ks;  float _pm3;
    float Ns;  float Metallic;  float Roughness;  float ReflectionStrength;
    float useMetallicMap;  float useRoughnessMap;  float useAOMap;  float useEnvMap;
    vec3  EmissiveFactor;  float EmissiveStrength;
    float NormalScale;  float _pm4;  float _pm5;  float _pm6;
} mat;

layout(set = 0, binding = 5) uniform EnvBuffer {
    float envIntensity;
    vec3  envTint;
    float mipLODBias;
    float fresnel0;
    vec2  _pe;
} env;

// ── PBR Helpers ───────────────────────────────────────────────────────────────
vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = pow(max(dot(N, H), 0.0), 2.0);
    float denom  = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / max(PI * denom * denom, 0.001);
}
float GeomSchlickGGX(float NdotV, float roughness) {
    float k = pow(roughness + 1.0, 2.0) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return GeomSchlickGGX(max(dot(N, V), 0.0), roughness)
         * GeomSchlickGGX(max(dot(N, L), 0.0), roughness);
}

#define DIRECTIONAL 0
#define POINT       1
#define SPOT        2

vec3 ProcessLight(LightStruct light, vec3 N, vec3 V, vec3 worldPos,
                  float roughness, float metallic, vec3 albedo, vec3 F0)
{
    if (light.lActive == 0) return vec3(0.0);
    vec3 L = vec3(0.0); float atten = 1.0;

    if (light.type == DIRECTIONAL) {
        L = normalize(-light.direction);
    } else {
        vec3 lv = light.position - worldPos;
        float d = length(lv);
        L = normalize(lv);
        if (light.type == POINT) {
            atten = clamp(1.0 - d / light.range, 0.0, 1.0) / (1.0 + d * d);
        } else {
            vec3 sd = normalize(-light.direction);
            float sc = dot(sd, -L);
            float sf = smoothstep(cos(light.outerCone), cos(light.innerCone), sc);
            atten = sf / (1.0 + pow(d, light.lightFalloff));
        }
    }
    atten *= max(light.baseIntensity + light.intensity, 0.0);
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0001) return vec3(0.0);

    vec3 H = normalize(V + L);
    float rAdj = 1.0 + light.Reflection;
    vec3 F   = FresnelSchlick(max(dot(H, V), 0.0), F0 * rAdj);
    float NDF= DistributionGGX(N, H, roughness / (1.0 + light.Shiningness));
    float G  = GeometrySmith(N, V, L, roughness);
    vec3 spec= (NDF * G * F) / (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001);
    vec3 kD  = (vec3(1.0) - F) * (1.0 - metallic);
    return (kD * albedo / PI + spec * light.specularColor * rAdj)
           * light.color * NdotL * atten;
}

// ── Main ──────────────────────────────────────────────────────────────────────
void main()
{
    vec4 albedoColor = texture(diffuseTexture, vTexCoord);
    albedoColor.rgb *= mat.Kd;
    if (db.debugMode == 2) { fragColor = albedoColor; return; }

    float metallicV  = (mat.useMetallicMap  > 0.5) ? texture(metallicMap,  vTexCoord).b : mat.Metallic;
    float roughnessV = (mat.useRoughnessMap > 0.5) ? texture(roughnessMap, vTexCoord).g : mat.Roughness;
    float aoV        = (mat.useAOMap        > 0.5) ? texture(aoMap,        vTexCoord).r : 1.0;

    if (db.debugMode == 9) { fragColor = vec4(vec3(metallicV), 1.0); return; }

    vec3 normalTS = texture(normalMap, vTexCoord).xyz * 2.0 - 1.0;
    normalTS.y   = -normalTS.y;
    normalTS.xy *= mat.NormalScale;

    vec3 N = normalize(vNormal);
    vec3 T = normalize(vTangent);
    vec3 B = normalize(vBitangent);
    vec3 normalWS = normalize(mat3(T, B, N) * normalTS);
    if (db.debugMode == 1) { fragColor = vec4(abs(normalWS), 1.0); return; }

    vec3 V  = normalize(cb.cameraPosition - vWorldPosition);
    vec3 F0 = mix(vec3(env.fresnel0), albedoColor.rgb * mat.Ks, metallicV);

    vec3 envRefl = vec3(0.0);
    if (mat.useEnvMap > 0.5) {
        envRefl = textureLod(environmentMap, reflect(-V, normalWS),
                             roughnessV * 5.0 + env.mipLODBias).rgb;
        envRefl *= env.envTint * env.envIntensity
                 * FresnelSchlick(max(dot(normalWS, V), 0.0), F0)
                 * mat.ReflectionStrength;
    }
    if (db.debugMode == 8) { fragColor = vec4(envRefl, 1.0); return; }

    vec3 finalColor = mat.Ka * albedoColor.rgb * aoV;
    if ((lb.numLights == 0 && glb.globalLightCount == 0) || db.debugMode == 5) {
        if (mat.useEnvMap > 0.5 && db.debugMode != 5) finalColor += envRefl;
        fragColor = vec4(finalColor, albedoColor.a);
        return;
    }

    vec3 direct = vec3(0.0);
    vec3 lAmb   = vec3(0.0);
    for (int i = 0; i < lb.numLights; i++) {
        if (lb.lights[i].lActive != 0) lAmb += lb.lights[i].ambient;
        direct += ProcessLight(lb.lights[i], normalWS, V, vWorldPosition,
                               roughnessV, metallicV, albedoColor.rgb, F0);
    }
    for (int gi = 0; gi < glb.globalLightCount; gi++) {
        direct += ProcessLight(glb.globalLights[gi], normalWS, V, vWorldPosition,
                               roughnessV, metallicV, albedoColor.rgb, F0);
    }

    finalColor += direct + mat.Ka * albedoColor.rgb * aoV * max(length(lAmb), 0.0);
    if (db.debugMode == 3) { fragColor = vec4(direct, 1.0); return; }

    finalColor += mat.EmissiveFactor * mat.EmissiveStrength;
    if (mat.useEnvMap > 0.5) finalColor += envRefl;

    // Reinhard + gamma
    finalColor = finalColor / (finalColor + vec3(1.0));
    finalColor = pow(clamp(finalColor, 0.0, 1.0), vec3(1.0 / 2.2));

    fragColor = vec4(finalColor, albedoColor.a);
}
