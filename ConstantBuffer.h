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

// ==================================================================
// Corrected MaterialGPU (ConstantBuffer.h)
// Now includes Metallic, Roughness & Reflection factors
// ==================================================================
struct MaterialGPU
{
    XMFLOAT3 Ka;                                        // Ambient color
    float pad1;
    XMFLOAT3 Kd;                                        // Diffuse color
    float pad2;
    XMFLOAT3 Ks;                                        // Specular color
    float pad3;
    float Ns;                                           // Specular exponent (shininess)
    float Metallic;                                     // Base metallic factor [0-1]
    float Roughness;                                    // Base roughness factor [0-1]
    float ReflectionStrength;                           // Global reflection strength multiplier
    float useMetallicMap;                               // Flag for using metallic map
    float useRoughnessMap;                              // Flag for using roughness map
    float useAOMap;                                     // Flag for using ambient occlusion map
    float useEnvMap;                                    // Flag for using environment map
};

// -----------------------------------------------------------
// Environment Settings Buffer
// -----------------------------------------------------------
struct EnvBufferGPU
{
    float envIntensity;                                 // Environment map intensity
    XMFLOAT3 envTint;                                   // Environment map tint color
    float mipLODBias;                                   // Mip level bias for environment sampling
    float fresnel0;                                     // Base fresnel reflectance at normal incidence (F0)
    float _padEnv[2];
};

