#include "Includes.h"
#include "Lights.h"

Light::Light(const std::string& name, LightStruct myLight) {
    data = myLight;
    data.active = true;
}

void Light::SetPosition(const XMFLOAT3& pos) { data.position = pos; }
void Light::SetDirection(const XMFLOAT3& dir) { data.direction = dir; }
void Light::SetColor(const XMFLOAT3& color) { data.color = color; }
void Light::SetAmbient(const XMFLOAT3& amb) { data.ambient = amb; }
void Light::SetIntensity(float i) { data.intensity = i; }
void Light::SetRange(float r) { data.range = r; }
void Light::SetAngle(float a) { data.angle = a; }
void Light::SetActive(bool state) { data.active = state; }

LightStruct Light::GetStruct() const {
    return data;
}

// -------------------- LightsManager --------------------

void LightsManager::CreateLight(const std::wstring& name, LightStruct type) {
    std::lock_guard<std::mutex> lock(mtx);
    LightStruct light{};
    light = type;
    light.active = true;
    lightMap[name] = light;
}

void LightsManager::UpdateLight(const std::wstring& name, const LightStruct& updatedData) {
    std::lock_guard<std::mutex> lock(mtx);
    if (lightMap.find(name) != lightMap.end()) {
        lightMap[name] = updatedData;
    }
}

bool LightsManager::GetLight(const std::wstring& name, LightStruct& outData) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = lightMap.find(name);
    if (it != lightMap.end()) {
        outData = it->second;
        return true;
    }
    return false;
}

void LightsManager::RemoveLight(const std::wstring& name) {
    std::lock_guard<std::mutex> lock(mtx);
    lightMap.erase(name);
}

std::vector<LightStruct> LightsManager::GetAllLights() {
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<LightStruct> result;
    for (const auto& [_, light] : lightMap) {
        result.push_back(light);
    }
    return result;
}

int LightsManager::GetLightCount() {
	std::lock_guard<std::mutex> lock(mtx);
	return static_cast<int>(lightMap.size());
}

void LightsManager::AnimateLights(float deltaTime)
{
    std::lock_guard<std::mutex> lock(mtx);

    for (auto& [name, light] : lightMap)
    {
        // Ensure baseIntensity is valid if unset (Otherwise we will see black objects) if no other lights been used.
        if (light.baseIntensity == 0.0f && light.intensity > 0.0f)
            light.baseIntensity = light.intensity;

        light.animTimer += deltaTime * light.animSpeed;

        switch (light.animMode)
        {
        case int(LightAnimMode::Pulse):
        {
            float pulse = 0.5f * sinf(light.animTimer * XM_2PI) + 0.5f; // [0,1]
            light.intensity = light.baseIntensity + (pulse * light.animAmplitude);
            break;
        }
        case int(LightAnimMode::Flicker):
        {
            float jitter = static_cast<float>(rand()) / RAND_MAX; // [0,1]
            light.intensity = light.baseIntensity + (jitter - 0.5f) * light.animAmplitude;
            break;
        }
        case int(LightAnimMode::Strobe):
        {
            float toggle = fmodf(light.animTimer, 1.0f);
            light.intensity = (toggle > 0.5f) ? light.baseIntensity : 0.0f;
            break;
        }
        case int(LightAnimMode::None):
        default:
            light.intensity = light.baseIntensity;
            break;
        }

        if (light.animTimer > 10000.0f)
            light.animTimer = 0.0f;
    }
}
