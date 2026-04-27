# FXManager Class - Complete Usage Guide

## Table of Contents

1. [Overview](#overview)
2. [Important Notes](#important-notes)
3. [Getting Started](#getting-started)
4. [Initialization and Cleanup](#initialization-and-cleanup)
5. [Color Fade Effects](#color-fade-effects)
6. [Scroll Effects](#scroll-effects)
7. [Particle Explosion Effects](#particle-explosion-effects)
8. [Starfield Effects](#starfield-effects)
9. [Text Scroller Effects](#text-scroller-effects)
10. [Advanced Features](#advanced-features)
11. [Window Resize Handling](#window-resize-handling)
12. [Thread Safety](#thread-safety)
13. [Performance Considerations](#performance-considerations)
14. [Troubleshooting](#troubleshooting)
15. [Code Examples](#code-examples)

---

## Overview

The FXManager class is a comprehensive visual effects system designed for real-time rendering applications. It provides a queue-based approach to managing multiple visual effects simultaneously, including fade transitions, scrolling backgrounds, particle systems, starfields, and text scrolling effects.

### Key Features
- **Multi-threaded rendering support** with thread-safe operations
- **Queue-based effect management** for complex effect sequences
- **Multiple effect types** including fades, scrolls, particles, and text
- **Callback system** for chaining effects and events
- **DirectX 11 integration** with shader-based rendering
- **Window resize handling** with state preservation
- **Performance optimized** for real-time applications

---

## Important Notes

⚠️ **RENDERER COMPATIBILITY WARNING** ⚠️

**This FXManager implementation is currently designed specifically for DirectX 11 rendering systems. It requires further development and adaptation to work with other rendering backends including:**

- DirectX 12
- OpenGL
- Vulkan
- Other custom rendering systems

**Key DirectX 11 Dependencies:**
- Uses ID3D11Device and ID3D11DeviceContext for rendering operations
- Shader compilation and management specific to DirectX 11
- Blend state and render state management using DirectX 11 APIs
- Vertex buffer and constant buffer operations

**Future Development Required:**
- Abstract rendering interface implementation
- Shader compilation for different APIs
- Platform-specific resource management
- API-agnostic rendering calls

---

## Getting Started

### Prerequisites
- DirectX 11 compatible renderer
- ThreadManager for thread safety
- Debug system for logging
- Vector and Color utility classes

### Basic Setup
```cpp
#include "DX_FXManager.h"

// Global FXManager instance
FXManager fxManager;

// In your application initialization
fxManager.Initialize();
```

---

## Initialization and Cleanup

### Initialization
```cpp
void InitializeApplication() {
    // Initialize renderer first
    renderer->Initialize(hwnd, hInstance);
    
    // Initialize FXManager after renderer is ready
    fxManager.Initialize();
}
```

### Cleanup
```cpp
void CleanupApplication() {
    // Cleanup FXManager before renderer
    fxManager.CleanUp();
    
    // Then cleanup renderer
    renderer->Cleanup();
}
```

### Automatic Cleanup
The FXManager automatically cleans up when destroyed, but manual cleanup is recommended for proper resource management.

---

## Color Fade Effects

### Basic Fade Operations

#### Fade to Black
```cpp
// Fade to black over 2 seconds with no delay
fxManager.FadeToBlack(2.0f, 0.0f);
```

#### Fade to White
```cpp
// Fade to white over 1.5 seconds with 0.5 second delay
fxManager.FadeToWhite(1.5f, 0.5f);
```

#### Fade to Custom Color
```cpp
// Fade to red over 3 seconds
XMFLOAT4 redColor(1.0f, 0.0f, 0.0f, 1.0f);
fxManager.FadeToColor(redColor, 3.0f, 0.0f);
```

### Advanced Fade Operations

#### Fade with Callback
```cpp
// Fade out then execute callback when complete
XMFLOAT4 blackColor(0.0f, 0.0f, 0.0f, 1.0f);
fxManager.FadeOutThenCallback(blackColor, 2.0f, 0.0f, []() {
    // This code executes when fade completes
    sceneManager.LoadNextScene();
});
```

#### Sequence Fading
```cpp
// Fade out to black, execute callback, then fade in from black
XMFLOAT4 fadeOut(0.0f, 0.0f, 0.0f, 1.0f);
XMFLOAT4 fadeIn(0.0f, 0.0f, 0.0f, 0.0f);

fxManager.FadeOutInSequence(fadeOut, fadeIn, 1.5f, 0.0f, []() {
    // Midpoint callback - executed between fade out and fade in
    gameState.SwitchLevel();
});
```

#### Check Fade Status
```cpp
if (fxManager.IsFadeActive()) {
    // A fade effect is currently running
    // Skip input processing or other operations
}
```

---

## Scroll Effects

### Basic Scrolling

#### Start Scroll Effect
```cpp
// Start a scrolling background effect
fxManager.StartScrollEffect(
    BlitObj2DIndexType::IMG_SCROLLBG1,  // Texture to scroll
    FXSubType::ScrollLeft,               // Direction
    2,                                   // Speed (pixels per frame)
    800,                                 // Tile width
    600,                                 // Tile height
    0.0f                                 // Delay before starting
);
```

#### Available Scroll Directions
- `ScrollRight` - Move texture right
- `ScrollLeft` - Move texture left  
- `ScrollUp` - Move texture up
- `ScrollDown` - Move texture down
- `ScrollUpAndLeft` - Diagonal movement
- `ScrollUpAndRight` - Diagonal movement
- `ScrollDownAndLeft` - Diagonal movement
- `ScrollDownAndRight` - Diagonal movement

### Advanced Scrolling

#### Parallax Scrolling
```cpp
// Create multiple layers with different depths
fxManager.StartParallaxLayer(
    BlitObj2DIndexType::IMG_SCROLLBG1,  // Background layer
    FXSubType::ScrollLeft,
    1,                                   // Base speed
    0.3f,                               // Depth multiplier (slower)
    1024, 768,                          // Tile dimensions
    0.0f,                               // No delay
    false                               // Not camera linked
);

fxManager.StartParallaxLayer(
    BlitObj2DIndexType::IMG_SCROLLBG2,  // Foreground layer
    FXSubType::ScrollLeft,
    1,                                   // Base speed
    1.5f,                               // Depth multiplier (faster)
    1024, 768,                          // Tile dimensions
    0.0f,                               // No delay
    true                                // Camera linked
);
```

#### Dynamic Speed Control
```cpp
// Update scroll speed at runtime
fxManager.UpdateScrollSpeed(BlitObj2DIndexType::IMG_SCROLLBG1, 5);

// Smooth speed transitions
fxManager.FadeScrollSpeed(
    BlitObj2DIndexType::IMG_SCROLLBG1,
    2,      // From speed
    8,      // To speed
    3.0f    // Over 3 seconds
);
```

#### Pause and Resume
```cpp
// Pause scrolling
fxManager.PauseScroll(BlitObj2DIndexType::IMG_SCROLLBG1);

// Resume scrolling
fxManager.ResumeScroll(BlitObj2DIndexType::IMG_SCROLLBG1);

// Change direction
fxManager.SetScrollDirection(BlitObj2DIndexType::IMG_SCROLLBG1, FXSubType::ScrollRight);
```

#### Stop Scroll Effect
```cpp
fxManager.StopScrollEffect(BlitObj2DIndexType::IMG_SCROLLBG1);
```

---

## Particle Explosion Effects

### Basic Particle Explosion
```cpp
// Create explosion at screen coordinates
fxManager.CreateParticleExplosion(
    400,    // X position (center of explosion)
    300,    // Y position (center of explosion)
    50,     // Number of particles
    100     // Maximum radius
);
```

### Particle System Features
- **Radial distribution** - Particles spread in all directions
- **Color variation** - Random colors from predefined palette
- **Speed variation** - Each particle has unique speed
- **Delay system** - Staggered particle movement for realistic effect
- **Fade out** - Particles fade as they reach maximum distance

### Particle Properties
- **Position** - Each particle tracks X/Y coordinates
- **Color** - RGBA values with transparency support
- **Speed** - Individual movement speed per particle
- **Angle** - Direction of movement (radians)
- **Radius** - Current distance from explosion center
- **Max Radius** - Maximum distance before particle completion

---

## Starfield Effects

### Signature

```cpp
void CreateStarfield(
    int       numStars,      // Number of stars in the field
    float     circularRadius,// Spread radius of the star distribution
    float     resetDepthPos, // Maximum z-depth before a star resets
    XMFLOAT3  startPos,      // Center/target position (default: {0, 0, 0})
    bool      reverse        // If true, stars travel TO startPos instead of past camera (default: false)
);
```

`startPos` and `reverse` are optional — existing calls without them continue to work unchanged.

### Parameter Reference

| Parameter | Description |
|---|---|
| `numStars` | Total number of stars rendered simultaneously |
| `circularRadius` | Radius of the cylindrical distribution around the centre axis |
| `resetDepthPos` | z-depth at which stars are spawned (default) or recycled (reverse) |
| `startPos.x/y` | World-space offset for the centre of the distribution |
| `startPos.z` | z-offset added to the far spawn point (default mode) |
| `reverse` | `false` = stars fly past camera; `true` = stars converge toward `startPos` |

---

### Default Mode — Stars Fly Past Camera

Stars spawn far away (at `startPos.z + resetDepthPos`) distributed in a cylinder around (`startPos.x`, `startPos.y`) and travel toward z = 0 (the camera). Each star resets to the far end when it passes the near plane.

```cpp
// Centred on world origin — classic hyperspace/warp effect
fxManager.CreateStarfield(100, 800.0f, 1000.0f);

// Same effect but offset 200 units right and 100 units up
fxManager.CreateStarfield(
    100,
    800.0f,
    1000.0f,
    XMFLOAT3(200.0f, 100.0f, 0.0f)
);

// Denser, tighter field offset in all three axes
fxManager.CreateStarfield(
    200,
    400.0f,
    1500.0f,
    XMFLOAT3(0.0f, 0.0f, 200.0f)   // Stars start 200 units further back
);
```

---

### Reverse Mode — Stars Converge Toward a Target

When `reverse = true` the motion is inverted. Stars spawn spread across `circularRadius` near the camera and travel away from it. As z increases toward `resetDepthPos` their x/y positions smoothly converge to `startPos.x/y`, creating a "flying into a vanishing point" or deceleration-from-warp look. Stars fade out as they recede and reset near the camera to repeat.

```cpp
// Stars converge to world origin — deceleration/arrival effect
fxManager.CreateStarfield(
    100,
    800.0f,
    1000.0f,
    XMFLOAT3(0.0f, 0.0f, 0.0f),
    true    // reverse
);

// Stars converge to an off-centre target (e.g. a wormhole at the right edge)
fxManager.CreateStarfield(
    150,
    600.0f,
    1200.0f,
    XMFLOAT3(300.0f, -80.0f, 500.0f),
    true
);
```

---

### Managing the Starfield

```cpp
// Check whether a starfield is currently active
if (fxManager.starfieldID > 0) {
    // Starfield is running
}

// Stop the starfield and free its particles
fxManager.StopStarfield();
```

### Starfield Features

- **3D positioning** — Stars live in world space with full perspective projection
- **Perspective scaling** — Stars grow larger as they approach the camera
- **Depth-based fade** — Alpha driven by distance; stars fade smoothly in/out
- **Continuous recycling** — Stars reset automatically; the effect runs until `StopStarfield()` is called
- **Origin offset** — `startPos` shifts the entire distribution without changing any other behaviour
- **Reverse mode** — Spatial convergence creates a distinct visual separate from the default fly-through
- **Off-screen culling** — Stars outside normalised device coordinates are skipped each frame

---

## Text Scroller Effects

The FXManager provides four types of text scrolling effects for different use cases.

### Left to Right Scroller
```cpp
// Create left-to-right text scroller
fxManager.CreateTextScrollerLTOR(
    L"Welcome to the Game!",            // Text to display
    L"Arial",                           // Font name
    24.0f,                              // Font size
    XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),  // White color
    100.0f, 200.0f,                     // Region position (X, Y)
    600.0f, 100.0f,                     // Region size (Width, Height)
    50.0f,                              // Scroll speed
    2.0f,                               // Center hold time
    8.0f,                               // Total duration
    1.0f,                               // Character spacing
    8.0f                                // Word spacing
);
```

### Right to Left Scroller
```cpp
// Create right-to-left text scroller
fxManager.CreateTextScrollerRTOL(
    L"Game Over",                       // Text to display
    L"MayaCulpa",                       // Font name
    32.0f,                              // Font size
    XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f),  // Red color
    0.0f, 250.0f,                       // Region position
    800.0f, 150.0f,                     // Region size
    75.0f,                              // Scroll speed
    3.0f,                               // Center hold time
    10.0f                               // Total duration
);
```

### Consistent Scroller
```cpp
// Create continuous scrolling text (like news ticker)
fxManager.CreateTextScrollerConsistent(
    L"Breaking News: Latest updates from the game world...",
    L"Arial",                           // Font name
    18.0f,                              // Font size
    XMFLOAT4(1.0f, 1.0f, 0.0f, 1.0f),  // Yellow color
    0.0f, 50.0f,                        // Region position
    800.0f, 30.0f,                      // Region size
    30.0f,                              // Scroll speed
    FLT_MAX                             // Infinite duration
);
```

### Movie Credits Scroller
```cpp
// Create movie-style credits
std::vector<std::wstring> credits = {
    L"Game Director: John Doe",
    L"Lead Programmer: Jane Smith", 
    L"Art Director: Bob Johnson",
    L"Music Composer: Alice Brown",
    L"",  // Empty line for spacing
    L"Special Thanks:",
    L"Our Amazing Community"
};

fxManager.CreateTextScrollerMovie(
    credits,                            // Lines of text
    L"Times New Roman",                 // Font name
    20.0f,                              // Font size
    XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f),  // Light gray
    100.0f, 100.0f,                     // Region position
    600.0f, 400.0f,                     // Region size
    25.0f,                              // Scroll speed
    35.0f,                              // Line spacing
    30.0f                               // Duration
);
```

### Text Scroller Control
```cpp
// Get the effect ID when creating (store return value if needed)
int scrollerID = /* from creation */;

// Pause text scroller
fxManager.PauseTextScroller(scrollerID);

// Resume text scroller
fxManager.ResumeTextScroller(scrollerID);

// Stop text scroller
fxManager.StopTextScroller(scrollerID);
```

### Text Scroller Features
- **Transparency effects** - Fade in/out at region boundaries
- **Font support** - Custom font selection per scroller
- **Flexible positioning** - Define exact scroll regions
- **Speed control** - Adjustable scroll speeds
- **Hold functionality** - Pause text in center (LTOR/RTOL)
- **Character spacing** - Fine-tune text appearance
- **Word spacing** - Additional control over text layout

---

## Advanced Features

### Effect Chaining
```cpp
// Chain effects to run in sequence
fxManager.ChainEffect(firstEffectID, secondEffectID);
```

### Effect Control
```cpp
// Cancel specific effect
fxManager.CancelEffect(effectID);

// Restart specific effect
fxManager.RestartEffect(effectID);
```

### Custom FX Items
```cpp
// Create custom effect manually
FXItem customEffect;
customEffect.type = FXType::ColorFader;
customEffect.subtype = FXSubType::FadeToTargetColor;
customEffect.duration = 2.0f;
customEffect.targetColor = XMFLOAT4(0.5f, 0.0f, 0.5f, 1.0f);
customEffect.fxID = 1001;

fxManager.AddEffect(customEffect);
```

---

## Window Resize Handling

The FXManager provides automatic handling of window resize events to prevent crashes and maintain effect continuity.

### During Resize
```cpp
// Called when resize begins (typically in WM_SIZE handler)
fxManager.StopAllFXForResize();

// Your resize code here
// renderer->Resize(newWidth, newHeight);

// Called when resize completes
fxManager.RestartFXAfterResize();
```

### Resize Safety Features
- **State preservation** - Active effects are remembered
- **Safe stopping** - All effects stopped before DirectX recreation
- **Automatic restart** - Effects restored after resize completion
- **Thread safety** - Protected against concurrent access during resize
- **Resource validation** - Ensures DirectX resources are valid before restart

---

## Thread Safety

The FXManager is designed with thread safety in mind:

### Thread-Safe Operations
- Effect addition and removal
- Rendering operations
- State management
- Callback execution

### Threading Guidelines
```cpp
// Safe to call from any thread
fxManager.FadeToBlack(2.0f, 0.0f);

// Rendering should be called from main render thread
fxManager.Render();       // 3D effects
fxManager.Render2D();     // 2D effects
```

### Lock Management
The FXManager uses ThreadLockHelper for safe locking:
- Automatic timeout handling
- RAII-style lock management
- Deadlock prevention
- Performance monitoring

---

## Performance Considerations

### Optimization Tips

1. **Effect Limits**
   - Limit concurrent effects to maintain performance
   - Remove completed effects promptly
   - Use appropriate timeouts

2. **Particle Systems**
   - Reduce particle count for better performance
   - Use smaller maximum radius values
   - Consider LOD based on screen size

3. **Text Scrollers**
   - Limit number of concurrent text effects
   - Use shorter text strings when possible
   - Optimize font rendering

4. **Memory Management**
   - Effects are automatically cleaned up
   - Callbacks are removed after execution
   - Vectors are properly managed

### Performance Monitoring
```cpp
// Check if effects are being processed efficiently
if (fxManager.effects.size() > 50) {
    // Too many effects - consider cleanup
}
```

---

## Troubleshooting

### Common Issues

#### Effects Not Rendering
- Ensure FXManager is initialized after renderer
- Check that renderer is properly initialized
- Verify DirectX 11 compatibility

#### Callbacks Not Executing
- Ensure effect completes (progress reaches 1.0)
- Check for proper FXID matching
- Verify thread safety

#### Performance Issues
- Reduce particle counts
- Limit concurrent effects
- Check for infinite duration effects

#### Memory Leaks
- Ensure proper cleanup on application exit
- Check for infinite timeout values
- Monitor callback cleanup

### Debug Output
Enable debug output by defining:
```cpp
#define _DEBUG_FXMANAGER_
```

This provides detailed logging of:
- Effect creation and destruction
- Callback execution
- State changes
- Performance metrics

---

## Code Examples

### Complete Scene Transition
```cpp
void TransitionToNewScene() {
    // Fade to black with callback
    fxManager.FadeOutThenCallback(
        XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f),  // Black
        1.5f,                               // 1.5 second duration
        0.0f,                               // No delay
        []() {
            // Load new scene
            sceneManager.LoadScene("newlevel.scene");
            
            // Fade back in
            fxManager.FadeToImage(1.0f, 0.0f);
        }
    );
}
```

### Dynamic Background System
```cpp
void SetupDynamicBackground() {
    // Layer 1: Distant stars (slow)
    fxManager.StartParallaxLayer(
        BlitObj2DIndexType::IMG_SCROLLBG1,
        FXSubType::ScrollLeft,
        1, 0.2f, 1024, 768, 0.0f, false
    );
    
    // Layer 2: Planets (medium)
    fxManager.StartParallaxLayer(
        BlitObj2DIndexType::IMG_SCROLLBG2,
        FXSubType::ScrollLeft,
        1, 0.6f, 1024, 768, 0.0f, false
    );
    
    // Layer 3: Foreground objects (fast)
    fxManager.StartParallaxLayer(
        BlitObj2DIndexType::IMG_SCROLLBG3,
        FXSubType::ScrollLeft,
        1, 1.4f, 1024, 768, 0.0f, true
    );
}
```

### Interactive Explosion System
```cpp
void HandleExplosion(int x, int y, float intensity) {
    // Calculate particle count based on intensity
    int particles = static_cast<int>(20 + intensity * 30);
    int radius = static_cast<int>(50 + intensity * 100);
    
    // Create explosion
    fxManager.CreateParticleExplosion(x, y, particles, radius);
    
    // Add screen shake with fade
    XMFLOAT4 flashColor(1.0f, 0.8f, 0.6f, 0.3f);
    fxManager.FadeToColor(flashColor, 0.1f, 0.0f);
}
```

### News Ticker System
```cpp
void SetupNewsTicker() {
    std::wstring newsText = L"Latest News: Player achievements unlocked! "
                           L"New content available! "
                           L"Server maintenance scheduled... ";
    
    fxManager.CreateTextScrollerConsistent(
        newsText,
        L"Arial",
        16.0f,
        XMFLOAT4(1.0f, 1.0f, 0.0f, 1.0f),  // Yellow
        0.0f, screenHeight - 30.0f,         // Bottom of screen
        screenWidth, 25.0f,                 // Full width, 25px high
        40.0f,                              // Moderate speed
        FLT_MAX                             // Run forever
    );
}
```

### Starfield Warp Sequence

A two-phase sequence: warp in (default, stars fly past) transitions to arrival (reverse, stars converge to the destination point) via a fade.

```cpp
void BeginWarpSequence(XMFLOAT3 destinationPos) {
    // Phase 1: Engage warp — classic fly-through centred on world origin
    fxManager.CreateStarfield(
        120,
        900.0f,
        1200.0f,
        XMFLOAT3(0.0f, 0.0f, 0.0f)
        // reverse defaults to false
    );
}

void EndWarpSequence(XMFLOAT3 destinationPos) {
    // Stop the fly-through field
    fxManager.StopStarfield();

    // Phase 2: Deceleration — stars converge toward the destination
    fxManager.CreateStarfield(
        120,
        900.0f,
        1200.0f,
        destinationPos,
        true    // reverse: stars travel TO destinationPos
    );

    // Fade to scene once the arrival animation completes
    fxManager.FadeOutThenCallback(
        XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f),
        1.5f,
        2.5f,   // Delay — let the convergence play for 2.5 s first
        [this]() {
            fxManager.StopStarfield();
            sceneManager.LoadNextScene();
        }
    );
}
```

---

This comprehensive guide covers all major features of the FXManager class. For additional support or advanced usage scenarios, refer to the source code documentation and debug output for detailed operation information.