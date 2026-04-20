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

#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>                                // For DirectX math types like XMFLOAT4, XMMATRIX, etc.

using namespace DirectX;

// ConstantBuffer structure.
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
// GPU Constant Buffer Layouts (Must match ModelPixel.hlsl exactly)
// NOTE: These structures exist ONLY to guarantee correct ByteWidth and
//       correct field ordering when mapping constant buffers.
// =====================================================================
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)

// -------------------------------------------------------------
// MaterialGPU - Matches cbuffer MaterialBuffer : register(b4)
// Size: 80 bytes (multiple of 16 required by D3D11 constant buffers)
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
    float pad4;                                   // Padding
    float pad5;                                   // Padding
    float pad6;                                   // Padding — keeps struct at 112 bytes (multiple of 16)
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

#endif



