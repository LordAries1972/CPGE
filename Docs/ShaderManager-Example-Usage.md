# ShaderManager Usage Guide

## Table of Contents
1.  [Basic Setup and Initialization](#basic-setup-and-initialization)
2.  [Loading Individual Shaders](#loading-individual-shaders)
3.  [Creating Shader Programs](#creating-shader-programs)
4.  [Using Shaders in Rendering](#using-shaders-in-rendering)
5.  [Integration with Models](#integration-with-models)
6.  [Integration with Lighting System](#integration-with-lighting-system)
7.  [Integration with Scene Manager](#integration-with-scene-manager)
8.  [Hot-Reloading Support](#hot-reloading-support)
9.  [Performance Monitoring](#performance-monitoring)
10. [Error Handling](#error-handling)
11. [Platform-Specific Usage](#platform-specific-usage)
12. [Advanced Features](#advanced-features)
13. [Manual Thread Lock Usage](#manual-thread-lock-usage)
14. [Best Practices Summary](#best-practices-summary)
---

## Basic Setup and Initialization

### 1. Include Headers and Declare Global Instance

```cpp
// In your main header or global includes
#include "ShaderManager.h"

// Global instance declaration (add to main.cpp)
extern ShaderManager shaderManager;
```

### 2. Initialize in Main Application

```cpp
// In WinMain() or your main initialization function
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // ... other initialization code ...
    
    // Initialize renderer first
    renderer = std::make_shared<DX11Renderer>();
    if (!renderer->Initialize(hwnd, hInstance)) {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Failed to initialize renderer!");
        return -1;
    }
    
    // Initialize shader manager with renderer
    if (!shaderManager.Initialize(renderer)) {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Failed to initialize ShaderManager!");
        return -1;
    }
    
    debug.logLevelMessage(LogLevel::LOG_INFO, L"ShaderManager initialized successfully.");
    
    // ... rest of initialization ...
}
```

### 3. Cleanup on Exit

```cpp
// In your cleanup/shutdown function
void CleanupApplication() {
    // Clean up shader manager before renderer
    shaderManager.CleanUp();
    
    // Clean up other systems
    if (renderer) {
        renderer->Cleanup();
    }
    
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Application cleanup completed.");
}
```

---

## Loading Individual Shaders

### 1. Basic Shader Loading

```cpp
// Load a vertex shader
bool loadSuccess = shaderManager.LoadShader(
    "BasicVertex",                              // Shader name for lookup
    L"./Assets/Shaders/BasicVertex.hlsl",     // File path
    ShaderType::VERTEX_SHADER                  // Shader type
);

if (loadSuccess) {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Basic vertex shader loaded successfully.");
} else {
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to load basic vertex shader.");
}

// Load a pixel shader
bool pixelLoadSuccess = shaderManager.LoadShader(
    "BasicPixel",                               // Shader name for lookup
    L"./Assets/Shaders/BasicPixel.hlsl",      // File path
    ShaderType::PIXEL_SHADER                   // Shader type
);
```

### 2. Loading with Custom Compilation Profile

```cpp
// Create custom shader profile
ShaderProfile customProfile;
customProfile.entryPoint = "VSMain";                   // Custom entry point
customProfile.profileVersion = "vs_5_0";               // Specific HLSL version
customProfile.optimized = true;                        // Enable optimization
customProfile.debugInfo = false;                       // Disable debug info for release
customProfile.defines.push_back("USE_NORMAL_MAPPING"); // Add preprocessor define
customProfile.defines.push_back("MAX_LIGHTS=8");       // Add define with value

// Load shader with custom profile
bool success = shaderManager.LoadShader(
    "AdvancedVertex",
    L"./Assets/Shaders/AdvancedVertex.hlsl",
    ShaderType::VERTEX_SHADER,
    customProfile
);
```

### 3. Loading Shader from String (Runtime Generation)

```cpp
// Generate shader code at runtime
std::string dynamicShaderCode = R"(
cbuffer ConstantBuffer : register(b0) {
    matrix worldMatrix;
    matrix viewMatrix;
    matrix projectionMatrix;
    float4 color;
};

struct VS_INPUT {
    float3 position : POSITION;
    float2 texCoord : TEXCOORD0;
};

struct PS_INPUT {
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

PS_INPUT main(VS_INPUT input) {
    PS_INPUT output;
    
    float4 worldPos = mul(float4(input.position, 1.0f), worldMatrix);
    float4 viewPos = mul(worldPos, viewMatrix);
    output.position = mul(viewPos, projectionMatrix);
    output.texCoord = input.texCoord;
    
    return output;
}
)";

// Load from string
bool success = shaderManager.LoadShaderFromString(
    "DynamicVertex",
    dynamicShaderCode,
    ShaderType::VERTEX_SHADER
);
```

---

## Creating Shader Programs

### 1. Basic Shader Program Creation

```cpp
// Create a basic shader program with vertex and pixel shaders
bool programSuccess = shaderManager.CreateShaderProgram(
    "BasicProgram",         // Program name
    "BasicVertex",          // Vertex shader name
    "BasicPixel"            // Pixel shader name
);

if (programSuccess) {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Basic shader program created successfully.");
} else {
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to create basic shader program.");
}
```

### 2. Advanced Shader Program with Geometry Shader

```cpp
// Load geometry shader first
shaderManager.LoadShader(
    "ParticleGeometry",
    L"./Assets/Shaders/ParticleGeometry.hlsl",
    ShaderType::GEOMETRY_SHADER
);

// Create program with geometry shader
bool advancedSuccess = shaderManager.CreateShaderProgram(
    "ParticleProgram",      // Program name
    "ParticleVertex",       // Vertex shader name
    "ParticlePixel",        // Pixel shader name
    "ParticleGeometry"      // Geometry shader name (optional)
);
```

### 3. Tessellation Shader Program

```cpp
// Load tessellation shaders
shaderManager.LoadShader("TerrainHull", L"./Assets/Shaders/TerrainHull.hlsl", ShaderType::HULL_SHADER);
shaderManager.LoadShader("TerrainDomain", L"./Assets/Shaders/TerrainDomain.hlsl", ShaderType::DOMAIN_SHADER);

// Create tessellation program
bool tessSuccess = shaderManager.CreateShaderProgram(
    "TerrainProgram",       // Program name
    "TerrainVertex",        // Vertex shader name
    "TerrainPixel",         // Pixel shader name
    "",                     // No geometry shader
    "TerrainHull",          // Hull shader name
    "TerrainDomain"         // Domain shader name
);
```

---

## Using Shaders in Rendering

### 1. Basic Shader Usage in Render Loop

```cpp
// In your render function
void RenderFrame() {
    // Clear render targets
    // ... clear code ...
    
    // Use shader program for rendering
    if (shaderManager.UseShaderProgram("BasicProgram")) {
        // Render objects that use this shader
        RenderBasicObjects();
        
        // Unbind shader when done
        shaderManager.UnbindShaderProgram();
    } else {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to bind basic shader program.");
    }
    
    // Use different shader for other objects
    if (shaderManager.UseShaderProgram("ParticleProgram")) {
        RenderParticleEffects();
        shaderManager.UnbindShaderProgram();
    }
    
    // Present frame
    // ... present code ...
}
```

### 2. Conditional Shader Usage

```cpp
void RenderTerrain() {
    // Check if advanced tessellation is available
    if (shaderManager.DoesProgramExist("TerrainProgram")) {
        // Use advanced tessellation shader
        shaderManager.UseShaderProgram("TerrainProgram");
        RenderTerrainWithTessellation();
    } else {
        // Fallback to basic terrain shader
        shaderManager.UseShaderProgram("BasicProgram");
        RenderBasicTerrain();
    }
    
    shaderManager.UnbindShaderProgram();
}
```

### 3. Multi-Pass Rendering

```cpp
void RenderSceneMultiPass() {
    // First pass: Shadow mapping
    if (shaderManager.UseShaderProgram("ShadowMapProgram")) {
        RenderShadowPass();
        shaderManager.UnbindShaderProgram();
    }
    
    // Second pass: Lighting
    if (shaderManager.UseShaderProgram("LightingProgram")) {
        RenderLightingPass();
        shaderManager.UnbindShaderProgram();
    }
    
    // Third pass: Post-processing
    if (shaderManager.UseShaderProgram("PostProcessProgram")) {
        RenderPostProcessPass();
        shaderManager.UnbindShaderProgram();
    }
}
```

---

## Integration with Models

### 1. Binding Shaders to Models

```cpp
// In your model initialization or scene setup
void SetupModelShaders() {
    // Load model-specific shaders
    shaderManager.LoadShader("ModelVertex", L"./Assets/Shaders/ModelVShader.hlsl", ShaderType::VERTEX_SHADER);
    shaderManager.LoadShader("ModelPixel", L"./Assets/Shaders/ModelPShader.hlsl", ShaderType::PIXEL_SHADER);
    shaderManager.CreateShaderProgram("ModelProgram", "ModelVertex", "ModelPixel");
    
    // Bind shader to specific models
    for (int i = 0; i < MAX_MODELS; ++i) {
        if (models[i].m_isLoaded) {
            if (!shaderManager.BindShaderToModel("ModelProgram", &models[i])) {
                debug.logDebugMessage(LogLevel::LOG_WARNING, L"Failed to bind shader to model %d", i);
            }
        }
    }
}
```

### 2. Model-Specific Shader Selection

```cpp
void RenderModel(Model& model) {
    // Select shader based on model properties
    std::string shaderProgram = "BasicProgram";
    
    if (model.m_modelInfo.materials.size() > 0) {
        // Use advanced material shader for complex models
        shaderProgram = "MaterialProgram";
    }
    
    if (model.m_modelInfo.localLights.size() > 0) {
        // Use lighting shader for lit models
        shaderProgram = "LightingProgram";
    }
    
    // Apply shader and render
    if (shaderManager.UseShaderProgram(shaderProgram)) {
        model.Render(deviceContext, deltaTime);
        shaderManager.UnbindShaderProgram();
    }
}
```

### 3. PBR Material Shader Integration

```cpp
void SetupPBRShaders() {
    // Load PBR shaders with material support
    ShaderProfile pbrProfile;
    pbrProfile.defines.push_back("USE_PBR_MATERIALS");
    pbrProfile.defines.push_back("USE_NORMAL_MAPPING");
    pbrProfile.defines.push_back("USE_ROUGHNESS_MAPPING");
    pbrProfile.defines.push_back("USE_METALLIC_MAPPING");
    
    shaderManager.LoadShader("PBRVertex", L"./Assets/Shaders/PBRVertex.hlsl", 
                            ShaderType::VERTEX_SHADER, pbrProfile);
    shaderManager.LoadShader("PBRPixel", L"./Assets/Shaders/PBRPixel.hlsl", 
                            ShaderType::PIXEL_SHADER, pbrProfile);
    
    shaderManager.CreateShaderProgram("PBRProgram", "PBRVertex", "PBRPixel");
}
```

---

## Integration with Lighting System

### 1. Setting Up Lighting Shaders

```cpp
void InitializeLightingSystem() {
    // Load lighting-specific shaders
    ShaderProfile lightingProfile;
    lightingProfile.defines.push_back("MAX_LIGHTS=" + std::to_string(MAX_LIGHTS));
    lightingProfile.defines.push_back("USE_POINT_LIGHTS");
    lightingProfile.defines.push_back("USE_DIRECTIONAL_LIGHTS");
    lightingProfile.defines.push_back("USE_SPOT_LIGHTS");
    
    shaderManager.LoadShader("LightingVertex", L"./Assets/Shaders/LightingVertex.hlsl", 
                            ShaderType::VERTEX_SHADER, lightingProfile);
    shaderManager.LoadShader("LightingPixel", L"./Assets/Shaders/LightingPixel.hlsl", 
                            ShaderType::PIXEL_SHADER, lightingProfile);
    
    shaderManager.CreateShaderProgram("LightingProgram", "LightingVertex", "LightingPixel");
    
    // Configure lighting system with shaders
    if (!shaderManager.SetupLightingShaders(&lightsManager)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to setup lighting shaders.");
    }
}
```

### 2. Dynamic Light Count Shaders

```cpp
void UpdateLightingShaders(int activeLightCount) {
    // Generate shader with specific light count for optimization
    std::string shaderName = "DynamicLighting_" + std::to_string(activeLightCount);
    
    if (!shaderManager.DoesShaderExist(shaderName)) {
        // Create new shader variant with specific light count
        ShaderProfile dynamicProfile;
        dynamicProfile.defines.push_back("ACTIVE_LIGHT_COUNT=" + std::to_string(activeLightCount));
        
        shaderManager.LoadShader(shaderName + "_Vertex", 
                                L"./Assets/Shaders/DynamicLightingVertex.hlsl", 
                                ShaderType::VERTEX_SHADER, dynamicProfile);
        shaderManager.LoadShader(shaderName + "_Pixel", 
                                L"./Assets/Shaders/DynamicLightingPixel.hlsl", 
                                ShaderType::PIXEL_SHADER, dynamicProfile);
        
        shaderManager.CreateShaderProgram(shaderName, 
                                        shaderName + "_Vertex", 
                                        shaderName + "_Pixel");
    }
    
    // Use optimized shader
    shaderManager.UseShaderProgram(shaderName);
}
```

---

## Integration with Scene Manager

### 1. Scene-Specific Shader Loading

```cpp
void LoadSceneShaders(SceneManager& sceneManager) {
    // Load shaders based on scene requirements
    if (!shaderManager.LoadSceneShaders(&sceneManager)) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Some scene shaders failed to load.");
    }
    
    // Check for platform-specific optimizations
    std::wstring exporter = sceneManager.GetLastDetectedExporter();
    
    if (exporter == L"Sketchfab") {
        // Load Sketchfab-optimized shaders
        shaderManager.LoadShader("SketchfabVertex", L"./Assets/Shaders/SketchfabVertex.hlsl", 
                                ShaderType::VERTEX_SHADER);
        shaderManager.LoadShader("SketchfabPixel", L"./Assets/Shaders/SketchfabPixel.hlsl", 
                                ShaderType::PIXEL_SHADER);
        shaderManager.CreateShaderProgram("SketchfabProgram", "SketchfabVertex", "SketchfabPixel");
    }
}
```

### 2. Scene Transition Shader Effects

```cpp
void HandleSceneTransition(SceneType fromScene, SceneType toScene) {
    // Load transition effect shaders
    shaderManager.LoadShader("FadeVertex", L"./Assets/Shaders/FadeVertex.hlsl", 
                            ShaderType::VERTEX_SHADER);
    shaderManager.LoadShader("FadePixel", L"./Assets/Shaders/FadePixel.hlsl", 
                            ShaderType::PIXEL_SHADER);
    shaderManager.CreateShaderProgram("FadeProgram", "FadeVertex", "FadePixel");
    
    // Apply transition effect
    if (shaderManager.UseShaderProgram("FadeProgram")) {
        RenderTransitionEffect();
        shaderManager.UnbindShaderProgram();
    }
    
    // Clean up transition shaders
    shaderManager.UnloadShader("FadeVertex");
    shaderManager.UnloadShader("FadePixel");
}
```

---

## Hot-Reloading Support

### 1. Enable Hot-Reloading for Development

```cpp
void SetupDevelopmentMode() {
    #if defined(_DEBUG)
        // Enable hot-reloading in debug builds
        shaderManager.EnableHotReloading(true);
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Shader hot-reloading enabled for development.");
    #endif
}
```

### 2. Manual Hot-Reload Check

```cpp
void UpdateShaders() {
    #if defined(_DEBUG)
        // Check for shader file changes (call in main loop)
        static auto lastCheck = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        
        // Check every 2 seconds
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastCheck).count() >= 2) {
            shaderManager.CheckForShaderFileChanges();
            lastCheck = now;
        }
    #endif
}
```

### 3. Specific Shader Reload

```cpp
void ReloadSpecificShader(const std::string& shaderName) {
    #if defined(_DEBUG)
        if (shaderManager.DoesShaderExist(shaderName)) {
            if (shaderManager.ReloadShader(shaderName)) {
                debug.logDebugMessage(LogLevel::LOG_INFO, L"Successfully reloaded shader: %hs", shaderName.c_str());
            } else {
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"Failed to reload shader: %hs", shaderName.c_str());
            }
        }
    #endif
}
```

---

## Performance Monitoring

### 1. Get Shader Statistics

```cpp
void PrintShaderStatistics() {
    ShaderManagerStats stats = shaderManager.GetStatistics();
    
    debug.logDebugMessage(LogLevel::LOG_INFO, L"=== SHADER MANAGER STATISTICS ===");
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Total Shaders Loaded: %d", stats.totalShadersLoaded);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Total Programs Linked: %d", stats.totalProgramsLinked);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Compilation Failures: %d", stats.compilationFailures);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Linking Failures: %d", stats.linkingFailures);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Estimated Memory Usage: %llu bytes", stats.memoryUsage);
}
```

### 2. Validate All Shaders

```cpp
void ValidateShaderIntegrity() {
    if (shaderManager.ValidateAllShaders()) {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"All shaders validated successfully.");
    } else {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Shader validation failed - check logs for details.");
        
        // Print detailed debug information
        shaderManager.PrintDebugInfo();
    }
}
```

### 3. Performance Monitoring Loop

```cpp
void MonitorShaderPerformance() {
    static int frameCount = 0;
    static auto lastStatsTime = std::chrono::steady_clock::now();
    
    frameCount++;
    
    // Print statistics every 300 frames (~5 seconds at 60 FPS)
    if (frameCount % 300 == 0) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastStatsTime).count();
        
        if (elapsed >= 5) {
            PrintShaderStatistics();
            lastStatsTime = now;
        }
    }
}
```

---

## Error Handling

### 1. Comprehensive Error Checking

```cpp
bool LoadAndValidateShader(const std::string& name, const std::wstring& path, ShaderType type) {
    // Attempt to load shader
    if (!shaderManager.LoadShader(name, path, type)) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Failed to load shader '%hs' from '%ls'", 
                             name.c_str(), path.c_str());
        
        // Get shader resource to check error details
        ShaderResource* shader = shaderManager.GetShader(name);
        if (shader && !shader->compilationErrors.empty()) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Compilation error: %hs", 
                                 shader->compilationErrors.c_str());
        }
        
        return false;
    }
    
    // Validate loaded shader
    ShaderResource* shader = shaderManager.GetShader(name);
    if (!shader || !shader->isCompiled) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Shader '%hs' loaded but not compiled properly", 
                             name.c_str());
        return false;
    }
    
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Shader '%hs' loaded and validated successfully", 
                         name.c_str());
    return true;
}
```

### 2. Fallback Shader System

```cpp
void SetupFallbackShaders() {
    // Create simple fallback shaders for when loading fails
    std::string fallbackVertexCode = R"(
        cbuffer ConstantBuffer : register(b0) {
            matrix wvpMatrix;
        };
        
        struct VS_INPUT {
            float3 position : POSITION;
        };
        
        struct PS_INPUT {
            float4 position : SV_POSITION;
        };
        
        PS_INPUT main(VS_INPUT input) {
            PS_INPUT output;
            output.position = mul(float4(input.position, 1.0f), wvpMatrix);
            return output;
        }
    )";
    
    std::string fallbackPixelCode = R"(
        struct PS_INPUT {
            float4 position : SV_POSITION;
        };
        
        float4 main(PS_INPUT input) : SV_Target {
            return float4(1.0f, 0.0f, 1.0f, 1.0f); // Magenta error color
        }
    )";
    
    // Load fallback shaders
    shaderManager.LoadShaderFromString("FallbackVertex", fallbackVertexCode, ShaderType::VERTEX_SHADER);
    shaderManager.LoadShaderFromString("FallbackPixel", fallbackPixelCode, ShaderType::PIXEL_SHADER);
    shaderManager.CreateShaderProgram("FallbackProgram", "FallbackVertex", "FallbackPixel");
}

void UseFallbackShaderIfNeeded(const std::string& preferredProgram) {
    if (shaderManager.DoesProgramExist(preferredProgram)) {
        shaderManager.UseShaderProgram(preferredProgram);
    } else {
        debug.logDebugMessage(LogLevel::LOG_WARNING, L"Preferred shader program '%hs' not available, using fallback", 
                             preferredProgram.c_str());
        shaderManager.UseShaderProgram("FallbackProgram");
    }
}
```

---

## Platform-Specific Usage

### 1. DirectX Platform Specific

```cpp
void SetupDirectXShaders() {
    #if defined(__USE_DIRECTX_11__)
        // DirectX 11 specific shader loading
        ShaderProfile dx11Profile;
        dx11Profile.profileVersion = "vs_5_0";  // DirectX 11 vertex shader version
        dx11Profile.optimized = true;
        
        shaderManager.LoadShader("DX11Vertex", L"./Assets/Shaders/DX11Vertex.hlsl", 
                                ShaderType::VERTEX_SHADER, dx11Profile);
        
        dx11Profile.profileVersion = "ps_5_0";  // DirectX 11 pixel shader version
        shaderManager.LoadShader("DX11Pixel", L"./Assets/Shaders/DX11Pixel.hlsl", 
                                ShaderType::PIXEL_SHADER, dx11Profile);
    #endif
}
```

### 2. OpenGL Platform Specific

```cpp
void SetupOpenGLShaders() {
    #if defined(__USE_OPENGL__)
        // OpenGL specific shader loading
        ShaderProfile glProfile;
        glProfile.profileVersion = "330 core";  // OpenGL 3.3 core profile
        glProfile.optimized = true;
        
        shaderManager.LoadShader("GLVertex", L"./Assets/Shaders/GLVertex.glsl", 
                                ShaderType::VERTEX_SHADER, glProfile);
        shaderManager.LoadShader("GLFragment", L"./Assets/Shaders/GLFragment.glsl", 
                                ShaderType::PIXEL_SHADER, glProfile);
        
        shaderManager.CreateShaderProgram("GLProgram", "GLVertex", "GLFragment");
    #endif
}
```

### 3. Cross-Platform Compatibility

```cpp
void LoadPlatformOptimizedShaders() {
    ShaderPlatform currentPlatform = ShaderManager::DetectCurrentPlatform();
    
    switch (currentPlatform) {
        case ShaderPlatform::PLATFORM_DIRECTX11:
            SetupDirectXShaders();
            break;
            
        case ShaderPlatform::PLATFORM_OPENGL:
            SetupOpenGLShaders();
            break;
            
        case ShaderPlatform::PLATFORM_VULKAN:
            // Future Vulkan implementation
            break;
            
        default:
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Unknown platform, using generic shaders.");
            SetupFallbackShaders();
            break;
    }
}
```

---

## Advanced Features

### 1. Shader Variant System

```cpp
class ShaderVariantManager {
public:
    void CreateShaderVariants(const std::string& baseName, const std::wstring& basePath) {
        // Create different variants with different defines
        std::vector<std::vector<std::string>> variants = {
            {"USE_NORMAL_MAPPING"},
            {"USE_NORMAL_MAPPING", "USE_PARALLAX_MAPPING"},
            {"USE_NORMAL_MAPPING", "USE_PARALLAX_MAPPING", "USE_PBR"},
            {"USE_INSTANCING"},
            {"USE_INSTANCING", "USE_NORMAL_MAPPING"}
        };
        
        for (size_t i = 0; i < variants.size(); ++i) {
            std::string variantName = baseName + "_Variant" + std::to_string(i);
            
            ShaderProfile profile;
            profile.defines = variants[i];
            
            shaderManager.LoadShader(variantName, basePath, ShaderType::VERTEX_SHADER, profile);
        }
    }
    
    std::string SelectBestVariant(const std::string& baseName, const Model& model) {
        // Select shader variant based on model properties
        if (model.m_modelInfo.normalMapSRVs.size() > 0) {
            if (model.m_modelInfo.useMetallicMap && model.m_modelInfo.useRoughnessMap) {
                return baseName + "_Variant2"; // PBR variant
            }
            return baseName + "_Variant0"; // Normal mapping variant
        }
        
        return baseName; // Base variant
    }
};
```

### 2. Compute Shader Integration

```cpp
void SetupComputeShaders() {
    // Load compute shader for GPU particle simulation
    shaderManager.LoadShader("ParticleCompute", L"./Assets/Shaders/ParticleCompute.hlsl", 
                            ShaderType::COMPUTE_SHADER);
    
    // Load compute shader for post-processing effects
    shaderManager.LoadShader("BlurCompute", L"./Assets/Shaders/BlurCompute.hlsl", 
                            ShaderType::COMPUTE_SHADER);
}

void RunComputeShader(const std::string& shaderName, int threadsX, int threadsY, int threadsZ) {
    #if defined(__USE_DIRECTX_11__)
        ShaderResource* computeShader = shaderManager.GetShader(shaderName);
        if (computeShader && computeShader->d3d11ComputeShader) {
            void* deviceContext = renderer->GetDeviceContext();
            ID3D11DeviceContext* d3dContext = static_cast<ID3D11DeviceContext*>(deviceContext);
            
            // Bind compute shader
            d3dContext->CSSetShader(computeShader->d3d11ComputeShader.Get(), nullptr, 0);
            
            // Dispatch compute threads
            d3dContext->Dispatch(threadsX, threadsY, threadsZ);
            
            // Unbind compute shader
            d3dContext->CSSetShader(nullptr, nullptr, 0);
        }
    #endif
}
```

### 3. Shader Resource Management

```cpp
class AdvancedShaderManager {
public:
    void PreloadSceneShaders(const std::vector<std::string>& shaderList) {
        // Preload shaders for faster scene transitions
        for (const std::string& shaderName : shaderList) {
            if (!shaderManager.DoesShaderExist(shaderName)) {
                std::wstring shaderPath = L"./Assets/Shaders/" + 
                                        std::wstring(shaderName.begin(), shaderName.end()) + L".hlsl";
                
                // Determine shader type from name convention
                ShaderType type = ShaderType::VERTEX_SHADER;
                if (shaderName.find("Pixel") != std::string::npos || 
                    shaderName.find("Fragment") != std::string::npos) {
                    type = ShaderType::PIXEL_SHADER;
                } else if (shaderName.find("Geometry") != std::string::npos) {
                    type = ShaderType::GEOMETRY_SHADER;
                } else if (shaderName.find("Compute") != std::string::npos) {
                    type = ShaderType::COMPUTE_SHADER;
                }
                
                shaderManager.LoadShader(shaderName, shaderPath, type);
            }
        }
    }
    
    void UnloadUnusedShaders() {
        // Get list of all loaded shaders
        std::vector<std::string> loadedShaders = shaderManager.GetLoadedShaderNames();
        
        for (const std::string& shaderName : loadedShaders) {
            ShaderResource* shader = shaderManager.GetShader(shaderName);
            if (shader && !shader->isInUse && shader->referenceCount == 0) {
                // Shader is not currently in use, safe to unload
                shaderManager.UnloadShader(shaderName);
                debug.logDebugMessage(LogLevel::LOG_INFO, L"Unloaded unused shader: %hs", shaderName.c_str());
            }
        }
    }
    
    void OptimizeShaderMemory() {
        // Force garbage collection of unused shader resources
        UnloadUnusedShaders();
        
        // Print memory statistics
        ShaderManagerStats stats = shaderManager.GetStatistics();
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Shader memory optimized - current usage: %llu bytes", 
                             stats.memoryUsage);
    }
};
```

---

## Complete Usage Examples

### 1. Full Application Integration Example

```cpp
// Complete example showing ShaderManager integration in a game engine

class GameEngine {
private:
    std::shared_ptr<Renderer> m_renderer;
    ShaderVariantManager m_shaderVariants;
    AdvancedShaderManager m_advancedShaderManager;
    
public:
    bool Initialize(HWND hwnd, HINSTANCE hInstance) {
        // Initialize renderer first
        m_renderer = std::make_shared<DX11Renderer>();
        if (!m_renderer->Initialize(hwnd, hInstance)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Failed to initialize renderer!");
            return false;
        }
        
        // Initialize shader manager
        if (!shaderManager.Initialize(m_renderer)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Failed to initialize ShaderManager!");
            return false;
        }
        
        // Setup development features
        SetupDevelopmentMode();
        
        // Load core shaders
        if (!LoadCoreShaders()) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Failed to load core shaders!");
            return false;
        }
        
        // Setup shader variants
        SetupShaderVariants();
        
        // Configure lighting system
        InitializeLightingSystem();
        
        // Setup fallback system
        SetupFallbackShaders();
        
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GameEngine initialized successfully with ShaderManager.");
        return true;
    }
    
    void Update(float deltaTime) {
        // Update shader hot-reloading in debug builds
        #if defined(_DEBUG)
            static float shaderCheckTimer = 0.0f;
            shaderCheckTimer += deltaTime;
            
            if (shaderCheckTimer >= 2.0f) {  // Check every 2 seconds
                shaderManager.CheckForShaderFileChanges();
                shaderCheckTimer = 0.0f;
            }
        #endif
        
        // Update other systems...
    }
    
    void Render() {
        // Clear render targets
        ClearRenderTargets();
        
        // Render scene with appropriate shaders
        RenderOpaqueObjects();
        RenderTransparentObjects();
        RenderParticleEffects();
        RenderUI();
        
        // Present frame
        PresentFrame();
    }
    
    void Shutdown() {
        // Clean up in reverse order of initialization
        shaderManager.CleanUp();
        
        if (m_renderer) {
            m_renderer->Cleanup();
        }
        
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GameEngine shutdown completed.");
    }

private:
    bool LoadCoreShaders() {
        // Define core shaders needed by the engine
        struct ShaderInfo {
            std::string name;
            std::wstring path;
            ShaderType type;
            std::vector<std::string> defines;
        };
        
        std::vector<ShaderInfo> coreShaders = {
            {"BasicVertex", L"./Assets/Shaders/BasicVertex.hlsl", ShaderType::VERTEX_SHADER, {}},
            {"BasicPixel", L"./Assets/Shaders/BasicPixel.hlsl", ShaderType::PIXEL_SHADER, {}},
            {"ModelVertex", L"./Assets/Shaders/ModelVShader.hlsl", ShaderType::VERTEX_SHADER, {}},
            {"ModelPixel", L"./Assets/Shaders/ModelPShader.hlsl", ShaderType::PIXEL_SHADER, {}},
            {"UIVertex", L"./Assets/Shaders/UIVertex.hlsl", ShaderType::VERTEX_SHADER, {"USE_2D_PROJECTION"}},
            {"UIPixel", L"./Assets/Shaders/UIPixel.hlsl", ShaderType::PIXEL_SHADER, {"USE_ALPHA_BLENDING"}},
        };
        
        bool allLoaded = true;
        for (const auto& shaderInfo : coreShaders) {
            ShaderProfile profile;
            profile.defines = shaderInfo.defines;
            profile.optimized = true;
            
            if (!shaderManager.LoadShader(shaderInfo.name, shaderInfo.path, shaderInfo.type, profile)) {
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"Failed to load core shader: %hs", shaderInfo.name.c_str());
                allLoaded = false;
            }
        }
        
        if (allLoaded) {
            // Create shader programs
            shaderManager.CreateShaderProgram("BasicProgram", "BasicVertex", "BasicPixel");
            shaderManager.CreateShaderProgram("ModelProgram", "ModelVertex", "ModelPixel");
            shaderManager.CreateShaderProgram("UIProgram", "UIVertex", "UIPixel");
        }
        
        return allLoaded;
    }
    
    void SetupShaderVariants() {
        // Create shader variants for different rendering scenarios
        m_shaderVariants.CreateShaderVariants("ModelVertex", L"./Assets/Shaders/ModelVShader.hlsl");
        m_shaderVariants.CreateShaderVariants("ModelPixel", L"./Assets/Shaders/ModelPShader.hlsl");
    }
    
    void RenderOpaqueObjects() {
        // Render all opaque geometry
        for (int i = 0; i < MAX_SCENE_MODELS; ++i) {
            if (scene_models[i].m_isLoaded && !IsTransparent(scene_models[i])) {
                // Select appropriate shader variant for this model
                std::string shaderProgram = m_shaderVariants.SelectBestVariant("ModelProgram", scene_models[i]);
                
                if (shaderManager.UseShaderProgram(shaderProgram)) {
                    scene_models[i].Render(static_cast<ID3D11DeviceContext*>(m_renderer->GetDeviceContext()), 
                                         GetDeltaTime());
                    shaderManager.UnbindShaderProgram();
                } else {
                    // Fallback to basic rendering
                    UseFallbackShaderIfNeeded("ModelProgram");
                    scene_models[i].Render(static_cast<ID3D11DeviceContext*>(m_renderer->GetDeviceContext()), 
                                         GetDeltaTime());
                    shaderManager.UnbindShaderProgram();
                }
            }
        }
    }
    
    void RenderTransparentObjects() {
        // Sort transparent objects by depth
        SortTransparentObjects();
        
        // Render with alpha blending shader
        if (shaderManager.UseShaderProgram("TransparentProgram")) {
            for (auto& transparentModel : GetTransparentModels()) {
                transparentModel->Render(static_cast<ID3D11DeviceContext*>(m_renderer->GetDeviceContext()), 
                                       GetDeltaTime());
            }
            shaderManager.UnbindShaderProgram();
        }
    }
    
    void RenderParticleEffects() {
        // Use compute shader for particle simulation
        if (shaderManager.DoesShaderExist("ParticleCompute")) {
            RunComputeShader("ParticleCompute", 64, 1, 1);  // 64 thread groups
        }
        
        // Render particles with geometry shader
        if (shaderManager.UseShaderProgram("ParticleProgram")) {
            RenderAllParticleSystems();
            shaderManager.UnbindShaderProgram();
        }
    }
    
    void RenderUI() {
        // Switch to UI shader for 2D rendering
        if (shaderManager.UseShaderProgram("UIProgram")) {
            // Set 2D projection matrix
            SetUI2DProjection();
            
            // Render UI elements
            RenderUIElements();
            
            shaderManager.UnbindShaderProgram();
        }
    }
};
```

### 2. Scene-Specific Shader Management

```cpp
class SceneShaderManager {
private:
    std::unordered_map<SceneType, std::vector<std::string>> m_sceneShaders;
    
public:
    void Initialize() {
        // Define shaders needed for each scene type
        m_sceneShaders[SCENE_SPLASH] = {"SplashVertex", "SplashPixel", "FadeTransitionPixel"};
        m_sceneShaders[SCENE_GAMEPLAY] = {"ModelVertex", "ModelPixel", "LightingVertex", "LightingPixel", 
                                        "ParticleVertex", "ParticlePixel", "ParticleGeometry"};
        m_sceneShaders[SCENE_EDITOR] = {"WireframeVertex", "WireframePixel", "DebugVertex", "DebugPixel",
                                      "GridVertex", "GridPixel"};
    }
    
    void LoadSceneShaders(SceneType sceneType) {
        auto it = m_sceneShaders.find(sceneType);
        if (it != m_sceneShaders.end()) {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Loading shaders for scene type: %d", (int)sceneType);
            
            for (const std::string& shaderName : it->second) {
                if (!shaderManager.DoesShaderExist(shaderName)) {
                    LoadShaderByName(shaderName);
                }
            }
            
            // Create scene-specific programs
            CreateScenePrograms(sceneType);
        }
    }
    
    void UnloadSceneShaders(SceneType sceneType) {
        auto it = m_sceneShaders.find(sceneType);
        if (it != m_sceneShaders.end()) {
            for (const std::string& shaderName : it->second) {
                // Only unload if not used by other scenes
                if (CanUnloadShader(shaderName)) {
                    shaderManager.UnloadShader(shaderName);
                }
            }
        }
    }
    
private:
    void LoadShaderByName(const std::string& shaderName) {
        // Determine shader properties from name
        ShaderType type = GetShaderTypeFromName(shaderName);
        std::wstring path = L"./Assets/Shaders/" + std::wstring(shaderName.begin(), shaderName.end()) + L".hlsl";
        
        // Create appropriate profile
        ShaderProfile profile = CreateProfileForShader(shaderName);
        
        if (!shaderManager.LoadShader(shaderName, path, type, profile)) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Failed to load scene shader: %hs", shaderName.c_str());
        }
    }
    
    ShaderProfile CreateProfileForShader(const std::string& shaderName) {
        ShaderProfile profile;
        profile.optimized = true;
        
        // Add specific defines based on shader name
        if (shaderName.find("Lighting") != std::string::npos) {
            profile.defines.push_back("MAX_LIGHTS=" + std::to_string(MAX_LIGHTS));
            profile.defines.push_back("USE_DYNAMIC_LIGHTING");
        }
        
        if (shaderName.find("Particle") != std::string::npos) {
            profile.defines.push_back("MAX_PARTICLES=1024");
            profile.defines.push_back("USE_GPU_SIMULATION");
        }
        
        if (shaderName.find("Debug") != std::string::npos) {
            profile.debugInfo = true;
            profile.optimized = false;
        }
        
        return profile;
    }
    
    void CreateScenePrograms(SceneType sceneType) {
        switch (sceneType) {
            case SCENE_SPLASH:
                shaderManager.CreateShaderProgram("SplashProgram", "SplashVertex", "SplashPixel");
                break;
                
            case SCENE_GAMEPLAY:
                shaderManager.CreateShaderProgram("GameplayModelProgram", "ModelVertex", "ModelPixel");
                shaderManager.CreateShaderProgram("GameplayLightingProgram", "LightingVertex", "LightingPixel");
                shaderManager.CreateShaderProgram("GameplayParticleProgram", "ParticleVertex", "ParticlePixel", "ParticleGeometry");
                break;
                
            case SCENE_EDITOR:
                shaderManager.CreateShaderProgram("EditorWireframeProgram", "WireframeVertex", "WireframePixel");
                shaderManager.CreateShaderProgram("EditorDebugProgram", "DebugVertex", "DebugPixel");
                shaderManager.CreateShaderProgram("EditorGridProgram", "GridVertex", "GridPixel");
                break;
        }
    }
    
    bool CanUnloadShader(const std::string& shaderName) {
        // Check if shader is used by multiple scenes
        int usageCount = 0;
        for (const auto& pair : m_sceneShaders) {
            const auto& shaderList = pair.second;
            if (std::find(shaderList.begin(), shaderList.end(), shaderName) != shaderList.end()) {
                usageCount++;
            }
        }
        
        return usageCount <= 1;  // Only unload if used by one scene or less
    }
};
```

### 3. Performance Optimization Example

```cpp
class ShaderPerformanceOptimizer {
private:
    struct ShaderMetrics {
        std::chrono::high_resolution_clock::time_point lastUsed;
        int usageCount;
        size_t compilationTime;
        bool isHotPath;  // Frequently used shaders
    };
    
    std::unordered_map<std::string, ShaderMetrics> m_shaderMetrics;
    
public:
    void TrackShaderUsage(const std::string& shaderName) {
        auto now = std::chrono::high_resolution_clock::now();
        
        auto it = m_shaderMetrics.find(shaderName);
        if (it != m_shaderMetrics.end()) {
            it->second.lastUsed = now;
            it->second.usageCount++;
            
            // Mark as hot path if used frequently
            if (it->second.usageCount > 100) {
                it->second.isHotPath = true;
            }
        } else {
            ShaderMetrics metrics;
            metrics.lastUsed = now;
            metrics.usageCount = 1;
            metrics.isHotPath = false;
            m_shaderMetrics[shaderName] = metrics;
        }
    }
    
    void OptimizeShaderPerformance() {
        auto now = std::chrono::high_resolution_clock::now();
        const auto fiveMinutes = std::chrono::minutes(5);
        
        // Find unused shaders
        std::vector<std::string> unusedShaders;
        for (const auto& pair : m_shaderMetrics) {
            if (now - pair.second.lastUsed > fiveMinutes && !pair.second.isHotPath) {
                unusedShaders.push_back(pair.first);
            }
        }
        
        // Unload unused shaders to free memory
        for (const std::string& shaderName : unusedShaders) {
            if (shaderManager.DoesShaderExist(shaderName)) {
                ShaderResource* shader = shaderManager.GetShader(shaderName);
                if (shader && shader->referenceCount == 0 && !shader->isInUse) {
                    shaderManager.UnloadShader(shaderName);
                    m_shaderMetrics.erase(shaderName);
                    
                    debug.logDebugMessage(LogLevel::LOG_INFO, L"Unloaded unused shader for optimization: %hs", 
                                         shaderName.c_str());
                }
            }
        }
        
        // Preload hot path shaders if not loaded
        PreloadHotPathShaders();
    }
    
    void PreloadHotPathShaders() {
        // Define critical shaders that should always be loaded
        std::vector<std::string> criticalShaders = {
            "ModelVertex", "ModelPixel", "UIVertex", "UIPixel", "BasicVertex", "BasicPixel"
        };
        
        for (const std::string& shaderName : criticalShaders) {
            if (!shaderManager.DoesShaderExist(shaderName)) {
                // Load critical shader
                ShaderType type = GetShaderTypeFromName(shaderName);
                std::wstring path = L"./Assets/Shaders/" + std::wstring(shaderName.begin(), shaderName.end()) + L".hlsl";
                
                if (shaderManager.LoadShader(shaderName, path, type)) {
                    m_shaderMetrics[shaderName].isHotPath = true;
                    debug.logDebugMessage(LogLevel::LOG_INFO, L"Preloaded critical shader: %hs", shaderName.c_str());
                }
            }
        }
    }
    
    void GeneratePerformanceReport() {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"=== SHADER PERFORMANCE REPORT ===");
        
        // Sort shaders by usage count
        std::vector<std::pair<std::string, ShaderMetrics>> sortedShaders(m_shaderMetrics.begin(), m_shaderMetrics.end());
        std::sort(sortedShaders.begin(), sortedShaders.end(), 
                 [](const auto& a, const auto& b) { return a.second.usageCount > b.second.usageCount; });
        
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Top 10 Most Used Shaders:");
        for (size_t i = 0; i < std::min(sortedShaders.size(), size_t(10)); ++i) {
            const auto& pair = sortedShaders[i];
            debug.logDebugMessage(LogLevel::LOG_INFO, L"  %d. %hs - Used %d times %s", 
                                 (int)(i + 1), pair.first.c_str(), pair.second.usageCount,
                                 pair.second.isHotPath ? L"(HOT PATH)" : L"");
        }
        
        // Show overall statistics
        ShaderManagerStats stats = shaderManager.GetStatistics();
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Total Shaders Loaded: %d", stats.totalShadersLoaded);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Memory Usage: %llu KB", stats.memoryUsage / 1024);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Compilation Failures: %d", stats.compilationFailures);
    }
};
```

## Manual Thread Lock Usage

### 1. Manual Thread Lock Usage

```cpp
// Example of manual thread safety for custom shader operations
void CustomShaderOperation() {
    // Acquire lock with custom timeout
    if (shaderManager.AcquireShaderLock(500)) { // 500ms timeout
        try {
            // Perform thread-safe shader operations
            shaderManager.LoadShader("CustomShader", L"./Assets/Shaders/Custom.hlsl", ShaderType::VERTEX_SHADER);
            shaderManager.CreateShaderProgram("CustomProgram", "CustomShader", "DefaultPixel");
            
            // Operations completed successfully
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Custom shader operations completed successfully.");
        }
        catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Exception during shader operations: %hs", e.what());
        }
        
        // Always release the lock
        shaderManager.ReleaseShaderLock();
    } else {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire shader lock for custom operations.");
    }
}
```

### 2. RAII-Style Usage with Custom Helper:

```cpp
// Custom RAII wrapper for shader lock
class ShaderLockGuard {
private:
    bool m_lockAcquired;
    
public:
    explicit ShaderLockGuard(int timeoutMs = 1000) : m_lockAcquired(false) {
        m_lockAcquired = shaderManager.AcquireShaderLock(timeoutMs);
        if (!m_lockAcquired) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"ShaderLockGuard failed to acquire lock.");
        }
    }
    
    ~ShaderLockGuard() {
        if (m_lockAcquired) {
            shaderManager.ReleaseShaderLock();
        }
    }
    
    bool IsLocked() const { return m_lockAcquired; }
    
    // Prevent copying
    ShaderLockGuard(const ShaderLockGuard&) = delete;
    ShaderLockGuard& operator=(const ShaderLockGuard&) = delete;
};

// Usage example
void SafeShaderOperations() {
    ShaderLockGuard lock(2000); // 2 second timeout
    if (lock.IsLocked()) {
        // Perform shader operations safely
        // Lock automatically released when 'lock' goes out of scope
    }
}
```

### 3. Conditional Locking for Performance-Critical Code:

```cpp
void PerformanceShaderUpdate(bool needsThreadSafety = true) {
    bool hasLock = false;
    
    if (needsThreadSafety) {
        hasLock = shaderManager.AcquireShaderLock(50); // Short timeout for performance
        if (!hasLock) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Skipping shader update due to lock contention.");
            return;
        }
    }
    
    // Perform fast shader operations
    shaderManager.CheckForShaderFileChanges();
    
    if (hasLock) {
        shaderManager.ReleaseShaderLock();
    }
}
```

### 4. Multi-Threading Shader Loading:

```cpp
void LoadShadersInParallel() {
    // Thread 1: Load model shaders
    std::thread modelThread([]() {
        if (shaderManager.AcquireShaderLock(3000)) {
            shaderManager.LoadShader("ModelVertex", L"./Assets/Shaders/ModelVertex.hlsl", ShaderType::VERTEX_SHADER);
            shaderManager.LoadShader("ModelPixel", L"./Assets/Shaders/ModelPixel.hlsl", ShaderType::PIXEL_SHADER);
            shaderManager.ReleaseShaderLock();
        }
    });
    
    // Thread 2: Load UI shaders
    std::thread uiThread([]() {
        if (shaderManager.AcquireShaderLock(3000)) {
            shaderManager.LoadShader("UIVertex", L"./Assets/Shaders/UIVertex.hlsl", ShaderType::VERTEX_SHADER);
            shaderManager.LoadShader("UIPixel", L"./Assets/Shaders/UIPixel.hlsl", ShaderType::PIXEL_SHADER);
            shaderManager.ReleaseShaderLock();
        }
    });
    
    // Wait for both threads to complete
    modelThread.join();
    uiThread.join();
}
```

---

## Best Practices Summary

### 1. Initialization Order
1. Initialize Renderer first
2. Initialize ShaderManager with Renderer
3. Load core/fallback shaders
4. Setup shader variants and specializations
5. Configure integration with other systems

### 2. Error Handling
- Always check return values from shader operations
- Implement fallback shaders for critical functionality
- Use proper debug logging with detailed error messages
- Validate shaders after loading

### 3. Performance Optimization
- Use hot-reloading only in debug builds
- Unload unused shaders to manage memory
- Preload critical shaders at startup
- Use shader variants for different scenarios
- Monitor shader usage patterns

### 4. Resource Management
- Clean up shaders in reverse order of loading
- Use reference counting for shared shaders
- Implement proper cleanup in destructors
- Monitor memory usage regularly

### 5. Platform Compatibility
- Use platform detection for automatic shader selection
- Implement fallbacks for unsupported features
- Test on all target platforms
- Use appropriate shader profiles for each platform

This comprehensive guide shows how to effectively use the ShaderManager system in your game engine. 

The examples demonstrate real-world usage patterns and best practices for optimal performance and maintainability.