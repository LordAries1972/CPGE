// Enhanced ModelPShader.hlsl with improved reflection and metallic support

// Texture samplers (DirectX 11 pixel shader binding)
Texture2D diffuseTexture : register(t0);                // Albedo / base color map
Texture2D normalMap : register(t1);                     // Tangent-space normal map
Texture2D metallicMap : register(t2);                   // Metallic map (R channel)
Texture2D roughnessMap : register(t3);                  // Roughness map (R channel) 
Texture2D aoMap : register(t4);                         // Ambient occlusion map (R channel)
TextureCube environmentMap : register(t5);              // Environment map for reflections
SamplerState samplerState : register(s0);               // Sampler
SamplerState envSamplerState : register(s1);            // Environment map sampler

#define MAX_LIGHTS 8
#define MAX_GLOBAL_LIGHTS 8
#define PI 3.14159265359

// -----------------------------------------------------------
// Per-object Constant Buffer
// -----------------------------------------------------------
cbuffer ConstantBuffer : register(b0)
{
    matrix worldMatrix;
    matrix viewMatrix;
    matrix projectionMatrix;
    float3 cameraPosition;
    float padding;
}

// -----------------------------------------------------------
// Debug Control Buffer
// -----------------------------------------------------------
// 0 = Full, 
// 1 = Normals, 
// 2 = Texture Only, 
// 3 = Lighting Only, 
// 4 = Specular Only, 
// 5 = NoLighting, 
// 6 = Materials Only, 
// 7 = Shadows Only, 
// 8 = Reflection Only, 
// 9 = Metallic Only
// -----------------------------------------------------------
cbuffer DebugBuffer : register(b2)
{
    int debugMode; 
    float3 _padDebug;
}

// -----------------------------------------------------------
// Light Type Definitions
// -----------------------------------------------------------
#define LIGHT_TYPE_DIRECTIONAL 0
#define LIGHT_TYPE_POINT       1
#define LIGHT_TYPE_SPOT        2

// LightStruct for ModelPShader.hlsl - GPU Matching CPU Layout
struct LightStruct
{
    float3 position;
    float _pad0;
    float3 direction;
    float _pad1;
    float3 color;
    float _pad2;
    float3 ambient;
    float intensity;
    float3 specularColor;
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
    float _pad4;
    float innerCone;
    float outerCone;

    float lightFalloff;
    float Shiningness;
    float Reflection;
    float _pad5[1];

    // === MANDATORY ADDITION TO MATCH CPU ===
    float _pad6[4]; // Final 16 bytes padding to 160 bytes total
};

// -----------------------------------------------------------
// Scene-local Lights
// -----------------------------------------------------------
cbuffer LightBuffer : register(b1)
{
    int numLights;
    float3 _pad;
    LightStruct lights[MAX_LIGHTS];
};

cbuffer GlobalLightBuffer : register(b3)
{
    int globalLightCount;
    float3 _padG;
    LightStruct globalLights[MAX_GLOBAL_LIGHTS];
};

// ==================================================================
// Enhanced Material Buffer Layout
// ==================================================================
cbuffer MaterialBuffer : register(b4)
{
    float3 Ka;                                      // Ambient color
    float pad1;
    float3 Kd;                                      // Diffuse color
    float pad2;
    float3 Ks;                                      // Specular color
    float pad3;
    float Ns;                                       // Specular exponent (shininess)
    float Metallic;                                 // Base metallic factor [0-1]
    float Roughness;                                // Base roughness factor [0-1]
    float ReflectionStrength;                       // Global reflection strength multiplier
    float useMetallicMap;                           // Flag for using metallic map
    float useRoughnessMap;                          // Flag for using roughness map
    float useAOMap;                                 // Flag for using ambient occlusion map
    float useEnvMap;                                // Flag for using environment map
};

// -----------------------------------------------------------
// Environment Settings Buffer
// -----------------------------------------------------------
cbuffer EnvBuffer : register(b5)
{
    float envIntensity;                             // Environment map intensity
    float3 envTint;                                 // Environment map tint color
    float mipLODBias;                               // Mip level bias for environment sampling
    float fresnel0;                                 // Base fresnel reflectance at normal incidence (F0)
    float2 _padEnv;
}

