// ModelPixel.frag — Vulkan Fragment Shader (GLSL 450 / SPIR-V source)
// Compiled to SPIR-V via: glslangValidator -V ModelPixel.frag -o ModelPixel.frag.spv
//
// Descriptor layout matches CreateDescriptorSetLayouts() in VULKAN_Renderer.cpp:
//   set 0, binding 0  — TransformUBO  (model/view/proj/camPos/scale)
//   set 0, binding 1  — MaterialUBO   (Kd/Ka/metallic/roughness/emissive/normalScale/flags)
//   set 1, binding 0  — diffuseTexture (combined image sampler)
//   set 1, binding 1  — normalMap
//   set 1, binding 2  — ormTexture    (GLTF pbrMetallicRoughness: R=AO, G=roughness, B=metallic)
//   set 1, binding 3  — aoTexture     (standalone ambient occlusion)
//   set 1, binding 4  — glossTexture
//   set 1, binding 5  — emissiveTexture
//   push_constant (fragment) — single directional light (lightDir/intensity/color/ambient)
//
// Normal map convention: GLTF 2.0 / OpenGL format stored with V-origin at top.
// The green channel is flipped (normalTS.y = -normalTS.y) to match Vulkan's
// V-down UV convention relative to the tangent-space handedness.
//
#version 450

layout(location = 0) out vec4 fragColor;

layout(location = 0) in vec3 vWorldPosition;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vTexCoord;
layout(location = 3) in vec3 vViewDirection;   // normalize(camPos - worldPos) from vertex stage
layout(location = 4) in vec3 vTangent;
layout(location = 5) in vec3 vBitangent;       // cross(tangent, normal) from vertex stage

// ── Texture samplers (set 1) ──────────────────────────────────────────────────
layout(set = 1, binding = 0) uniform sampler2D diffuseTexture;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D ormTexture;
layout(set = 1, binding = 3) uniform sampler2D aoTexture;
layout(set = 1, binding = 4) uniform sampler2D glossTex;
layout(set = 1, binding = 5) uniform sampler2D emissiveTex;

#define PI 3.14159265359

// ── Uniform buffers (set 0) ───────────────────────────────────────────────────
layout(set = 0, binding = 0) uniform TransformUBO {
    mat4  model;
    mat4  view;
    mat4  proj;
    vec4  camPos;   // xyz = camera world position
    vec4  scale;    // xyz = model scale (baked into world matrix; usually 1,1,1)
} cb;

// Layout must match VKMatUBO in Models.h (80 bytes, 5 × float4 rows)
layout(set = 0, binding = 1) uniform MaterialUBO {
    vec3  Kd;              float metallic;
    vec3  Ka;              float roughness;
    vec3  emissive;        float emissiveStrength;
    float normalScale;     float useNormal;     float useORM;          float useAO;
    float useDiffuseMap;   float useGlossMap;   float useEmissiveMap;  float _pad;
} mat;

// ── Push constant: directional light ─────────────────────────────────────────
layout(push_constant) uniform LightPC {
    vec3  lightDir;          // world-space direction the light travels
    float lightIntensity;
    vec3  lightColor;
    float ambientStrength;
} lpc;

// ── PBR helpers ───────────────────────────────────────────────────────────────
vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a   = roughness * roughness;
    float a2  = a * a;
    float NdH = max(dot(N, H), 0.0);
    float d   = (NdH * NdH) * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 0.001);
}

float GeomSchlickGGX(float NdotV, float roughness) {
    float k = pow(roughness + 1.0, 2.0) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return GeomSchlickGGX(max(dot(N, V), 0.0), roughness)
         * GeomSchlickGGX(max(dot(N, L), 0.0), roughness);
}

// ── Main ──────────────────────────────────────────────────────────────────────
void main()
{
    // Albedo — modulate by diffuse colour factor
    vec4 albedoColor  = texture(diffuseTexture, vTexCoord);
    albedoColor.rgb  *= mat.Kd;

    // PBR parameters — prefer texture channels when flags are set
    float metallicV  = mat.metallic;
    float roughnessV = mat.roughness;
    float aoV        = 1.0;

    if (mat.useORM > 0.5) {
        // GLTF pbrMetallicRoughness packed ORM: R=occlusion, G=roughness, B=metallic
        vec3 orm  = texture(ormTexture, vTexCoord).rgb;
        aoV        = orm.r;
        roughnessV = orm.g;
        metallicV  = orm.b;
    }
    if (mat.useAO > 0.5) {
        // Standalone AO overrides the R channel if a separate AO map is present
        aoV = texture(aoTexture, vTexCoord).r;
    }

    // Surface normal — perturb via normal map if present
    vec3 N = normalize(vNormal);
    if (mat.useNormal > 0.5) {
        vec3 normalTS  = texture(normalMap, vTexCoord).xyz * 2.0 - 1.0;
        normalTS.y    = -normalTS.y;            // GLTF/OpenGL → Vulkan UV handedness
        normalTS.xy  *= mat.normalScale;
        vec3 T = normalize(vTangent);
        vec3 B = normalize(vBitangent);
        N = normalize(mat3(T, B, N) * normalTS);
    }

    // View and light vectors
    vec3 V     = normalize(vViewDirection);
    vec3 L     = normalize(-lpc.lightDir);      // negate: lpc.lightDir is the ray direction
    vec3 H     = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    // Cook-Torrance BRDF
    vec3 F0  = mix(vec3(0.04), albedoColor.rgb, metallicV);
    vec3 F   = FresnelSchlick(max(dot(H, V), 0.0), F0);
    float NDF = DistributionGGX(N, H, roughnessV);
    float G   = GeometrySmith(N, V, L, roughnessV);
    vec3 spec  = (NDF * G * F) / (4.0 * NdotV * NdotL + 0.001);
    vec3 kD    = (vec3(1.0) - F) * (1.0 - metallicV);

    vec3 direct   = (kD * albedoColor.rgb / PI + spec)
                  * lpc.lightColor * lpc.lightIntensity * NdotL;
    vec3 ambient  = mat.Ka * albedoColor.rgb * aoV * lpc.ambientStrength;

    // Emissive — sample texture when bound, otherwise use emissiveFactor colour
    vec3 emissiveSample = mat.useEmissiveMap > 0.5
        ? texture(emissiveTex, vTexCoord).rgb
        : vec3(1.0);                            // factor-only: shader multiplies colour below
    vec3 emissive = mat.emissive * emissiveSample * mat.emissiveStrength;

    // Linear output — the SRGB swap chain (VK_FORMAT_B8G8R8A8_SRGB) applies
    // hardware gamma correction automatically; manual tone-map + pow(1/2.2)
    // would cause double gamma and produce an over-bright result vs DX12.
    vec3 color = ambient + direct + emissive;
    fragColor = vec4(color, albedoColor.a);
}
