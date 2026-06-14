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
9. [Warp Dot Tunnel Effects](#warp-dot-tunnel-effects)
10. [Text Scroller Effects](#text-scroller-effects)
11. [Loading Screen Text Fade Effect](#loading-screen-text-fade-effect)
12. [ZoomInOut Effects](#zoominout-effects)
13. [Fireworks Effects](#fireworks-effects)
14. [Advanced Features](#advanced-features)
15. [Window Resize Handling](#window-resize-handling)
16. [Thread Safety](#thread-safety)
17. [Performance Considerations](#performance-considerations)
18. [Troubleshooting](#troubleshooting)
19. [Code Examples](#code-examples)

---

## Overview

The FXManager class is a comprehensive visual effects system designed for real-time rendering applications. It provides a queue-based approach to managing multiple visual effects simultaneously, including fade transitions, scrolling backgrounds, particle systems, starfields, and text scrolling effects.

### Key Features
- **Multi-threaded rendering support** with thread-safe operations
- **Queue-based effect management** for complex effect sequences
- **Multiple effect types** including fades, scrolls, particles, starfields, text, 3D warp tunnels, and loading-screen text fade overlays
- **Callback system** for chaining effects and events
- **DirectX 11, Vulkan, and OpenGL integration** — full feature parity across all three renderers
- **Window resize handling** with state preservation
- **Performance optimized** using precalculated math lookup tables for real-time applications

---

## Important Notes

⚠️ **RENDERER COMPATIBILITY NOTE** ⚠️

**The engine currently ships four fully-featured FXManager implementations:**

| Class | File | Renderer |
|---|---|---|
| `FXManager` | `DX_FXManager.h/.cpp` | DirectX 11 |
| `FXManager` | `DX12FXManager.h/.cpp` | DirectX 12 |
| `VKFXManager` | `VULKAN_FXManager.h/.cpp` | Vulkan (Windows/Linux/Android) |
| `GLFXManager` | `OpenGLFXManager.h/.cpp` | OpenGL |

All four implementations provide identical public APIs and support all effect types including that is based in the DX11 FXManager. The global `fxManager` instance is automatically the correct type for the active renderer — game code does not need to branch on the renderer.

**Key DirectX 11 Dependencies (DX path only):**
- Uses `ID3D11Device` and `ID3D11DeviceContext` for rendering operations
- Shader compilation and blend state management via DirectX 11 APIs
- Vertex buffer and constant buffer operations

**Vulkan Notes:**
- Fullscreen fade quad rendered via inline GLSL compiled with shaderc at runtime (falls back gracefully if shaderc is unavailable)
- Pixel-level effects (`Blit2DColoredPixel`) draw to the Direct2D CPU overlay on Windows, or to the CPU rasterizer on Linux/Android
- `VkCommandBuffer` is passed through for future GPU-native effects; current effects do not submit Vulkan draw commands directly

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
- `ScrollUpAndLeft` - Diagonal movement (SE => NW)
- `ScrollUpAndRight` - Diagonal movement (SW => NE)
- `ScrollDownAndLeft` - Diagonal movement (NE => SW)
- `ScrollDownAndRight` - Diagonal movement (NW => SE)

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

## Warp Dot Tunnel Effects

The WarpDotTunnel effect renders a perspective-projected 3D tunnel of rotating dot rings — inspired by the classic *Doctor Who* 
TARDIS title sequence. Rings of dots travel along the Z axis, spinning and drifting in X/Y, creating the illusion of flying through an infinitely deep vortex.

The effect is fully integrated into the resize system and runs on both DirectX 11 (`FXManager`) and Vulkan (`VKFXManager`) with identical behaviour.

---

### Signature

```cpp
void Init3DWarpDOTTunnel(
    float           x,             // Tunnel origin — world X
    float           y,             // Tunnel origin — world Y
    float           z,             // Tunnel origin — world Z (near end)
    float           minRadius,     // Dot-circle radius at the far end  (small)
    float           maxRadius,     // Dot-circle radius at the near end (large)
    TunnelSpinCycle spinCycle,     // None | Clockwise | AntiClockwise
    int             travelSpeed,   // Base speed in world units / second
    bool            reverseTravel, // false = toward camera; true = away from camera
    int             dotsPerCircle, // Dots evenly spread across the full 360° of each ring
    int             density        // Simultaneous rings in flight (1–100)
);
```

---

### Parameter Reference

| Parameter | Type | Description |
|---|---|---|
| `x, y, z` | `float` | World-space origin of the tunnel. `z` is the near (camera-side) end. |
| `minRadius` | `float` | Radius of the dot ring when it is at the far end of the tunnel (small). |
| `maxRadius` | `float` | Radius of the dot ring when it reaches the near end; ring ends its cycle here. |
| `spinCycle` | `TunnelSpinCycle` | Rotational direction applied to every ring each frame. |
| `travelSpeed` | `int` | Base movement speed in world units/second. Actual speed is modulated per frame (see acceleration below). Spin speed is derived as `travelSpeed × 0.05` rad/s. |
| `reverseTravel` | `bool` | `false` — rings move toward the camera (classic warp-in). `true` — rings move away from the camera (warp-out / dive-in). |
| `dotsPerCircle` | `int` | Number of dots placed evenly around the 360° of each ring. Minimum 3. |
| `density` | `int` | Number of rings simultaneously active (clamped 1–100). Rings are staggered at startup so visual density is immediately uniform. |

---

### TunnelSpinCycle Enum

```cpp
enum class TunnelSpinCycle {
    None,           // Rings do not rotate
    Clockwise,      // Rings rotate clockwise when viewed from the camera
    AntiClockwise,  // Rings rotate counter-clockwise when viewed from the camera
};
```

---

### How It Works

#### Travel and Acceleration

The tunnel is 800 world units deep (both DX and VK — `Init3DWarpDOTTunnel` sets `totalDistance = 800` regardless of the struct default). Each ring's normalised travel progress `t` runs from `0.0` (far end) to `1.0` (near/camera end).

- **Forward travel** (`reverseTravel = false`): speed is scaled by `1.0 + t⁴ × 10.0`. The minimum factor of `1.0` guarantees far rings are always visibly moving; the quartic surge near the camera delivers the explosive Doctor Who rush.
- **Reverse travel** (`reverseTravel = true`): speed is scaled by `1.0 + t⁴ × 6.0`. Rings are fastest at the camera-near end and decelerate toward the focal vanishing point — a warp-deceleration or dive-away feel.

When a ring reaches its destination end it instantly resets to the origin end with a new birth position, maintaining the continuous stream.

#### Ring Density and Staggering

At creation, `density` rings are pre-spawned with their Z positions staggered evenly along the tunnel (ring `i` starts at fraction `i / density` of the total distance). Their birth XY offsets are also staggered around the full circle (`fraction × 2π`) so the sidewave weave is visible immediately. This means a density-5 tunnel has rings at 0 %, 20 %, 40 %, 60 %, and 80 % of travel from the first frame — no warm-up period.

#### XY Path (Sidewave Birth Offset)

Each ring receives its XY centre **once at birth** and then travels straight from that point — there is no mid-flight drift. The birth position is sampled from a slowly rotating oscillator that advances every frame:

```
phase   = sideWaveTime × kSideWaveSpeed
bornCx  = startX + kSideWaveRadius × sin(phase)
bornCy  = startY + kSideWaveRadius × cos(phase)
```

Because `sideWaveTime` accumulates each frame independently of any individual ring's travel, consecutive rings are born at slightly different offsets around the circle. The result is a winding tunnel that looks like it curves through space, even though each individual ring always travels in a straight line.

**DX constants** — `kSideWaveRadius = 80.0f`, `kSideWaveSpeed = 0.85 rad/s`.  
**VK constants** — `kSideWaveRadius = 60.0f`, `kSideWaveSpeed = 0.50 rad/s` (slightly narrower, slower sway).

At startup, rings are pre-staggered around the full circle (`fraction × 2π`) so the tunnel already looks like it is weaving from the very first frame — no warm-up period.

#### Camera Look-Ahead

Each frame, rings are sorted by their normalised travel progress (nearest ring first). The camera look-target is set to the ring at sorted index `min(19, ringCount − 1)` — approximately 20 rings ahead of the camera. The target is exponentially smoothed:

```
alpha = 1 − exp(−kCameraSmooth × dt)
smoothLookTarget += (lookRing.position − smoothLookTarget) × alpha
renderer->myCamera.SetTarget(smoothLookTarget)
```

**DX**: `kCameraSmooth = 4.0` (snappier). **VK**: `kCameraSmooth = 3.0` (slightly lazier).  
The exponential filter is framerate-independent and covers ~95 % of the distance in `1 / kCameraSmooth` seconds.

#### Visual Properties

| Property | Far end (t = 0) | Near end (t = 1) |
|---|---|---|
| Ring radius | `minRadius` | `maxRadius` |
| Dot pixel size | 1 px | 4 px |
| Colour | Darkest gray (`kGrayRamp[colorStep]`, min luminance 0.08) | Brightest gray (max luminance 1.0) — each ring cycles its own step |
| Alpha | Fades in from 0 over first 8 % of travel | Fades out to 0 over last 8 % of travel |

Each ring is assigned an independent `colorStep` index (0–7) at birth, cycling through an 8-shade ramp:

```
kGrayRamp = { 0.08, 0.19, 0.30, 0.44, 0.58, 0.72, 0.86, 1.0 }
gray = kGrayRamp[ring.colorStep % kGraySteps]
```

All three channels (R, G, B) are set to `gray`, producing a pure monochrome white tunnel.

---

### Basic Usage

#### Classic Forward Warp Tunnel

```cpp
// 24 dots per ring, 6 rings in flight, clockwise spin
fxManager.Init3DWarpDOTTunnel(
    0.0f, 0.0f, 50.0f,          // Origin at world centre, z=50
    8.0f,                        // minRadius — tiny rings at the far end
    180.0f,                      // maxRadius — large rings near camera
    TunnelSpinCycle::Clockwise,
    100,                         // travelSpeed
    false,                       // forward — rings fly toward camera
    24,                          // dotsPerCircle
    6                            // density
);
```

#### Reverse Warp (Dive Away)

```cpp
// Rings start large at camera, shrink as they recede — "diving into the tunnel"
fxManager.Init3DWarpDOTTunnel(
    0.0f, 0.0f, 50.0f,
    8.0f,
    200.0f,
    TunnelSpinCycle::AntiClockwise,
    120,
    true,                        // reverseTravel — rings travel away from camera
    32,
    8
);
```

#### Dense Intense Warp

```cpp
// High density and many dots per ring for an intense vortex
fxManager.Init3DWarpDOTTunnel(
    0.0f, 0.0f, 20.0f,
    4.0f,
    250.0f,
    TunnelSpinCycle::Clockwise,
    150,
    false,
    48,
    20                           // 20 rings simultaneously
);
```

#### No Spin — Pure Tunnel

```cpp
fxManager.Init3DWarpDOTTunnel(
    0.0f, 0.0f, 0.0f,
    6.0f, 160.0f,
    TunnelSpinCycle::None,       // no rotation
    80, false, 24, 5
);
```

---

### Stopping the Tunnel

```cpp
fxManager.StopWarpDotTunnel();
```

```cpp
// Check whether the tunnel is active before stopping
if (fxManager.tunnelID > 0) {
    fxManager.StopWarpDotTunnel();
}
```

---

### Render Integration

The tunnel renders automatically once started — no per-frame calls are needed in your game code. Internally:

- `Render()` calls `UpdateWarpDotTunnel()` each frame to advance ring positions and spin angles.
- `RenderFX(fxManager.tunnelID, context, viewMatrix)` is called from the renderer's render loop (already wired in `DXRenderFrame.cpp` and `VULKAN_RenderFrame.cpp`).

The tunnel uses the camera's view and projection matrices for perspective projection and `Blit2DColoredPixel` to draw each dot, exactly like the Starfield effect.

---

### Resize Handling

The tunnel is automatically stopped before a DirectX/Vulkan resource recreation and its active state is preserved in `ActiveFXState.tunnelActive`. No additional code is required in your resize handler. The tunnel's `sideWaveTime` accumulator and `smoothLookTarget` are reset each time `Init3DWarpDOTTunnel` is called, so a post-resize restart begins cleanly. If you restart effects manually after resize, check:

```cpp
if (savedFXState.tunnelActive) {
    // Re-call Init3DWarpDOTTunnel with the original parameters
}
```

---

### Warp Tunnel Features

- **Perspective projection** — dots are transformed through the camera view/projection matrix; near rings appear large and bright, far rings small and dim
- **Quartic acceleration profile** — forward travel: `1.0 + t⁴ × 10`; reverse: `1.0 + t⁴ × 6`; minimum factor 1.0 keeps far rings always visibly moving; quartic surge near camera gives the classic warp rush
- **Density staggering** — rings pre-distributed at startup around the birth circle so the tunnel looks like it is already weaving from frame one; no warm-up period
- **Sidewave birth offset** — each ring gets a fixed XY birth position sampled from a slowly rotating oscillator (`kSideWaveRadius × sin/cos(sideWaveTime × kSideWaveSpeed)`); rings travel straight from there — the winding look is produced by the varying birth offsets, not mid-flight drift
- **Smooth camera look-ahead** — each frame the camera target is set to the ring ~20 positions ahead (nearest-first sort), exponentially smoothed at `kCameraSmooth` for a gliding follow rather than a snap
- **Sequential gray ramp** — each ring cycles independently through an 8-shade monochrome ramp (0.08–1.0 luminance) giving the tunnel a clean white-dot Doctor Who aesthetic
- **Per-dot spin** — each ring accumulates a rotation offset every frame; spin speed tied to `travelSpeed`
- **Depth-based visual scaling** — radius, dot size, and alpha all interpolate smoothly with depth progress
- **Continuous looping** — rings wrap to origin with a fresh birth offset automatically; effect runs until `StopWarpDotTunnel()` is called
- **Resize safe** — active state saved and restored across window resize events
- **Vulkan parity** — `VKFXManager` provides identical API and behaviour (slightly narrower sidewave and lazier camera smooth by default)

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

## Loading Screen Text Fade Effect

The `TextFadeInOut` effect displays styled text on the loading screen that smoothly fades in from a start color and fades out to a target color. It is designed for use during loading cycles so players can see real-time status messages without the screen appearing frozen.

### How It Works

Text effects progress through four phases managed by the `TextFadePhase` state machine:

| Phase | Description |
|---|---|
| `FadeIn` | Alpha/color lerps from `startColor` to `endColor` over `fadeInDuration` seconds |
| `Holding` | Text is fully visible at `endColor`; waits for `displayDuration` seconds (or forever if -1) |
| `FadeOut` | Color lerps from `endColor` to `fadeOutColor` over `fadeOutDuration` seconds |
| `Stopped` | Effect is marked complete and removed automatically by `RemoveCompletedEffects()` |

Calling `ShowLoadingText()` while a previous message is visible triggers a **cross-fade**: the old text starts its FadeOut and the new text begins with a `pendingDelay` equal to the remaining fade-out time so messages never overlap.

Calling `StopLoadingText()` sets `immediateStop = true`, which skips any remaining fade and marks all TextFadeInOut effects as Stopped in the next update.

---

### TextRenderStyle — Font Attributes

Defined in `Renderer.h`. Pass a pointer to `ShowLoadingText()` to override the default loading-screen font.

```cpp
struct TextRenderStyle {
    std::wstring fontName      = L"Arial";
    float        fontSize      = 16.0f;
    bool         bold          = false;
    bool         italic        = false;
    bool         underline     = false;
    bool         strikethrough = false;
};
```

---

### Platform-Resolved Default Font — `LoadingTextFX` Namespace

The `LoadingTextFX` namespace (defined in each `*FXManager.h`) resolves the default font at compile time based on the target platform:

| Platform | Font |
|---|---|
| Windows | `Segoe UI` |
| Android | `Roboto` |
| iOS / tvOS | `SF Pro Text` |
| macOS | `Helvetica Neue` |
| Linux | `Sans` |
| Fallback | `Arial` |

```cpp
// Example — access the compile-time resolved font name
std::wstring platformFont = LoadingTextFX::kDefaultFont;  // e.g. L"Segoe UI" on Windows

// Default style constants also available:
constexpr float kDefaultFontSize   = 18.0f;
constexpr bool  kDefaultBold       = false;
constexpr bool  kDefaultItalic     = false;
constexpr bool  kDefaultUnderline  = false;
```

---

### `TextFadeData` — Internal Effect Data

Each TextFadeInOut `FXItem` carries a `TextFadeData` struct (or `VKTextFadeData` / `GLTextFadeData` for the other renderers). Fields you control via `ShowLoadingText()`:

| Field | Type | Description |
|---|---|---|
| `text` | `std::wstring` | The string to display |
| `fontStyle` | `TextRenderStyle` | Full font attributes (see above) |
| `startColor` | `XMFLOAT4` | Color at start of fade-in (default: transparent black) |
| `endColor` | `XMFLOAT4` | Fully-visible color (default: opaque white) |
| `fadeOutColor` | `XMFLOAT4` | Color at end of fade-out (default: transparent black) |
| `posX`, `posY` | `float` | Screen position; -1.0f = auto (bottom-left of window) |
| `fadeInDuration` | `float` | Seconds for fade-in transition (default: 0.5s) |
| `fadeOutDuration` | `float` | Seconds for fade-out transition (default: 0.3s) |
| `displayDuration` | `float` | Seconds to hold fully visible; -1.0f = hold forever until replaced |
| `pendingDelay` | `float` | Cross-fade wait (set automatically by `ShowLoadingText()`) |
| `phase` | `TextFadePhase` | Current state-machine phase (managed internally) |
| `immediateStop` | `bool` | When true, skips fade and marks effect Stopped immediately |

---

### API Reference

#### `ShowLoadingText()`

```cpp
int ShowLoadingText(
    const std::wstring& text,
    XMFLOAT4 endColor          = { 1.0f, 1.0f, 1.0f, 1.0f },  // opaque white
    float    fadeInDuration    = 0.5f,
    float    fadeOutDuration   = 0.3f,
    XMFLOAT4 startColor        = { 0.0f, 0.0f, 0.0f, 0.0f },  // transparent black
    float    posX              = -1.0f,   // -1 = auto bottom-left
    float    posY              = -1.0f,   // -1 = auto bottom-left
    const TextRenderStyle* fontStyle = nullptr  // nullptr = use LoadingTextFX defaults
);
```

Returns the `fxID` of the newly created effect. The previous message (if any) automatically begins fading out, and the new text starts after the cross-fade delay.

#### `StopLoadingText()`

```cpp
void StopLoadingText();
```

Immediately stops all active TextFadeInOut effects. No fade-out is performed. Effects are removed on the next `RemoveCompletedEffects()` pass.

#### `RenderLoadingText()`

```cpp
void RenderLoadingText();
```

Updates timing and renders all active TextFadeInOut effects. **Must be called every frame from inside an active Direct2D `BeginDraw`/`EndDraw` context.** The correct place is inside `RenderBackgroundImage()` during loading scene branches.

> ⚠️ Do **not** call `RenderLoadingText()` after `EndDraw()` — the D2D context must be open when this executes.

---

### Integration — Where to Call `RenderLoadingText()`

`RenderLoadingText()` is integrated in `RenderBackgroundImage()` for both the DX11 and Vulkan renderers. Add it inside the loading-screen branch so it runs every frame during loading:

**DX11 (`DXRenderFrame.cpp`)**
```cpp
// Inside RenderBackgroundImage(), loading scene branch:
if (!threadManager.threadVars.bLoaderTaskFinished.load()) {
    Blit2DObjectToSize(BlitObj2DIndexType::IMG_LOADING, ...);
    fxManager.RenderLoadingText();  // Draw loading-screen text overlay
}
```

**Vulkan (`VULKAN_RenderFrame.cpp`)**
```cpp
// Inside the loading ring animation block:
if (!threadManager.threadVars.bLoaderTaskFinished.load()) {
    // ... ring animation ...
    fxManager.RenderLoadingText();  // Loading-screen text fade overlay
}
```

---

### Usage Examples

#### Basic Loading Status Messages

```cpp
// Show initial loading message
fxManager.ShowLoadingText(L"Initializing engine systems...");

// Later, update with a new message — old text fades out, new fades in automatically
fxManager.ShowLoadingText(L"Loading assets...");

// Final message with longer hold
fxManager.ShowLoadingText(
    L"Almost ready!",
    { 1.0f, 1.0f, 1.0f, 1.0f },  // White
    0.5f,                          // Fade in over 0.5s
    0.3f,                          // Fade out over 0.3s
    { 0.0f, 0.0f, 0.0f, 0.0f },   // Start transparent
    -1.0f, -1.0f                   // Auto-position (bottom-left)
);
```

#### Colored Status with Custom Font

```cpp
TextRenderStyle warningStyle;
warningStyle.fontName  = L"Segoe UI";
warningStyle.fontSize  = 20.0f;
warningStyle.bold      = true;
warningStyle.italic    = false;

fxManager.ShowLoadingText(
    L"Compiling shaders — this may take a moment...",
    { 1.0f, 0.85f, 0.0f, 1.0f },  // Yellow
    0.4f,                           // Fast fade-in
    0.3f,                           // Fast fade-out
    { 0.0f, 0.0f, 0.0f, 0.0f },    // Start transparent
    20.0f, -1.0f,                   // 20px from left, auto vertical
    &warningStyle
);
```

#### Persistent Message (hold forever until replaced)

```cpp
// displayDuration defaults to -1 (hold forever) — text stays until StopLoadingText() or
// a new ShowLoadingText() call pushes it out
fxManager.ShowLoadingText(
    L"Connecting to server...",
    { 0.6f, 1.0f, 0.6f, 1.0f }  // Soft green
);
```

#### Immediate Stop When Loading Completes

```cpp
// Loading thread signals completion:
fxManager.StopLoadingText();

// Then transition to gameplay
fxManager.FadeToImage(0.8f, 0.0f);
sceneManager.SetScene(SCENE_GAMEPLAY);
```

#### Sequential Update Messages During a Loading Pipeline

```cpp
// Show a sequence of status messages during an async load:
void OnLoadStageChanged(const std::wstring& stageName) {
    // Cross-fade automatically handles the transition between messages
    fxManager.ShowLoadingText(
        stageName,
        { 1.0f, 1.0f, 1.0f, 1.0f },   // White
        0.25f,                           // Very fast fade-in for quick updates
        0.2f,                            // Very fast fade-out
        { 0.0f, 0.0f, 0.0f, 0.0f }
    );
}

// Call from the loading thread (thread-safe queue):
OnLoadStageChanged(L"Loading textures...");
OnLoadStageChanged(L"Building navigation mesh...");
OnLoadStageChanged(L"Spawning entities...");
```

---

### Renderer Notes

| Renderer | Bold/Italic | Underline/Strikethrough | Platform |
|---|---|---|---|
| DX11 (`FXManager`) | IDWriteTextFormat weight/style | IDWriteTextLayout range flags | Windows only |
| Vulkan (`VKFXManager`) | IDWriteTextFormat (Windows); stub on Linux/Android | IDWriteTextLayout (Windows) | Windows + Linux + Android |
| OpenGL (`GLFXManager`) | GDI `CreateFontW` FW_BOLD / italic BOOL | GDI underline/strikethrough BOOL → DIB texture | Windows; fallback to `DrawMyTextWithFont` on Linux/Android |

---

## ZoomInOut Effects

The ZoomInOut FX applies a pulsing center-crop zoom to a 2D blit image and/or adjusts the 3D camera FOV. It bounces continuously between 0% and a configurable maximum depth until stopped, producing a heartbeat-style zoom loop.

### ZoomInOut — How It Works

- **2D path** — the FXManager intercepts the normal `Blit2DObjectToSize` call for the linked image. On each frame it crops from the image center by the current zoom factor, then scales the cropped region back to the original destination rect using `Blit2DCenteredZoom`. The RenderFrame skips the normal blit for that image while the FX is active.
- **3D path** — `GetCurrent3DZoomFactor()` returns the live zoom level so the render pipeline can narrow the camera FOV proportionally.
- **Bounce loop** — the zoom animates in to `depth`, reverses outward to 0, then repeats. Calling `StopZooming()` sets a flag; the effect completes its current outward journey before removing itself cleanly.

### ZoomFXFunction Enum

| Value | Description |
|---|---|
| `ZoomFXFunction::Zoom2D` | Zoom the linked 2D blit image only |
| `ZoomFXFunction::Zoom3D` | Adjust 3D camera FOV only |
| `ZoomFXFunction::ZoomBoth` | Apply zoom to both 2D image and 3D camera |

### ZoomData / GLZoomData / VKZoomData Struct

| Field | Type | Description |
|---|---|---|
| `function` | `ZoomFXFunction` | Which dimension(s) are zoomed |
| `depth` | `float` | Maximum zoom depth, clamped 0.0–0.75 (fraction of image dims) |
| `speed` | `float` | Zoom speed in units per second |
| `link2DImg` | `int` | `BlitObj2DIndexType` cast to int; -1 = unused |
| `currentZoomLevel` | `float` | Live zoom factor (managed internally) |
| `zoomingIn` | `bool` | Internal bounce direction flag |
| `stopRequested` | `bool` | Set by `StopZooming()`; clears after outward journey |
| `destX/Y/W/H` | `int` | Destination rect for the zoomed blit |

### ZoomInOut — API Reference

#### `ZoomInitialise`

```cpp
void ZoomInitialise(ZoomFXFunction function, float depth, float speed,
                    int link2DImg = -1,
                    int destX = 0, int destY = 0, int destW = 0, int destH = 0);
```

Stores the zoom configuration ready for `StartZoom()`. `depth` is clamped to `[0.0, 0.75]`. `link2DImg` is the integer value of the `BlitObj2DIndexType` enum entry for the image to zoom.

#### `StartZoom`

```cpp
void StartZoom(float speed = 0.0f);
```

Creates and activates the ZoomInOut effect using the stored configuration. Pass `speed > 0` to override the speed set in `ZoomInitialise`. Stops any currently running zoom first. Sets `fxManager.zoomID` to the new effect ID.

#### `StopZooming`

```cpp
void StopZooming();
```

Signals the active zoom to stop cleanly. The effect continues its current outward journey to zero before being removed — there is no abrupt pop.

#### `IsImageZoomActive`

```cpp
bool IsImageZoomActive(int imgID) const;
```

Returns `true` when a ZoomInOut effect is actively zooming the specified image ID. Used by the RenderFrame alongside `RenderZoomedImage` to decide which path to take at each image blit.

#### `RenderZoomedImage`

```cpp
void RenderZoomedImage(int imgID, int destX, int destY, int destW, int destH);
```

Blits the zoomed version of `imgID` at the exact render-order position where the normal `Blit2DObjectToSize` call would occur. Called directly from the RenderFrame when `IsImageZoomActive` returns `true`. The dest parameters must match those that would be passed to `Blit2DObjectToSize` so the zoomed image occupies the same screen area. Internally finds the active ZoomInOut effect for `imgID` and calls `Blit2DCenteredZoom` with the current zoom level.

This design ensures the zoomed image is composited in the correct frame order — after any background content that precedes it and before any 3D starfield or overlay that follows, exactly preserving the visual layering of the original scene.

#### `GetCurrent3DZoomFactor`

```cpp
float GetCurrent3DZoomFactor() const;
```

Returns the live zoom level (0.0–depth) for any active 3D or BOTH zoom effect. Returns 0.0 when no 3D zoom is active.

### Basic Usage — 2D Image Zoom

```cpp
// Zoom the game-title background image with a 30% max depth at medium speed
fxManager.ZoomInitialise(
    ZoomFXFunction::Zoom2D,       // 2D image only
    0.30f,                        // max zoom depth (30%)
    0.25f,                        // speed (units/sec)
    int(BlitObj2DIndexType::IMG_GAMEINTRO1),  // image to zoom
    0, 0, iOrigWidth, iOrigHeight // destination rect
);
fxManager.StartZoom();            // begin the bounce loop
```

To stop it later:
```cpp
fxManager.StopZooming();          // completes current outward journey then removes
```

### Basic Usage — 3D Camera Zoom

```cpp
// Pulse the 3D scene FOV with 20% depth
fxManager.ZoomInitialise(ZoomFXFunction::Zoom3D, 0.20f, 0.15f);
fxManager.StartZoom();
```

Then in the render pipeline, read `GetCurrent3DZoomFactor()` and apply it to the camera FOV:
```cpp
float z = fxManager.GetCurrent3DZoomFactor();  // 0.0 = no zoom
float fovDeg = baseFOV * (1.0f - z * 0.5f);   // e.g. tighten FOV by half the depth
myCamera.SetFOV(fovDeg);
```

### Basic Usage — Combined 2D + 3D

```cpp
fxManager.ZoomInitialise(
    ZoomFXFunction::ZoomBoth,
    0.25f, 0.20f,
    int(BlitObj2DIndexType::IMG_GAMEINTRO1),
    0, 0, iOrigWidth, iOrigHeight
);
fxManager.StartZoom(0.30f);       // override speed at start time
```

### Stopping with a Speed Override

```cpp
// Existing zoom runs at 0.25 speed; stop it and restart faster
fxManager.StopZooming();
fxManager.ZoomInitialise(ZoomFXFunction::Zoom2D, 0.30f, 0.50f,
    int(BlitObj2DIndexType::IMG_GAMEINTRO1),
    0, 0, iOrigWidth, iOrigHeight);
fxManager.StartZoom();
```

### RenderFrame Integration

Each `Blit2DObjectToSize` call in the RenderFrame is replaced by an `IsImageZoomActive` / `RenderZoomedImage` if-else branch. When zoom is active the zoomed version is blitted at exactly the same render-order position — preserving layer ordering with effects like the 3D starfield that follow. No game-code changes are needed:

```cpp
// Pattern used in all four RenderFrame files — game code does not need to change this:
if (m_d2dTextures[int(BlitObj2DIndexType::IMG_GAMEINTRO1)]) {
    if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_GAMEINTRO1)))
        fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_GAMEINTRO1), 0, 0, w, h);
    else
        Blit2DObjectToSize(BlitObj2DIndexType::IMG_GAMEINTRO1, 0, 0, w, h);
}
```

`Render2D()` still runs `UpdateZoomInOut()` each frame to advance the bounce animation, but no longer calls `ApplyZoom2D()` for the 2D blit — that is handled entirely at the call site above.

### ZoomInOut Features

- **Bounce loop** — automatically reverses at max depth and 0%; no manual control needed
- **Clean stop** — `StopZooming()` never produces a visual pop; always exits at zoom = 0
- **Simultaneous 2D + 3D** — `ZoomBoth` animates both the blit image crop and the camera FOV in lock-step
- **Correct render order** — `RenderZoomedImage()` is called at the exact position of the normal blit, so effects like the 3D starfield that follow in the frame still render on top as expected
- **Per-image guard** — `IsImageZoomActive(imgID)` returns false for any image not linked to the active zoom, so other blits are unaffected
- **Speed override** — `StartZoom(speed)` lets callers override the speed at the point of activation without recalling `ZoomInitialise`

---

## Fireworks Effects

The Fireworks effect launches coloured rockets upward from the bottom of the screen. Each rocket travels to a random target height (35%–75% of the screen height from the base), then triggers a particle burst. Up to 10 rockets are active simultaneously; new ones fire at the interval set by `freqRate`. The effect renders on the same 2D overlay layer as the starfield via `Render2D()`.

### API

| Method | Description |
| --- | --- |
| `StartFireworks(float freqRate)` | Begin the effect. `freqRate` is seconds between each new rocket launch (minimum 0.1 s). The first rocket fires immediately. |
| `StopFireworks()` | Stop immediately and remove all rockets/particles. |

The manager also exposes `int fireworksID` (0 when inactive), which mirrors `starfieldID` and `tunnelID` in purpose.

### Behaviour Details

| Property | Value |
| --- | --- |
| Max simultaneous rockets | 10 |
| Launch X | Random across full screen width |
| Target height | 35%–75% of screen height from the bottom base |
| Rocket speed | 2–8 px/frame (random per rocket) |
| Rocket colour | Random RGB per rocket |
| Explosion radius | 10–100 px (random per rocket) |
| Particle count | 1–15 per explosion (random) |
| Burst colour | One shared random RGB per explosion |
| Particle initial speed | 1 px/step at origin |
| Particle max speed | 5 px/step at max radius (linear acceleration) |
| Particle alpha fade | Quadratic: `alpha = 1 - (radius/maxRadius)^2` |

### Fireworks Basic Usage

```cpp
// Start fireworks launching one rocket every 0.5 seconds
fxManager.StartFireworks(0.5f);

// Stop the fireworks
fxManager.StopFireworks();
```

### Slow Ceremonial Display

```cpp
// One rocket every 2 seconds — grand finale pacing
fxManager.StartFireworks(2.0f);
```

### Rapid Burst Display

```cpp
// New rocket every 0.2 seconds — dense celebration effect
fxManager.StartFireworks(0.2f);
```

### With a Fade-Out Sequence

```cpp
// Start fireworks, then fade to black and stop after 5 seconds
fxManager.StartFireworks(0.4f);

fxManager.FadeOutThenCallback(
    XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f),
    1.5f,   // Fade duration
    5.0f,   // Delay — let fireworks run for 5 s first
    [this]() {
        fxManager.StopFireworks();
        sceneManager.LoadNextScene();
    }
);
```

### Fireworks Renderer Notes

- All four renderers (DX11, DX12, OpenGL, Vulkan) implement identical behaviour.
- Fireworks are rendered via `Render2D()` using `renderer->Blit2DColoredPixel()` — no shader changes required.
- `StopAllFX()` and `StopAllFXForResize()` automatically stop and save/restore fireworks state.
- `fireworksID` is reset to 0 when stopped. Check `fireworksID > 0` to test whether fireworks are running.

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

3. **Warp Dot Tunnel**
   - `dotsPerCircle × density` `Blit2DColoredPixel` calls are issued every frame — keep `density ≤ 20` and `dotsPerCircle ≤ 48` for comfortable frame budgets
   - Only one tunnel can be active at a time (`tunnelID` is a single slot); call `StopWarpDotTunnel()` before calling `Init3DWarpDOTTunnel` again to change parameters
   - Birth-offset calculations use plain `sinf`/`cosf` but are called only once per ring per wrap (not per dot per frame)
   - The camera look-ahead sort is O(density log density) per frame; negligible for density ≤ 100

4. **Text Scrollers**
   - Limit number of concurrent text effects
   - Use shorter text strings when possible
   - Optimize font rendering

5. **Memory Management**
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

### Loading Screen Text Fade Pipeline

A realistic loading sequence that pushes status messages through `ShowLoadingText()` and stops cleanly when the load is complete.

```cpp
// Called from the loading thread (via a thread-safe message queue):
void PushLoadingStatus(const std::wstring& message) {
    // Cross-fade is handled automatically — just call ShowLoadingText() again
    fxManager.ShowLoadingText(
        message,
        { 1.0f, 1.0f, 1.0f, 1.0f },   // Opaque white
        0.25f,                           // Fast fade-in (quick updates)
        0.2f,                            // Fast fade-out
        { 0.0f, 0.0f, 0.0f, 0.0f }      // Start transparent
    );
}

void RunLoadingPipeline() {
    PushLoadingStatus(L"Initializing engine systems...");
    LoadEngineSystems();

    PushLoadingStatus(L"Loading world geometry...");
    LoadWorldGeometry();

    PushLoadingStatus(L"Streaming textures...");
    StreamTextures();

    PushLoadingStatus(L"Spawning entities...");
    SpawnEntities();

    // Stop the overlay — no fade-out, just remove immediately
    fxManager.StopLoadingText();

    // Then transition to gameplay
    fxManager.FadeToImage(0.8f, 0.0f);
    sceneManager.SetScene(SCENE_GAMEPLAY);
}
```

### Warp Dot Tunnel Sequences

#### Simple Doctor Who Intro Tunnel

```cpp
void StartWarpTunnel() {
    fxManager.Init3DWarpDOTTunnel(
        0.0f, 0.0f, 50.0f,          // world origin, z=50
        6.0f,                        // tiny rings at the far end
        200.0f,                      // large rings near the camera
        TunnelSpinCycle::Clockwise,
        120,                         // travel speed
        false,                       // forward — rings fly toward camera
        32,                          // dots per ring
        8                            // 8 rings simultaneously
    );
}

void StopWarpTunnel() {
    if (fxManager.tunnelID > 0)
        fxManager.StopWarpDotTunnel();
}
```

#### Tunnel → Scene Transition

Starts a warp tunnel during loading, then fades to the new scene when ready.

```cpp
void EnterWarpAndLoad(std::function<void()> loadCallback) {
    // Kick off the tunnel immediately
    fxManager.Init3DWarpDOTTunnel(
        0.0f, 0.0f, 50.0f,
        6.0f, 200.0f,
        TunnelSpinCycle::Clockwise,
        120, false, 32, 8
    );

    // After 3 seconds fade to black, run the load callback, then stop the tunnel
    fxManager.FadeOutThenCallback(
        XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f),  // fade to black
        1.5f,                                // fade duration
        3.0f,                                // delay — let tunnel play first
        [loadCallback]() {
            fxManager.StopWarpDotTunnel();
            loadCallback();                  // load the next scene
            fxManager.FadeToImage(1.5f, 0.0f);
        }
    );
}
```

#### Reverse Tunnel — Warp Deceleration / Arrival

```cpp
void WarpArrival() {
    // Reverse: rings start large at camera and shrink into the distance
    fxManager.Init3DWarpDOTTunnel(
        0.0f, 0.0f, 50.0f,
        5.0f,                        // minRadius (far, end state)
        220.0f,                      // maxRadius (near, start state for reverse)
        TunnelSpinCycle::AntiClockwise,
        140,
        true,                        // reverseTravel — deceleration feel
        28,
        10
    );

    // Stop after 4 seconds and transition
    fxManager.FadeOutThenCallback(
        XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f),
        1.0f, 4.0f,
        []() {
            fxManager.StopWarpDotTunnel();
            sceneManager.BeginGameplay();
        }
    );
}
```

#### Dense Intense Vortex

```cpp
void MaxIntensityVortex() {
    fxManager.Init3DWarpDOTTunnel(
        0.0f, 0.0f, 10.0f,
        3.0f,                        // very small far rings
        240.0f,
        TunnelSpinCycle::Clockwise,
        180,                         // high speed
        false,
        48,                          // many dots per ring
        16                           // many rings at once
    );
}
```

---

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

This comprehensive guide covers all major features of the FXManager class. For additional support or advanced usage scenarios, refer to the source files listed below and enable `_DEBUG_FXMANAGER_` for detailed runtime logging.

### Source File Reference

| File | Purpose |
|---|---|
| [`DX_FXManager.h`](../DX_FXManager.h) | DirectX 11 FXManager — types, structs, class declaration |
| [`DX_FXManager.cpp`](../DX_FXManager.cpp) | DirectX 11 FXManager — full implementation |
| [`VULKAN_FXManager.h`](../VULKAN_FXManager.h) | Vulkan VKFXManager — types, structs, class declaration |
| [`VULKAN_FXManager.cpp`](../VULKAN_FXManager.cpp) | Vulkan VKFXManager — full implementation |
| [`OpenGLFXManager.h`](../OpenGLFXManager.h) | OpenGL GLFXManager — types, structs, class declaration |
| [`OpenGLFXManager.cpp`](../OpenGLFXManager.cpp) | OpenGL GLFXManager — full implementation |
| [`MathPrecalculation.h`](../MathPrecalculation.h) | `FAST_MATH` lookup tables used by tunnel and starfield |