// -----------------------------------------------------------
// Pixel Shader Input (matches ModelVShader.hlsl)
// -----------------------------------------------------------
struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 texCoord : TEXCOORD2;
    float3 viewDirection : TEXCOORD3;
    float3 tangent : TEXCOORD4;
    float3 bitangent : TEXCOORD5;
};

// -----------------------------------------------------------
// Helper Functions for PBR Lighting
// -----------------------------------------------------------

// Schlick's approximation of Fresnel reflectance
float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// Calculate normal distribution function (GGX/Trowbridge-Reitz)
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return a2 / max(denom, 0.001);
}

// Calculate geometric attenuation (Smith GGX)
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = GeometrySchlickGGX(NdotV, roughness);
    float ggx2 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

// -----------------------------------------------------------
// Process Light Function (to avoid duplicate code)
// -----------------------------------------------------------
float3 ProcessLight(LightStruct light, float3 N, float3 V, float3 worldPos, float roughness, float metallic, float3 albedo, float3 F0)
{
    if (light.active == 0)
        return float3(0, 0, 0);
        
    float3 L;
    float attenuation = 1.0f;

    // Calculate light direction and attenuation based on light type
    if (light.type == LIGHT_TYPE_DIRECTIONAL)
    {
        L = normalize(-light.direction);
    }
    else
    {
        float3 lightVec = light.position - worldPos;
        float dist = length(lightVec);
        L = normalize(lightVec);

        if (light.type == LIGHT_TYPE_POINT)
        {
            attenuation = saturate(1.0f - dist / light.range) / (1.0f + dist * dist);
        }
        else if (light.type == LIGHT_TYPE_SPOT)
        {
            float3 spotDir = normalize(-light.direction);
            float spotCos = dot(spotDir, -L);
            float inner = cos(light.innerCone);
            float outer = cos(light.outerCone);
            float spotFalloff = smoothstep(outer, inner, spotCos);
            float distanceFalloff = 1.0f / (1.0f + pow(dist, light.lightFalloff));
            attenuation = spotFalloff * distanceFalloff;
        }
    }

    // Calculate total light intensity with animation support
    float totalIntensity = max(light.baseIntensity + light.intensity, 0.0f);
    attenuation *= totalIntensity;

    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0001f)
        return float3(0, 0, 0);

    // Calculate halfway vector for specular
    float3 H = normalize(V + L);
    
    // Adjust the reflection strength based on light's reflection factor
    float reflectionAdjustment = 1.0f + light.Reflection;
    
    // Calculate Fresnel term
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0 * reflectionAdjustment);
    
    // Calculate Normal Distribution Function
    float NDF = DistributionGGX(N, H, roughness / (1.0 + light.Shiningness));
    
    // Calculate Geometry term
    float G = GeometrySmith(N, V, L, roughness);
    
    // Calculate specular term (Cook-Torrance BRDF)
    float3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
    float3 specular = numerator / denominator;
    
    // Energy conservation - the diffuse and specular light can't exceed the incoming light intensity
    float3 kS = F;
    float3 kD = float3(1.0, 1.0, 1.0) - kS;
    kD *= 1.0 - metallic; // Pure metals have no diffuse lighting
    
    // Final diffuse and specular calculations
    float3 diffuseColor = kD * albedo / PI;
    float3 specularColor = specular * light.specularColor * reflectionAdjustment;
    
    // Calculate final contribution from this light
    float3 lightColor = (diffuseColor + specularColor) * light.color * NdotL * attenuation;
    
    return lightColor;
}

