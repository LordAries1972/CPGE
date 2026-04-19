# Model Class Usage Guide

## Table of Contents

1. [Overview](#overview)
2. [Prerequisites](#prerequisites)
3. [Basic Usage](#basic-usage)
   - [Creating a Model Instance](#creating-a-model-instance)
   - [Loading a Model from File](#loading-a-model-from-file)
   - [Setting Up for Rendering](#setting-up-for-rendering)

4. [Advanced Features](#advanced-features)
   - [Material Management](#material-management)
   - [Texture Loading](#texture-loading)
   - [Animation System](#animation-system)
   - [Lighting Integration](#lighting-integration)

5. [PBR (Physically Based Rendering) Support](#pbr-physically-based-rendering-support)
   - [Loading PBR Textures](#loading-pbr-textures)
   - [Setting PBR Properties](#setting-pbr-properties)
   - [Environment Mapping](#environment-mapping)

6. [Rendering Pipeline](#rendering-pipeline)
   - [Rendering a Model](#rendering-a-model)
   - [Updating Transforms](#updating-transforms)
   - [Debug Information](#debug-information)

7. [Memory Management](#memory-management)
   - [Resource Cleanup](#resource-cleanup)
   - [Model Copying](#model-copying)

8. [Error Handling](#error-handling)
9. [Performance Considerations](#performance-considerations)
10. [Troubleshooting](#troubleshooting)

11. [Code Examples](#code-examples)
    - [Complete Basic Example](#complete-basic-example)
    - [Advanced PBR Example](#advanced-pbr-example)
    - [Animation Example](#animation-example)

---

## Overview

The `Model` class is a comprehensive 3D model management system designed currently for DirectX 11 rendering (Others to come). It provides functionality for loading 3D models from various file formats (primarily Wavefront OBJ), managing materials and textures, handling animations, and integrating with lighting systems. The class supports both traditional and Physically Based Rendering (PBR) workflows.

### Key Features

- **Multi-format Support**: Primary support for Wavefront OBJ files with MTL materials
- **PBR Rendering**: Full support for metallic/roughness workflow
- **Animation System**: Built-in animation state management
- **Lighting Integration**: Seamless integration with the engine's lighting system
- **Memory Management**: Automatic resource cleanup and management
- **Thread Safety**: Thread-safe operations with mutex protection
- **Debug Support**: Comprehensive debugging and logging capabilities

---

## Prerequisites

Before using the Model class, ensure you have:

1. **DirectX 11 Runtime**: The class requires DirectX 11 for GPU operations
2. **Renderer Instance**: A valid shared pointer to a Renderer object
3. **Asset Directory**: Properly configured asset directory path (`AssetsDir`)
4. **Threading System**: ThreadManager and related systems initialized
5. **Debug System**: Debug logging system active

### Required Headers

```cpp
#include "Models.h"
#include "DX11Renderer.h"
#include "Lights.h"
#include "Debug.h"
```

### Global Dependencies

```cpp
extern std::shared_ptr<Renderer> renderer;
extern Debug debug;
extern ThreadManager threadManager;
extern LightsManager lightsManager;
```

---

## Basic Usage

### Creating a Model Instance

```cpp
// Create a new model instance
Model myModel;

// Or use the global models array
extern Model models[MAX_MODELS];
Model& cube = models[MODEL_CUBE1];
```

### Loading a Model from File

```cpp
// Load a model from an OBJ file
std::wstring modelPath = L"assets/models/spaceship.obj";
int modelID = 1;

bool success = myModel.LoadModel(modelPath, modelID);
if (!success) {
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to load model: " + modelPath);
    return false;
}
```

### Setting Up for Rendering

```cpp
// Setup the model for GPU rendering
if (!myModel.SetupModelForRendering()) {
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to setup model for rendering");
    return false;
}

// Apply default lighting
myModel.ApplyDefaultLightingFromManager(lightsManager);
```

---

## Advanced Features

### Material Management

The Model class automatically loads materials from MTL files when loading OBJ models:

```cpp
// Materials are automatically loaded from the MTL file
// Access materials through the model's material map
for (const auto& [name, material] : myModel.m_materials) {
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Material: %hs", name.c_str());
    debug.logDebugMessage(LogLevel::LOG_INFO, L"  Diffuse: (%.2f, %.2f, %.2f)", 
                         material.Kd.x, material.Kd.y, material.Kd.z);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"  Specular: (%.2f, %.2f, %.2f)", 
                         material.Ks.x, material.Ks.y, material.Ks.z);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"  Shininess: %.2f", material.Ns);
}
```

### Texture Loading

Textures are automatically loaded when materials reference them:

```cpp
// Check if textures were loaded successfully
if (!myModel.m_modelInfo.textures.empty()) {
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Loaded %zu textures", 
                         myModel.m_modelInfo.textures.size());
} else {
    debug.logLevelMessage(LogLevel::LOG_WARNING, L"No textures loaded, using fallback");
}

// Manually create a solid color texture if needed
auto fallbackTexture = std::make_shared<Texture>();
if (fallbackTexture->CreateSolidColorTexture(256, 256, XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f))) {
    myModel.m_modelInfo.textures.push_back(fallbackTexture);
}
```

### Animation System

```cpp
// Update animation in your render loop
float deltaTime = 0.016f; // 60 FPS
myModel.UpdateAnimation(deltaTime);

// Trigger special effects
myModel.TriggerEffect(1); // Trigger effect with ID 1

// Set position for animated models
myModel.SetPosition(XMFLOAT3(10.0f, 5.0f, 0.0f));
```

### Lighting Integration

```cpp
// Apply lighting from the global lights manager
myModel.ApplyDefaultLightingFromManager(lightsManager);

// Check how many lights were applied
debug.logDebugMessage(LogLevel::LOG_INFO, L"Applied %zu lights to model", 
                     myModel.m_modelInfo.localLights.size());

// Update lighting during runtime
myModel.UpdateModelLighting();
```

---

## PBR (Physically Based Rendering) Support

### Loading PBR Textures

```cpp
// Setup PBR resources first
if (!myModel.SetupPBRResources()) {
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to setup PBR resources");
    return false;
}

// Load metallic map
std::wstring metallicPath = L"assets/textures/spaceship_metallic.png";
if (myModel.LoadMetallicMap(metallicPath)) {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Loaded metallic map successfully");
}

// Load roughness map
std::wstring roughnessPath = L"assets/textures/spaceship_roughness.png";
if (myModel.LoadRoughnessMap(roughnessPath)) {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Loaded roughness map successfully");
}

// Load ambient occlusion map
std::wstring aoPath = L"assets/textures/spaceship_ao.png";
if (myModel.LoadAOMap(aoPath)) {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Loaded AO map successfully");
}
```

### Setting PBR Properties

```cpp
// Set PBR material properties
float metallic = 0.8f;      // 80% metallic
float roughness = 0.3f;     // 30% rough (fairly smooth)
float reflectionStrength = 1.0f; // Full reflection strength

myModel.SetPBRProperties(metallic, roughness, reflectionStrength);

// Set environment properties
float envIntensity = 1.2f;  // 120% environment intensity
XMFLOAT3 envTint = { 1.0f, 0.9f, 0.8f }; // Warm tint
float mipBias = 0.0f;       // No mip bias
float fresnel0 = 0.04f;     // Standard dielectric fresnel

myModel.SetEnvironmentProperties(envIntensity, envTint, mipBias, fresnel0);
```

### Environment Mapping

```cpp
// Load environment cube map (DDS format)
std::wstring envMapPath = L"assets/environment/sunset_cubemap.dds";
if (myModel.LoadEnvironmentMap(envMapPath)) {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Environment map loaded successfully");
    
    // Update environment buffer
    myModel.UpdateEnvironmentBuffer();
}
```

---

## Rendering Pipeline

### Rendering a Model

```cpp
// In your render loop
void RenderScene(ID3D11DeviceContext* deviceContext, float deltaTime) {
    // Update model transforms
    XMMATRIX world = XMMatrixTranslation(0.0f, 0.0f, 5.0f);
    myModel.m_modelInfo.worldMatrix = world;
    
    // Update view and projection matrices
    auto dx11 = std::dynamic_pointer_cast<DX11Renderer>(renderer);
    myModel.m_modelInfo.viewMatrix = dx11->myCamera.GetViewMatrix();
    myModel.m_modelInfo.projectionMatrix = dx11->myCamera.GetProjectionMatrix();
    
    // Render the model
    myModel.Render(deviceContext, deltaTime);
}
```

### Updating Transforms

```cpp
// Update model position
myModel.m_modelInfo.position = XMFLOAT3(x, y, z);

// Update model scale
myModel.m_modelInfo.scale = XMFLOAT3(2.0f, 2.0f, 2.0f); // Double size

// Update model rotation
myModel.m_modelInfo.rotation = XMFLOAT3(0.0f, XM_PI, 0.0f); // 180° Y rotation

// Rebuild world matrix
XMMATRIX scale = XMMatrixScaling(myModel.m_modelInfo.scale.x, 
                                myModel.m_modelInfo.scale.y, 
                                myModel.m_modelInfo.scale.z);
XMMATRIX rotation = XMMatrixRotationRollPitchYaw(myModel.m_modelInfo.rotation.x,
                                                myModel.m_modelInfo.rotation.y,
                                                myModel.m_modelInfo.rotation.z);
XMMATRIX translation = XMMatrixTranslation(myModel.m_modelInfo.position.x,
                                          myModel.m_modelInfo.position.y,
                                          myModel.m_modelInfo.position.z);

myModel.m_modelInfo.worldMatrix = scale * rotation * translation;
```

### Debug Information

```cpp
// Enable debug output in Debug.h
#define _DEBUG_MODEL_
#define _DEBUG_MODEL_RENDERER_

// Get debug information during rendering
myModel.DebugInfoForModel();

// Check model statistics
debug.logDebugMessage(LogLevel::LOG_INFO, L"Model vertices: %zu", 
                     myModel.m_modelInfo.vertices.size());
debug.logDebugMessage(LogLevel::LOG_INFO, L"Model indices: %zu", 
                     myModel.m_modelInfo.indices.size());
debug.logDebugMessage(LogLevel::LOG_INFO, L"Model materials: %zu", 
                     myModel.m_materials.size());
```

---

## Memory Management

### Resource Cleanup

```cpp
// Cleanup is automatic in destructor, but can be called manually
myModel.DestroyModel();

// Check if model is destroyed
if (myModel.bIsDestroyed) {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Model successfully destroyed");
}
```

### Model Copying

```cpp
// Copy model data from another model
Model sourceModel;
Model targetModel;

// Load source model
sourceModel.LoadModel(L"assets/models/spaceship.obj", 1);
sourceModel.SetupModelForRendering();

// Copy to target model
targetModel.CopyFrom(sourceModel);

// Target model now has same geometry but can have different transforms
targetModel.m_modelInfo.position = XMFLOAT3(10.0f, 0.0f, 0.0f);
```

---

## Error Handling

### Common Error Scenarios

```cpp
// Check if model loading succeeded
if (!myModel.LoadModel(modelPath, modelID)) {
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Model loading failed");
    
    // Check common issues:
    if (!std::filesystem::exists(modelPath)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Model file does not exist");
    }
    
    if (!renderer) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Renderer not initialized");
    }
    
    return false;
}

// Check rendering setup
if (!myModel.SetupModelForRendering()) {
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Rendering setup failed");
    
    // Check GPU resources
    auto device = static_cast<ID3D11Device*>(renderer->GetDevice());
    if (!device) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"No valid D3D11 device");
    }
    
    return false;
}

// Validate model data before rendering
if (!myModel.m_isLoaded || myModel.bIsDestroyed) {
    debug.logLevelMessage(LogLevel::LOG_WARNING, L"Model not ready for rendering");
    return;
}
```

---

## Performance Considerations

### Optimization Tips

1. **Batch Similar Models**: Use model instancing for multiple copies
2. **Texture Sharing**: Reuse textures across models when possible
3. **LOD System**: Implement level-of-detail for distant models
4. **Culling**: Don't render models outside the view frustum

```cpp
// Example: Check if model should be rendered
bool ShouldRenderModel(const Model& model, const Camera& camera) {
    // Simple distance check
    XMVECTOR modelPos = XMLoadFloat3(&model.m_modelInfo.position);
    XMVECTOR cameraPos = XMLoadFloat3(&camera.GetPosition());
    float distance = XMVectorGetX(XMVector3Length(modelPos - cameraPos));
    
    // Don't render if too far away
    const float MAX_RENDER_DISTANCE = 1000.0f;
    return distance < MAX_RENDER_DISTANCE;
}

// Example: Texture memory management
void OptimizeTextureMemory() {
    // Release unused textures
    for (auto& model : models) {
        if (!model.m_isLoaded) {
            model.m_modelInfo.textures.clear();
            model.m_modelInfo.textureSRVs.clear();
        }
    }
}
```

---

## Troubleshooting

### Common Issues and Solutions

#### Issue: Model not rendering
**Solutions:**
- Check if `LoadModel()` returned true
- Verify `SetupModelForRendering()` succeeded
- Ensure renderer is properly initialized
- Check if model is within camera view

#### Issue: Textures appear black or missing
**Solutions:**
- Verify texture file paths are correct
- Check if MTL file references existing textures
- Ensure texture formats are supported (PNG, JPG, etc.)
- Look for fallback texture loading in logs

#### Issue: Materials not loading
**Solutions:**
- Ensure MTL file is in the same directory as OBJ
- Check MTL file syntax and material names
- Verify asset directory path is correct

#### Issue: Performance problems
**Solutions:**
- Enable vertex/index buffer optimization
- Use appropriate texture sizes
- Implement frustum culling
- Consider level-of-detail systems

---

## Code Examples

### Complete Basic Example

```cpp
#include "Models.h"
#include "DX11Renderer.h"
#include "Lights.h"

class ModelExample {
private:
    Model m_spaceshipModel;
    bool m_initialized = false;

public:
    bool Initialize() {
        // Load the model
        std::wstring modelPath = AssetsDir / L"models/spaceship.obj";
        
        if (!m_spaceshipModel.LoadModel(modelPath, MODEL_SPACESHIP)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to load spaceship model");
            return false;
        }
        
        // Setup for rendering
        if (!m_spaceshipModel.SetupModelForRendering()) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to setup spaceship for rendering");
            return false;
        }
        
        // Apply lighting
        m_spaceshipModel.ApplyDefaultLightingFromManager(lightsManager);
        
        // Set initial position
        m_spaceshipModel.m_modelInfo.position = XMFLOAT3(0.0f, 0.0f, 5.0f);
        m_spaceshipModel.m_modelInfo.scale = XMFLOAT3(1.0f, 1.0f, 1.0f);
        m_spaceshipModel.m_modelInfo.rotation = XMFLOAT3(0.0f, 0.0f, 0.0f);
        
        m_initialized = true;
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Model example initialized successfully");
        return true;
    }
    
    void Update(float deltaTime) {
        if (!m_initialized) return;
        
        // Update model matrices
        auto dx11 = std::dynamic_pointer_cast<DX11Renderer>(renderer);
        m_spaceshipModel.m_modelInfo.viewMatrix = dx11->myCamera.GetViewMatrix();
        m_spaceshipModel.m_modelInfo.projectionMatrix = dx11->myCamera.GetProjectionMatrix();
        
        // Build world matrix
        XMMATRIX scale = XMMatrixScaling(m_spaceshipModel.m_modelInfo.scale.x,
                                        m_spaceshipModel.m_modelInfo.scale.y,
                                        m_spaceshipModel.m_modelInfo.scale.z);
        XMMATRIX rotation = XMMatrixRotationRollPitchYaw(m_spaceshipModel.m_modelInfo.rotation.x,
                                                        m_spaceshipModel.m_modelInfo.rotation.y,
                                                        m_spaceshipModel.m_modelInfo.rotation.z);
        XMMATRIX translation = XMMatrixTranslation(m_spaceshipModel.m_modelInfo.position.x,
                                                  m_spaceshipModel.m_modelInfo.position.y,
                                                  m_spaceshipModel.m_modelInfo.position.z);
        
        m_spaceshipModel.m_modelInfo.worldMatrix = scale * rotation * translation;
    }
    
    void Render(ID3D11DeviceContext* deviceContext, float deltaTime) {
        if (!m_initialized) return;
        
        m_spaceshipModel.Render(deviceContext, deltaTime);
    }
    
    void Cleanup() {
        if (m_initialized) {
            m_spaceshipModel.DestroyModel();
            m_initialized = false;
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Model example cleaned up");
        }
    }
};
```

### Advanced PBR Example

```cpp
class PBRModelExample {
private:
    Model m_metalSphere;
    
public:
    bool InitializePBRModel() {
        // Load base model
        if (!m_metalSphere.LoadModel(L"assets/models/sphere.obj", MODEL_METAL_SPHERE)) {
            return false;
        }
        
        // Setup PBR resources
        if (!m_metalSphere.SetupPBRResources()) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to setup PBR resources");
            return false;
        }
        
        // Load PBR textures
        m_metalSphere.LoadMetallicMap(L"assets/textures/metal_metallic.png");
        m_metalSphere.LoadRoughnessMap(L"assets/textures/metal_roughness.png");
        m_metalSphere.LoadAOMap(L"assets/textures/metal_ao.png");
        m_metalSphere.LoadEnvironmentMap(L"assets/environment/workshop.dds");
        
        // Setup PBR properties
        m_metalSphere.SetPBRProperties(0.9f, 0.1f, 1.0f); // Very metallic, very smooth
        
        // Setup environment
        XMFLOAT3 warmTint = { 1.1f, 1.0f, 0.9f };
        m_metalSphere.SetEnvironmentProperties(1.5f, warmTint, 0.0f, 0.04f);
        
        // Complete setup
        if (!m_metalSphere.SetupModelForRendering()) {
            return false;
        }
        
        m_metalSphere.ApplyDefaultLightingFromManager(lightsManager);
        
        debug.logLevelMessage(LogLevel::LOG_INFO, L"PBR model initialized successfully");
        return true;
    }
    
    void UpdatePBR(float deltaTime) {
        // Update environment buffer with new settings
        m_metalSphere.UpdateEnvironmentBuffer();
        
        // Animate roughness over time for demo
        float animatedRoughness = 0.1f + 0.4f * (sinf(deltaTime) + 1.0f) * 0.5f;
        m_metalSphere.SetPBRProperties(0.9f, animatedRoughness, 1.0f);
    }
};
```

### Animation Example

```cpp
class AnimatedModelExample {
private:
    Model m_robot;
    float m_rotationSpeed = 1.0f;
    XMFLOAT3 m_targetPosition = { 10.0f, 0.0f, 0.0f };
    
public:
    bool InitializeAnimatedModel() {
        if (!m_robot.LoadModel(L"assets/models/robot.obj", MODEL_ROBOT)) {
            return false;
        }
        
        if (!m_robot.SetupModelForRendering()) {
            return false;
        }
        
        m_robot.ApplyDefaultLightingFromManager(lightsManager);
        
        // Set initial transform
        m_robot.m_modelInfo.position = XMFLOAT3(0.0f, 0.0f, 0.0f);
        m_robot.m_modelInfo.scale = XMFLOAT3(0.5f, 0.5f, 0.5f);
        m_robot.m_modelInfo.rotation = XMFLOAT3(0.0f, 0.0f, 0.0f);
        
        return true;
    }
    
    void UpdateAnimation(float deltaTime) {
        // Rotate the robot
        m_robot.m_modelInfo.rotation.y += m_rotationSpeed * deltaTime;
        
        // Move towards target
        XMVECTOR currentPos = XMLoadFloat3(&m_robot.m_modelInfo.position);
        XMVECTOR targetPos = XMLoadFloat3(&m_targetPosition);
        XMVECTOR direction = XMVector3Normalize(targetPos - currentPos);
        
        float speed = 2.0f; // units per second
        XMVECTOR newPos = currentPos + direction * speed * deltaTime;
        XMStoreFloat3(&m_robot.m_modelInfo.position, newPos);
        
        // Check if reached target
        float distance = XMVectorGetX(XMVector3Length(targetPos - newPos));
        if (distance < 0.1f) {
            // Pick new random target
            m_targetPosition = XMFLOAT3(
                static_cast<float>(rand() % 20 - 10),
                0.0f,
                static_cast<float>(rand() % 20 - 10)
            );
        }
        
        // Update the model's animation system
        m_robot.UpdateAnimation(deltaTime);
        
        // Trigger occasional effects
        static float effectTimer = 0.0f;
        effectTimer += deltaTime;
        if (effectTimer > 3.0f) {
            m_robot.TriggerEffect(1); // Trigger effect every 3 seconds
            effectTimer = 0.0f;
        }
    }
};
```

---

This comprehensive guide covers all aspects of using the Model class effectively. For additional information or specific use cases not covered here, refer to the inline documentation within the source code or contact the development team.