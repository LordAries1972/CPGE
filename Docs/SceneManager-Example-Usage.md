# SceneManager & GLTFAnimator Class Usage Guide

## Table of Contents

1. [Introduction](#introduction)
2. [Overview](#overview)
3. [Prerequisites](#prerequisites)
4. [SceneManager Class](#scenemanager-class)
   - 4.1. [Basic Setup](#basic-setup)
   - 4.2. [Loading GLTF Scenes](#loading-gltf-scenes)
   - 4.3. [Loading GLB Scenes](#loading-glb-scenes)
   - 4.4. [Scene Management](#scene-management)
   - 4.5. [Parent-Child Relationships](#parent-child-relationships)
5. [GLTFAnimator Class](#gltfanimator-class)
   - 5.1. [Animation System Overview](#animation-system-overview)
   - 5.2. [Animation Data Structures](#animation-data-structures)
   - 5.3. [Starting Animations](#starting-animations)
   - 5.4. [Animation Playback Control](#animation-playback-control)
   - 5.5. [Animation Queries](#animation-queries)
6. [Integration Examples](#integration-examples)
   - 6.1. [Basic Scene Loading](#basic-scene-loading)
   - 6.2. [Animated Scene Loading](#animated-scene-loading)
   - 6.3. [Multiple Animation Control](#multiple-animation-control)
   - 6.4. [Game Loop Integration](#game-loop-integration)
7. [Advanced Usage](#advanced-usage)
   - 7.1. [Custom Animation Timing](#custom-animation-timing)
   - 7.2. [Animation Blending](#animation-blending)
   - 7.3. [Performance Optimization](#performance-optimization)
8. [Debugging and Troubleshooting](#debugging-and-troubleshooting)
   - 8.1. [Debug Output](#debug-output)
   - 8.2. [Common Issues](#common-issues)
   - 8.3. [Performance Monitoring](#performance-monitoring)
9. [Best Practices](#best-practices)
10. [API Reference](#api-reference)
    - 10.1. [SceneManager Methods](#scenemanager-methods)
    - 10.2. [GLTFAnimator Methods](#gltfanimator-methods)

---

## Introduction

The SceneManager and GLTFAnimator classes provide a comprehensive solution for loading, managing, and animating 3D scenes from GLTF and GLB files. This guide will walk you through everything you need to know to effectively use these systems in your DirectX 11/12 application.

The SceneManager handles static scene loading, model management, and parent-child relationships, while the GLTFAnimator provides full animation support including keyframe interpolation, quaternion rotations, and hierarchical animation control.

## Overview

### Key Features

- **Full GLTF 2.0 Support**: Load both .gltf (JSON + .bin) and .glb (binary) formats
- **Hierarchical Animation**: Animate entire model hierarchies based on parent model IDs
- **Multiple Interpolation Types**: Linear, Step, and Cubic Spline interpolation
- **Quaternion Rotations**: Proper SLERP interpolation for smooth rotations
- **Flexible Playback Control**: Start, stop, pause, resume, speed control, and looping
- **Parent-Child Relationships**: Automatic detection and management of model hierarchies
- **Memory Safe**: Proper cleanup and resource management
- **Debug Support**: Comprehensive logging and error reporting

### System Architecture

```
Application
    ├── SceneManager (Scene loading and management)
    │   ├── ParseGLTFScene() - Load .gltf files
    │   ├── ParseGLBScene() - Load .glb files
    │   └── UpdateSceneAnimations() - Update all animations
    │
    └── GLTFAnimator (Animation system)
        ├── Animation Parsing
        ├── Keyframe Interpolation
        ├── Playback Control
        └── Hierarchy Management
```

## Prerequisites

Before using the SceneManager and GLTFAnimator classes, ensure you have:

1. **DirectX 11 Environment**: Properly initialized DX11 renderer
2. **nlohmann/json Library**: For JSON parsing
3. **Debug System**: Debug.h included for logging
4. **Exception Handler**: ExceptionHandler system active
5. **Model System**: Models.h and Model class properly set up

### Required Headers

```cpp
#include "SceneManager.h"
#include "GLTFAnimator.h"
#include "Models.h"
#include "Debug.h"
```

### Global Declarations

```cpp
extern SceneManager sceneManager;
extern GLTFAnimator gltfAnimator;
extern Debug debug;
```

## SceneManager Class

### Basic Setup

#### Initialization

```cpp
// Initialize the SceneManager with your renderer
std::shared_ptr<DX11Renderer> renderer = std::make_shared<DX11Renderer>();
bool success = sceneManager.Initialize(renderer);

if (!success) {
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to initialize SceneManager");
    return false;
}
```

#### Scene Types

The SceneManager supports various scene types for different application states:

```cpp
enum SceneType {
    SCENE_NONE = 0,
    SCENE_INITIALISE,
    SCENE_SPLASH,
    SCENE_INTRO,
    SCENE_INTRO_MOVIE,
    SCENE_GAMEPLAY,
    SCENE_GAMEOVER,
    SCENE_CREDITS,
    SCENE_EDITOR,
    SCENE_LOAD_MP3
};
```

### Loading GLTF Scenes

#### Basic GLTF Loading

```cpp
// Load a .gltf file with separate .bin file
std::wstring gltfPath = L"assets/models/character.gltf";
bool loaded = sceneManager.ParseGLTFScene(gltfPath);

if (loaded) {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"GLTF scene loaded successfully");
    
    // Check if animations were loaded
    if (sceneManager.bAnimationsLoaded) {
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Scene contains %d animations", 
                            gltfAnimator.GetAnimationCount());
    }
} else {
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to load GLTF scene");
}
```

#### GLTF with Custom Path Handling

```cpp
// Load GLTF with error checking and path validation
std::wstring basePath = L"C:/GameAssets/Models/";
std::wstring modelName = L"character_rigged.gltf";
std::wstring fullPath = basePath + modelName;

// Verify file exists before loading
if (std::filesystem::exists(fullPath)) {
    bool success = sceneManager.ParseGLTFScene(fullPath);
    
    if (success) {
        // Get exporter information
        const std::wstring& exporter = sceneManager.GetLastDetectedExporter();
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Loaded model from: %ls", exporter.c_str());
        
        // Check for Sketchfab-specific handling
        if (sceneManager.IsSketchfabScene()) {
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Applying Sketchfab compatibility patches");
        }
    }
}
```

### Loading GLB Scenes

#### Basic GLB Loading

```cpp
// Load a .glb binary file (self-contained)
std::wstring glbPath = L"assets/models/environment.glb";
bool loaded = sceneManager.ParseGLBScene(glbPath);

if (loaded) {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"GLB scene loaded successfully");
    
    // GLB files are self-contained, so check for embedded animations
    if (sceneManager.bAnimationsLoaded) {
        gltfAnimator.DebugPrintAnimationInfo();
    }
}
```

#### GLB with Size Validation

```cpp
// Load GLB with file size checking
std::wstring glbPath = L"assets/large_scene.glb";

// Check file size before loading
auto fileSize = std::filesystem::file_size(glbPath);
const size_t maxFileSize = 100 * 1024 * 1024; // 100MB limit

if (fileSize > maxFileSize) {
    debug.logDebugMessage(LogLevel::LOG_WARNING, L"GLB file is very large: %lld bytes", fileSize);
}

bool success = sceneManager.ParseGLBScene(glbPath);
if (success) {
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Successfully loaded large GLB scene (%lld bytes)", fileSize);
}
```

### Scene Management

#### Scene Switching

```cpp
// Set up scene transitions
sceneManager.SetGotoScene(SCENE_GAMEPLAY);

// In your main loop, check for scene transitions
if (sceneManager.bSceneSwitching) {
    // Clean up current scene
    sceneManager.CleanUp();
    
    // Initialize new scene
    sceneManager.InitiateScene();
    
    // Load new scene content based on scene type
    switch (sceneManager.stSceneType) {
        case SCENE_GAMEPLAY:
            sceneManager.ParseGLTFScene(L"assets/gameplay_level.gltf");
            break;
        case SCENE_CREDITS:
            sceneManager.ParseGLBScene(L"assets/credits_scene.glb");
            break;
    }
}
```

#### Scene State Management

```cpp
// Save scene state to file
std::wstring saveFile = L"saves/scene_state.dat";
bool saved = sceneManager.SaveSceneState(saveFile);

if (saved) {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Scene state saved successfully");
}

// Load scene state from file
bool loaded = sceneManager.LoadSceneState(saveFile);
if (loaded) {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Scene state restored");
}
```

### Parent-Child Relationships

The SceneManager automatically manages parent-child relationships using the `iParentModelID` field:

#### Understanding Parent-Child IDs

```cpp
// After loading a scene, inspect parent-child relationships
for (int i = 0; i < MAX_SCENE_MODELS; ++i) {
    if (sceneManager.scene_models[i].m_isLoaded) {
        const ModelInfo& info = sceneManager.scene_models[i].m_modelInfo;
        
        if (info.iParentModelID == -1) {
            // This is a parent model
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Parent Model: ID=%d, Name=%ls", info.ID, info.name.c_str());
        } else {
            // This is a child model
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Child Model: ID=%d, Parent=%d, Name=%ls", 
                info.ID, info.iParentModelID, info.name.c_str());
        }
    }
}
```

#### Finding Model Hierarchies

```cpp
// Function to find all children of a parent model
std::vector<int> FindChildModels(int parentModelID) {
    std::vector<int> children;
    
    for (int i = 0; i < MAX_SCENE_MODELS; ++i) {
        if (sceneManager.scene_models[i].m_isLoaded) {
            const ModelInfo& info = sceneManager.scene_models[i].m_modelInfo;
            
            if (info.iParentModelID == parentModelID) {
                children.push_back(info.ID);
            }
        }
    }
    
    return children;
}

// Usage example
int parentID = 5;
auto children = FindChildModels(parentID);
debug.logDebugMessage(LogLevel::LOG_INFO, L"Parent %d has %d children", 
                    parentID, static_cast<int>(children.size()));
```

## GLTFAnimator Class

### Animation System Overview

The GLTFAnimator manages all animation-related functionality, from parsing GLTF animation data to real-time playback control.

#### Key Concepts

- **Animation**: A collection of samplers and channels that define how objects move over time
- **Sampler**: Defines keyframe times and values with interpolation method
- **Channel**: Connects a sampler to a specific node and property (translation, rotation, scale)
- **Instance**: A playing animation associated with a parent model ID

### Animation Data Structures

#### Animation Interpolation Types

```cpp
enum class AnimationInterpolation : int {
    LINEAR = 0,        // Smooth linear interpolation
    STEP = 1,          // Instant transitions
    CUBICSPLINE = 2    // Smooth curved interpolation
};
```

#### Animation Target Properties

```cpp
enum class AnimationTargetPath : int {
    TRANSLATION = 0,   // Position (X, Y, Z)
    ROTATION = 1,      // Rotation (Quaternion X, Y, Z, W)
    SCALE = 2,         // Scale (X, Y, Z)
    WEIGHTS = 3        // Morph target weights (not implemented)
};
```

### Starting Animations

#### Basic Animation Playback

```cpp
// Start the first animation for parent model ID 0
int parentModelID = 0;
int animationIndex = 0;

bool started = gltfAnimator.StartAnimation(parentModelID, animationIndex);
if (started) {
    debug.logDebugMessage(LogLevel::LOG_INFO, 
        L"Started animation %d for parent model %d", animationIndex, parentModelID);
} else {
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to start animation");
}
```

#### Animation with Custom Settings

```cpp
// Start animation with custom playback settings
int parentModelID = 2;
int animationIndex = 1;

// Create animation instance first
bool created = gltfAnimator.CreateAnimationInstance(animationIndex, parentModelID);
if (created) {
    // Configure animation settings
    gltfAnimator.SetAnimationSpeed(parentModelID, 0.5f);      // Half speed
    gltfAnimator.SetAnimationLooping(parentModelID, true);    // Enable looping
    
    // Start playback
    gltfAnimator.StartAnimation(parentModelID, animationIndex);
    
    debug.logDebugMessage(LogLevel::LOG_INFO, 
        L"Started custom animation: Parent=%d, Speed=0.5x, Looping=ON", parentModelID);
}
```

### Animation Playback Control

#### Play/Pause/Stop Controls

```cpp
// Pause animation
bool paused = gltfAnimator.PauseAnimation(parentModelID);
if (paused) {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Animation paused");
}

// Resume animation
bool resumed = gltfAnimator.ResumeAnimation(parentModelID);
if (resumed) {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Animation resumed");
}

// Stop animation completely
bool stopped = gltfAnimator.StopAnimation(parentModelID);
if (stopped) {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Animation stopped");
}
```

#### Speed and Timing Control

```cpp
// Change animation speed during playback
int parentModelID = 1;

// Double speed
gltfAnimator.SetAnimationSpeed(parentModelID, 2.0f);

// Reverse playback
gltfAnimator.SetAnimationSpeed(parentModelID, -1.0f);

// Jump to specific time (in seconds)
float targetTime = 5.5f;
gltfAnimator.SetAnimationTime(parentModelID, targetTime);

debug.logDebugMessage(LogLevel::LOG_INFO, 
    L"Animation time set to %.2f seconds", targetTime);
```

### Animation Queries

#### Getting Animation Information

```cpp
// Get total number of loaded animations
int animationCount = gltfAnimator.GetAnimationCount();
debug.logDebugMessage(LogLevel::LOG_INFO, L"Total animations available: %d", animationCount);

// Get specific animation details
for (int i = 0; i < animationCount; ++i) {
    const GLTFAnimation* animation = gltfAnimator.GetAnimation(i);
    if (animation) {
        float duration = gltfAnimator.GetAnimationDuration(i);
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"Animation %d: %ls (%.2f seconds, %d samplers, %d channels)",
            i, animation->name.c_str(), duration,
            static_cast<int>(animation->samplers.size()),
            static_cast<int>(animation->channels.size()));
    }
}
```

#### Checking Animation Status

```cpp
// Check if animation is currently playing
bool isPlaying = gltfAnimator.IsAnimationPlaying(parentModelID);
if (isPlaying) {
    // Get current animation time
    float currentTime = gltfAnimator.GetAnimationTime(parentModelID);
    
    // Get animation instance for more details
    AnimationInstance* instance = gltfAnimator.GetAnimationInstance(parentModelID);
    if (instance) {
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"Animation Status: Playing=%s, Time=%.2f, Speed=%.2f, Looping=%s",
            isPlaying ? L"YES" : L"NO",
            currentTime,
            instance->playbackSpeed,
            instance->isLooping ? L"YES" : L"NO");
    }
}
```

## Integration Examples

### Basic Scene Loading

Here's a complete example of loading and displaying a static GLTF scene:

```cpp
class GameApplication {
private:
    SceneManager sceneManager;
    std::shared_ptr<DX11Renderer> renderer;
    
public:
    CharacterAnimationSystem() : characterParentID(-1), currentState(IDLE), previousState(IDLE) {
        // Map character states to animation indices
        stateToAnimation[IDLE] = 0;
        stateToAnimation[WALKING] = 1;
        stateToAnimation[RUNNING] = 2;
        stateToAnimation[JUMPING] = 3;
        stateToAnimation[ATTACKING] = 4;
    }
    
    bool Initialize(int parentID) {
        characterParentID = parentID;
        
        // Validate parent model exists
        if (!IsValidParentModel(parentID)) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid character parent ID: %d", parentID);
            return false;
        }
        
        // Create animation instance
        bool created = gltfAnimator.CreateAnimationInstance(stateToAnimation[IDLE], characterParentID);
        if (!created) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to create character animation instance");
            return false;
        }
        
        // Set up initial state
        SetState(IDLE);
        
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Character animation system initialized for parent ID %d", parentID);
        return true;
    }
    
    void SetState(CharacterState newState) {
        if (newState == currentState) return;
        
        previousState = currentState;
        currentState = newState;
        
        // Get animation index for new state
        auto it = stateToAnimation.find(newState);
        if (it == stateToAnimation.end()) {
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"No animation mapped for state %d", static_cast<int>(newState));
            return;
        }
        
        int animationIndex = it->second;
        
        // Configure animation based on state
        ConfigureAnimationForState(newState);
        
        // Start the animation
        bool started = gltfAnimator.StartAnimation(characterParentID, animationIndex);
        if (started) {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Character state changed: %d -> %d (Animation %d)", 
                                static_cast<int>(previousState), static_cast<int>(currentState), animationIndex);
        }
    }
    
    CharacterState GetCurrentState() const {
        return currentState;
    }
    
    void Update(float deltaTime) {
        // Handle state-specific logic
        switch (currentState) {
            case JUMPING:
                HandleJumpingState(deltaTime);
                break;
            case ATTACKING:
                HandleAttackingState(deltaTime);
                break;
            default:
                // Most states are handled by continuous animation
                break;
        }
    }
    
private:
    void ConfigureAnimationForState(CharacterState state) {
        switch (state) {
            case IDLE:
                gltfAnimator.SetAnimationLooping(characterParentID, true);
                gltfAnimator.SetAnimationSpeed(characterParentID, 1.0f);
                break;
                
            case WALKING:
                gltfAnimator.SetAnimationLooping(characterParentID, true);
                gltfAnimator.SetAnimationSpeed(characterParentID, 1.0f);
                break;
                
            case RUNNING:
                gltfAnimator.SetAnimationLooping(characterParentID, true);
                gltfAnimator.SetAnimationSpeed(characterParentID, 1.5f); // Faster
                break;
                
            case JUMPING:
                gltfAnimator.SetAnimationLooping(characterParentID, false); // Play once
                gltfAnimator.SetAnimationSpeed(characterParentID, 1.2f);
                break;
                
            case ATTACKING:
                gltfAnimator.SetAnimationLooping(characterParentID, false); // Play once
                gltfAnimator.SetAnimationSpeed(characterParentID, 1.8f); // Fast attack
                break;
        }
    }
    
    void HandleJumpingState(float deltaTime) {
        // Check if jump animation finished
        if (!gltfAnimator.IsAnimationPlaying(characterParentID)) {
            // Jump finished, return to appropriate state
            SetState(IDLE); // Or determine based on movement input
        }
    }
    
    void HandleAttackingState(float deltaTime) {
        // Check if attack animation finished
        if (!gltfAnimator.IsAnimationPlaying(characterParentID)) {
            // Attack finished, return to previous state or idle
            SetState(previousState == ATTACKING ? IDLE : previousState);
        }
    }
    
    bool IsValidParentModel(int parentID) {
        for (int i = 0; i < MAX_SCENE_MODELS; ++i) {
            if (sceneManager.scene_models[i].m_isLoaded) {
                const ModelInfo& info = sceneManager.scene_models[i].m_modelInfo;
                if (info.ID == parentID && info.iParentModelID == -1) {
                    return true;
                }
            }
        }
        return false;
    }
};
```

### Interactive Animation Controller

```cpp
class InteractiveAnimationController {
private:
    struct InteractiveObject {
        int parentModelID;
        std::wstring name;
        std::vector<int> availableAnimations;
        int currentAnimationIndex;
        bool isInteractable;
        float interactionCooldown;
        float cooldownTimer;
    };
    
    std::vector<InteractiveObject> interactiveObjects;
    
public:
    void RegisterInteractiveObject(int parentModelID, const std::wstring& name, 
                                 const std::vector<int>& animations, float cooldown = 2.0f) {
        InteractiveObject obj;
        obj.parentModelID = parentModelID;
        obj.name = name;
        obj.availableAnimations = animations;
        obj.currentAnimationIndex = 0;
        obj.isInteractable = true;
        obj.interactionCooldown = cooldown;
        obj.cooldownTimer = 0.0f;
        
        interactiveObjects.push_back(obj);
        
        // Start with first animation if available
        if (!animations.empty()) {
            gltfAnimator.StartAnimation(parentModelID, animations[0]);
            gltfAnimator.SetAnimationLooping(parentModelID, true);
        }
        
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Registered interactive object: %ls (Parent ID: %d)", 
                            name.c_str(), parentModelID);
    }
    
    bool InteractWithObject(const std::wstring& objectName) {
        for (auto& obj : interactiveObjects) {
            if (obj.name == objectName && obj.isInteractable) {
                // Cycle to next animation
                obj.currentAnimationIndex = (obj.currentAnimationIndex + 1) % obj.availableAnimations.size();
                int nextAnimation = obj.availableAnimations[obj.currentAnimationIndex];
                
                // Play the next animation
                gltfAnimator.StartAnimation(obj.parentModelID, nextAnimation);
                gltfAnimator.SetAnimationLooping(obj.parentModelID, true);
                
                // Set cooldown
                obj.isInteractable = false;
                obj.cooldownTimer = obj.interactionCooldown;
                
                debug.logDebugMessage(LogLevel::LOG_INFO, L"Interacted with %ls, playing animation %d", 
                                    objectName.c_str(), nextAnimation);
                return true;
            }
        }
        return false;
    }
    
    bool InteractWithObjectAtPosition(const XMFLOAT3& position, float maxDistance) {
        for (auto& obj : interactiveObjects) {
            if (!obj.isInteractable) continue;
            
            // Find the object's position
            for (int i = 0; i < MAX_SCENE_MODELS; ++i) {
                if (sceneManager.scene_models[i].m_isLoaded) {
                    const ModelInfo& info = sceneManager.scene_models[i].m_modelInfo;
                    if (info.ID == obj.parentModelID && info.iParentModelID == -1) {
                        // Calculate distance
                        float dx = info.position.x - position.x;
                        float dy = info.position.y - position.y;
                        float dz = info.position.z - position.z;
                        float distance = sqrt(dx*dx + dy*dy + dz*dz);
                        
                        if (distance <= maxDistance) {
                            return InteractWithObject(obj.name);
                        }
                        break;
                    }
                }
            }
        }
        return false;
    }
    
    void Update(float deltaTime) {
        for (auto& obj : interactiveObjects) {
            if (!obj.isInteractable) {
                obj.cooldownTimer -= deltaTime;
                if (obj.cooldownTimer <= 0.0f) {
                    obj.isInteractable = true;
                    obj.cooldownTimer = 0.0f;
                }
            }
        }
    }
    
    std::vector<std::wstring> GetNearbyInteractables(const XMFLOAT3& position, float maxDistance) {
        std::vector<std::wstring> nearby;
        
        for (const auto& obj : interactiveObjects) {
            if (!obj.isInteractable) continue;
            
            // Find object position and check distance
            for (int i = 0; i < MAX_SCENE_MODELS; ++i) {
                if (sceneManager.scene_models[i].m_isLoaded) {
                    const ModelInfo& info = sceneManager.scene_models[i].m_modelInfo;
                    if (info.ID == obj.parentModelID && info.iParentModelID == -1) {
                        float dx = info.position.x - position.x;
                        float dy = info.position.y - position.y;
                        float dz = info.position.z - position.z;
                        float distance = sqrt(dx*dx + dy*dy + dz*dz);
                        
                        if (distance <= maxDistance) {
                            nearby.push_back(obj.name);
                        }
                        break;
                    }
                }
            }
        }
        
        return nearby;
    }
};
```

### Scene Animation Sequencer

```cpp
class SceneAnimationSequencer {
private:
    struct SequenceStep {
        float startTime;
        int parentModelID;
        int animationIndex;
        float duration;
        float speed;
        bool loop;
        std::wstring description;
    };
    
    std::vector<SequenceStep> sequence;
    float sequenceTimer;
    bool isPlaying;
    bool isPaused;
    size_t currentStepIndex;
    
public:
    SceneAnimationSequencer() : sequenceTimer(0.0f), isPlaying(false), isPaused(false), currentStepIndex(0) {}
    
    void AddSequenceStep(float startTime, int parentModelID, int animationIndex, 
                        float duration = -1.0f, float speed = 1.0f, bool loop = false,
                        const std::wstring& description = L"") {
        SequenceStep step;
        step.startTime = startTime;
        step.parentModelID = parentModelID;
        step.animationIndex = animationIndex;
        step.duration = duration;
        step.speed = speed;
        step.loop = loop;
        step.description = description;
        
        sequence.push_back(step);
        
        // Sort by start time
        std::sort(sequence.begin(), sequence.end(), 
                 [](const SequenceStep& a, const SequenceStep& b) {
                     return a.startTime < b.startTime;
                 });
        
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Added sequence step at %.2fs: %ls", 
                            startTime, description.empty() ? L"Unnamed Step" : description.c_str());
    }
    
    void StartSequence() {
        sequenceTimer = 0.0f;
        currentStepIndex = 0;
        isPlaying = true;
        isPaused = false;
        
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Animation sequence started");
    }
    
    void StopSequence() {
        isPlaying = false;
        isPaused = false;
        
        // Stop all animations in the sequence
        for (const auto& step : sequence) {
            gltfAnimator.StopAnimation(step.parentModelID);
        }
        
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Animation sequence stopped");
    }
    
    void PauseSequence() {
        isPaused = true;
        
        // Pause all currently playing animations
        for (const auto& step : sequence) {
            if (gltfAnimator.IsAnimationPlaying(step.parentModelID)) {
                gltfAnimator.PauseAnimation(step.parentModelID);
            }
        }
        
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Animation sequence paused");
    }
    
    void ResumeSequence() {
        isPaused = false;
        
        // Resume all paused animations
        for (const auto& step : sequence) {
            AnimationInstance* instance = gltfAnimator.GetAnimationInstance(step.parentModelID);
            if (instance && !instance->isPlaying) {
                gltfAnimator.ResumeAnimation(step.parentModelID);
            }
        }
        
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Animation sequence resumed");
    }
    
    void Update(float deltaTime) {
        if (!isPlaying || isPaused) return;
        
        sequenceTimer += deltaTime;
        
        // Process any steps that should start at current time
        for (size_t i = currentStepIndex; i < sequence.size(); ++i) {
            const SequenceStep& step = sequence[i];
            
            if (sequenceTimer >= step.startTime) {
                ExecuteSequenceStep(step);
                currentStepIndex = i + 1;
            } else {
                break; // Steps are sorted by time, so we can break here
            }
        }
        
        // Check if sequence is complete
        if (currentStepIndex >= sequence.size()) {
            bool anyStillPlaying = false;
            for (const auto& step : sequence) {
                if (gltfAnimator.IsAnimationPlaying(step.parentModelID)) {
                    anyStillPlaying = true;
                    break;
                }
            }
            
            if (!anyStillPlaying) {
                isPlaying = false;
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Animation sequence completed");
            }
        }
    }
    
    float GetSequenceDuration() const {
        if (sequence.empty()) return 0.0f;
        
        float maxEndTime = 0.0f;
        for (const auto& step : sequence) {
            float endTime = step.startTime;
            if (step.duration > 0.0f) {
                endTime += step.duration;
            } else {
                // Use animation's natural duration
                float naturalDuration = gltfAnimator.GetAnimationDuration(step.animationIndex);
                if (naturalDuration > 0.0f) {
                    endTime += naturalDuration / step.speed;
                }
            }
            maxEndTime = std::max(maxEndTime, endTime);
        }
        
        return maxEndTime;
    }
    
    bool IsPlaying() const { return isPlaying; }
    bool IsPaused() const { return isPaused; }
    float GetCurrentTime() const { return sequenceTimer; }
    
    void ClearSequence() {
        StopSequence();
        sequence.clear();
        currentStepIndex = 0;
        sequenceTimer = 0.0f;
        
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Animation sequence cleared");
    }
    
private:
    void ExecuteSequenceStep(const SequenceStep& step) {
        // Create animation instance if needed
        bool hasInstance = gltfAnimator.GetAnimationInstance(step.parentModelID) != nullptr;
        if (!hasInstance) {
            gltfAnimator.CreateAnimationInstance(step.animationIndex, step.parentModelID);
        }
        
        // Configure animation
        gltfAnimator.SetAnimationSpeed(step.parentModelID, step.speed);
        gltfAnimator.SetAnimationLooping(step.parentModelID, step.loop);
        
        // Start animation
        bool started = gltfAnimator.StartAnimation(step.parentModelID, step.animationIndex);
        
        if (started) {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Executed sequence step at %.2fs: %ls", 
                                sequenceTimer, step.description.empty() ? L"Unnamed Step" : step.description.c_str());
        }
    }
};
```

## Troubleshooting Guide

### Common Error Messages and Solutions

#### "Failed to initialize SceneManager"
**Possible Causes:**
- Renderer not properly initialized
- DirectX 11 device creation failed
- Invalid renderer pointer

**Solutions:**
```cpp
// Verify renderer initialization
if (!renderer->IsInitialized()) {
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Renderer must be initialized before SceneManager");
    return false;
}

// Check DirectX 11 support
if (!renderer->SupportsDirectX11()) {
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"DirectX 11 not supported on this system");
    return false;
}
```

#### "JSON parse error in GLB/GLTF"
**Possible Causes:**
- Corrupted file
- Unsupported GLTF version
- Invalid JSON format

**Solutions:**
```cpp
// Validate file before parsing
bool ValidateGLTFFile(const std::wstring& filePath) {
    // Check file size
    auto fileSize = std::filesystem::file_size(filePath);
    if (fileSize == 0) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"GLTF file is empty");
        return false;
    }
    
    // Basic header validation for GLB
    if (filePath.ends_with(L".glb")) {
        std::ifstream file(filePath, std::ios::binary);
        uint32_t magic;
        file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        
        if (magic != 0x46546C67) { // 'glTF'
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Invalid GLB magic number");
            return false;
        }
    }
    
    return true;
}
```

#### "No animations found in GLTF document"
**Possible Causes:**
- File contains no animations
- Animations are in separate files
- Export settings excluded animations

**Solutions:**
```cpp
// Check for animation availability
void CheckAnimationAvailability(const std::wstring& filePath) {
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Checking animation availability in: %ls", filePath.c_str());
    
    // Try to manually inspect the JSON for animations
    if (filePath.ends_with(L".gltf")) {
        std::ifstream file(filePath);
        json doc;
        file >> doc;
        
        if (doc.contains("animations")) {
            int animCount = static_cast<int>(doc["animations"].size());
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Found %d animations in JSON", animCount);
        } else {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"No 'animations' section found in GLTF JSON");
        }
        
        // Check for nodes that might be animated
        if (doc.contains("nodes")) {
            int animatedNodes = 0;
            for (const auto& node : doc["nodes"]) {
                if (node.contains("translation") || node.contains("rotation") || node.contains("scale")) {
                    animatedNodes++;
                }
            }
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Found %d potentially animated nodes", animatedNodes);
        }
    }
}
```

#### "Animation not playing"
**Possible Causes:**
- Invalid parent model ID
- Animation index out of range
- Animation instance not created

**Solutions:**
```cpp
// Comprehensive animation troubleshooting
bool TroubleshootAnimation(int parentModelID, int animationIndex) {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Troubleshooting animation for parent ID %d, animation %d", 
                        parentModelID, animationIndex);
    
    // Check 1: Validate parent model ID
    bool parentExists = false;
    for (int i = 0; i < MAX_SCENE_MODELS; ++i) {
        if (sceneManager.scene_models[i].m_isLoaded) {
            const ModelInfo& info = sceneManager.scene_models[i].m_modelInfo;
            if (info.ID == parentModelID && info.iParentModelID == -1) {
                parentExists = true;
                debug.logDebugMessage(LogLevel::LOG_INFO, L"✓ Parent model found: %ls", info.name.c_str());
                break;
            }
        }
    }
    
    if (!parentExists) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"✗ Parent model ID %d not found", parentModelID);
        return false;
    }
    
    // Check 2: Validate animation index
    int animationCount = gltfAnimator.GetAnimationCount();
    if (animationIndex < 0 || animationIndex >= animationCount) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"✗ Animation index %d out of range (0-%d)", 
                            animationIndex, animationCount - 1);
        return false;
    }
    debug.logDebugMessage(LogLevel::LOG_INFO, L"✓ Animation index valid (%d/%d)", animationIndex, animationCount);
    
    // Check 3: Animation instance
    AnimationInstance* instance = gltfAnimator.GetAnimationInstance(parentModelID);
    if (!instance) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"⚠ No animation instance found, creating one...");
        bool created = gltfAnimator.CreateAnimationInstance(animationIndex, parentModelID);
        if (!created) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"✗ Failed to create animation instance");
            return false;
        }
        debug.logLevelMessage(LogLevel::LOG_INFO, L"✓ Animation instance created");
    } else {
        debug.logDebugMessage(LogLevel::LOG_INFO, L"✓ Animation instance exists (Playing: %s, Speed: %.2f)", 
                            instance->isPlaying ? L"Yes" : L"No", instance->playbackSpeed);
    }
    
    // Check 4: Try starting animation
    bool started = gltfAnimator.StartAnimation(parentModelID, animationIndex);
    if (started) {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"✓ Animation started successfully");
        return true;
    } else {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"✗ Failed to start animation");
        return false;
    }
}
```

### Performance Issues

#### High CPU Usage
**Symptoms:** Game runs slowly, high CPU usage in profiler
**Solutions:**
```cpp
// Optimize animation updates
class PerformanceOptimizer {
public:
    void OptimizeAnimationUpdates() {
        // Reduce update frequency for distant objects
        static float distanceCheckTimer = 0.0f;
        distanceCheckTimer += deltaTime;
        
        if (distanceCheckTimer >= 1.0f) { // Check every second
            XMFLOAT3 cameraPos = GetCameraPosition();
            
            for (int i = 0; i < MAX_SCENE_MODELS; ++i) {
                if (sceneManager.scene_models[i].m_isLoaded) {
                    const ModelInfo& info = sceneManager.scene_models[i].m_modelInfo;
                    if (info.iParentModelID == -1) {
                        float distance = CalculateDistance(cameraPos, info.position);
                        
                        if (distance > 50.0f) {
                            // Very distant - update at 10 FPS
                            SetAnimationUpdateRate(info.ID, 0.1f);
                        } else if (distance > 20.0f) {
                            // Distant - update at 30 FPS
                            SetAnimationUpdateRate(info.ID, 0.033f);
                        } else {
                            // Close - update at full rate
                            SetAnimationUpdateRate(info.ID, 0.016f);
                        }
                    }
                }
            }
            
            distanceCheckTimer = 0.0f;
        }
    }
    
private:
    void SetAnimationUpdateRate(int parentModelID, float updateInterval) {
        // Implementation would require extending GLTFAnimator to support variable update rates
        // For now, this is a conceptual example
    }
};
```

#### Memory Usage Growing
**Symptoms:** Memory usage increases over time, eventual crashes
**Solutions:**
```cpp
// Monitor and clean up resources
void MonitorMemoryUsage() {
    static int frameCount = 0;
    frameCount++;
    
    // Check every 60 frames (approximately 1 second at 60 FPS)
    if (frameCount % 60 == 0) {
        // Count loaded models
        int loadedModels = 0;
        int animatedModels = 0;
        
        for (int i = 0; i < MAX_SCENE_MODELS; ++i) {
            if (sceneManager.scene_models[i].m_isLoaded) {
                loadedModels++;
                
                const ModelInfo& info = sceneManager.scene_models[i].m_modelInfo;
                if (info.iParentModelID == -1 && gltfAnimator.IsAnimationPlaying(info.ID)) {
                    animatedModels++;
                }
            }
        }
        
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"Memory Monitor: %d models loaded, %d actively animated", 
            loadedModels, animatedModels);
        
        // Warn if too many models
        if (loadedModels > MAX_SCENE_MODELS * 0.8f) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, 
                L"High model count detected - consider scene optimization");
        }
    }
}
```

### File Format Issues

#### GLB Files Not Loading
**Check:** File format validation
```cpp
bool ValidateGLBFormat(const std::wstring& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;
    
    // Read GLB header
    struct GLBHeader {
        uint32_t magic;      // Should be 0x46546C67 ('glTF')
        uint32_t version;    // Should be 2
        uint32_t length;     // Total file length
    };
    
    GLBHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    
    if (header.magic != 0x46546C67) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid GLB magic: 0x%08X", header.magic);
        return false;
    }
    
    if (header.version != 2) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Unsupported GLB version: %d", header.version);
        return false;
    }
    
    auto fileSize = std::filesystem::file_size(filePath);
    if (header.length != fileSize) {
        debug.logDebugMessage(LogLevel::LOG_WARNING, 
            L"GLB header length (%d) doesn't match file size (%lld)", header.length, fileSize);
    }
    
    debug.logLevelMessage(LogLevel::LOG_INFO, L"GLB format validation passed");
    return true;
}
```

This comprehensive guide covers all aspects of using the SceneManager and GLTFAnimator classes, from basic setup to advanced animation techniques. The examples provided should give you a solid foundation for implementing 3D scene loading and animation in your DirectX 11 applications.
    bool Initialize() {
        // Initialize renderer
        renderer = std::make_shared<DX11Renderer>();
        if (!renderer->Initialize()) {
            return false;
        }
        
        // Initialize scene manager
        if (!sceneManager.Initialize(renderer)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to initialize SceneManager");
            return false;
        }
        
        // Load initial scene
        return LoadMainScene();
    }
    
    bool LoadMainScene() {
        // Clean up any existing scene
        sceneManager.CleanUp();
        
        // Load the main game scene
        std::wstring scenePath = L"assets/scenes/main_level.gltf";
        bool loaded = sceneManager.ParseGLTFScene(scenePath);
        
        if (loaded) {
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Main scene loaded successfully");
            
            // Set up camera to frame the scene
            sceneManager.AutoFrameSceneToCamera();
            
            return true;
        } else {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to load main scene");
            return false;
        }
    }
    
    void Update(float deltaTime) {
        // Update scene animations
        sceneManager.UpdateSceneAnimations(deltaTime);
        
        // Handle scene transitions
        if (sceneManager.bSceneSwitching) {
            sceneManager.InitiateScene();
        }
    }
    
    void Render() {
        // Render all loaded scene models
        for (int i = 0; i < MAX_SCENE_MODELS; ++i) {
            if (sceneManager.scene_models[i].m_isLoaded) {
                sceneManager.scene_models[i].Render(renderer->GetDeviceContext(), 0.016f);
            }
        }
    }
};
```

### Animated Scene Loading

Example of loading a scene with animations and controlling playback:

```cpp
class AnimatedSceneExample {
private:
    SceneManager sceneManager;
    int characterParentID;
    int environmentParentID;
    
public:
    bool LoadAnimatedScene() {
        // Load character with animations
        bool characterLoaded = sceneManager.ParseGLBScene(L"assets/character_animated.glb");
        if (!characterLoaded) {
            return false;
        }
        
        // Find the character's parent model ID
        characterParentID = FindMainCharacterParentID();
        
        // Load environment
        bool environmentLoaded = sceneManager.ParseGLTFScene(L"assets/environment.gltf");
        if (!environmentLoaded) {
            return false;
        }
        
        environmentParentID = FindEnvironmentParentID();
        
        // Set up character animations
        SetupCharacterAnimations();
        
        // Set up environment animations (if any)
        SetupEnvironmentAnimations();
        
        return true;
    }
    
private:
    int FindMainCharacterParentID() {
        // Look for parent models that might be the main character
        for (int i = 0; i < MAX_SCENE_MODELS; ++i) {
            if (sceneManager.scene_models[i].m_isLoaded) {
                const ModelInfo& info = sceneManager.scene_models[i].m_modelInfo;
                
                // Check if this is a parent model with a character-like name
                if (info.iParentModelID == -1) {
                    std::wstring name = info.name;
                    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                    
                    if (name.find(L"character") != std::wstring::npos ||
                        name.find(L"player") != std::wstring::npos ||
                        name.find(L"hero") != std::wstring::npos) {
                        return info.ID;
                    }
                }
            }
        }
        
        // If no named character found, return first parent model
        for (int i = 0; i < MAX_SCENE_MODELS; ++i) {
            if (sceneManager.scene_models[i].m_isLoaded) {
                const ModelInfo& info = sceneManager.scene_models[i].m_modelInfo;
                if (info.iParentModelID == -1) {
                    return info.ID;
                }
            }
        }
        
        return -1; // No parent models found
    }
    
    void SetupCharacterAnimations() {
        if (characterParentID == -1) return;
        
        // Check available animations
        int animationCount = gltfAnimator.GetAnimationCount();
        if (animationCount > 0) {
            // Start idle animation (usually the first one)
            gltfAnimator.StartAnimation(characterParentID, 0);
            gltfAnimator.SetAnimationLooping(characterParentID, true);
            gltfAnimator.SetAnimationSpeed(characterParentID, 1.0f);
            
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Character animations set up for parent ID %d", characterParentID);
        }
    }
    
    void SetupEnvironmentAnimations() {
        if (environmentParentID == -1) return;
        
        // Environment might have ambient animations (flags, water, etc.)
        int animationCount = gltfAnimator.GetAnimationCount();
        for (int i = 0; i < animationCount; ++i) {
            const GLTFAnimation* animation = gltfAnimator.GetAnimation(i);
            if (animation) {
                std::wstring name = animation->name;
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                
                // Look for environment-specific animations
                if (name.find(L"ambient") != std::wstring::npos ||
                    name.find(L"environment") != std::wstring::npos ||
                    name.find(L"background") != std::wstring::npos) {
                    
                    gltfAnimator.StartAnimation(environmentParentID, i);
                    gltfAnimator.SetAnimationLooping(environmentParentID, true);
                    gltfAnimator.SetAnimationSpeed(environmentParentID, 0.8f); // Slower for ambience
                    break;
                }
            }
        }
    }
};
```

### Multiple Animation Control

Example showing how to control multiple animations simultaneously:

```cpp
class MultiAnimationController {
private:
    struct AnimatedObject {
        int parentModelID;
        int currentAnimation;
        float animationTimer;
        bool isActive;
    };
    
    std::vector<AnimatedObject> animatedObjects;
    
public:
    void RegisterAnimatedObject(int parentModelID) {
        AnimatedObject obj;
        obj.parentModelID = parentModelID;
        obj.currentAnimation = 0;
        obj.animationTimer = 0.0f;
        obj.isActive = true;
        
        animatedObjects.push_back(obj);
        
        // Start default animation
        if (gltfAnimator.GetAnimationCount() > 0) {
            gltfAnimator.StartAnimation(parentModelID, 0);
            gltfAnimator.SetAnimationLooping(parentModelID, true);
        }
    }
    
    void PlayAnimationSequence(int parentModelID, const std::vector<int>& animationSequence) {
        // Find the object
        for (auto& obj : animatedObjects) {
            if (obj.parentModelID == parentModelID) {
                obj.animationSequence = animationSequence;
                obj.currentSequenceIndex = 0;
                
                if (!animationSequence.empty()) {
                    gltfAnimator.StopAnimation(parentModelID);
                    gltfAnimator.StartAnimation(parentModelID, animationSequence[0]);
                    gltfAnimator.SetAnimationLooping(parentModelID, false); // Don't loop sequences
                }
                break;
            }
        }
    }
    
    void Update(float deltaTime) {
        for (auto& obj : animatedObjects) {
            if (!obj.isActive) continue;
            
            obj.animationTimer += deltaTime;
            
            // Check if current animation finished (for sequences)
            if (!gltfAnimator.IsAnimationPlaying(obj.parentModelID) && !obj.animationSequence.empty()) {
                // Move to next animation in sequence
                obj.currentSequenceIndex++;
                
                if (obj.currentSequenceIndex < obj.animationSequence.size()) {
                    int nextAnimation = obj.animationSequence[obj.currentSequenceIndex];
                    gltfAnimator.StartAnimation(obj.parentModelID, nextAnimation);
                } else {
                    // Sequence finished, return to idle
                    obj.animationSequence.clear();
                    gltfAnimator.StartAnimation(obj.parentModelID, 0); // Idle animation
                    gltfAnimator.SetAnimationLooping(obj.parentModelID, true);
                }
            }
        }
    }
    
    void SetObjectAnimationSpeed(int parentModelID, float speed) {
        gltfAnimator.SetAnimationSpeed(parentModelID, speed);
    }
    
    void PauseAllAnimations() {
        for (const auto& obj : animatedObjects) {
            if (obj.isActive) {
                gltfAnimator.PauseAnimation(obj.parentModelID);
            }
        }
    }
    
    void ResumeAllAnimations() {
        for (const auto& obj : animatedObjects) {
            if (obj.isActive) {
                gltfAnimator.ResumeAnimation(obj.parentModelID);
            }
        }
    }
};
```

### Game Loop Integration

Complete example of integrating both systems into a game loop:

```cpp
class Game {
private:
    SceneManager sceneManager;
    MultiAnimationController animationController;
    std::shared_ptr<DX11Renderer> renderer;
    
    float totalTime;
    bool gameInitialized;
    
public:
    bool Initialize() {
        // Initialize renderer
        renderer = std::make_shared<DX11Renderer>();
        if (!renderer->Initialize()) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to initialize renderer");
            return false;
        }
        
        // Initialize scene manager
        if (!sceneManager.Initialize(renderer)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to initialize SceneManager");
            return false;
        }
        
        totalTime = 0.0f;
        gameInitialized = true;
        
        return LoadGameContent();
    }
    
    bool LoadGameContent() {
        // Load main game scene
        bool sceneLoaded = sceneManager.ParseGLBScene(L"assets/main_game.glb");
        if (!sceneLoaded) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to load main game scene");
            return false;
        }
        
        // Register all parent models for animation
        RegisterAnimatedObjects();
        
        // Set up initial game state
        SetupInitialAnimations();
        
        return true;
    }
    
    void Update(float deltaTime) {
        totalTime += deltaTime;
        
        if (!gameInitialized) return;
        
        // Update scene animations
        sceneManager.UpdateSceneAnimations(deltaTime);
        
        // Update animation controller
        animationController.Update(deltaTime);
        
        // Handle scene transitions
        if (sceneManager.bSceneSwitching) {
            HandleSceneTransition();
        }
        
        // Game-specific animation triggers
        HandleGameAnimationTriggers(deltaTime);
    }
    
    void Render() {
        if (!renderer || !gameInitialized) return;
        
        // Clear the frame
        renderer->ClearFrame();
        
        // Render all scene models
        for (int i = 0; i < MAX_SCENE_MODELS; ++i) {
            if (sceneManager.scene_models[i].m_isLoaded) {
                sceneManager.scene_models[i].Render(renderer->GetDeviceContext(), 0.016f);
            }
        }
        
        // Present the frame
        renderer->PresentFrame();
    }
    
private:
    void RegisterAnimatedObjects() {
        // Find all parent models and register them for animation
        for (int i = 0; i < MAX_SCENE_MODELS; ++i) {
            if (sceneManager.scene_models[i].m_isLoaded) {
                const ModelInfo& info = sceneManager.scene_models[i].m_modelInfo;
                
                if (info.iParentModelID == -1) { // This is a parent model
                    animationController.RegisterAnimatedObject(info.ID);
                    
                    debug.logDebugMessage(LogLevel::LOG_INFO, 
                        L"Registered animated object: %ls (ID: %d)", 
                        info.name.c_str(), info.ID);
                }
            }
        }
    }
    
    void SetupInitialAnimations() {
        // Set up different animation speeds for different objects
        for (int i = 0; i < MAX_SCENE_MODELS; ++i) {
            if (sceneManager.scene_models[i].m_isLoaded) {
                const ModelInfo& info = sceneManager.scene_models[i].m_modelInfo;
                
                if (info.iParentModelID == -1) {
                    // Analyze object name to determine animation settings
                    std::wstring name = info.name;
                    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                    
                    if (name.find(L"character") != std::wstring::npos) {
                        // Character gets normal speed animations
                        animationController.SetObjectAnimationSpeed(info.ID, 1.0f);
                    } else if (name.find(L"environment") != std::wstring::npos) {
                        // Environment gets slower ambient animations
                        animationController.SetObjectAnimationSpeed(info.ID, 0.5f);
                    } else if (name.find(L"prop") != std::wstring::npos) {
                        // Props get medium speed animations
                        animationController.SetObjectAnimationSpeed(info.ID, 0.8f);
                    }
                }
            }
        }
    }
    
    void HandleSceneTransition() {
        sceneManager.CleanUp();
        
        switch (sceneManager.GetGotoScene()) {
            case SCENE_GAMEPLAY:
                LoadGameplayScene();
                break;
            case SCENE_CREDITS:
                LoadCreditsScene();
                break;
            default:
                break;
        }
        
        sceneManager.InitiateScene();
    }
    
    void HandleGameAnimationTriggers(float deltaTime) {
        // Example: Change animations based on game events
        static float eventTimer = 0.0f;
        eventTimer += deltaTime;
        
        // Every 10 seconds, trigger special animations
        if (eventTimer >= 10.0f) {
            eventTimer = 0.0f;
            TriggerSpecialAnimations();
        }
    }
    
    void TriggerSpecialAnimations() {
        // Find character objects and play special animation sequence
        for (int i = 0; i < MAX_SCENE_MODELS; ++i) {
            if (sceneManager.scene_models[i].m_isLoaded) {
                const ModelInfo& info = sceneManager.scene_models[i].m_modelInfo;
                
                if (info.iParentModelID == -1) {
                    std::wstring name = info.name;
                    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                    
                    if (name.find(L"character") != std::wstring::npos) {
                        // Play animation sequence: wave -> bow -> return to idle
                        std::vector<int> sequence = {2, 3, 0}; // Animation indices
                        animationController.PlayAnimationSequence(info.ID, sequence);
                    }
                }
            }
        }
    }
    
    void LoadGameplayScene() {
        sceneManager.ParseGLBScene(L"assets/gameplay_level.glb");
        RegisterAnimatedObjects();
        SetupInitialAnimations();
    }
    
    void LoadCreditsScene() {
        sceneManager.ParseGLTFScene(L"assets/credits_scene.gltf");
        RegisterAnimatedObjects();
        
        // Credits scene might have special slow animations
        for (int i = 0; i < MAX_SCENE_MODELS; ++i) {
            if (sceneManager.scene_models[i].m_isLoaded) {
                const ModelInfo& info = sceneManager.scene_models[i].m_modelInfo;
                if (info.iParentModelID == -1) {
                    animationController.SetObjectAnimationSpeed(info.ID, 0.3f); // Very slow
                }
            }
        }
    }
};
```

## Advanced Usage

### Custom Animation Timing

Advanced timing control for complex animation sequences:

```cpp
class AdvancedAnimationTiming {
private:
    struct TimedAnimationEvent {
        int parentModelID;
        int animationIndex;
        float triggerTime;
        bool triggered;
        float duration;
    };
    
    std::vector<TimedAnimationEvent> animationEvents;
    float masterTimer;
    
public:
    void AddTimedAnimation(int parentModelID, int animationIndex, float triggerTime, float duration = -1.0f) {
        TimedAnimationEvent event;
        event.parentModelID = parentModelID;
        event.animationIndex = animationIndex;
        event.triggerTime = triggerTime;
        event.triggered = false;
        event.duration = duration; // -1 means use animation's natural duration
        
        animationEvents.push_back(event);
    }
    
    void Update(float deltaTime) {
        masterTimer += deltaTime;
        
        for (auto& event : animationEvents) {
            if (!event.triggered && masterTimer >= event.triggerTime) {
                // Trigger this animation
                gltfAnimator.StartAnimation(event.parentModelID, event.animationIndex);
                
                if (event.duration > 0.0f) {
                    // Set custom duration by adjusting speed
                    float naturalDuration = gltfAnimator.GetAnimationDuration(event.animationIndex);
                    if (naturalDuration > 0.0f) {
                        float speedMultiplier = naturalDuration / event.duration;
                        gltfAnimator.SetAnimationSpeed(event.parentModelID, speedMultiplier);
                    }
                }
                
                event.triggered = true;
                
                debug.logDebugMessage(LogLevel::LOG_INFO, 
                    L"Triggered timed animation: Parent=%d, Animation=%d at time=%.2f", 
                    event.parentModelID, event.animationIndex, masterTimer);
            }
        }
    }
    
    void Reset() {
        masterTimer = 0.0f;
        for (auto& event : animationEvents) {
            event.triggered = false;
        }
    }
    
    void CreateCinematicSequence() {
        // Example: Create a cinematic sequence with precise timing
        AddTimedAnimation(1, 0, 0.0f);    // Character starts walking at 0s
        AddTimedAnimation(2, 1, 2.5f);    // Door opens at 2.5s
        AddTimedAnimation(1, 2, 3.0f);    // Character waves at 3s
        AddTimedAnimation(3, 0, 4.0f);    // Camera movement at 4s
        AddTimedAnimation(1, 3, 6.0f);    // Character sits down at 6s
    }
};
```

### Animation Blending

Basic animation blending for smooth transitions:

```cpp
class AnimationBlender {
private:
    struct BlendInfo {
        int parentModelID;
        int fromAnimation;
        int toAnimation;
        float blendTime;
        float currentBlendTime;
        bool isBlending;
    };
    
    std::vector<BlendInfo> activeBlends;
    
public:
    void BlendToAnimation(int parentModelID, int targetAnimation, float blendDuration) {
        // Stop any existing blend for this object
        StopBlend(parentModelID);
        
        // Find current animation
        AnimationInstance* instance = gltfAnimator.GetAnimationInstance(parentModelID);
        if (!instance) return;
        
        BlendInfo blend;
        blend.parentModelID = parentModelID;
        blend.fromAnimation = instance->animationIndex;
        blend.toAnimation = targetAnimation;
        blend.blendTime = blendDuration;
        blend.currentBlendTime = 0.0f;
        blend.isBlending = true;
        
        activeBlends.push_back(blend);
        
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"Starting blend: Parent=%d, From=%d, To=%d, Duration=%.2f", 
            parentModelID, blend.fromAnimation, blend.toAnimation, blendDuration);
    }
    
    void Update(float deltaTime) {
        for (auto it = activeBlends.begin(); it != activeBlends.end();) {
            BlendInfo& blend = *it;
            
            if (!blend.isBlending) {
                it = activeBlends.erase(it);
                continue;
            }
            
            blend.currentBlendTime += deltaTime;
            float blendFactor = blend.currentBlendTime / blend.blendTime;
            
            if (blendFactor >= 1.0f) {
                // Blend complete - switch to target animation
                gltfAnimator.StartAnimation(blend.parentModelID, blend.toAnimation);
                blend.isBlending = false;
                
                debug.logDebugMessage(LogLevel::LOG_INFO, 
                    L"Blend completed: Parent=%d, Now playing animation=%d", 
                    blend.parentModelID, blend.toAnimation);
                
                it = activeBlends.erase(it);
            } else {
                // Continue blending (simplified - real blending would interpolate transforms)
                float fromWeight = 1.0f - blendFactor;
                float toWeight = blendFactor;
                
                // For demonstration, adjust animation speed to simulate blending
                float blendedSpeed = fromWeight * 1.0f + toWeight * 1.0f;
                gltfAnimator.SetAnimationSpeed(blend.parentModelID, blendedSpeed);
                
                ++it;
            }
        }
    }
    
private:
    void StopBlend(int parentModelID) {
        auto it = std::remove_if(activeBlends.begin(), activeBlends.end(),
            [parentModelID](const BlendInfo& blend) {
                return blend.parentModelID == parentModelID;
            });
        activeBlends.erase(it, activeBlends.end());
    }
};
```

### Performance Optimization

Optimize animation performance for complex scenes:

```cpp
class AnimationOptimizer {
private:
    float updateInterval;
    float timeSinceLastUpdate;
    std::vector<int> lowPriorityObjects;
    std::vector<int> highPriorityObjects;
    
public:
    AnimationOptimizer() : updateInterval(0.033f), timeSinceLastUpdate(0.0f) {} // 30 FPS for low priority
    
    void SetObjectPriority(int parentModelID, bool highPriority) {
        if (highPriority) {
            // Remove from low priority if present
            auto it = std::find(lowPriorityObjects.begin(), lowPriorityObjects.end(), parentModelID);
            if (it != lowPriorityObjects.end()) {
                lowPriorityObjects.erase(it);
            }
            
            // Add to high priority
            if (std::find(highPriorityObjects.begin(), highPriorityObjects.end(), parentModelID) == highPriorityObjects.end()) {
                highPriorityObjects.push_back(parentModelID);
            }
        } else {
            // Remove from high priority if present
            auto it = std::find(highPriorityObjects.begin(), highPriorityObjects.end(), parentModelID);
            if (it != highPriorityObjects.end()) {
                highPriorityObjects.erase(it);
            }
            
            // Add to low priority
            if (std::find(lowPriorityObjects.begin(), lowPriorityObjects.end(), parentModelID) == lowPriorityObjects.end()) {
                lowPriorityObjects.push_back(parentModelID);
            }
        }
    }
    
    void OptimizedUpdate(float deltaTime) {
        timeSinceLastUpdate += deltaTime;
        
        // Always update high priority objects
        for (int parentID : highPriorityObjects) {
            UpdateSingleObject(parentID, deltaTime);
        }
        
        // Update low priority objects at reduced frequency
        if (timeSinceLastUpdate >= updateInterval) {
            for (int parentID : lowPriorityObjects) {
                UpdateSingleObject(parentID, timeSinceLastUpdate);
            }
            timeSinceLastUpdate = 0.0f;
        }
    }
    
    void SetDistanceBasedPriority(const XMFLOAT3& cameraPosition, float highPriorityDistance) {
        // Automatically set priority based on distance from camera
        for (int i = 0; i < MAX_SCENE_MODELS; ++i) {
            if (sceneManager.scene_models[i].m_isLoaded) {
                const ModelInfo& info = sceneManager.scene_models[i].m_modelInfo;
                
                if (info.iParentModelID == -1) { // Parent models only
                    // Calculate distance from camera
                    XMFLOAT3 modelPos = info.position;
                    float dx = modelPos.x - cameraPosition.x;
                    float dy = modelPos.y - cameraPosition.y;
                    float dz = modelPos.z - cameraPosition.z;
                    float distance = sqrt(dx*dx + dy*dy + dz*dz);
                    
                    SetObjectPriority(info.ID, distance <= highPriorityDistance);
                }
            }
        }
    }
    
private:
    void UpdateSingleObject(int parentModelID, float deltaTime) {
        // Custom update logic for individual objects
        if (gltfAnimator.IsAnimationPlaying(parentModelID)) {
            // Object is animating, ensure it gets proper updates
            // This could include custom interpolation or effect triggers
        }
    }
};
```

## Debugging and Troubleshooting

### Debug Output

Enable comprehensive debug output for troubleshooting:

```cpp
// In Debug.h, enable these flags for detailed output:
#define _DEBUG_SCENEMANAGER_     // Scene loading and management debug
#define _DEBUG_GLTFANIMATOR_     // Animation system debug

// Example debug session
void DebugAnimationSystem() {
    // Print all loaded animations
    gltfAnimator.DebugPrintAnimationInfo();
    
    // Print scene model hierarchy
    debug.logLevelMessage(LogLevel::LOG_INFO, L"=== SCENE MODEL HIERARCHY ===");
    for (int i = 0; i < MAX_SCENE_MODELS; ++i) {
        if (sceneManager.scene_models[i].m_isLoaded) {
            const ModelInfo& info = sceneManager.scene_models[i].m_modelInfo;
            
            if (info.iParentModelID == -1) {
                debug.logDebugMessage(LogLevel::LOG_INFO, L"PARENT: ID=%d, Name=%ls", 
                                    info.ID, info.name.c_str());
                
                // Find and list children
                for (int j = 0; j < MAX_SCENE_MODELS; ++j) {
                    if (sceneManager.scene_models[j].m_isLoaded) {
                        const ModelInfo& childInfo = sceneManager.scene_models[j].m_modelInfo;
                        if (childInfo.iParentModelID == info.ID) {
                            debug.logDebugMessage(LogLevel::LOG_INFO, L"  CHILD: ID=%d, Name=%ls", 
                                                childInfo.ID, childInfo.name.c_str());
                        }
                    }
                }
            }
        }
    }
    
    // Print active animation instances
    debug.logLevelMessage(LogLevel::LOG_INFO, L"=== ACTIVE ANIMATIONS ===");
    for (int i = 0; i < MAX_SCENE_MODELS; ++i) {
        if (sceneManager.scene_models[i].m_isLoaded) {
            const ModelInfo& info = sceneManager.scene_models[i].m_modelInfo;
            
            if (info.iParentModelID == -1) {
                bool isPlaying = gltfAnimator.IsAnimationPlaying(info.ID);
                if (isPlaying) {
                    float currentTime = gltfAnimator.GetAnimationTime(info.ID);
                    AnimationInstance* instance = gltfAnimator.GetAnimationInstance(info.ID);
                    
                    if (instance) {
                        debug.logDebugMessage(LogLevel::LOG_INFO, 
                            L"PLAYING: Parent=%d, Animation=%d, Time=%.2f, Speed=%.2f", 
                            info.ID, instance->animationIndex, currentTime, instance->playbackSpeed);
                    }
                }
            }
        }
    }
}
```

### Common Issues

#### Issue 1: Animations Not Playing

```cpp
bool DiagnoseAnimationIssues(int parentModelID) {
    // Check if animations were loaded
    if (!sceneManager.bAnimationsLoaded) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"No animations loaded from scene file");
        return false;
    }
    
    // Check if any animations exist
    int animationCount = gltfAnimator.GetAnimationCount();
    if (animationCount == 0) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"No animations available in animator");
        return false;
    }
    
    // Check if parent model ID exists
    bool parentExists = false;
    for (int i = 0; i < MAX_SCENE_MODELS; ++i) {
        if (sceneManager.scene_models[i].m_isLoaded) {
            const ModelInfo& info = sceneManager.scene_models[i].m_modelInfo;
            if (info.ID == parentModelID && info.iParentModelID == -1) {
                parentExists = true;
                break;
            }
        }
    }
    
    if (!parentExists) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Parent model ID %d not found", parentModelID);
        return false;
    }
    
    // Check animation instance
    AnimationInstance* instance = gltfAnimator.GetAnimationInstance(parentModelID);
    if (!instance) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"No animation instance for parent ID. Creating one...");
        bool created = gltfAnimator.CreateAnimationInstance(0, parentModelID);
        if (!created) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to create animation instance");
            return false;
        }
    }
    
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Animation system appears to be working correctly");
    return true;
}
```

#### Issue 2: Performance Problems

```cpp
void DiagnosePerformanceIssues() {
    // Check for too many active animations
    int activeAnimations = 0;
    for (int i = 0; i < MAX_SCENE_MODELS; ++i) {
        if (sceneManager.scene_models[i].m_isLoaded) {
            const ModelInfo& info = sceneManager.scene_models[i].m_modelInfo;
            if (info.iParentModelID == -1 && gltfAnimator.IsAnimationPlaying(info.ID)) {
                activeAnimations++;
            }
        }
    }
    
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Active animations: %d", activeAnimations);
    
    if (activeAnimations > 10) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, 
            L"High number of active animations may impact performance");
    }
    
    // Check for complex animations
    for (int i = 0; i < gltfAnimator.GetAnimationCount(); ++i) {
        const GLTFAnimation* animation = gltfAnimator.GetAnimation(i);
        if (animation) {
            int totalKeyframes = 0;
            for (const auto& sampler : animation->samplers) {
                totalKeyframes += static_cast<int>(sampler.keyframes.size());
            }
            
            if (totalKeyframes > 1000) {
                debug.logDebugMessage(LogLevel::LOG_WARNING, 
                    L"Animation %d has many keyframes (%d) - consider optimization", 
                    i, totalKeyframes);
            }
        }
    }
}
```

### Performance Monitoring

```cpp
class AnimationPerformanceMonitor {
private:
    float updateTimeAccumulator;
    int updateCount;
    float maxUpdateTime;
    float avgUpdateTime;
    
public:
    void BeginFrame() {
        frameStartTime = GetCurrentTime();
    }
    
    void EndFrame() {
        float frameTime = GetCurrentTime() - frameStartTime;
        updateTimeAccumulator += frameTime;
        updateCount++;
        
        if (frameTime > maxUpdateTime) {
            maxUpdateTime = frameTime;
        }
        
        // Calculate average every 60 frames
        if (updateCount >= 60) {
            avgUpdateTime = updateTimeAccumulator / updateCount;
            
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Animation Performance: Avg=%.3fms, Max=%.3fms", 
                avgUpdateTime * 1000.0f, maxUpdateTime * 1000.0f);
            
            // Reset counters
            updateTimeAccumulator = 0.0f;
            updateCount = 0;
            maxUpdateTime = 0.0f;
        }
    }
    
private:
    float frameStartTime;
    
    float GetCurrentTime() {
        // Return time in seconds (implementation depends on your timing system)
        return static_cast<float>(timeGetTime()) / 1000.0f;
    }
};
```

## Best Practices

### Loading and Initialization

1. **Always check return values** from loading functions
2. **Initialize SceneManager before loading scenes**
3. **Load animations after scene geometry**
4. **Validate parent model IDs before starting animations**

```cpp
// Good practice example
bool LoadSceneWithValidation(const std::wstring& scenePath) {
    // Step 1: Validate file exists
    if (!std::filesystem::exists(scenePath)) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Scene file not found: %ls", scenePath.c_str());
        return false;
    }
    
    // Step 2: Clean up existing scene
    sceneManager.CleanUp();
    gltfAnimator.ClearAllAnimations();
    
    // Step 3: Load scene
    bool loaded = false;
    if (scenePath.ends_with(L".glb")) {
        loaded = sceneManager.ParseGLBScene(scenePath);
    } else if (scenePath.ends_with(L".gltf")) {
        loaded = sceneManager.ParseGLTFScene(scenePath);
    } else {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Unsupported file format");
        return false;
    }
    
    if (!loaded) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to load scene");
        return false;
    }
    
    // Step 4: Validate loaded content
    bool hasModels = false;
    for (int i = 0; i < MAX_SCENE_MODELS; ++i) {
        if (sceneManager.scene_models[i].m_isLoaded) {
            hasModels = true;
            break;
        }
    }
    
    if (!hasModels) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Scene loaded but contains no models");
    }
    
    // Step 5: Log animation availability
    if (sceneManager.bAnimationsLoaded) {
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Scene loaded with %d animations", 
                            gltfAnimator.GetAnimationCount());
    }
    
    return true;
}
```

### Animation Management

1. **Use meaningful parent model IDs**
2. **Always check if animations exist before starting them**
3. **Set appropriate looping and speed settings**
4. **Clean up animation instances when switching scenes**

```cpp
// Good practice for animation setup
void SetupCharacterAnimations(int characterParentID) {
    // Validate parent exists
    if (!IsValidParentModel(characterParentID)) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid character parent ID: %d", characterParentID);
        return;
    }
    
    // Check if animations are available
    int animationCount = gltfAnimator.GetAnimationCount();
    if (animationCount == 0) {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"No animations available for character");
        return;
    }
    
    // Create animation instance with validation
    bool created = gltfAnimator.CreateAnimationInstance(0, characterParentID);
    if (!created) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to create animation instance");
        return;
    }
    
    // Set up appropriate settings
    gltfAnimator.SetAnimationLooping(characterParentID, true);
    gltfAnimator.SetAnimationSpeed(characterParentID, 1.0f);
    
    // Start idle animation
    bool started = gltfAnimator.StartAnimation(characterParentID, 0);
    if (started) {
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Character animations initialized successfully");
    }
}

bool IsValidParentModel(int parentModelID) {
    for (int i = 0; i < MAX_SCENE_MODELS; ++i) {
        if (sceneManager.scene_models[i].m_isLoaded) {
            const ModelInfo& info = sceneManager.scene_models[i].m_modelInfo;
            if (info.ID == parentModelID && info.iParentModelID == -1) {
                return true;
            }
        }
    }
    return false;
}
```

### Memory Management

1. **Call CleanUp() when switching scenes**
2. **Remove animation instances for deleted objects**
3. **Monitor memory usage with large scenes**

```cpp
// Proper cleanup example
void SwitchToNewScene(const std::wstring& newScenePath) {
    // Step 1: Stop all animations
    for (int i = 0; i < MAX_SCENE_MODELS; ++i) {
        if (sceneManager.scene_models[i].m_isLoaded) {
            const ModelInfo& info = sceneManager.scene_models[i].m_modelInfo;
            if (info.iParentModelID == -1) {
                gltfAnimator.StopAnimation(info.ID);
                gltfAnimator.RemoveAnimationInstance(info.ID);
            }
        }
    }
    
    // Step 2: Clear all animation data
    gltfAnimator.ClearAllAnimations();
    
    // Step 3: Clean up scene
    sceneManager.CleanUp();
    
    // Step 4: Load new scene
    LoadSceneWithValidation(newScenePath);
}
```

## API Reference

### SceneManager Methods

#### Core Methods

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `Initialize()` | `std::shared_ptr<Renderer> renderer` | `bool` | Initialize the scene manager with renderer |
| `CleanUp()` | None | `void` | Clean up all scene resources |
| `ParseGLTFScene()` | `const std::wstring& gltfFile` | `bool` | Load .gltf scene file |
| `ParseGLBScene()` | `const std::wstring& glbFile` | `bool` | Load .glb scene file |
| `UpdateSceneAnimations()` | `float deltaTime` | `void` | Update all scene animations |

#### Scene Management

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `SetGotoScene()` | `SceneType gotoScene` | `void` | Set next scene to transition to |
| `GetGotoScene()` | None | `SceneType` | Get the next scene type |
| `InitiateScene()` | None | `void` | Complete scene transition |
| `SaveSceneState()` | `const std::wstring& path` | `bool` | Save current scene state |
| `LoadSceneState()` | `const std::wstring& path` | `bool` | Load saved scene state |

#### Query Methods

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `IsSketchfabScene()` | None | `bool` | Check if scene was exported from Sketchfab |
| `GetLastDetectedExporter()` | None | `const std::wstring&` | Get detected exporter name |
| `AutoFrameSceneToCamera()` | `float fovYRadians, float padding` | `void` | Frame camera to scene bounds |

### GLTFAnimator Methods

#### Animation Control

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `StartAnimation()` | `int parentModelID, int animationIndex` | `bool` | Start playing animation |
| `StopAnimation()` | `int parentModelID` | `bool` | Stop animation playback |
| `PauseAnimation()` | `int parentModelID` | `bool` | Pause animation |
| `ResumeAnimation()` | `int parentModelID` | `bool` | Resume paused animation |
| `CreateAnimationInstance()` | `int animationIndex, int parentModelID` | `bool` | Create new animation instance |
| `RemoveAnimationInstance()` | `int parentModelID` | `void` | Remove animation instance |

#### Animation Settings

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `SetAnimationSpeed()` | `int parentModelID, float speed` | `bool` | Set animation playback speed |
| `SetAnimationLooping()` | `int parentModelID, bool looping` | `bool` | Enable/disable animation looping |
| `SetAnimationTime()` | `int parentModelID, float time` | `bool` | Set current animation time |

#### Query Methods

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `GetAnimationCount()` | None | `int` | Get total number of loaded animations |
| `GetAnimation()` | `int index` | `const GLTFAnimation*` | Get animation by index |
| `GetAnimationInstance()` | `int parentModelID` | `AnimationInstance*` | Get animation instance |
| `GetAnimationTime()` | `int parentModelID` | `float` | Get current animation time |
| `GetAnimationDuration()` | `int animationIndex` | `float` | Get animation duration |
| `IsAnimationPlaying()` | `int parentModelID` | `bool` | Check if animation is playing |

#### System Methods

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `ParseAnimationsFromGLTF()` | `const json& doc, const std::vector<uint8_t>& binaryData` | `bool` | Parse animations from GLTF data |
| `UpdateAnimations()` | `float deltaTime, Model* sceneModels, int maxModels` | `void` | Update all animations |
| `ClearAllAnimations()` | None | `void` | Clear all animation data |
| `DebugPrintAnimationInfo()` | None | `void` | Print debug information |

