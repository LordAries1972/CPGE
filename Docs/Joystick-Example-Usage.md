# Joystick Class - Example Usage Guide

## Table of Contents

1.  [Overview](#overview)
2.  [Class Features](#class-features)
3.  [Quick Start Guide](#quick-start-guide)
4.  [Basic Setup](#basic-setup)
5.  [Detection and Initialization](#detection-and-initialization)
6.  [Movement Modes](#movement-modes)
    - [2D Movement Mode](#2d-movement-mode)
    - [3D Movement Mode](#3d-movement-mode)
7.  [Configuration Examples](#configuration-examples)
8.  [Reading Joystick Input](#reading-joystick-input)
9.  [Button Mapping](#button-mapping)
10. [Advanced Usage](#advanced-usage)
11. [Best Practices](#best-practices)
12. [Troubleshooting](#troubleshooting)
13. [API Reference](#api-reference)

## Overview

The Joystick class provides comprehensive joystick/gamepad support for Windows-based applications. It supports both 2D and 3D movement modes, with automatic joystick detection, configurable sensitivity settings, and seamless integration with the camera system for 3D navigation.

## Class Features

- **Automatic Joystick Detection**: Automatically detects up to 2 connected joysticks
- **Dual Movement Modes**: Supports both 2D and 3D movement paradigms
- **Camera Integration**: Direct integration with the Camera class for 3D movement
- **Configurable Sensitivity**: Adjustable movement and rotation sensitivity
- **Deadzone Support**: Built-in deadzone handling to prevent drift
- **Button Mapping**: Save/load custom button mappings (JSON format)
- **Thread-Safe**: Safe to use in multi-threaded applications
- **Debug Support**: Comprehensive debug output when enabled

## Quick Start Guide

### Basic Example - 3D Camera Control

```cpp
#include "Joystick.h"
#include "DXCamera.h"

// Create joystick and camera instances
Joystick gamepad;
Camera camera;

// Configure for 3D movement
gamepad.setCamera(&camera);
gamepad.ConfigureFor3DMovement();

// In your main loop
void gameLoop() {
    // Process joystick movement (updates camera automatically)
    gamepad.processJoystickMovement(0); // Use joystick ID 0
    
    // Your rendering code here...
}
```

### Basic Example - 2D Movement

```cpp
#include "Joystick.h"

// Create joystick instance
Joystick gamepad;

// Configure for 2D movement
gamepad.ConfigureFor2DMovement();

// In your main loop
void gameLoop() {
    // Process joystick movement
    gamepad.processJoystickMovement(0);
    
    // Get 2D position
    float x = gamepad.getLastX();
    float y = gamepad.getLastY();
    
    // Use x, y for your 2D character/cursor positioning
}
```

## Basic Setup

### 1. Include Required Headers

```cpp
#include "Joystick.h"
#include "DXCamera.h"  // Only needed for 3D mode
```

### 2. Create Joystick Instance

```cpp
// Create a joystick instance
Joystick joystick;

// The constructor automatically detects connected joysticks
```

### 3. Check for Connected Joysticks

```cpp
// Check how many joysticks are connected
size_t numJoysticks = joystick.NumOfJoysticks();

if (numJoysticks > 0) {
    std::cout << "Found " << numJoysticks << " joystick(s)" << std::endl;
} else {
    std::cout << "No joysticks detected" << std::endl;
}
```

## Detection and Initialization

### Manual Joystick Detection

```cpp
// Manually trigger joystick detection
joystick.detectJoysticks();

// Check detection results
if (joystick.NumOfJoysticks() > 0) {
    std::cout << "Joysticks detected and ready to use" << std::endl;
}
```

### Reading Raw Joystick State

```cpp
JoystickState state;
int joystickID = 0; // Use first joystick

if (joystick.readJoystickState(joystickID, state)) {
    // Access raw joystick data
    std::cout << "X: " << state.info.dwXpos << std::endl;
    std::cout << "Y: " << state.info.dwYpos << std::endl;
    std::cout << "Buttons: " << state.info.dwButtons << std::endl;
}
```

## Movement Modes

### 2D Movement Mode

Perfect for 2D games, menus, or cursor control.

```cpp
// Setup 2D movement
joystick.ConfigureFor2DMovement();

// Set custom sensitivity (optional)
joystick.setMovementSensitivity(2.0f); // Higher = faster movement

// In your update loop
void update2DGame() {
    joystick.processJoystickMovement(0);
    
    // Get current 2D position
    float playerX = joystick.getLastX();
    float playerY = joystick.getLastY();
    
    // Update your 2D sprite/character position
    updatePlayerPosition(playerX, playerY);
}
```

### 3D Movement Mode

Ideal for 3D games with first-person or third-person camera control.

```cpp
// Setup 3D movement with camera
Camera gameCamera;
joystick.setCamera(&gameCamera);
joystick.ConfigureFor3DMovement();

// Set custom sensitivities (optional)
joystick.setMovementSensitivity(0.1f);  // Movement speed
joystick.setRotationSensitivity(0.02f); // Look sensitivity

// In your update loop
void update3DGame() {
    // This automatically updates the camera based on joystick input
    joystick.processJoystickMovement(0);
    
    // Camera is now updated - proceed with rendering
}
```

## Configuration Examples

### Dynamic Mode Switching

```cpp
class Game {
private:
    Joystick joystick;
    Camera camera;
    bool is3DMode = true;

public:
    void switchControlMode() {
        is3DMode = !is3DMode;
        joystick.SwitchModes(camera, is3DMode);
        
        if (is3DMode) {
            std::cout << "Switched to 3D camera control" << std::endl;
        } else {
            std::cout << "Switched to 2D movement" << std::endl;
        }
    }
    
    void update() {
        joystick.processJoystickMovement(0);
        
        if (!is3DMode) {
            // Handle 2D positioning
            float x = joystick.getLastX();
            float y = joystick.getLastY();
            // Update 2D elements...
        }
        // 3D camera is automatically handled
    }
};
```

### Custom Sensitivity Settings

```cpp
// Fine-tune movement for different game types

// For a slow, precise game
joystick.setMovementSensitivity(0.02f);
joystick.setRotationSensitivity(0.005f);

// For a fast-paced action game
joystick.setMovementSensitivity(0.15f);
joystick.setRotationSensitivity(0.03f);

// For 2D platformer
joystick.ConfigureFor2DMovement();
joystick.setMovementSensitivity(3.0f);
```

## Reading Joystick Input

### Getting Normalized Axis Values

```cpp
// Get all normalized axis values (-1.0 to 1.0)
JoystickAxes axes = joystick.getNormalizedAxes(0);

std::cout << "Left Stick X: " << axes.x << std::endl;
std::cout << "Left Stick Y: " << axes.y << std::endl;
std::cout << "Right Stick X: " << axes.rx << std::endl;
std::cout << "Right Stick Y: " << axes.ry << std::endl;
std::cout << "Triggers/Z: " << axes.z << std::endl;
```

### Manual Movement Processing

```cpp
// Process movement manually instead of using automatic processing
JoystickAxes axes = joystick.getNormalizedAxes(0);

// Custom movement logic
if (std::abs(axes.x) > 0.1f) {
    // Handle horizontal movement
    playerVelocity.x = axes.x * moveSpeed;
}

if (std::abs(axes.y) > 0.1f) {
    // Handle vertical movement
    playerVelocity.y = axes.y * moveSpeed;
}

// Custom camera rotation
if (std::abs(axes.rx) > 0.1f || std::abs(axes.ry) > 0.1f) {
    camera.rotate(axes.rx * rotSpeed, axes.ry * rotSpeed);
}
```

## Button Mapping

### Loading and Saving Button Mappings

```cpp
// Load button mappings from file
if (joystick.loadMapping("custom_controls.json")) {
    std::cout << "Custom controls loaded" << std::endl;
} else {
    std::cout << "Using default controls" << std::endl;
}

// Process button-to-keyboard mapping
joystick.processJoystickInput();

// Save current mappings
joystick.saveMapping("my_controls.json");
```

### Custom Button Handling

```cpp
// Read joystick state and handle buttons manually
JoystickState state;
if (joystick.readJoystickState(0, state)) {
    // Check specific buttons (bit flags)
    if (state.info.dwButtons & 0x01) {
        // Button 0 pressed (usually 'A' on Xbox controller)
        handleJumpAction();
    }
    
    if (state.info.dwButtons & 0x02) {
        // Button 1 pressed (usually 'B' on Xbox controller)
        handleActionButton();
    }
    
    if (state.info.dwButtons & 0x04) {
        // Button 2 pressed (usually 'X' on Xbox controller)
        handleSpecialAction();
    }
}
```

## Advanced Usage

### Multi-Joystick Support

```cpp
// Handle multiple joysticks
for (size_t i = 0; i < joystick.NumOfJoysticks(); ++i) {
    // Process each joystick individually
    joystick.processJoystickMovement(static_cast<int>(i));
    
    // Or get individual axis data
    JoystickAxes axes = joystick.getNormalizedAxes(static_cast<int>(i));
    
    // Handle player-specific movement
    updatePlayer(i, axes);
}
```

### Integration with Game States

```cpp
class GameStateManager {
private:
    Joystick joystick;
    Camera camera;
    
public:
    void handleMenuState() {
        // Use 2D mode for menu navigation
        joystick.ConfigureFor2DMovement();
        joystick.setMovementSensitivity(1.0f);
        
        joystick.processJoystickMovement(0);
        
        float x = joystick.getLastX();
        float y = joystick.getLastY();
        
        updateMenuCursor(x, y);
    }
    
    void handleGameplayState() {
        // Use 3D mode for gameplay
        joystick.setCamera(&camera);
        joystick.ConfigureFor3DMovement();
        
        joystick.processJoystickMovement(0);
        // Camera automatically updated
    }
};
```

### Performance Optimization

```cpp
// Only process joystick input when needed
class OptimizedInput {
private:
    Joystick joystick;
    std::chrono::steady_clock::time_point lastUpdate;
    std::chrono::milliseconds updateInterval{16}; // ~60 FPS
    
public:
    void update() {
        auto now = std::chrono::steady_clock::now();
        
        if (now - lastUpdate >= updateInterval) {
            joystick.processJoystickMovement(0);
            lastUpdate = now;
        }
    }
};
```

## Best Practices

### 1. Always Check for Joystick Availability

```cpp
// Check before processing
if (joystick.NumOfJoysticks() > 0) {
    joystick.processJoystickMovement(0);
} else {
    // Fall back to keyboard input
    handleKeyboardInput();
}
```

### 2. Handle Mode Switching Gracefully

```cpp
void switchToMenuMode() {
    // Save current 3D camera state if needed
    savedCameraPosition = camera.GetPosition();
    
    // Switch to 2D mode
    joystick.ConfigureFor2DMovement();
}

void switchToGameMode() {
    // Restore camera state
    camera.SetPosition(savedCameraPosition.x, savedCameraPosition.y, savedCameraPosition.z);
    
    // Switch to 3D mode
    joystick.setCamera(&camera);
    joystick.ConfigureFor3DMovement();
}
```

### 3. Use Appropriate Sensitivity Values

```cpp
// Different sensitivities for different situations
void configureForVehicle() {
    joystick.setMovementSensitivity(0.2f);  // Faster for vehicles
    joystick.setRotationSensitivity(0.05f);
}

void configureForStealth() {
    joystick.setMovementSensitivity(0.03f); // Slower for precision
    joystick.setRotationSensitivity(0.01f);
}
```

### 4. Implement Deadzone Handling

The class automatically handles deadzones, but you can check if input is significant:

```cpp
JoystickAxes axes = joystick.getNormalizedAxes(0);

// Only process if movement is significant
if (std::abs(axes.x) > 0.05f || std::abs(axes.y) > 0.05f) {
    // Process movement
    joystick.processJoystickMovement(0);
}
```

## Troubleshooting

### Common Issues and Solutions

#### 1. No Joysticks Detected

```cpp
// Force re-detection
joystick.detectJoysticks();

if (joystick.NumOfJoysticks() == 0) {
    std::cout << "Please connect a gamepad and restart" << std::endl;
    // Implement keyboard fallback
}
```

#### 2. Joystick Input Not Working

```cpp
// Check if joystick is readable
JoystickState state;
if (!joystick.readJoystickState(0, state)) {
    std::cout << "Cannot read joystick 0 - may be disconnected" << std::endl;
    // Re-detect joysticks
    joystick.detectJoysticks();
}
```

#### 3. Camera Not Moving in 3D Mode

```cpp
// Ensure camera is set before processing
if (joystick.getMovementMode() == MovementMode::MODE_3D) {
    if (joystick.m_camera == nullptr) {
        std::cout << "Camera not set for 3D mode!" << std::endl;
        joystick.setCamera(&camera);
    }
}
```

#### 4. Movement Too Sensitive/Slow

```cpp
// Adjust sensitivity dynamically
JoystickAxes axes = joystick.getNormalizedAxes(0);

// Check if user is making large movements
if (std::abs(axes.x) > 0.8f || std::abs(axes.y) > 0.8f) {
    // Reduce sensitivity for large movements
    joystick.setMovementSensitivity(0.02f);
} else {
    // Normal sensitivity
    joystick.setMovementSensitivity(0.1f);
}
```

### Debug Information

Enable debug output by defining `_DUBUG_JOYSTICK_` in Debug.h:

```cpp
// In Debug.h, add:
#define _DUBUG_JOYSTICK_

// This will provide detailed logging of:
// - Joystick detection
// - Movement processing
// - Button state changes
// - Axis values
```

## API Reference

### Core Methods

| Method | Description | Parameters | Return |
|--------|-------------|------------|---------|
| `detectJoysticks()` | Scan for connected joysticks | None | void |
| `readJoystickState()` | Read raw joystick state | `int joystickID, JoystickState& state` | bool |
| `processJoystickMovement()` | Process movement based on current mode | `int joystickID = 0` | void |
| `getNormalizedAxes()` | Get normalized axis values | `int joystickID = 0` | JoystickAxes |

### Configuration Methods

| Method | Description | Parameters | Return |
|--------|-------------|------------|---------|
| `ConfigureFor2DMovement()` | Setup for 2D movement mode | None | void |
| `ConfigureFor3DMovement()` | Setup for 3D movement mode | None | void |
| `setMovementMode()` | Set movement mode | `MovementMode mode` | void |
| `setCamera()` | Set camera reference for 3D mode | `Camera* camera` | void |
| `setMovementSensitivity()` | Set movement sensitivity | `float sensitivity` | void |
| `setRotationSensitivity()` | Set rotation sensitivity | `float sensitivity` | void |

### Query Methods

| Method | Description | Parameters | Return |
|--------|-------------|------------|---------|
| `NumOfJoysticks()` | Get number of detected joysticks | None | size_t |
| `getMovementMode()` | Get current movement mode | None | MovementMode |
| `getLastX()` | Get last 2D X position | None | float |
| `getLastY()` | Get last 2D Y position | None | float |

### Button Mapping Methods

| Method | Description | Parameters | Return |
|--------|-------------|------------|---------|
| `loadMapping()` | Load button mappings from file | `const std::string& filename` | bool |
| `saveMapping()` | Save button mappings to file | `const std::string& filename` | bool |
| `processJoystickInput()` | Process button-to-key mapping | None | void |

### Data Structures

#### JoystickAxes
```cpp
struct JoystickAxes {
    float x;    // Left stick X (-1.0 to 1.0)
    float y;    // Left stick Y (-1.0 to 1.0)
    float z;    // Triggers/throttle (-1.0 to 1.0)
    float rx;   // Right stick X (-1.0 to 1.0)
    float ry;   // Right stick Y (-1.0 to 1.0)
    float rz;   // Additional rotation axis (-1.0 to 1.0)
};
```

#### MovementMode
```cpp
enum class MovementMode {
    MODE_2D,    // 2D movement (updates internal position)
    MODE_3D     // 3D movement (updates camera)
};
```

This comprehensive guide should help you integrate and use the Joystick class effectively in your projects. Remember to always check for joystick availability and handle both connected and disconnected states gracefully in your applications.