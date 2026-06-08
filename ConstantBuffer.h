// *------------------------------------------------------------------
// Constant Buffers for shaders.
//
// Note: The following structures are aligned to 16 bytes to ensure
// proper alignment for DirectX.  This is important for performance
// and correctness in GPU memory access.
//
// Please see Lights.h for the Lights Constant Buffer structure.
// *------------------------------------------------------------------
#pragma once

#include "Includes.h"

#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
    #include <d3d11.h>
    #include <wrl/client.h>
    #include <DirectXMath.h>
    using namespace DirectX;
#endif

// ConstantBuffer structure — used by all renderers.
// DX11/12: maps to cbuffer register(b0) exactly.
// OpenGL/Vulkan: mirrors layout via std140 UBO (see ModelVertex.glsl).
struct alignas(16) ConstantBuffer
{
    XMMATRIX worldMatrix;                               // World transformation matrix.
    XMMATRIX viewMatrix;                                // View transformation matrix.
    XMMATRIX projectionMatrix;                          // Projection transformation matrix.
    XMFLOAT3 cameraPosition;                            // Camera position in world space.
    float padding;                                      // Padding to align to 16-byte boundary

    XMFLOAT3 modelScale;                                // Scale vector
    float  padding2;
};

// =====================================================================
// GPU Constant Buffer Layouts (Must match ModelPixel.hlsl / ModelPixel.glsl exactly)
// NOTE: These structures exist ONLY to guarantee correct ByteWidth and
//       correct field ordering when mapping constant buffers.
// =====================================================================
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)

// -------------------------------------------------------------
// MaterialGPU - Matches cbuffer MaterialBuffer : register(b4)
// Size: 112 bytes (multiple of 16 required by D3D11 constant buffers)
// -------------------------------------------------------------
struct alignas(16) MaterialGPU
{
    XMFLOAT3 Ka;                                  // Ambient color.
    float pad1;                                   // Padding to 16 byte boundary.
    XMFLOAT3 Kd;                                  // Diffuse color.
    float pad2;                                   // Padding to 16 byte boundary.
    XMFLOAT3 Ks;                                  // Specular color.
    float pad3;                                   // Padding to 16 byte boundary.
    float Ns;                                     // Shininess exponent.
    float Metallic;                               // Base metallic factor.
    float Roughness;                              // Base roughness factor.
    float ReflectionStrength;                     // Reflection strength multiplier.
    float useMetallicMap;                         // 1.0 = use metallic map.
    float useRoughnessMap;                        // 1.0 = use roughness map.
    float useAOMap;                               // 1.0 = use ambient occlusion map.
    float useEnvMap;                              // 1.0 = use environment map.
    XMFLOAT3 EmissiveFactor;                      // Emissive colour (RGB)
    float EmissiveStrength;                       // KHR_materials_emissive_strength multiplier
    float NormalScale;                            // normalTexture.scale (1.0 = default)
    float useDiffuseMap;                          // 1.0 = sample t0 * Kd; 0.0 = use Kd directly (solid colour material)
    float useGlossMap;                            // 1.0 = t6 gloss map active (roughness = 1 - gloss.r)
    float useEmissiveMap;                         // 1.0 = t7 emissive texture active (* EmissiveFactor)
};

// -------------------------------------------------------------
// ShadowBufferGPU - Matches cbuffer ShadowBuffer : register(b6)
// Size: 80 bytes (multiple of 16 required by D3D11 constant buffers)
// -------------------------------------------------------------
struct alignas(16) ShadowBufferGPU
{
    XMMATRIX lightViewProj;                      // Light view-projection matrix (64 bytes)
    float shadowBias;                            // Depth bias to prevent shadow acne
    float shadowStrength;                        // Shadow darkness multiplier [0-1]
    float useShadowMap;                          // 1.0 = shadow map at t8 is bound and active
    float shadowMapSize;                         // Shadow map resolution (e.g. 2048.0) for PCF texel offset
};

// -------------------------------------------------------------
// EnvBufferGPU - Matches cbuffer EnvBuffer : register(b5)
// -------------------------------------------------------------
struct alignas(16) EnvBufferGPU
{
    float envIntensity;                           // Environment intensity.
    XMFLOAT3 envTint;                             // Environment tint.
    float mipLODBias;                             // Mip LOD bias.
    float fresnel0;                               // Fresnel base reflectance.
    XMFLOAT2 padEnv;                              // Padding to 16 byte boundary.
};

#endif // __USE_DIRECTX_11__ || __USE_DIRECTX_12__