// -----------------------------------------------------------
// Pixel Shader Entry Point
// -----------------------------------------------------------
float4 main(PS_INPUT input) : SV_TARGET
{
    // === Sample the diffuse (albedo) texture
    float4 albedoColor = diffuseTexture.Sample(samplerState, input.texCoord);
    
    // === TextureOnly Debug Mode
    if (debugMode == 2)
        return albedoColor;

    // === Sample material maps
    float metallicValue = useMetallicMap > 0.5f ? metallicMap.Sample(samplerState, input.texCoord).r : Metallic;
    float roughnessValue = useRoughnessMap > 0.5f ? roughnessMap.Sample(samplerState, input.texCoord).r : Roughness;
    float aoValue = useAOMap > 0.5f ? aoMap.Sample(samplerState, input.texCoord).r : 1.0f;
    
    // === MetallicOnly Debug Mode
    if (debugMode == 9)
        return float4(metallicValue, metallicValue, metallicValue, 1.0f);
        
    // === Sample the normal map
    float3 normalTS = normalMap.Sample(samplerState, input.texCoord).xyz;
    normalTS = normalTS * 2.0f - 1.0f; // Transform from [0,1] to [-1,1]

    // === Construct TBN Matrix
    float3 N = normalize(input.normal);
    float3 T = normalize(input.tangent);
    float3 B = normalize(cross(N, T));
    float3x3 TBN = float3x3(T, B, N);

    // === Transform normal into world space
    float3 normalWS = normalize(mul(normalTS, TBN));

    // === NormalsOnly Debug Mode
    if (debugMode == 1)
        return float4(abs(normalWS), 1.0f);

    // === View direction
    float3 V = normalize(cameraPosition - input.worldPosition);
    
    // === Calculate base Fresnel value (F0)
    // For metals, F0 is derived from the specular color; for non-metals it's a constant (usually 0.04)
    float3 F0 = lerp(float3(fresnel0, fresnel0, fresnel0), albedoColor.rgb * Ks, metallicValue);
    
    // === Calculate environment reflection
    float3 reflectionVector = reflect(-V, normalWS);
    float roughnessMip = roughnessValue * 5.0 + mipLODBias; // Convert roughness to mip level
    float3 envReflection = float3(0, 0, 0);
    
    if (useEnvMap > 0.5f)
    {
        envReflection = environmentMap.SampleLevel(envSamplerState, reflectionVector, roughnessMip).rgb;
        envReflection *= envTint * envIntensity;
        
        // Calculate reflection strength based on view angle (Fresnel effect)
        float3 fresnelFactor = FresnelSchlick(max(dot(normalWS, V), 0.0), F0);
        envReflection *= fresnelFactor * ReflectionStrength;
    }
    
    // === ReflectionOnly Debug Mode
    if (debugMode == 8)
        return float4(envReflection, 1.0f);
    
    // === Ambient lighting with occlusion
    float3 ambient = Ka * albedoColor.rgb * aoValue;
    float3 finalColor = ambient;
    
    // === Early exit: NoLighting Debug Mode
    if ((numLights == 0 && globalLightCount == 0) || debugMode == 5)
    {
        // Still add environment reflection if enabled
        if (useEnvMap > 0.5f && debugMode != 5)
            finalColor += envReflection;
        return float4(finalColor, albedoColor.a);
    }

    // ================================
    // Calculate all lights
    // ================================
    float3 diffuseAccum = float3(0, 0, 0);
    float3 specularAccum = float3(0, 0, 0);
    float3 directLighting = float3(0, 0, 0);
    
    // Process local lights
    for (int i = 0; i < numLights; ++i)
    {
        float3 lightContribution = ProcessLight(
            lights[i], normalWS, V, input.worldPosition,
            roughnessValue, metallicValue, albedoColor.rgb, F0
        );
        
        directLighting += lightContribution;
    }
    
    // Process global lights
    for (int gi = 0; gi < globalLightCount; ++gi)
    {
        float3 lightContribution = ProcessLight(
            globalLights[gi], normalWS, V, input.worldPosition,
            roughnessValue, metallicValue, albedoColor.rgb, F0
        );
        
        directLighting += lightContribution;
    }
    
    // Add direct lighting to final color
    finalColor += directLighting;
    
    // Add environment reflection if enabled
    if (useEnvMap > 0.5f && debugMode != 3 && debugMode != 4)
    {
        finalColor += envReflection;
    }
    
    // === Debug modes for lighting components
    if (debugMode == 3) // LightingOnly - Only show diffuse
    {
        return float4(diffuseAccum, albedoColor.a);
    }
    else if (debugMode == 4) // SpecularOnly
    {
        return float4(specularAccum, albedoColor.a);
    }
    else if (debugMode == 6) // MaterialsOnly
    {
        float3 materialDebug = float3(
            metallicValue, // R
            1.0 - roughnessValue, // G (inverted to represent smoothness)
            aoValue // B
        );
        return float4(materialDebug, 1.0);
    }
    else if (debugMode == 7) // ShadowsOnly
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    // === Final output color with alpha from albedo
    return float4(finalColor, albedoColor.a);
}