#pragma once

#include "Includes.h"
#include "Renderer.h"
#include "Debug.h"
#include "Vector2.h"
#include "Color.h"
#include "ConstantBuffer.h"

using namespace DirectX;

enum class LightType {
    DIRECTIONAL,
    POINT,
    SPOT
};

enum class LightAnimMode : int
{
    None = 0,
    Flicker,
    Pulse,
    Strobe
};

//=============================================================================================================
// LightBuffer - Matches layout in ModelPShader.hlsl (register b1)
//=============================================================================================================
constexpr int MAX_LIGHTS = 8;                                     // This must match the Pixel Shader
constexpr int MAX_GLOBAL_LIGHTS = 8;                              // This must match the Pixel Shader

//=============================================================================================================
// Corrected LightStruct - 256 bytes total (Because we are using arrays
// inside a constant buffer (LightBuffer & GlobalLightBuffer), it will be forced to 256 bytes per LightStruct)
// GPU Padding Enforced to ensure correctness.
//=============================================================================================================
struct LightStruct
{
    XMFLOAT3 position;
    float _pad0;
    XMFLOAT3 direction;
    float _pad1;
    XMFLOAT3 color;
    float _pad2;
    XMFLOAT3 ambient;
    float intensity;
    XMFLOAT3 specularColor;
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
    // ================
    // FINAL ENFORCED GPU-PAD:
    float _pad7[24];                            // EXTRA 96 bytes to reach 256 bytes total
};

struct alignas(16) LightBuffer
{
    int numLights;
    float padding[3];                           // Padding to 16-byte alignment
    LightStruct lights[MAX_LIGHTS];
};

struct alignas(16) GlobalLightBuffer
{
    int numLights;
    float padding[3];                           // Padding to 16-byte alignment
    LightStruct lights[MAX_GLOBAL_LIGHTS];
};

//=============================================================================
// Light Class Declaration
//=============================================================================
class Light {
public:
    Light(const std::string& name, LightStruct myLight);

    void SetPosition(const XMFLOAT3& pos);
    void SetDirection(const XMFLOAT3& dir);
    void SetColor(const XMFLOAT3& color);
    void SetAmbient(const XMFLOAT3& amb);
    void SetIntensity(float intensity);
    void SetRange(float range);
    void SetAngle(float angle);
    void SetActive(bool state);

    LightStruct GetStruct() const;

private:
    LightStruct data;
};

class LightsManager {
public:
    void CreateLight(const std::wstring& name, LightStruct type);
    void UpdateLight(const std::wstring& name, const LightStruct& updatedData);
    bool GetLight(const std::wstring& name, LightStruct& outData);
    void RemoveLight(const std::wstring& name);
    void AnimateLights(float deltaTime);
    int GetLightCount();

    std::vector<LightStruct> GetAllLights();

private:
    std::unordered_map<std::wstring, LightStruct> lightMap;
    std::mutex mtx;
};
