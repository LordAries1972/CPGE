# Light Class - Complete Usage Guide

## Table of Contents

1. [Introduction](#introduction)
2. [System Architecture Overview](#system-architecture-overview)
3. [Light Class Structure](#light-class-structure)
4. [LightStruct Detailed Reference](#lightstruct-detailed-reference)
5. [LightsManager Class](#lightsmanager-class)
6. [Basic Usage Examples](#basic-usage-examples)
7. [Advanced Lighting Scenarios](#advanced-lighting-scenarios)
8. [Light Animation System](#light-animation-system)
9. [Performance Considerations](#performance-considerations)
10. [Shader Integration](#shader-integration)
11. [Best Practices](#best-practices)
12. [Troubleshooting](#troubleshooting)
13. [Complete Example Implementation](#complete-example-implementation)
14. [Platform-Specific Notes](#platform-specific-notes)

---

## Introduction

The Light Class system provides a comprehensive lighting solution for the game engine, supporting multiple light types, animations, and efficient GPU-based rendering. This system is designed to work seamlessly with DirectX 11/12, OpenGL, and Vulkan rendering backends.

### Key Features

- **Multiple Light Types**: Directional, Point, and Spot lights
- **Light Animation**: Flicker, Pulse, and Strobe effects
- **Thread-Safe Management**: Concurrent light creation and modification
- **GPU-Optimized**: Structured for efficient shader usage
- **Scene Integration**: Works with GLTF scene loading and SceneManager

---

## System Architecture Overview

The lighting system consists of three main components:

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│   Light Class   │───▶│  LightsManager   │───▶│  GPU Buffers    │
│   (Individual)  │    │   (Collection)   │    │  (Rendering)    │
└─────────────────┘    └──────────────────┘    └─────────────────┘
         │                       │                       │
         ▼                       ▼                       ▼
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│   LightStruct   │    │   Thread Safety  │    │ Shader Buffers  │
│   (Data)        │    │   (Mutex)        │    │ (b1 register)   │
└─────────────────┘    └──────────────────┘    └─────────────────┘
```

---

## Light Class Structure

### LightStruct Definition

The `LightStruct` is the core data structure that defines all light properties:

```cpp
struct LightStruct
{
    XMFLOAT3 position;        // World position of the light
    float _pad0;              // GPU padding (required)
    
    XMFLOAT3 direction;       // Light direction (for directional/spot lights)
    float _pad1;              // GPU padding (required)
    
    XMFLOAT3 color;           // RGB color values (0.0-1.0 range)
    float _pad2;              // GPU padding (required)
    
    XMFLOAT3 ambient;         // Ambient contribution
    float intensity;          // Light intensity multiplier
    
    XMFLOAT3 specularColor;   // Specular highlight color
    float _pad3;              // GPU padding (required)
    
    float range;              // Maximum light distance
    float angle;              // Spot light cone angle (radians)
    int type;                 // LightType enum value
    int active;               // 1 = active, 0 = inactive
    
    int animMode;             // LightAnimMode enum value
    float animTimer;          // Current animation time
    float animSpeed;          // Animation speed multiplier
    float baseIntensity;      // Original intensity (for animations)
    
    float animAmplitude;      // Animation strength
    float _pad4;              // GPU padding (required)
    float innerCone;          // Spot light inner cone (full intensity)
    float outerCone;          // Spot light outer cone (falloff)
    
    float lightFalloff;       // Distance falloff factor
    float Shiningness;        // Specular shininess
    float Reflection;         // Reflection factor
    float _pad5[1];           // GPU padding (required)
    
    float _pad6[4];           // Final 16 bytes padding
    float _pad7[24];          // EXTRA 96 bytes to reach 256 bytes total
};
```

### Light Types

```cpp
enum class LightType {
    DIRECTIONAL,    // Sun-like light with parallel rays
    POINT,          // Omnidirectional light source
    SPOT            // Cone-shaped directional light
};
```

### Animation Modes

```cpp
enum class LightAnimMode : int
{
    None = 0,       // No animation
    Flicker,        // Random intensity variation
    Pulse,          // Smooth sine wave intensity
    Strobe          // Sharp on/off flashing
};
```

---

## LightStruct Detailed Reference

### Position and Direction

```cpp
// For Point Lights
light.position = XMFLOAT3(5.0f, 10.0f, 0.0f);  // World position
light.direction = XMFLOAT3(0.0f, 0.0f, 0.0f);  // Not used for point lights

// For Directional Lights (like sun)
light.position = XMFLOAT3(0.0f, 0.0f, 0.0f);   // Position irrelevant
light.direction = XMFLOAT3(0.0f, -1.0f, 0.2f); // Downward angle

// For Spot Lights
light.position = XMFLOAT3(0.0f, 5.0f, 0.0f);   // Light source position
light.direction = XMFLOAT3(0.0f, -1.0f, 0.0f); // Pointing down
```

### Color Properties

```cpp
// Basic white light
light.color = XMFLOAT3(1.0f, 1.0f, 1.0f);
light.ambient = XMFLOAT3(0.1f, 0.1f, 0.1f);    // Subtle ambient
light.specularColor = XMFLOAT3(1.0f, 1.0f, 1.0f);

// Warm orange light (like fire)
light.color = XMFLOAT3(1.0f, 0.6f, 0.2f);
light.ambient = XMFLOAT3(0.2f, 0.1f, 0.05f);
light.specularColor = XMFLOAT3(1.0f, 0.8f, 0.4f);

// Cool blue light (like moonlight)
light.color = XMFLOAT3(0.4f, 0.6f, 1.0f);
light.ambient = XMFLOAT3(0.05f, 0.1f, 0.2f);
light.specularColor = XMFLOAT3(0.6f, 0.8f, 1.0f);
```

### Intensity and Range

```cpp
light.intensity = 1.0f;          // Normal intensity
light.baseIntensity = 1.0f;      // Store original for animations
light.range = 50.0f;             // 50 unit maximum distance
light.lightFalloff = 1.0f;       // Linear falloff (1.0 = linear, 2.0 = quadratic)
```

### Spot Light Configuration

```cpp
light.type = int(LightType::SPOT);
light.angle = XMConvertToRadians(45.0f);        // Total cone angle
light.innerCone = XMConvertToRadians(30.0f);    // Full intensity cone
light.outerCone = XMConvertToRadians(45.0f);    // Falloff to zero
```

---

## LightsManager Class

### Creating Lights

```cpp
LightsManager lightsManager;

// Create a basic point light
LightStruct pointLight = {};
pointLight.type = int(LightType::POINT);
pointLight.position = XMFLOAT3(0.0f, 5.0f, 0.0f);
pointLight.color = XMFLOAT3(1.0f, 1.0f, 1.0f);
pointLight.intensity = 1.0f;
pointLight.range = 30.0f;
pointLight.active = 1;

lightsManager.CreateLight(L"MainPointLight", pointLight);
```

### Managing Lights

```cpp
// Update light properties
LightStruct updatedLight = pointLight;
updatedLight.intensity = 2.0f;
updatedLight.color = XMFLOAT3(1.0f, 0.5f, 0.2f);
lightsManager.UpdateLight(L"MainPointLight", updatedLight);

// Retrieve light for modification
LightStruct currentLight;
if (lightsManager.GetLight(L"MainPointLight", currentLight)) {
    // Modify currentLight properties
    currentLight.animMode = int(LightAnimMode::Flicker);
    lightsManager.UpdateLight(L"MainPointLight", currentLight);
}

// Remove light
lightsManager.RemoveLight(L"MainPointLight");

// Get all lights for rendering
std::vector<LightStruct> allLights = lightsManager.GetAllLights();
```

---

## Basic Usage Examples

### Example 1: Simple Room Lighting

```cpp
void SetupRoomLighting(LightsManager& lightManager)
{
    // Ceiling light (main illumination)
    LightStruct ceilingLight = {};
    ceilingLight.type = int(LightType::POINT);
    ceilingLight.position = XMFLOAT3(0.0f, 8.0f, 0.0f);
    ceilingLight.color = XMFLOAT3(1.0f, 0.95f, 0.8f);      // Warm white
    ceilingLight.ambient = XMFLOAT3(0.1f, 0.1f, 0.1f);
    ceilingLight.specularColor = XMFLOAT3(1.0f, 1.0f, 1.0f);
    ceilingLight.intensity = 1.2f;
    ceilingLight.range = 25.0f;
    ceilingLight.lightFalloff = 2.0f;                       // Quadratic falloff
    ceilingLight.active = 1;
    ceilingLight.animMode = int(LightAnimMode::None);
    
    lightManager.CreateLight(L"CeilingLight", ceilingLight);
    
    // Table lamp (accent lighting)
    LightStruct tableLamp = {};
    tableLamp.type = int(LightType::POINT);
    tableLamp.position = XMFLOAT3(5.0f, 3.0f, -2.0f);
    tableLamp.color = XMFLOAT3(1.0f, 0.7f, 0.3f);          // Warm orange
    tableLamp.ambient = XMFLOAT3(0.05f, 0.03f, 0.01f);
    tableLamp.specularColor = XMFLOAT3(1.0f, 0.8f, 0.5f);
    tableLamp.intensity = 0.8f;
    tableLamp.range = 15.0f;
    tableLamp.lightFalloff = 2.0f;
    tableLamp.active = 1;
    tableLamp.animMode = int(LightAnimMode::None);
    
    lightManager.CreateLight(L"TableLamp", tableLamp);
}
```

### Example 2: Outdoor Scene Lighting

```cpp
void SetupOutdoorLighting(LightsManager& lightManager)
{
    // Sun (directional light)
    LightStruct sunLight = {};
    sunLight.type = int(LightType::DIRECTIONAL);
    sunLight.position = XMFLOAT3(0.0f, 0.0f, 0.0f);        // Position irrelevant
    sunLight.direction = XMFLOAT3(0.3f, -0.8f, 0.5f);      // Angled sunlight
    XMStoreFloat3(&sunLight.direction, 
                  XMVector3Normalize(XMLoadFloat3(&sunLight.direction)));
    sunLight.color = XMFLOAT3(1.0f, 0.95f, 0.8f);          // Slightly warm
    sunLight.ambient = XMFLOAT3(0.2f, 0.2f, 0.25f);        // Skylight ambient
    sunLight.specularColor = XMFLOAT3(1.0f, 1.0f, 0.9f);
    sunLight.intensity = 1.5f;
    sunLight.range = 1000.0f;                               // Very far range
    sunLight.active = 1;
    sunLight.animMode = int(LightAnimMode::None);
    
    lightManager.CreateLight(L"SunLight", sunLight);
    
    // Street lamp (spot light)
    LightStruct streetLamp = {};
    streetLamp.type = int(LightType::SPOT);
    streetLamp.position = XMFLOAT3(-10.0f, 12.0f, 5.0f);
    streetLamp.direction = XMFLOAT3(0.2f, -1.0f, 0.0f);    // Slight forward angle
    XMStoreFloat3(&streetLamp.direction, 
                  XMVector3Normalize(XMLoadFloat3(&streetLamp.direction)));
    streetLamp.color = XMFLOAT3(1.0f, 0.9f, 0.6f);         // Warm street light
    streetLamp.ambient = XMFLOAT3(0.02f, 0.02f, 0.01f);
    streetLamp.specularColor = XMFLOAT3(1.0f, 1.0f, 0.8f);
    streetLamp.intensity = 2.0f;
    streetLamp.range = 40.0f;
    streetLamp.angle = XMConvertToRadians(60.0f);           // Wide cone
    streetLamp.innerCone = XMConvertToRadians(30.0f);      // Sharp center
    streetLamp.outerCone = XMConvertToRadians(60.0f);      // Soft edges
    streetLamp.lightFalloff = 2.0f;
    streetLamp.active = 1;
    streetLamp.animMode = int(LightAnimMode::None);
    
    lightManager.CreateLight(L"StreetLamp01", streetLamp);
}
```

---

## Advanced Lighting Scenarios

### Dynamic Day/Night Cycle

```cpp
class DayNightCycle
{
private:
    LightsManager& lightManager;
    float timeOfDay;        // 0.0 = midnight, 0.5 = noon, 1.0 = midnight
    float cycleSpeed;       // How fast the cycle progresses
    
public:
    DayNightCycle(LightsManager& lm) : lightManager(lm), timeOfDay(0.5f), cycleSpeed(0.01f) {}
    
    void Update(float deltaTime)
    {
        timeOfDay += cycleSpeed * deltaTime;
        if (timeOfDay > 1.0f) timeOfDay -= 1.0f;
        
        UpdateSunlight();
        UpdateMoonlight();
        UpdateStreetLights();
    }
    
private:
    void UpdateSunlight()
    {
        LightStruct sunLight;
        if (!lightManager.GetLight(L"Sun", sunLight)) return;
        
        // Calculate sun angle (rises at 0.25, sets at 0.75)
        float sunPhase = (timeOfDay - 0.25f) * 2.0f;  // -0.5 to 1.5 range
        float sunHeight = sin(sunPhase * XM_PI);       // -1 to 1 range
        
        if (sunHeight > 0.0f) {
            // Sun is above horizon
            sunLight.active = 1;
            sunLight.intensity = sunHeight * 1.5f;
            
            // Calculate sun direction based on time
            float sunAngle = sunPhase * XM_PI;
            sunLight.direction = XMFLOAT3(
                cos(sunAngle) * 0.3f,    // East-West movement
                sin(sunAngle),           // Height
                0.5f                     // North-South bias
            );
            XMStoreFloat3(&sunLight.direction, 
                          XMVector3Normalize(XMLoadFloat3(&sunLight.direction)));
            
            // Color temperature changes throughout day
            float colorTemp = 0.5f + sunHeight * 0.5f;
            sunLight.color = XMFLOAT3(1.0f, 0.8f + colorTemp * 0.2f, 0.6f + colorTemp * 0.4f);
            
            // Ambient lighting intensity
            sunLight.ambient = XMFLOAT3(
                0.1f + sunHeight * 0.3f,
                0.1f + sunHeight * 0.3f,
                0.15f + sunHeight * 0.25f
            );
        } else {
            // Sun is below horizon
            sunLight.active = 0;
        }
        
        lightManager.UpdateLight(L"Sun", sunLight);
    }
    
    void UpdateMoonlight()
    {
        LightStruct moonLight;
        if (!lightManager.GetLight(L"Moon", moonLight)) {
            // Create moon light if it doesn't exist
            moonLight = {};
            moonLight.type = int(LightType::DIRECTIONAL);
            moonLight.color = XMFLOAT3(0.4f, 0.5f, 0.8f);      // Cool blue
            moonLight.ambient = XMFLOAT3(0.02f, 0.03f, 0.05f);
            moonLight.specularColor = XMFLOAT3(0.6f, 0.7f, 1.0f);
            moonLight.range = 1000.0f;
        }
        
        // Moon is opposite to sun
        float moonPhase = timeOfDay < 0.5f ? timeOfDay + 0.5f : timeOfDay - 0.5f;
        float moonHeight = sin((moonPhase - 0.25f) * 2.0f * XM_PI);
        
        if (moonHeight > 0.0f) {
            moonLight.active = 1;
            moonLight.intensity = moonHeight * 0.3f;    // Much dimmer than sun
            
            // Moon direction
            float moonAngle = (moonPhase - 0.25f) * 2.0f * XM_PI;
            moonLight.direction = XMFLOAT3(
                cos(moonAngle) * 0.2f,
                sin(moonAngle),
                0.3f
            );
            XMStoreFloat3(&moonLight.direction, 
                          XMVector3Normalize(XMLoadFloat3(&moonLight.direction)));
        } else {
            moonLight.active = 0;
        }
        
        lightManager.UpdateLight(L"Moon", moonLight);
    }
    
    void UpdateStreetLights()
    {
        // Street lights turn on when sun is low
        float sunPhase = (timeOfDay - 0.25f) * 2.0f;
        float sunHeight = sin(sunPhase * XM_PI);
        
        bool shouldBeOn = sunHeight < 0.2f;  // Turn on during twilight/night
        
        // Update all street lights
        for (int i = 1; i <= 10; ++i) {  // Assuming 10 street lights
            std::wstring lightName = L"StreetLight" + std::to_wstring(i);
            LightStruct streetLight;
            
            if (lightManager.GetLight(lightName, streetLight)) {
                streetLight.active = shouldBeOn ? 1 : 0;
                if (shouldBeOn) {
                    // Intensity varies slightly for realism
                    streetLight.intensity = 1.8f + sin(timeOfDay * 10.0f + i) * 0.2f;
                }
                lightManager.UpdateLight(lightName, streetLight);
            }
        }
    }
};
```

### Interactive Lighting System

```cpp
class InteractiveLightingSystem
{
private:
    LightsManager& lightManager;
    std::map<std::wstring, bool> lightSwitches;
    
public:
    InteractiveLightingSystem(LightsManager& lm) : lightManager(lm) {}
    
    // Toggle light on/off
    void ToggleLight(const std::wstring& lightName)
    {
        LightStruct light;
        if (lightManager.GetLight(lightName, light)) {
            light.active = light.active ? 0 : 1;
            lightManager.UpdateLight(lightName, light);
            lightSwitches[lightName] = light.active != 0;
        }
    }
    
    // Dim light to specific intensity
    void DimLight(const std::wstring& lightName, float targetIntensity, float duration)
    {
        LightStruct light;
        if (lightManager.GetLight(lightName, light)) {
            // Store original intensity if not already dimming
            if (light.animMode == int(LightAnimMode::None)) {
                light.baseIntensity = light.intensity;
            }
            
            // Set up custom animation for dimming
            light.animMode = int(LightAnimMode::Pulse);  // Reuse pulse for smooth transition
            light.animSpeed = 1.0f / duration;           // Complete transition in 'duration' seconds
            light.animAmplitude = targetIntensity - light.baseIntensity;
            light.animTimer = 0.0f;
            
            lightManager.UpdateLight(lightName, light);
        }
    }
    
    // Change light color with smooth transition
    void ChangeColor(const std::wstring& lightName, XMFLOAT3 newColor, float duration)
    {
        // This would require additional fields in LightStruct for color animation
        // For now, we'll do immediate color change
        LightStruct light;
        if (lightManager.GetLight(lightName, light)) {
            light.color = newColor;
            lightManager.UpdateLight(lightName, light);
        }
    }
    
    // Create emergency lighting (red, flashing)
    void ActivateEmergencyLighting()
    {
        // Get all lights and convert them to emergency mode
        std::vector<LightStruct> allLights = lightManager.GetAllLights();
        
        for (size_t i = 0; i < allLights.size(); ++i) {
            LightStruct& light = allLights[i];
            
            // Skip directional lights (sun/moon)
            if (light.type == int(LightType::DIRECTIONAL)) continue;
            
            // Store original properties
            light.baseIntensity = light.intensity;
            
            // Set emergency properties
            light.color = XMFLOAT3(1.0f, 0.1f, 0.1f);        // Red color
            light.intensity = 0.5f;
            light.animMode = int(LightAnimMode::Strobe);      // Flashing
            light.animSpeed = 2.0f;                           // Fast flashing
            light.animAmplitude = 1.0f;                       // Full intensity variation
            light.active = 1;
            
            // Update the light (would need light name for this to work)
            // This is a simplified example - in practice you'd need to track light names
        }
    }
};
```

---

## Light Animation System

### Animation Implementation

The animation system updates light properties over time. Here's how to implement custom animations:

```cpp
void LightsManager::AnimateLights(float deltaTime)
{
    std::lock_guard<std::mutex> lock(mtx);
    
    for (auto& lightPair : lightMap) {
        LightStruct& light = lightPair.second;
        
        if (light.animMode == int(LightAnimMode::None) || light.active == 0) {
            continue;
        }
        
        light.animTimer += deltaTime * light.animSpeed;
        
        switch (static_cast<LightAnimMode>(light.animMode)) {
            case LightAnimMode::Flicker:
                ApplyFlickerAnimation(light, deltaTime);
                break;
                
            case LightAnimMode::Pulse:
                ApplyPulseAnimation(light, deltaTime);
                break;
                
            case LightAnimMode::Strobe:
                ApplyStrobeAnimation(light, deltaTime);
                break;
        }
    }
}

void ApplyFlickerAnimation(LightStruct& light, float deltaTime)
{
    // Random flicker using noise function
    float flicker = (sin(light.animTimer * 10.0f) + 
                    sin(light.animTimer * 15.7f) + 
                    sin(light.animTimer * 23.1f)) / 3.0f;
    
    flicker *= light.animAmplitude;  // Scale by animation strength
    
    light.intensity = light.baseIntensity + flicker * 0.3f;
    light.intensity = max(0.0f, light.intensity);  // Prevent negative intensity
}

void ApplyPulseAnimation(LightStruct& light, float deltaTime)
{
    // Smooth sine wave pulse
    float pulse = sin(light.animTimer * 2.0f * XM_PI);
    pulse *= light.animAmplitude;
    
    light.intensity = light.baseIntensity + pulse * 0.5f;
    light.intensity = max(0.0f, light.intensity);
}

void ApplyStrobeAnimation(LightStruct& light, float deltaTime)
{
    // Sharp on/off strobe
    float strobe = fmod(light.animTimer, 1.0f);
    
    if (strobe < 0.1f) {  // 10% on time
        light.intensity = light.baseIntensity + light.animAmplitude;
    } else {
        light.intensity = light.baseIntensity * 0.1f;  // Very dim when off
    }
}
```

### Custom Animation Examples

```cpp
void SetupCampfireLight(LightsManager& lightManager)
{
    LightStruct campfire = {};
    campfire.type = int(LightType::POINT);
    campfire.position = XMFLOAT3(0.0f, 1.0f, 0.0f);
    campfire.color = XMFLOAT3(1.0f, 0.4f, 0.1f);           // Orange flame color
    campfire.ambient = XMFLOAT3(0.1f, 0.02f, 0.01f);
    campfire.specularColor = XMFLOAT3(1.0f, 0.6f, 0.2f);
    campfire.intensity = 1.5f;
    campfire.baseIntensity = 1.5f;
    campfire.range = 20.0f;
    campfire.lightFalloff = 2.0f;
    campfire.active = 1;
    
    // Campfire flicker settings
    campfire.animMode = int(LightAnimMode::Flicker);
    campfire.animSpeed = 1.0f;                              // Normal speed
    campfire.animAmplitude = 0.8f;                          // Strong variation
    campfire.animTimer = 0.0f;
    
    lightManager.CreateLight(L"Campfire", campfire);
}

void SetupPoliceCarLights(LightsManager& lightManager)
{
    // Red light
    LightStruct redLight = {};
    redLight.type = int(LightType::SPOT);
    redLight.position = XMFLOAT3(-1.0f, 2.0f, 5.0f);
    redLight.direction = XMFLOAT3(0.0f, -0.5f, -1.0f);
    XMStoreFloat3(&redLight.direction, 
                  XMVector3Normalize(XMLoadFloat3(&redLight.direction)));
    redLight.color = XMFLOAT3(1.0f, 0.0f, 0.0f);           // Pure red
    redLight.intensity = 2.0f;
    redLight.baseIntensity = 2.0f;
    redLight.range = 30.0f;
    redLight.angle = XMConvertToRadians(45.0f);
    redLight.innerCone = XMConvertToRadians(20.0f);
    redLight.outerCone = XMConvertToRadians(45.0f);
    redLight.active = 1;
    
    // Fast strobe
    redLight.animMode = int(LightAnimMode::Strobe);
    redLight.animSpeed = 4.0f;                              // Very fast
    redLight.animAmplitude = 1.0f;
    
    lightManager.CreateLight(L"PoliceRed", redLight);
    
    // Blue light (opposite timing)
    LightStruct blueLight = redLight;
    blueLight.position = XMFLOAT3(1.0f, 2.0f, 5.0f);
    blueLight.color = XMFLOAT3(0.0f, 0.0f, 1.0f);          // Pure blue
    blueLight.animTimer = 0.5f;                             // Start 180° out of phase
    
    lightManager.CreateLight(L"PoliceBlue", blueLight);
}
```

---

## Performance Considerations

### GPU Buffer Management

The light system is designed for efficient GPU usage:

```cpp
// Maximum lights that can be rendered simultaneously
constexpr int MAX_LIGHTS = 8;          // Local lights per object
constexpr int MAX_GLOBAL_LIGHTS = 8;   // Scene-wide global lights

// Buffer structures for GPU
struct alignas(16) LightBuffer
{
    int numLights;
    float padding[3];
    LightStruct lights[MAX_LIGHTS];     // 256 bytes each = 2KB total
};

struct alignas(16) GlobalLightBuffer  
{
    int numLights;
    float padding[3];
    LightStruct lights[MAX_GLOBAL_LIGHTS];  // 256 bytes each = 2KB total
};
```

### Performance Optimization Tips

```cpp
class OptimizedLightManager
{
private:
    std::vector<LightStruct> activeLights;      // Only active lights
    std::vector<LightStruct> staticLights;      // Non-animated lights
    std::vector<LightStruct> animatedLights;    // Lights requiring updates
    bool lightsDirty = true;                    // Needs GPU buffer update
    
public:
    void OptimizeForRendering()
    {
        if (!lightsDirty) return;
        
        activeLights.clear();
        staticLights.clear();
        animatedLights.clear();
        
        // Separate lights by type for efficient processing
        for (const auto& lightPair : lightMap) {
            const LightStruct& light = lightPair.second;
            
            if (light.active == 0) continue;  // Skip inactive lights
            
            activeLights.push_back(light);
            
            if (light.animMode == int(LightAnimMode::None)) {
                staticLights.push_back(light);
            } else {
                animatedLights.push_back(light);
            }
        }
        
        // Sort by importance (distance to camera, intensity, etc.)
        SortLightsByImportance();
        
        lightsDirty = false;
    }
    
private:
    void SortLightsByImportance()
    {
        // Sort by intensity * range (approximate influence)
        std::sort(activeLights.begin(), activeLights.end(),
            [](const LightStruct& a, const LightStruct& b) {
                float scoreA = a.intensity * a.range;
                float scoreB = b.intensity * b.range;
                return scoreA > scoreB;
            });
    }
};
```

### Level-of-Detail (LOD) for Lights

```cpp
class LightLODSystem
{
private:
    struct LightLOD {
        float highDetailDistance = 50.0f;      // Full quality
        float mediumDetailDistance = 100.0f;   // Reduced quality
        float lowDetailDistance = 200.0f;      // Basic lighting only
    };
    
    LightLOD lodSettings;
    XMFLOAT3 cameraPosition;
    
public:
    void UpdateLightLOD(const XMFLOAT3& camPos, LightsManager& lightManager)
    {
        cameraPosition = camPos;
        std::vector<LightStruct> allLights = lightManager.GetAllLights();
        
        for (size_t i = 0; i < allLights.size(); ++i) {
            LightStruct& light = allLights[i];
            float distance = CalculateDistance(camPos, light.position);
            
            if (distance > lodSettings.lowDetailDistance) {
                // Very far - disable light
                light.active = 0;
            }
            else if (distance > lodSettings.mediumDetailDistance) {
                // Far - basic lighting only
                light.active = 1;
                light.animMode = int(LightAnimMode::None);  // Disable animations
                light.intensity *= 0.5f;                    // Reduce intensity
            }
            else if (distance > lodSettings.highDetailDistance) {
                // Medium distance - reduced quality
                light.active = 1;
                if (light.animMode != int(LightAnimMode::None)) {
                    light.animSpeed *= 0.5f;  // Slower animations
                }
            }
            // Close distance - full quality (no changes needed)
        }
    }
    
private:
    float CalculateDistance(const XMFLOAT3& a, const XMFLOAT3& b)
    {
        XMVECTOR va = XMLoadFloat3(&a);
        XMVECTOR vb = XMLoadFloat3(&b);
        return XMVectorGetX(XMVector3Length(XMVectorSubtract(va, vb)));
    }
};
```

---

## Shader Integration

### HLSL Shader Setup

The light system integrates with shaders through constant buffers:

```hlsl
// ModelPShader.hlsl - Pixel Shader Light Buffer
cbuffer LightBuffer : register(b1)
{
    int numLights;
    float3 padding;
    LightStruct lights[8];  // MAX_LIGHTS = 8
};

cbuffer GlobalLightBuffer : register(b2)
{
    int numGlobalLights;
    float3 globalPadding;
    LightStruct globalLights[8];  // MAX_GLOBAL_LIGHTS = 8
};

// Light calculation function
float3 CalculateLighting(float3 worldPos, float3 normal, float3 viewDir, 
                        float3 albedo, float metallic, float roughness)
{
    float3 finalColor = float3(0, 0, 0);
    
    // Process global lights (sun, moon, etc.)
    for (int i = 0; i < numGlobalLights; ++i)
    {
        if (globalLights[i].active == 0) continue;
        finalColor += CalculateSingleLight(globalLights[i], worldPos, normal, 
                                         viewDir, albedo, metallic, roughness);
    }
    
    // Process local lights (point, spot lights)
    for (int i = 0; i < numLights; ++i)
    {
        if (lights[i].active == 0) continue;
        finalColor += CalculateSingleLight(lights[i], worldPos, normal, 
                                         viewDir, albedo, metallic, roughness);
    }
    
    return finalColor;
}

float3 CalculateSingleLight(LightStruct light, float3 worldPos, float3 normal,
                           float3 viewDir, float3 albedo, float metallic, float roughness)
{
    float3 lightDir;
    float attenuation = 1.0;
    
    // Calculate light direction and attenuation based on type
    if (light.type == 0) // DIRECTIONAL
    {
        lightDir = normalize(-light.direction.xyz);
        // No attenuation for directional lights
    }
    else if (light.type == 1) // POINT
    {
        float3 lightVec = light.position.xyz - worldPos;
        float distance = length(lightVec);
        lightDir = lightVec / distance;
        
        // Distance attenuation
        attenuation = 1.0 / (1.0 + light.lightFalloff * distance * distance / (light.range * light.range));
        attenuation = saturate(attenuation);
    }
    else if (light.type == 2) // SPOT
    {
        float3 lightVec = light.position.xyz - worldPos;
        float distance = length(lightVec);
        lightDir = lightVec / distance;
        
        // Distance attenuation
        attenuation = 1.0 / (1.0 + light.lightFalloff * distance * distance / (light.range * light.range));
        
        // Spot light cone attenuation
        float3 spotDir = normalize(light.direction.xyz);
        float cosAngle = dot(-lightDir, spotDir);
        float outerCos = cos(light.outerCone);
        float innerCos = cos(light.innerCone);
        
        float spotAttenuation = saturate((cosAngle - outerCos) / (innerCos - outerCos));
        attenuation *= spotAttenuation;
    }
    
    // Calculate lighting components
    float NdotL = saturate(dot(normal, lightDir));
    
    // Diffuse
    float3 diffuse = albedo * light.color.rgb * light.intensity * NdotL;
    
    // Specular (simplified Blinn-Phong)
    float3 halfDir = normalize(lightDir + viewDir);
    float NdotH = saturate(dot(normal, halfDir));
    float specularPower = (1.0 - roughness) * 128.0;
    float3 specular = light.specularColor.rgb * pow(NdotH, specularPower) * light.intensity;
    
    // Ambient
    float3 ambient = albedo * light.ambient.rgb;
    
    return (diffuse + specular + ambient) * attenuation;
}
```

### C++ Shader Buffer Updates

```cpp
class LightBufferManager
{
private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> lightConstantBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> globalLightConstantBuffer;
    LightBuffer currentLightBuffer;
    GlobalLightBuffer currentGlobalLightBuffer;
    bool lightBufferDirty = true;
    bool globalLightBufferDirty = true;
    
public:
    bool Initialize(ID3D11Device* device)
    {
        // Create light constant buffer
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        bufferDesc.ByteWidth = sizeof(LightBuffer);
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        
        HRESULT hr = device->CreateBuffer(&bufferDesc, nullptr, &lightConstantBuffer);
        if (FAILED(hr)) return false;
        
        // Create global light constant buffer
        bufferDesc.ByteWidth = sizeof(GlobalLightBuffer);
        hr = device->CreateBuffer(&bufferDesc, nullptr, &globalLightConstantBuffer);
        if (FAILED(hr)) return false;
        
        return true;
    }
    
    void UpdateLightBuffers(ID3D11DeviceContext* context, 
                           const std::vector<LightStruct>& localLights,
                           const std::vector<LightStruct>& globalLights)
    {
        // Update local lights buffer
        if (lightBufferDirty || HasLightsChanged(localLights)) {
            UpdateLocalLightBuffer(context, localLights);
            lightBufferDirty = false;
        }
        
        // Update global lights buffer
        if (globalLightBufferDirty || HasGlobalLightsChanged(globalLights)) {
            UpdateGlobalLightBuffer(context, globalLights);
            globalLightBufferDirty = false;
        }
    }
    
    void BindBuffers(ID3D11DeviceContext* context)
    {
        ID3D11Buffer* buffers[] = { lightConstantBuffer.Get(), globalLightConstantBuffer.Get() };
        context->PSSetConstantBuffers(1, 2, buffers);  // Bind to slots b1 and b2
    }
    
private:
    void UpdateLocalLightBuffer(ID3D11DeviceContext* context, 
                               const std::vector<LightStruct>& lights)
    {
        // Copy lights to buffer (max MAX_LIGHTS)
        currentLightBuffer.numLights = min((int)lights.size(), MAX_LIGHTS);
        
        for (int i = 0; i < currentLightBuffer.numLights; ++i) {
            currentLightBuffer.lights[i] = lights[i];
        }
        
        // Update GPU buffer
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = context->Map(lightConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        if (SUCCEEDED(hr)) {
            memcpy(mappedResource.pData, &currentLightBuffer, sizeof(LightBuffer));
            context->Unmap(lightConstantBuffer.Get(), 0);
        }
    }
    
    void UpdateGlobalLightBuffer(ID3D11DeviceContext* context, 
                                const std::vector<LightStruct>& globalLights)
    {
        // Copy global lights to buffer (max MAX_GLOBAL_LIGHTS)
        currentGlobalLightBuffer.numLights = min((int)globalLights.size(), MAX_GLOBAL_LIGHTS);
        
        for (int i = 0; i < currentGlobalLightBuffer.numLights; ++i) {
            currentGlobalLightBuffer.lights[i] = globalLights[i];
        }
        
        // Update GPU buffer
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = context->Map(globalLightConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        if (SUCCEEDED(hr)) {
            memcpy(mappedResource.pData, &currentGlobalLightBuffer, sizeof(GlobalLightBuffer));
            context->Unmap(globalLightConstantBuffer.Get(), 0);
        }
    }
    
    bool HasLightsChanged(const std::vector<LightStruct>& lights)
    {
        if ((int)lights.size() != currentLightBuffer.numLights) return true;
        
        for (int i = 0; i < currentLightBuffer.numLights; ++i) {
            if (memcmp(&lights[i], &currentLightBuffer.lights[i], sizeof(LightStruct)) != 0) {
                return true;
            }
        }
        return false;
    }
    
    bool HasGlobalLightsChanged(const std::vector<LightStruct>& globalLights)
    {
        if ((int)globalLights.size() != currentGlobalLightBuffer.numLights) return true;
        
        for (int i = 0; i < currentGlobalLightBuffer.numLights; ++i) {
            if (memcmp(&globalLights[i], &currentGlobalLightBuffer.lights[i], sizeof(LightStruct)) != 0) {
                return true;
            }
        }
        return false;
    }
};
```

---

## Best Practices

### 1. Light Organization

```cpp
// Organize lights by functionality
class GameLightingSystem
{
private:
    LightsManager& lightManager;
    
    // Different categories of lights
    std::vector<std::wstring> environmentLights;    // Sun, moon, sky
    std::vector<std::wstring> staticLights;         // Building lights, street lamps
    std::vector<std::wstring> dynamicLights;        // Vehicle lights, effects
    std::vector<std::wstring> playerLights;         // Flashlight, weapon muzzle flash
    
public:
    void OrganizeLights()
    {
        // Environment lights (highest priority)
        environmentLights = { L"Sun", L"Moon", L"SkyAmbient" };
        
        // Static scene lights (medium priority)
        staticLights = { L"StreetLamp01", L"StreetLamp02", L"BuildingLight01" };
        
        // Dynamic gameplay lights (variable priority)
        dynamicLights = { L"CarHeadlights", L"Explosion01", L"Campfire" };
        
        // Player-related lights (high priority when near player)
        playerLights = { L"Flashlight", L"MuzzleFlash" };
    }
    
    void UpdateLightPriorities(const XMFLOAT3& playerPosition)
    {
        // Update light importance based on distance to player
        for (const auto& lightName : dynamicLights) {
            LightStruct light;
            if (lightManager.GetLight(lightName, light)) {
                float distance = CalculateDistance(playerPosition, light.position);
                
                // Reduce intensity for very distant lights
                if (distance > 100.0f) {
                    light.intensity *= 0.5f;
                } else if (distance > 200.0f) {
                    light.active = 0;  // Disable very far lights
                }
                
                lightManager.UpdateLight(lightName, light);
            }
        }
    }
    
private:
    float CalculateDistance(const XMFLOAT3& a, const XMFLOAT3& b)
    {
        XMVECTOR va = XMLoadFloat3(&a);
        XMVECTOR vb = XMLoadFloat3(&b);
        return XMVectorGetX(XMVector3Length(XMVectorSubtract(va, vb)));
    }
};
```

### 2. Memory Management

```cpp
class LightMemoryManager
{
private:
    static constexpr size_t MAX_TOTAL_LIGHTS = 256;
    static constexpr size_t LIGHT_POOL_SIZE = MAX_TOTAL_LIGHTS * sizeof(LightStruct);
    
    std::unique_ptr<uint8_t[]> lightPool;
    std::bitset<MAX_TOTAL_LIGHTS> usedSlots;
    size_t nextFreeSlot = 0;
    
public:
    LightMemoryManager()
    {
        lightPool = std::make_unique<uint8_t[]>(LIGHT_POOL_SIZE);
    }
    
    LightStruct* AllocateLight()
    {
        // Find next free slot
        for (size_t i = nextFreeSlot; i < MAX_TOTAL_LIGHTS; ++i) {
            if (!usedSlots[i]) {
                usedSlots[i] = true;
                nextFreeSlot = i + 1;
                
                // Return pointer to light in pool
                return reinterpret_cast<LightStruct*>(
                    lightPool.get() + (i * sizeof(LightStruct))
                );
            }
        }
        
        // Pool is full
        return nullptr;
    }
    
    void DeallocateLight(LightStruct* light)
    {
        if (!light) return;
        
        // Calculate slot index
        size_t offset = reinterpret_cast<uint8_t*>(light) - lightPool.get();
        size_t slot = offset / sizeof(LightStruct);
        
        if (slot < MAX_TOTAL_LIGHTS) {
            usedSlots[slot] = false;
            nextFreeSlot = min(nextFreeSlot, slot);
            
            // Clear the light data
            memset(light, 0, sizeof(LightStruct));
        }
    }
    
    size_t GetUsedLightCount() const
    {
        return usedSlots.count();
    }
    
    float GetMemoryUsagePercent() const
    {
        return (float)GetUsedLightCount() / MAX_TOTAL_LIGHTS * 100.0f;
    }
};
```

### 3. Configuration Management

```cpp
class LightingConfiguration
{
public:
    struct Settings {
        int maxActiveLights = 8;                // GPU buffer limit
        float defaultLightRange = 50.0f;        // Default range for new lights
        float animationUpdateRate = 60.0f;      // Animations per second
        bool enableLightLOD = true;             // Distance-based optimization
        bool enableShadows = true;              // Shadow casting
        float ambientLightLevel = 0.1f;         // Global ambient
        float lightIntensityScale = 1.0f;       // Global intensity multiplier
    };
    
private:
    Settings settings;
    std::string configFilePath;
    
public:
    LightingConfiguration(const std::string& configPath = "lighting_config.json")
        : configFilePath(configPath)
    {
        LoadFromFile();
    }
    
    void LoadFromFile()
    {
        // Load settings from JSON file
        std::ifstream file(configFilePath);
        if (!file.is_open()) {
            SaveToFile();  // Create default config
            return;
        }
        
        try {
            nlohmann::json j;
            file >> j;
            
            settings.maxActiveLights = j.value("maxActiveLights", 8);
            settings.defaultLightRange = j.value("defaultLightRange", 50.0f);
            settings.animationUpdateRate = j.value("animationUpdateRate", 60.0f);
            settings.enableLightLOD = j.value("enableLightLOD", true);
            settings.enableShadows = j.value("enableShadows", true);
            settings.ambientLightLevel = j.value("ambientLightLevel", 0.1f);
            settings.lightIntensityScale = j.value("lightIntensityScale", 1.0f);
        }
        catch (const std::exception& e) {
            // Use defaults if parsing fails
        }
    }
    
    void SaveToFile()
    {
        nlohmann::json j;
        j["maxActiveLights"] = settings.maxActiveLights;
        j["defaultLightRange"] = settings.defaultLightRange;
        j["animationUpdateRate"] = settings.animationUpdateRate;
        j["enableLightLOD"] = settings.enableLightLOD;
        j["enableShadows"] = settings.enableShadows;
        j["ambientLightLevel"] = settings.ambientLightLevel;
        j["lightIntensityScale"] = settings.lightIntensityScale;
        
        std::ofstream file(configFilePath);
        if (file.is_open()) {
            file << j.dump(4);  // Pretty print with 4-space indentation
        }
    }
    
    const Settings& GetSettings() const { return settings; }
    Settings& GetSettings() { return settings; }
};
```

---

## Troubleshooting

### Common Issues and Solutions

#### 1. Lights Not Appearing

**Problem**: Lights are created but don't affect rendering.

**Solutions**:
```cpp
void DiagnoseLightIssues(const std::wstring& lightName, LightsManager& lightManager)
{
    LightStruct light;
    if (!lightManager.GetLight(lightName, light)) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, 
            L"Light '%ls' does not exist!", lightName.c_str());
        return;
    }
    
    // Check if light is active
    if (light.active == 0) {
        debug.logDebugMessage(LogLevel::LOG_WARNING, 
            L"Light '%ls' is inactive!", lightName.c_str());
    }
    
    // Check intensity
    if (light.intensity <= 0.0f) {
        debug.logDebugMessage(LogLevel::LOG_WARNING, 
            L"Light '%ls' has zero or negative intensity: %f", 
            lightName.c_str(), light.intensity);
    }
    
    // Check range
    if (light.range <= 0.0f) {
        debug.logDebugMessage(LogLevel::LOG_WARNING, 
            L"Light '%ls' has invalid range: %f", 
            lightName.c_str(), light.range);
    }
    
    // Check if light type is valid
    if (light.type < 0 || light.type > 2) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, 
            L"Light '%ls' has invalid type: %d", 
            lightName.c_str(), light.type);
    }
    
    // Check position (for non-directional lights)
    if (light.type != int(LightType::DIRECTIONAL)) {
        float distance = sqrt(light.position.x * light.position.x + 
                             light.position.y * light.position.y + 
                             light.position.z * light.position.z);
        if (distance > 10000.0f) {
            debug.logDebugMessage(LogLevel::LOG_WARNING, 
                L"Light '%ls' is very far from origin: distance = %f", 
                lightName.c_str(), distance);
        }
    }
}
```

#### 2. Performance Problems

**Problem**: Frame rate drops when many lights are active.

**Solutions**:
```cpp
class LightPerformanceProfiler
{
private:
    std::chrono::high_resolution_clock::time_point lastUpdate;
    float lightUpdateTime = 0.0f;
    int activeLightCount = 0;
    
public:
    void StartProfiling()
    {
        lastUpdate = std::chrono::high_resolution_clock::now();
    }
    
    void EndProfiling(int lightCount)
    {
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - lastUpdate);
        
        lightUpdateTime = duration.count() / 1000.0f;  // Convert to milliseconds
        activeLightCount = lightCount;
        
        // Log performance warnings
        if (lightUpdateTime > 5.0f) {  // More than 5ms for light updates
            debug.logDebugMessage(LogLevel::LOG_WARNING, 
                L"Light update taking too long: %.2fms for %d lights", 
                lightUpdateTime, activeLightCount);
        }
        
        if (activeLightCount > 16) {  // Too many active lights
            debug.logDebugMessage(LogLevel::LOG_WARNING, 
                L"Too many active lights: %d (consider LOD system)", 
                activeLightCount);
        }
    }
    
    void OptimizePerformance(LightsManager& lightManager)
    {
        if (lightUpdateTime > 10.0f || activeLightCount > 20) {
            // Emergency optimization: disable distant lights
            std::vector<LightStruct> allLights = lightManager.GetAllLights();
            
            // Sort by distance from camera (would need camera position)
            // Disable furthest lights
            
            debug.logLevelMessage(LogLevel::LOG_INFO, 
                L"Emergency light optimization activated");
        }
    }
};
```

#### 3. Animation Issues

**Problem**: Light animations not working or appearing jerky.

**Solutions**:
```cpp
void FixAnimationIssues(LightsManager& lightManager, float deltaTime)
{
    // Check if deltaTime is reasonable
    if (deltaTime > 0.1f) {  // More than 100ms frame time
        debug.logDebugMessage(LogLevel::LOG_WARNING, 
            L"Large deltaTime detected: %.3fs - may cause animation issues", 
            deltaTime);
        
        // Clamp deltaTime to prevent large animation jumps
        deltaTime = min(deltaTime, 0.033f);  // Max 30 FPS worth of time
    }
    
    if (deltaTime <= 0.0f) {
        debug.logDebugMessage(LogLevel::LOG_WARNING, 
            L"Invalid deltaTime: %.6f", deltaTime);
        return;
    }
    
    // Animate lights with clamped deltaTime
    lightManager.AnimateLights(deltaTime);
}
```

---

## Complete Example Implementation

### Full Game Scene Setup

```cpp
class GameScene
{
private:
    LightsManager lightManager;
    LightingConfiguration config;
    DayNightCycle dayNight;
    InteractiveLightingSystem interactives;
    LightPerformanceProfiler profiler;
    LightBufferManager bufferManager;
    
public:
    GameScene() : dayNight(lightManager), interactives(lightManager)
    {
        SetupSceneLighting();
    }
    
    bool Initialize(ID3D11Device* device)
    {
        return bufferManager.Initialize(device);
    }
    
    void SetupSceneLighting()
    {
        // Environment lighting
        SetupEnvironmentLights();
        
        // Static scene lights
        SetupStaticLights();
        
        // Interactive lights
        SetupInteractiveLights();
        
        // Dynamic effects
        SetupEffectLights();
    }
    
    void Update(float deltaTime, const XMFLOAT3& cameraPosition)
    {
        profiler.StartProfiling();
        
        // Update day/night cycle
        dayNight.Update(deltaTime);
        
        // Update light animations
        lightManager.AnimateLights(deltaTime);
        
        // Update light LOD based on camera position
        UpdateLightLOD(cameraPosition);
        
        profiler.EndProfiling(lightManager.GetLightCount());
        
        // Performance optimization if needed
        profiler.OptimizePerformance(lightManager);
    }
    
    void Render(ID3D11DeviceContext* context)
    {
        // Get lights for rendering
        std::vector<LightStruct> sceneLights = lightManager.GetAllLights();
        
        // Separate into local and global lights
        std::vector<LightStruct> localLights;
        std::vector<LightStruct> globalLights;
        
        for (const auto& light : sceneLights) {
            if (light.type == int(LightType::DIRECTIONAL)) {
                globalLights.push_back(light);
            } else {
                localLights.push_back(light);
            }
        }
        
        // Update GPU buffers
        bufferManager.UpdateLightBuffers(context, localLights, globalLights);
        bufferManager.BindBuffers(context);
    }
    
private:
    void SetupEnvironmentLights()
    {
        // Sun
        LightStruct sun = {};
        sun.type = int(LightType::DIRECTIONAL);
        sun.direction = XMFLOAT3(0.3f, -0.8f, 0.5f);
        XMStoreFloat3(&sun.direction, XMVector3Normalize(XMLoadFloat3(&sun.direction)));
        sun.color = XMFLOAT3(1.0f, 0.95f, 0.8f);
        sun.ambient = XMFLOAT3(0.2f, 0.2f, 0.25f);
        sun.specularColor = XMFLOAT3(1.0f, 1.0f, 0.9f);
        sun.intensity = 1.5f;
        sun.range = 1000.0f;
        sun.active = 1;
        lightManager.CreateLight(L"Sun", sun);
        
        // Moon
        LightStruct moon = {};
        moon.type = int(LightType::DIRECTIONAL);
        moon.direction = XMFLOAT3(-0.2f, -0.6f, -0.3f);
        XMStoreFloat3(&moon.direction, XMVector3Normalize(XMLoadFloat3(&moon.direction)));
        moon.color = XMFLOAT3(0.4f, 0.5f, 0.8f);
        moon.ambient = XMFLOAT3(0.02f, 0.03f, 0.05f);
        moon.specularColor = XMFLOAT3(0.6f, 0.7f, 1.0f);
        moon.intensity = 0.3f;
        moon.range = 1000.0f;
        moon.active = 0;  // Will be controlled by day/night cycle
        lightManager.CreateLight(L"Moon", moon);
    }
    
    void SetupStaticLights()
    {
        // Street lamps
        for (int i = 0; i < 5; ++i) {
            LightStruct streetLamp = {};
            streetLamp.type = int(LightType::SPOT);
            streetLamp.position = XMFLOAT3(i * 20.0f - 40.0f, 8.0f, 10.0f);
            streetLamp.direction = XMFLOAT3(0.1f, -1.0f, 0.0f);
            XMStoreFloat3(&streetLamp.direction, 
                          XMVector3Normalize(XMLoadFloat3(&streetLamp.direction)));
            streetLamp.color = XMFLOAT3(1.0f, 0.9f, 0.6f);
            streetLamp.ambient = XMFLOAT3(0.02f, 0.02f, 0.01f);
            streetLamp.specularColor = XMFLOAT3(1.0f, 1.0f, 0.8f);
            streetLamp.intensity = 2.0f;
            streetLamp.range = 25.0f;
            streetLamp.angle = XMConvertToRadians(50.0f);
            streetLamp.innerCone = XMConvertToRadians(25.0f);
            streetLamp.outerCone = XMConvertToRadians(50.0f);
            streetLamp.lightFalloff = 2.0f;
            streetLamp.active = 1;
            streetLamp.animMode = int(LightAnimMode::None);
            
            std::wstring lightName = L"StreetLamp" + std::to_wstring(i + 1);
            lightManager.CreateLight(lightName, streetLamp);
        }
        
        // Building interior lights
        for (int i = 0; i < 3; ++i) {
            LightStruct buildingLight = {};
            buildingLight.type = int(LightType::POINT);
            buildingLight.position = XMFLOAT3(30.0f + i * 15.0f, 6.0f, -20.0f);
            buildingLight.color = XMFLOAT3(1.0f, 0.95f, 0.85f);
            buildingLight.ambient = XMFLOAT3(0.05f, 0.05f, 0.04f);
            buildingLight.specularColor = XMFLOAT3(1.0f, 1.0f, 0.9f);
            buildingLight.intensity = 1.0f;
            buildingLight.range = 20.0f;
            buildingLight.lightFalloff = 1.5f;
            buildingLight.active = 1;
            buildingLight.animMode = int(LightAnimMode::None);
            
            std::wstring lightName = L"BuildingLight" + std::to_wstring(i + 1);
            lightManager.CreateLight(lightName, buildingLight);
        }
    }
    
    void SetupInteractiveLights()
    {
        // Player flashlight (initially off)
        LightStruct flashlight = {};
        flashlight.type = int(LightType::SPOT);
        flashlight.position = XMFLOAT3(0.0f, 1.8f, 0.0f);  // Player eye level
        flashlight.direction = XMFLOAT3(0.0f, 0.0f, 1.0f);  // Forward
        flashlight.color = XMFLOAT3(1.0f, 1.0f, 0.9f);     // Cool white
        flashlight.ambient = XMFLOAT3(0.0f, 0.0f, 0.0f);   // No ambient
        flashlight.specularColor = XMFLOAT3(1.0f, 1.0f, 1.0f);
        flashlight.intensity = 3.0f;
        flashlight.range = 50.0f;
        flashlight.angle = XMConvertToRadians(30.0f);
        flashlight.innerCone = XMConvertToRadians(15.0f);
        flashlight.outerCone = XMConvertToRadians(30.0f);
        flashlight.lightFalloff = 1.0f;
        flashlight.active = 0;  // Start turned off
        flashlight.animMode = int(LightAnimMode::None);
        
        lightManager.CreateLight(L"PlayerFlashlight", flashlight);
        
        // Car headlights
        for (int i = 0; i < 2; ++i) {
            LightStruct headlight = {};
            headlight.type = int(LightType::SPOT);
            headlight.position = XMFLOAT3(-5.0f + i * 2.0f, 1.0f, 0.0f);
            headlight.direction = XMFLOAT3(0.0f, -0.1f, 1.0f);
            XMStoreFloat3(&headlight.direction, 
                          XMVector3Normalize(XMLoadFloat3(&headlight.direction)));
            headlight.color = XMFLOAT3(1.0f, 1.0f, 0.95f);
            headlight.ambient = XMFLOAT3(0.0f, 0.0f, 0.0f);
            headlight.specularColor = XMFLOAT3(1.0f, 1.0f, 1.0f);
            headlight.intensity = 4.0f;
            headlight.range = 80.0f;
            headlight.angle = XMConvertToRadians(40.0f);
            headlight.innerCone = XMConvertToRadians(20.0f);
            headlight.outerCone = XMConvertToRadians(40.0f);
            headlight.lightFalloff = 1.0f;
            headlight.active = 0;  // Start turned off
            headlight.animMode = int(LightAnimMode::None);
            
            std::wstring lightName = L"CarHeadlight" + std::to_wstring(i + 1);
            lightManager.CreateLight(lightName, headlight);
        }
    }
    
    void SetupEffectLights()
    {
        // Campfire with flickering animation
        LightStruct campfire = {};
        campfire.type = int(LightType::POINT);
        campfire.position = XMFLOAT3(15.0f, 0.5f, -10.0f);
        campfire.color = XMFLOAT3(1.0f, 0.4f, 0.1f);       // Orange flame
        campfire.ambient = XMFLOAT3(0.1f, 0.02f, 0.01f);
        campfire.specularColor = XMFLOAT3(1.0f, 0.6f, 0.2f);
        campfire.intensity = 2.0f;
        campfire.baseIntensity = 2.0f;
        campfire.range = 15.0f;
        campfire.lightFalloff = 2.0f;
        campfire.active = 1;
        
        // Flickering animation
        campfire.animMode = int(LightAnimMode::Flicker);
        campfire.animSpeed = 1.5f;
        campfire.animAmplitude = 0.6f;
        campfire.animTimer = 0.0f;
        
        lightManager.CreateLight(L"Campfire", campfire);
        
        // Neon sign with pulsing animation
        LightStruct neonSign = {};
        neonSign.type = int(LightType::POINT);
        neonSign.position = XMFLOAT3(-25.0f, 4.0f, 5.0f);
        neonSign.color = XMFLOAT3(0.2f, 1.0f, 0.8f);       // Cyan neon
        neonSign.ambient = XMFLOAT3(0.01f, 0.05f, 0.04f);
        neonSign.specularColor = XMFLOAT3(0.4f, 1.0f, 1.0f);
        neonSign.intensity = 1.5f;
        neonSign.baseIntensity = 1.5f;
        neonSign.range = 12.0f;
        neonSign.lightFalloff = 1.5f;
        neonSign.active = 1;
        
        // Pulsing animation
        neonSign.animMode = int(LightAnimMode::Pulse);
        neonSign.animSpeed = 0.8f;
        neonSign.animAmplitude = 0.4f;
        neonSign.animTimer = 0.0f;
        
        lightManager.CreateLight(L"NeonSign", neonSign);
        
        // Emergency beacon (strobing)
        LightStruct beacon = {};
        beacon.type = int(LightType::SPOT);
        beacon.position = XMFLOAT3(0.0f, 10.0f, 0.0f);
        beacon.direction = XMFLOAT3(0.0f, -1.0f, 0.0f);
        beacon.color = XMFLOAT3(1.0f, 0.1f, 0.1f);         // Red warning
        beacon.ambient = XMFLOAT3(0.0f, 0.0f, 0.0f);
        beacon.specularColor = XMFLOAT3(1.0f, 0.2f, 0.2f);
        beacon.intensity = 3.0f;
        beacon.baseIntensity = 3.0f;
        beacon.range = 30.0f;
        beacon.angle = XMConvertToRadians(90.0f);
        beacon.innerCone = XMConvertToRadians(45.0f);
        beacon.outerCone = XMConvertToRadians(90.0f);
        beacon.lightFalloff = 1.0f;
        beacon.active = 0;  // Start disabled
        
        // Strobe animation
        beacon.animMode = int(LightAnimMode::Strobe);
        beacon.animSpeed = 3.0f;
        beacon.animAmplitude = 1.0f;
        beacon.animTimer = 0.0f;
        
        lightManager.CreateLight(L"EmergencyBeacon", beacon);
    }
    
    void UpdateLightLOD(const XMFLOAT3& cameraPosition)
    {
        // Simple LOD system based on distance
        std::vector<LightStruct> allLights = lightManager.GetAllLights();
        
        for (size_t i = 0; i < allLights.size(); ++i) {
            LightStruct& light = allLights[i];
            
            // Skip directional lights (sun/moon)
            if (light.type == int(LightType::DIRECTIONAL)) continue;
            
            float distance = CalculateDistance(cameraPosition, light.position);
            
            // Apply LOD based on distance
            if (distance > 150.0f) {
                light.active = 0;  // Disable very distant lights
            } else if (distance > 75.0f) {
                light.active = 1;
                light.animMode = int(LightAnimMode::None);  // Disable animations
                light.intensity = light.baseIntensity * 0.5f;  // Reduce intensity
            } else {
                light.active = 1;
                // Full quality for nearby lights
            }
        }
    }
    
    float CalculateDistance(const XMFLOAT3& a, const XMFLOAT3& b)
    {
        XMVECTOR va = XMLoadFloat3(&a);
        XMVECTOR vb = XMLoadFloat3(&b);
        return XMVectorGetX(XMVector3Length(XMVectorSubtract(va, vb)));
    }
};
```

### Integration with Model Rendering

```cpp
class ModelLightingIntegration
{
private:
    LightsManager& lightManager;
    
public:
    ModelLightingIntegration(LightsManager& lm) : lightManager(lm) {}
    
    void ApplyLightingToModel(Model& model, const XMFLOAT3& modelPosition)
    {
        // Get all active lights
        std::vector<LightStruct> allLights = lightManager.GetAllLights();
        
        // Filter lights by distance and influence
        std::vector<LightStruct> relevantLights;
        
        for (const auto& light : allLights) {
            if (light.active == 0) continue;
            
            // Always include directional lights (sun/moon)
            if (light.type == int(LightType::DIRECTIONAL)) {
                relevantLights.push_back(light);
                continue;
            }
            
            // Check distance for point/spot lights
            float distance = CalculateDistance(modelPosition, light.position);
            if (distance <= light.range) {
                relevantLights.push_back(light);
            }
        }
        
        // Sort by influence (closer lights first)
        std::sort(relevantLights.begin(), relevantLights.end(),
            [&modelPosition](const LightStruct& a, const LightStruct& b) {
                if (a.type == int(LightType::DIRECTIONAL) && b.type != int(LightType::DIRECTIONAL)) return true;
                if (b.type == int(LightType::DIRECTIONAL) && a.type != int(LightType::DIRECTIONAL)) return false;
                
                float distA = CalculateDistance(modelPosition, a.position);
                float distB = CalculateDistance(modelPosition, b.position);
                return distA < distB;
            });
        
        // Limit to maximum number of lights per model
        if (relevantLights.size() > MAX_LIGHTS) {
            relevantLights.resize(MAX_LIGHTS);
        }
        
        // Apply lights to model
        model.SetLights(relevantLights);
    }
    
private:
    float CalculateDistance(const XMFLOAT3& a, const XMFLOAT3& b)
    {
        XMVECTOR va = XMLoadFloat3(&a);
        XMVECTOR vb = XMLoadFloat3(&b);
        return XMVectorGetX(XMVector3Length(XMVectorSubtract(va, vb)));
    }
};
```

---

## Platform-Specific Notes

### DirectX 11/12 Implementation

```cpp
// DirectX-specific optimizations
class DX11LightSystem
{
private:
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<ID3D11Buffer> lightBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> globalLightBuffer;
    
public:
    bool Initialize(ID3D11Device* d3dDevice, ID3D11DeviceContext* d3dContext)
    {
        device = d3dDevice;
        context = d3dContext;
        
        // Create light constant buffers
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        bufferDesc.ByteWidth = sizeof(LightBuffer);
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        
        HRESULT hr = device->CreateBuffer(&bufferDesc, nullptr, &lightBuffer);
        if (FAILED(hr)) return false;
        
        bufferDesc.ByteWidth = sizeof(GlobalLightBuffer);
        hr = device->CreateBuffer(&bufferDesc, nullptr, &globalLightBuffer);
        if (FAILED(hr)) return false;
        
        return true;
    }
    
    void UpdateLightBuffers(const std::vector<LightStruct>& lights,
                           const std::vector<LightStruct>& globalLights)
    {
        // Update local lights
        if (!lights.empty()) {
            D3D11_MAPPED_SUBRESOURCE mappedResource;
            HRESULT hr = context->Map(lightBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
            if (SUCCEEDED(hr)) {
                LightBuffer* buffer = static_cast<LightBuffer*>(mappedResource.pData);
                buffer->numLights = min((int)lights.size(), MAX_LIGHTS);
                
                for (int i = 0; i < buffer->numLights; ++i) {
                    buffer->lights[i] = lights[i];
                }
                
                context->Unmap(lightBuffer.Get(), 0);
            }
        }
        
        // Update global lights
        if (!globalLights.empty()) {
            D3D11_MAPPED_SUBRESOURCE mappedResource;
            HRESULT hr = context->Map(globalLightBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
            if (SUCCEEDED(hr)) {
                GlobalLightBuffer* buffer = static_cast<GlobalLightBuffer*>(mappedResource.pData);
                buffer->numLights = min((int)globalLights.size(), MAX_GLOBAL_LIGHTS);
                
                for (int i = 0; i < buffer->numLights; ++i) {
                    buffer->lights[i] = globalLights[i];
                }
                
                context->Unmap(globalLightBuffer.Get(), 0);
            }
        }
        
        // Bind to pixel shader
        ID3D11Buffer* buffers[] = { lightBuffer.Get(), globalLightBuffer.Get() };
        context->PSSetConstantBuffers(1, 2, buffers);
    }
};
```

### OpenGL Implementation

```cpp
// OpenGL-specific implementation
class OpenGLLightSystem
{
private:
    GLuint lightUBO;        // Uniform Buffer Object for lights
    GLuint globalLightUBO;  // UBO for global lights
    
public:
    bool Initialize()
    {
        // Create Uniform Buffer Objects
        glGenBuffers(1, &lightUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, lightUBO);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(LightBuffer), nullptr, GL_DYNAMIC_DRAW);
        
        glGenBuffers(1, &globalLightUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, globalLightUBO);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(GlobalLightBuffer), nullptr, GL_DYNAMIC_DRAW);
        
        // Bind to uniform buffer binding points
        glBindBufferBase(GL_UNIFORM_BUFFER, 1, lightUBO);        // Binding point 1
        glBindBufferBase(GL_UNIFORM_BUFFER, 2, globalLightUBO);  // Binding point 2
        
        return true;
    }
    
    void UpdateLightBuffers(const std::vector<LightStruct>& lights,
                           const std::vector<LightStruct>& globalLights)
    {
        // Update local lights UBO
        LightBuffer lightBuffer = {};
        lightBuffer.numLights = min((int)lights.size(), MAX_LIGHTS);
        
        for (int i = 0; i < lightBuffer.numLights; ++i) {
            lightBuffer.lights[i] = lights[i];
        }
        
        glBindBuffer(GL_UNIFORM_BUFFER, lightUBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(LightBuffer), &lightBuffer);
        
        // Update global lights UBO
        GlobalLightBuffer globalBuffer = {};
        globalBuffer.numLights = min((int)globalLights.size(), MAX_GLOBAL_LIGHTS);
        
        for (int i = 0; i < globalBuffer.numLights; ++i) {
            globalBuffer.lights[i] = globalLights[i];
        }
        
        glBindBuffer(GL_UNIFORM_BUFFER, globalLightUBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(GlobalLightBuffer), &globalBuffer);
    }
    
    void Cleanup()
    {
        if (lightUBO) {
            glDeleteBuffers(1, &lightUBO);
            lightUBO = 0;
        }
        
        if (globalLightUBO) {
            glDeleteBuffers(1, &globalLightUBO);
            globalLightUBO = 0;
        }
    }
};
```

### Vulkan Implementation

```cpp
// Vulkan-specific implementation
class VulkanLightSystem
{
private:
    VkDevice device;
    VkBuffer lightBuffer;
    VkDeviceMemory lightBufferMemory;
    VkBuffer globalLightBuffer;
    VkDeviceMemory globalLightBufferMemory;
    VkDescriptorSet descriptorSet;
    
public:
    bool Initialize(VkDevice vkDevice, VkPhysicalDevice physicalDevice)
    {
        device = vkDevice;
        
        // Create light buffers
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(LightBuffer);
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &lightBuffer) != VK_SUCCESS) {
            return false;
        }
        
        bufferInfo.size = sizeof(GlobalLightBuffer);
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &globalLightBuffer) != VK_SUCCESS) {
            return false;
        }
        
        // Allocate memory for buffers
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, lightBuffer, &memRequirements);
        
        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = FindMemoryType(physicalDevice, memRequirements.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        
        if (vkAllocateMemory(device, &allocInfo, nullptr, &lightBufferMemory) != VK_SUCCESS) {
            return false;
        }
        
        vkBindBufferMemory(device, lightBuffer, lightBufferMemory, 0);
        
        // Similar allocation for global light buffer...
        
        return true;
    }
    
    void UpdateLightBuffers(const std::vector<LightStruct>& lights,
                           const std::vector<LightStruct>& globalLights)
    {
        // Map memory and update light buffer
        void* data;
        vkMapMemory(device, lightBufferMemory, 0, sizeof(LightBuffer), 0, &data);
        
        LightBuffer* lightBuffer = static_cast<LightBuffer*>(data);
        lightBuffer->numLights = min((int)lights.size(), MAX_LIGHTS);
        
        for (int i = 0; i < lightBuffer->numLights; ++i) {
            lightBuffer->lights[i] = lights[i];
        }
        
        vkUnmapMemory(device, lightBufferMemory);
        
        // Similar update for global lights...
    }
    
private:
    uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
    {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
        
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        
        return 0;  // Should handle error case
    }
};
```

---

## Conclusion

This comprehensive guide covers all aspects of the Light Class system, from basic usage to advanced scenarios. The system provides:

- **Flexible Light Types**: Support for directional, point, and spot lights
- **Rich Animation System**: Built-in flicker, pulse, and strobe animations
- **Performance Optimization**: LOD system and efficient GPU buffer management
- **Cross-Platform Support**: DirectX, OpenGL, and Vulkan implementations
- **Thread Safety**: Concurrent access and modification support
- **Scene Integration**: Seamless integration with GLTF loading and scene management

### Key Takeaways

1. **Always initialize lights properly** with valid type, position, color, and range values
2. **Use appropriate light types** for different scenarios (directional for sun/moon, point for general lighting, spot for focused illumination)
3. **Implement LOD systems** to maintain performance with many lights
4. **Leverage animations** to create dynamic and interesting lighting effects
5. **Monitor performance** using profiling tools and optimize based on target platform
6. **Follow platform-specific best practices** for optimal GPU utilization

The Light Class system is designed to be both powerful and easy to use, providing everything needed for sophisticated lighting in modern game engines.

---

*For additional support and updates, please refer to the engine documentation and community forums.*