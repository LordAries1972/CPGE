# Camera Class - Complete Usage Guide

## Table of Contents

1. [Introduction](#introduction)
2. [Camera Initialization](#camera-initialization)
3. [Basic Camera Movement](#basic-camera-movement)
   - [3.1 Position-Based Movement](#31-position-based-movement)
   - [3.2 Direction-Based Movement](#32-direction-based-movement)
4. [Advanced Camera Navigation](#advanced-camera-navigation)
   - [4.1 Jump Animation System](#41-jump-animation-system)
   - [4.2 Rotation Functions](#42-rotation-functions)
   - [4.3 Continuous Rotation Around Target](#43-continuous-rotation-around-target)
5. [Camera History System](#camera-history-system)
   - [5.1 Jump History Navigation](#51-jump-history-navigation)
   - [5.2 History Management](#52-history-management)
6. [Camera Configuration](#camera-configuration)
   - [6.1 View and Projection Setup](#61-view-and-projection-setup)
   - [6.2 Target and Focus Management](#62-target-and-focus-management)
7. [Status and Monitoring](#status-and-monitoring)
   - [7.1 Animation Status Checking](#71-animation-status-checking)
   - [7.2 Progress Monitoring](#72-progress-monitoring)
8. [Best Practices](#best-practices)
   - [8.1 Performance Considerations](#81-performance-considerations)
   - [8.2 Thread Safety](#82-thread-safety)
   - [8.3 Error Handling](#83-error-handling)
9. [Complete Example Implementation](#complete-example-implementation)
10. [Troubleshooting](#troubleshooting)
11. [API Reference Quick Guide](#api-reference-quick-guide)

---

## Introduction

The Camera class is a comprehensive DirectX-based camera system designed for 3D applications and games. It provides smooth animations, intelligent movement tracking, rotation capabilities, and a robust history system for navigation. The class leverages the MathPrecalculation system for optimized mathematical operations and integrates with the engine's threading and debugging systems.

### Key Features

- **Smooth Jump Animation**: Bezier curve-based movement with customizable speeds
- **Intelligent Rotation**: Single-axis and multi-axis rotation around targets or free-look
- **Continuous Rotation**: Automated orbital movement around target points
- **History System**: Navigate backward through previous camera positions
- **Focus Management**: Maintain focus on targets during movement
- **Status Monitoring**: Real-time progress tracking and state information
- **Thread-Safe Operations**: Integrated with ThreadManager for safe multi-threaded use

---

## Camera Initialization

### Basic Setup

```cpp
#include "DXCamera.h"
#include "Configuration.h"

// Create camera instance
Camera camera;

// Initialize with default settings (800x600 window)
camera.SetupDefaultCamera(800.0f, 600.0f);

// Or initialize with custom window dimensions
float windowWidth = 1920.0f;
float windowHeight = 1080.0f;
camera.SetupDefaultCamera(windowWidth, windowHeight);
```

### Custom Initialization

```cpp
// Set custom camera position
camera.SetPosition(10.0f, 5.0f, -15.0f);

// Set target to look at
XMFLOAT3 target = {0.0f, 0.0f, 0.0f};
camera.SetTarget(target);

// Configure near and far planes
camera.SetNearFar(0.1f, 1000.0f);

// Update view matrix after setup
camera.UpdateViewMatrix();
```

### Integration with Rendering Loop

```cpp
void InitializeCamera()
{
    // Camera is automatically initialized in constructor
    camera.SetupDefaultCamera(windowWidth, windowHeight);
    
    #if defined(_DEBUG_CAMERA_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Camera initialized successfully");
    #endif
}

void UpdateLoop()
{
    // IMPORTANT: Call this every frame for animations
    camera.UpdateJumpAnimation();
    
    // Get matrices for rendering
    XMMATRIX viewMatrix = camera.GetViewMatrix();
    XMMATRIX projMatrix = camera.GetProjectionMatrix();
    
    // Use matrices in your rendering pipeline
    // renderer->SetViewMatrix(viewMatrix);
    // renderer->SetProjectionMatrix(projMatrix);
}
```

---

## Basic Camera Movement

### 3.1 Position-Based Movement

#### Simple Movement Functions

```cpp
// Move camera along world axes
camera.MoveUp(2.0f);      // Move up 2 units
camera.MoveDown(1.5f);    // Move down 1.5 units
camera.MoveLeft(3.0f);    // Move left 3 units
camera.MoveRight(2.5f);   // Move right 2.5 units

// Move along camera's forward/backward direction
camera.MoveIn(5.0f);      // Move forward 5 units
camera.MoveOut(3.0f);     // Move backward 3 units

// Direct position setting
camera.SetPosition(10.0f, 5.0f, -20.0f);
```

#### Getting Current Position

```cpp
XMFLOAT3 currentPos = camera.GetPosition();
debug.logDebugMessage(LogLevel::LOG_INFO, 
    L"Camera position: (%.2f, %.2f, %.2f)", 
    currentPos.x, currentPos.y, currentPos.z);
```

### 3.2 Direction-Based Movement

#### Orientation Control

```cpp
// Set camera orientation using yaw and pitch
float yaw = XMConvertToRadians(45.0f);    // 45 degrees around Y-axis
float pitch = XMConvertToRadians(-15.0f); // Look down 15 degrees
camera.SetYawPitch(yaw, pitch);

// Set look direction directly
XMVECTOR forward = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f); // Look along X-axis
XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);      // Standard up vector
camera.SetLookDirection(forward, up);
```

---

## Advanced Camera Navigation

### 4.1 Jump Animation System

The jump animation system provides smooth, curved movement between camera positions with customizable speeds and focus behaviors.

#### Basic Jump Usage

```cpp
// Jump to a new position with medium speed, maintaining focus on current target
camera.JumpTo(15.0f, 8.0f, -25.0f, 2, true);

// Jump with different speeds:
// Speed 1 = Very Fast
// Speed 2 = Fast (default)
// Speed 3 = Medium
// Speed 4 = Slow
// Speed 5+ = Very Slow

// Fast jump without maintaining focus (free-look)
camera.JumpTo(0.0f, 10.0f, 0.0f, 1, false);
```

#### Focus Behavior Explanation

```cpp
// FocusOnTarget = true: Camera maintains view direction toward original target
XMFLOAT3 originalTarget = {5.0f, 0.0f, 5.0f};
camera.SetTarget(originalTarget);
camera.JumpTo(20.0f, 10.0f, -30.0f, 2, true);
// Camera will move to new position but continue looking at (5, 0, 5)

// FocusOnTarget = false: Camera maintains current view direction
camera.JumpTo(20.0f, 10.0f, -30.0f, 2, false);
// Camera moves to new position but keeps same forward direction
```

#### Jump Status Monitoring

```cpp
// Check if camera is currently jumping
if (camera.IsJumping())
{
    float progress = camera.GetJumpProgress(); // Returns 0.0 to 1.0
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"Jump progress: %.1f%%", progress * 100.0f);
}

// Cancel a jump in progress
if (userCancelInput && camera.IsJumping())
{
    camera.CancelJump();
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Jump cancelled by user");
}
```

### 4.2 Rotation Functions

#### Single-Axis Rotations

```cpp
// Rotate 90 degrees around X-axis (pitch rotation)
camera.RotateX(90.0f, 2, true); // 90 degrees, medium speed, focus on target

// Rotate 180 degrees around Y-axis (yaw rotation)
camera.RotateY(180.0f, 1, true); // 180 degrees, fast speed, focus on target

// Rotate 45 degrees around Z-axis (roll rotation)
camera.RotateZ(45.0f, 3, false); // 45 degrees, slower speed, free-look
```

#### Multi-Axis Rotation

```cpp
// Combine rotations around multiple axes
camera.RotateXYZ(30.0f, 45.0f, 15.0f, 2, true);
// X: 30°, Y: 45°, Z: 15°, medium speed, focus maintained
```

#### Smart Opposite Side Rotation

```cpp
// Automatically determine best axis for 180-degree rotation
camera.RotateToOppositeSide(2); // Medium speed
// Camera analyzes current orientation and chooses optimal rotation axis
```

### 4.3 Continuous Rotation Around Target

#### Basic Continuous Rotation

```cpp
// Start continuous rotation around Y-axis (horizontal orbit)
camera.MoveAroundTarget(false, true, false, true);
// X: false, Y: true, Z: false, continuous: true

// Rotate around multiple axes simultaneously
camera.MoveAroundTarget(true, true, false, true);
// X: true, Y: true, Z: false, continuous: true

// Single complete rotation (non-continuous)
camera.MoveAroundTarget(false, true, false, false);
// Will rotate 360° around Y-axis then stop
```

#### Custom Speed Rotation

```cpp
// Set custom rotation speed (degrees per second)
float customSpeed = 90.0f; // 90 degrees per second
camera.MoveAroundTarget(false, true, false, customSpeed, true);

// Very slow rotation for detailed observation
camera.MoveAroundTarget(true, false, false, 15.0f, true); // 15°/sec around X-axis
```

#### Rotation Control

```cpp
// Check rotation status
if (camera.IsRotatingAroundTarget())
{
    float progress = camera.GetRotationProgress(); // 0.0 to 1.0 for non-continuous
    XMFLOAT3 angles = camera.GetCurrentRotationAngles();
    
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"Rotation - Progress: %.1f%%, Angles: X=%.1f° Y=%.1f° Z=%.1f°", 
        progress * 100.0f, angles.x, angles.y, angles.z);
}

// Pause rotation
camera.PauseRotation();

// Resume rotation
camera.ResumeRotation();

// Change speed during rotation
camera.SetRotationSpeed(120.0f); // Change to 120 degrees per second

// Stop rotation completely
camera.StopRotating();
```

---

## Camera History System

### 5.1 Jump History Navigation

The camera automatically maintains a history of up to 10 recent jump operations, allowing users to navigate backward through previous positions.

#### Automatic History Recording

```cpp
// History is automatically recorded for every jump
camera.JumpTo(10.0f, 5.0f, -15.0f, 2, true);  // Position 1 - recorded
camera.JumpTo(20.0f, 8.0f, -25.0f, 1, false); // Position 2 - recorded
camera.JumpTo(5.0f, 12.0f, -10.0f, 3, true);  // Position 3 - recorded

// Each jump is stored with:
// - Start and end positions
// - Travel path
// - Speed settings
// - Focus behavior
// - Timestamp
```

#### Navigating History

```cpp
// Go back 1 jump in history
camera.JumpBackHistory(1);

// Go back 3 jumps in history
camera.JumpBackHistory(3);

// Go back to the very first recorded position
int historyCount = camera.GetJumpHistoryCount();
camera.JumpBackHistory(historyCount);
```

#### History Information

```cpp
// Get complete history
const std::vector<CameraJumpHistoryEntry>& history = camera.GetJumpHistory();

for (size_t i = 0; i < history.size(); ++i)
{
    const auto& entry = history[i];
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"History[%d]: (%.2f,%.2f,%.2f) -> (%.2f,%.2f,%.2f), Distance: %.2f, Focus: %s", 
        (int)i,
        entry.startPosition.x, entry.startPosition.y, entry.startPosition.z,
        entry.endPosition.x, entry.endPosition.y, entry.endPosition.z,
        entry.totalDistance,
        entry.focusOnTarget ? L"Yes" : L"No");
}
```

### 5.2 History Management

```cpp
// Check history count
int count = camera.GetJumpHistoryCount();
debug.logDebugMessage(LogLevel::LOG_INFO, L"Camera history contains %d entries", count);

// Clear all history
camera.ClearJumpHistory();
debug.logLevelMessage(LogLevel::LOG_INFO, L"Camera history cleared");
```

---

## Camera Configuration

### 6.1 View and Projection Setup

#### Custom Projection Matrix

```cpp
// Create custom perspective projection
float fovY = XMConvertToRadians(75.0f);    // 75-degree field of view
float aspectRatio = 16.0f / 9.0f;          // Widescreen aspect ratio
float nearPlane = 0.1f;                    // Near clipping plane
float farPlane = 2000.0f;                  // Far clipping plane

XMMATRIX customProjection = XMMatrixPerspectiveFovLH(fovY, aspectRatio, nearPlane, farPlane);
camera.SetProjectionMatrix(customProjection);
```

#### Custom View Matrix

```cpp
// Create custom view matrix
XMVECTOR eyePos = XMVectorSet(15.0f, 10.0f, -30.0f, 1.0f);
XMVECTOR lookAt = XMVectorSet(0.0f, 5.0f, 0.0f, 1.0f);
XMVECTOR upDir = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

XMMATRIX customView = XMMatrixLookAtLH(eyePos, lookAt, upDir);
camera.SetViewMatrix(customView);
```

### 6.2 Target and Focus Management

#### Setting View Targets

```cpp
// Set specific target point
XMFLOAT3 buildingTop = {25.0f, 15.0f, 40.0f};
camera.SetTarget(buildingTop);

// Move camera while maintaining view on target
camera.JumpTo(50.0f, 20.0f, -60.0f, 2, true);
// Camera moves to new position but continues looking at building top
```

#### Dynamic Target Tracking

```cpp
// Example: Track a moving object
XMFLOAT3 playerPosition = GetPlayerPosition(); // Your game logic
camera.SetTarget(playerPosition);

// Move camera to good viewing angle while tracking player
camera.JumpTo(playerPosition.x + 10.0f, playerPosition.y + 8.0f, playerPosition.z - 15.0f, 1, true);
```

---

## Status and Monitoring

### 7.1 Animation Status Checking

```cpp
void MonitorCameraStatus()
{
    // Check jump animation status
    if (camera.IsJumping())
    {
        float jumpProgress = camera.GetJumpProgress();
        debug.logDebugMessage(LogLevel::LOG_DEBUG, 
            L"Camera jumping: %.1f%% complete", jumpProgress * 100.0f);
    }
    
    // Check rotation status
    if (camera.IsRotatingAroundTarget())
    {
        float rotProgress = camera.GetRotationProgress();
        XMFLOAT3 currentAngles = camera.GetCurrentRotationAngles();
        XMFLOAT3 speeds = camera.GetRotationSpeeds();
        
        debug.logDebugMessage(LogLevel::LOG_DEBUG, 
            L"Rotating: %.1f%%, Angles(%.1f,%.1f,%.1f), Speeds(%.1f,%.1f,%.1f)", 
            rotProgress * 100.0f,
            currentAngles.x, currentAngles.y, currentAngles.z,
            speeds.x, speeds.y, speeds.z);
            
        // Check if rotation is paused
        if (camera.IsRotationPaused())
        {
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Rotation is currently paused");
        }
        
        // Get estimated completion time
        float timeToComplete = camera.GetEstimatedTimeToComplete();
        if (timeToComplete > 0.0f)
        {
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"Estimated time to complete rotation: %.2f seconds", timeToComplete);
        }
    }
}
```

### 7.2 Progress Monitoring

```cpp
// Complete status monitoring function
void DisplayCameraInfo()
{
    XMFLOAT3 pos = camera.GetPosition();
    debug.logDebugMessage(LogLevel::LOG_INFO, 
        L"Camera Position: (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);
    
    // Show history information
    int historyCount = camera.GetJumpHistoryCount();
    debug.logDebugMessage(LogLevel::LOG_INFO, L"History entries: %d/10", historyCount);
    
    // Show current activity
    if (camera.IsJumping())
    {
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Status: Jumping (%.1f%%)", 
            camera.GetJumpProgress() * 100.0f);
    }
    else if (camera.IsRotatingAroundTarget())
    {
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Status: Rotating (%.1f%%)", 
            camera.GetRotationProgress() * 100.0f);
    }
    else
    {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Status: Idle");
    }
}
```

---

## Best Practices

### 8.1 Performance Considerations

#### Efficient Update Calls

```cpp
void OptimizedUpdateLoop()
{
    // Always call UpdateJumpAnimation() every frame
    camera.UpdateJumpAnimation();
    
    // Only get matrices when needed
    static XMMATRIX lastViewMatrix;
    XMMATRIX currentView = camera.GetViewMatrix();
    
    // Check if view matrix changed before updating renderer
    if (memcmp(&lastViewMatrix, &currentView, sizeof(XMMATRIX)) != 0)
    {
        renderer->UpdateViewMatrix(currentView);
        lastViewMatrix = currentView;
    }
}
```

#### Avoid Unnecessary Operations

```cpp
// Good: Check status before operations
if (!camera.IsJumping())
{
    camera.JumpTo(newX, newY, newZ, speed, focus);
}

// Good: Batch multiple movements
camera.RotateXYZ(xAngle, yAngle, zAngle, speed, focus); // Better than three separate rotations

// Avoid: Rapid consecutive jumps
// camera.JumpTo(pos1); camera.JumpTo(pos2); camera.JumpTo(pos3); // Don't do this
```

### 8.2 Thread Safety

```cpp
#include "ThreadLockHelper.h"

void ThreadSafeCameraOperation()
{
    // Use ThreadLockHelper for thread-safe camera operations
    ThreadLockHelper lock(threadManager, "camera_operations", 1000);
    
    if (lock.IsLocked())
    {
        // Safe to modify camera
        camera.JumpTo(10.0f, 5.0f, -15.0f, 2, true);
        
        // Lock automatically released when leaving scope
    }
    else
    {
        debug.logLevelMessage(LogLevel::LOG_WARNING, 
            L"Could not acquire camera lock for thread-safe operation");
    }
}
```

### 8.3 Error Handling

```cpp
void RobustCameraOperation()
{
    try
    {
        // Validate inputs before camera operations
        if (IsValidPosition(targetX, targetY, targetZ))
        {
            if (!camera.IsJumping()) // Don't interrupt existing animations
            {
                camera.JumpTo(targetX, targetY, targetZ, speed, focus);
                
                #if defined(_DEBUG_CAMERA_)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                        L"Camera jump initiated to (%.2f, %.2f, %.2f)", 
                        targetX, targetY, targetZ);
                #endif
            }
            else
            {
                debug.logLevelMessage(LogLevel::LOG_WARNING, 
                    L"Camera jump requested but camera is already jumping");
            }
        }
        else
        {
            debug.logDebugMessage(LogLevel::LOG_ERROR, 
                L"Invalid camera position requested: (%.2f, %.2f, %.2f)", 
                targetX, targetY, targetZ);
        }
    }
    catch (const std::exception& e)
    {
        debug.logDebugMessage(LogLevel::LOG_CRITICAL, 
            L"Camera operation failed: %hs", e.what());
    }
}

bool IsValidPosition(float x, float y, float z)
{
    // Define your world bounds
    const float MAX_WORLD_SIZE = 1000.0f;
    
    return (abs(x) <= MAX_WORLD_SIZE && 
            abs(y) <= MAX_WORLD_SIZE && 
            abs(z) <= MAX_WORLD_SIZE);
}
```

---

## Complete Example Implementation

```cpp
// CameraExample.cpp - Complete camera usage example

#include "DXCamera.h"
#include "Configuration.h"
#include "Debug.h"

class CameraController
{
private:
    Camera camera;
    bool autoRotationEnabled;
    float lastUpdateTime;
    
public:
    CameraController() : autoRotationEnabled(false), lastUpdateTime(0.0f)
    {
        InitializeCamera();
    }
    
    void InitializeCamera()
    {
        // Setup camera with window dimensions
        camera.SetupDefaultCamera(1920.0f, 1080.0f);
        
        // Set initial position overlooking a scene
        camera.SetPosition(0.0f, 15.0f, -30.0f);
        
        // Look at scene center
        XMFLOAT3 sceneCenter = {0.0f, 0.0f, 0.0f};
        camera.SetTarget(sceneCenter);
        
        // Configure near/far planes for good precision
        camera.SetNearFar(0.1f, 1000.0f);
        
        #if defined(_DEBUG_CAMERA_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Camera controller initialized");
        #endif
    }
    
    void Update()
    {
        // CRITICAL: Update camera animations every frame
        camera.UpdateJumpAnimation();
        
        // Update auto-rotation if enabled
        if (autoRotationEnabled && !camera.IsJumping() && !camera.IsRotatingAroundTarget())
        {
            StartAutoRotation();
        }
    }
    
    void HandleInput(int keyCode)
    {
        switch (keyCode)
        {
            case VK_F1: // Jump to predefined location 1
                JumpToLocation1();
                break;
                
            case VK_F2: // Jump to predefined location 2
                JumpToLocation2();
                break;
                
            case VK_F3: // Jump to predefined location 3
                JumpToLocation3();
                break;
                
            case VK_F4: // Toggle auto-rotation
                ToggleAutoRotation();
                break;
                
            case VK_F5: // Go back in history
                GoBackInHistory();
                break;
                
            case VK_F6: // Rotate to opposite side
                RotateToOppositeSide();
                break;
                
            case VK_ESCAPE: // Cancel current operation
                CancelCurrentOperation();
                break;
                
            default:
                HandleDirectionalInput(keyCode);
                break;
        }
    }
    
    void JumpToLocation1()
    {
        // Overhead view
        camera.JumpTo(0.0f, 50.0f, -5.0f, 2, true);
        
        #if defined(_DEBUG_CAMERA_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Jumping to overhead view");
        #endif
    }
    
    void JumpToLocation2()
    {
        // Side view
        camera.JumpTo(40.0f, 10.0f, 0.0f, 1, true);
        
        #if defined(_DEBUG_CAMERA_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Jumping to side view");
        #endif
    }
    
    void JumpToLocation3()
    {
        // Low angle view
        camera.JumpTo(15.0f, 2.0f, -20.0f, 3, true);
        
        #if defined(_DEBUG_CAMERA_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Jumping to low angle view");
        #endif
    }
    
    void ToggleAutoRotation()
    {
        if (camera.IsRotatingAroundTarget())
        {
            camera.StopRotating();
            autoRotationEnabled = false;
            
            #if defined(_DEBUG_CAMERA_)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Auto-rotation disabled");
            #endif
        }
        else
        {
            autoRotationEnabled = true;
            StartAutoRotation();
            
            #if defined(_DEBUG_CAMERA_)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Auto-rotation enabled");
            #endif
        }
    }
    
    void StartAutoRotation()
    {
        if (!camera.IsJumping())
        {
            // Start slow, continuous rotation around Y-axis
            camera.MoveAroundTarget(false, true, false, 30.0f, true);
        }
    }
    
    void GoBackInHistory()
    {
        int historyCount = camera.GetJumpHistoryCount();
        if (historyCount > 0)
        {
            camera.JumpBackHistory(1);
            
            #if defined(_DEBUG_CAMERA_)
                debug.logDebugMessage(LogLevel::LOG_INFO, 
                    L"Going back 1 step in history (%d entries available)", historyCount);
            #endif
        }
        else
        {
            #if defined(_DEBUG_CAMERA_)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"No camera history available");
            #endif
        }
    }
    
    void RotateToOppositeSide()
    {
        if (!camera.IsJumping() && !camera.IsRotatingAroundTarget())
        {
            camera.RotateToOppositeSide(2);
            
            #if defined(_DEBUG_CAMERA_)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Rotating to opposite side");
            #endif
        }
    }
    
    void CancelCurrentOperation()
    {
        if (camera.IsJumping())
        {
            camera.CancelJump();
            
            #if defined(_DEBUG_CAMERA_)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Jump cancelled");
            #endif
        }
        
        if (camera.IsRotatingAroundTarget())
        {
            camera.StopRotating();
            autoRotationEnabled = false;
            
            #if defined(_DEBUG_CAMERA_)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Rotation cancelled");
            #endif
        }
    }
    
    void HandleDirectionalInput(int keyCode)
    {
        const float moveDistance = 2.0f;
        
        // Only allow direct movement when not animating
        if (camera.IsJumping() || camera.IsRotatingAroundTarget())
            return;
            
        switch (keyCode)
        {
            case VK_UP:
                camera.MoveUp(moveDistance);
                break;
                
            case VK_DOWN:
                camera.MoveDown(moveDistance);
                break;
                
            case VK_LEFT:
                camera.MoveLeft(moveDistance);
                break;
                
            case VK_RIGHT:
                camera.MoveRight(moveDistance);
                break;
                
            case VK_PRIOR: // Page Up
                camera.MoveIn(moveDistance);
                break;
                
            case VK_NEXT: // Page Down
                camera.MoveOut(moveDistance);
                break;
        }
    }
    
    // Get camera matrices for rendering
    XMMATRIX GetViewMatrix() const
    {
        return camera.GetViewMatrix();
    }
    
    XMMATRIX GetProjectionMatrix() const
    {
        return camera.GetProjectionMatrix();
    }
    
    // Status information
    void DisplayStatus()
    {
        XMFLOAT3 pos = camera.GetPosition();
        
        if (camera.IsJumping())
        {
            float progress = camera.GetJumpProgress();
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Camera Status: Jumping %.1f%% - Position: (%.2f, %.2f, %.2f)", 
                progress * 100.0f, pos.x, pos.y, pos.z);
        }
        else if (camera.IsRotatingAroundTarget())
        {
            float progress = camera.GetRotationProgress();
            XMFLOAT3 angles = camera.GetCurrentRotationAngles();
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Camera Status: Rotating %.1f%% - Angles: (%.1f°, %.1f°, %.1f°)", 
                progress * 100.0f, angles.x, angles.y, angles.z);
        }
        else
        {
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Camera Status: Idle - Position: (%.2f, %.2f, %.2f)", 
                pos.x, pos.y, pos.z);
        }
        
        int historyCount = camera.GetJumpHistoryCount();
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"History entries: %d", historyCount);
    }
};

// Usage in main application
CameraController cameraController;

void GameLoop()
{
    // Update camera every frame
    cameraController.Update();
    
    // Get matrices for rendering
    XMMATRIX viewMatrix = cameraController.GetViewMatrix();
    XMMATRIX projMatrix = cameraController.GetProjectionMatrix();
    
    // Set matrices in your renderer
    renderer->SetViewMatrix(viewMatrix);
    renderer->SetProjectionMatrix(projMatrix);
    
    // Handle input
    if (GetAsyncKeyState(VK_F1) & 0x8000) cameraController.HandleInput(VK_F1);
    if (GetAsyncKeyState(VK_F2) & 0x8000) cameraController.HandleInput(VK_F2);
    // ... etc for other keys
    
    // Display status periodically
    static int frameCounter = 0;
    if (++frameCounter % 60 == 0) // Every second at 60 FPS
    {
        cameraController.DisplayStatus();
    }
}
```

---

## Troubleshooting

### Common Issues and Solutions

#### Issue: Camera Not Moving During Jump Animation

**Problem**: Camera appears stuck even though `JumpTo()` was called.

**Solution**:
```cpp
// Ensure UpdateJumpAnimation() is called every frame
void UpdateLoop()
{
    camera.UpdateJumpAnimation(); // CRITICAL - Must be called every frame
    
    // Check if jump was properly initiated
    if (!camera.IsJumping())
    {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Jump may have failed to start");
    }
}
```

#### Issue: Rotation Not Working

**Problem**: `MoveAroundTarget()` called but no rotation occurs.

**Solution**:
```cpp
// Check for conflicting operations
if (camera.IsJumping())
{
    debug.logLevelMessage(LogLevel::LOG_WARNING, L"Cannot start rotation while jumping");
    return; // Wait for jump to complete
}

// Ensure target is set and at reasonable distance
XMFLOAT3 currentPos = camera.GetPosition();
XMFLOAT3 target = {0.0f, 0.0f, 0.0f}; // Your target point
camera.SetTarget(target);

// Calculate distance - should be > 0.1
float distance = sqrt(pow(currentPos.x - target.x, 2) + 
                     pow(currentPos.y - target.y, 2) + 
                     pow(currentPos.z - target.z, 2));

if (distance < 0.1f)
{
    debug.logLevelMessage(LogLevel::LOG_WARNING, L"Camera too close to target for rotation");
}
```

#### Issue: History Navigation Not Working

**Problem**: `JumpBackHistory()` called but nothing happens.

**Solution**:
```cpp
// Check history availability
int historyCount = camera.GetJumpHistoryCount();
if (historyCount == 0)
{
    debug.logLevelMessage(LogLevel::LOG_WARNING, L"No camera history available");
    return;
}

// Ensure camera is not busy
if (camera.IsJumping())
{
    debug.logLevelMessage(LogLevel::LOG_WARNING, L"Cannot navigate history while jumping");
    return;
}

// Valid history navigation
if (historyCount >= requestedSteps)
{
    camera.JumpBackHistory(requestedSteps);
}
else
{
    debug.logDebugMessage(LogLevel::LOG_WARNING, 
        L"Requested %d steps but only %d available", requestedSteps, historyCount);
    camera.JumpBackHistory(historyCount); // Go back as far as possible
}
```

#### Issue: Performance Problems

**Problem**: Frame rate drops when using camera animations.

**Solution**:
```cpp
// Optimize update frequency
void OptimizedCameraUpdate()
{
    // Always update animations
    camera.UpdateJumpAnimation();
    
    // Only log debug info occasionally
    #if defined(_DEBUG_CAMERA_)
        static int logCounter = 0;
        if (++logCounter % 300 == 0) // Every 5 seconds at 60 FPS
        {
            if (camera.IsJumping() || camera.IsRotatingAroundTarget())
            {
                // Log status
                XMFLOAT3 pos = camera.GetPosition();
                debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                    L"Camera position: (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);
            }
        }
    #endif
    
    // Reduce path complexity for distant jumps
    // (This would require modifying the Camera class internally)
}
```

#### Issue: Matrix Synchronization Problems

**Problem**: Rendered view doesn't match expected camera position.

**Solution**:
```cpp
void EnsureMatrixSync()
{
    // Force view matrix update after position changes
    camera.UpdateViewMatrix();
    
    // Verify matrices are valid
    XMMATRIX view = camera.GetViewMatrix();
    XMMATRIX proj = camera.GetProjectionMatrix();
    
    // Check for NaN or infinite values
    XMFLOAT4X4 viewF4x4;
    XMStoreFloat4x4(&viewF4x4, view);
    
    if (!isfinite(viewF4x4._11) || !isfinite(viewF4x4._22) || !isfinite(viewF4x4._33))
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Invalid view matrix detected");
        
        // Reset to default
        camera.SetupDefaultCamera(windowWidth, windowHeight);
    }
}
```

### Debug Output Analysis

#### Enable Camera Debugging

```cpp
// In Debug.h, add:
#define _DEBUG_CAMERA_

// This will enable detailed camera debug output showing:
// - Jump initiation and progress
// - Rotation status and angles
// - Position changes
// - History operations
// - Error conditions
```

#### Understanding Debug Messages

```cpp
// Example debug output interpretation:

// "[Camera] JumpTo called: target(10.00, 5.00, -15.00), speed=2, focusOnTarget=true"
// - Jump initiated to position (10, 5, -15)
// - Using speed setting 2 (fast)
// - Will maintain focus on current target

// "[Camera] Jump progress: 45.2%, position(7.23, 3.15, -11.45), focus=maintained"
// - Jump is 45% complete
// - Current position during animation
// - Focus behavior is active

// "[Camera] Rotation progress: X=90.0°, Y=180.0°, Z=0.0°, position(5.00, 8.00, -20.00)"
// - Multi-axis rotation in progress
// - Current accumulated rotation angles
// - Current camera position
```

---

## API Reference Quick Guide

### Movement Functions
| Function | Purpose | Parameters | Focus Control |
|----------|---------|------------|---------------|
| `JumpTo(x, y, z, speed, focus)` | Smooth animated movement | Position, speed (1-5), focus flag | Yes |
| `MoveUp(distance)` | Instant upward movement | Distance in units | No |
| `MoveDown(distance)` | Instant downward movement | Distance in units | No |
| `MoveLeft(distance)` | Instant left movement | Distance in units | No |
| `MoveRight(distance)` | Instant right movement | Distance in units | No |
| `MoveIn(distance)` | Move along forward vector | Distance in units | No |
| `MoveOut(distance)` | Move along backward vector | Distance in units | No |
| `SetPosition(x, y, z)` | Direct position setting | World coordinates | No |

### Rotation Functions
| Function | Purpose | Parameters | Focus Control |
|----------|---------|------------|---------------|
| `RotateX(degrees, speed, focus)` | Rotate around X-axis | Angle, speed (1-5), focus flag | Yes |
| `RotateY(degrees, speed, focus)` | Rotate around Y-axis | Angle, speed (1-5), focus flag | Yes |
| `RotateZ(degrees, speed, focus)` | Rotate around Z-axis | Angle, speed (1-5), focus flag | Yes |
| `RotateXYZ(x, y, z, speed, focus)` | Multi-axis rotation | Angles, speed, focus flag | Yes |
| `RotateToOppositeSide(speed)` | Smart 180° rotation | Speed (1-5) | Yes (automatic) |

### Continuous Rotation Functions
| Function | Purpose | Parameters | Notes |
|----------|---------|------------|--------|
| `MoveAroundTarget(x, y, z, continuous)` | Orbit around target | Axis flags, continuous flag | Default 60°/sec |
| `MoveAroundTarget(x, y, z, speed, continuous)` | Orbit with custom speed | Axis flags, speed, continuous flag | Custom degrees/sec |
| `StopRotating()` | Stop all rotation | None | Immediate stop |
| `PauseRotation()` | Temporarily pause rotation | None | Preserves state |
| `ResumeRotation()` | Resume paused rotation | None | Restores speed |
| `SetRotationSpeed(speed)` | Change rotation speed | Degrees per second | Affects active rotation |

### Status Functions
| Function | Return Type | Purpose |
|----------|-------------|---------|
| `IsJumping()` | `bool` | Check if jump animation active |
| `IsRotatingAroundTarget()` | `bool` | Check if rotation active |
| `IsRotationPaused()` | `bool` | Check if rotation paused |
| `GetJumpProgress()` | `float` | Jump completion (0.0-1.0) |
| `GetRotationProgress()` | `float` | Rotation completion (0.0-1.0) |
| `GetCurrentRotationAngles()` | `XMFLOAT3` | Current rotation angles |
| `GetRotationSpeeds()` | `XMFLOAT3` | Current rotation speeds |
| `GetEstimatedTimeToComplete()` | `float` | Seconds until completion |

### History Functions
| Function | Purpose | Parameters |
|----------|---------|------------|
| `JumpBackHistory(steps)` | Navigate backward in history | Number of steps |
| `GetJumpHistory()` | Get complete history | None (returns const reference) |
| `GetJumpHistoryCount()` | Get history entry count | None |
| `ClearJumpHistory()` | Clear all history | None |

### Configuration Functions
| Function | Purpose | Parameters |
|----------|---------|------------|
| `SetupDefaultCamera(width, height)` | Initialize with defaults | Window dimensions |
| `SetTarget(target)` | Set look-at target | XMFLOAT3 position |
| `SetNearFar(near, far)` | Set clipping planes | Near and far distances |
| `SetViewMatrix(matrix)` | Set custom view matrix | XMMATRIX |
| `SetProjectionMatrix(matrix)` | Set custom projection | XMMATRIX |
| `UpdateViewMatrix()` | Recalculate view matrix | None |

### Speed Settings Guide
| Speed Value | Description | Use Case |
|-------------|-------------|----------|
| 1 | Very Fast | Quick navigation, instant response |
| 2 | Fast | Standard responsive movement |
| 3 | Medium | Smooth, observable movement |
| 4 | Slow | Detailed observation, precise positioning |
| 5+ | Very Slow | Cinematic movement, presentations |

### Focus Behavior Guide
| Focus Flag | Behavior | Best For |
|------------|----------|----------|
| `true` | Maintain view direction toward target | Object inspection, orbiting |
| `false` | Maintain current view direction | Free exploration, flythrough |

---

**Note**: This camera system integrates with the engine's MathPrecalculation class for optimized mathematical operations and ThreadManager for thread-safe operations. Always ensure `UpdateJumpAnimation()` is called every frame for proper animation functionality.

**Debug Support**: Enable `_DEBUG_CAMERA_` in Debug.h for comprehensive logging of camera operations, which is invaluable for troubleshooting and understanding camera behavior during development.

---

*Camera-Example-Usage.md - Complete usage guide for the Camera Class*  
*Generated for C++17 DirectX-based game engine*