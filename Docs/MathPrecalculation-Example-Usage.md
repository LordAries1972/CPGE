# MathPrecalculation Class - Comprehensive Usage Example

## Overview

This comprehensive example demonstrates the complete usage of the MathPrecalculation class in various gaming scenarios. The MathPrecalculation class provides optimized mathematical operations through lookup tables and fast algorithms, significantly improving performance in real-time gaming applications.

## Table of Contents

1. [Key Features Demonstrated](#key-features-demonstrated)
   - [Important Notes](#important-notes)
   
2. [Basic Usage Examples](#basic-usage-examples)
   - [Vector-Math Tables (new)](#vector-math-functions-new)
3. [Advanced Gaming Scenarios](#advanced-gaming-scenarios)
4. [Performance Considerations](#performance-considerations)
5. [Integration Examples](#integration-examples)
6. [Complete Code Example](#complete-code-example)

## Key Features Demonstrated

### Important Notes

⚠️ **DIRECTX WARNING** ⚠️

**This has some usage of DirectX references within this systems and is NOT safe-guarded properly. It requires further development and to work on all OS platforms (This will be done very soon)**

### 1. Basic Trigonometric Functions
- Usage of all fast trigonometric macros (`FAST_SIN`, `FAST_COS`, `FAST_TAN`)
- Inverse trigonometric functions (`FAST_ASIN`, `FAST_ACOS`, `FAST_ATAN`, `FAST_ATAN2`)
- Simultaneous sin/cos calculations for efficiency
- Performance comparison with standard math functions

### 2. Color Conversion Systems
- RGB to YUV and YUV to RGB conversions for video processing
- Gamma correction for proper color display
- Float-based color conversions for shader usage
- Fast color clamping operations

### 3. Interpolation Functions
- All interpolation types: Linear, SmoothStep, SmootherStep
- Easing functions: EaseIn, EaseOut, EaseInOut
- Animation curve comparisons with visual feedback

### 4. Particle System Integration
- Precalculated particle directions for explosions
- Particle velocity calculations using fast math
- Real-time particle physics simulation
- Distance calculations and vector normalization

### 5. Matrix Transformations
- 3D entity transformation matrices
- Camera view matrix calculations
- Fast matrix multiplication operations
- Scale, rotation, and translation combinations

### 6. Text Rendering Optimizations
- Character width calculations for text layout
- Transparency calculations for scrolling text effects
- Font rendering performance optimizations

### 7. Advanced Gaming Scenarios
- Complete game loop integration
- Multi-entity management systems
- Lighting calculations with multiple light sources
- Real-time animation systems

### 8. Vector-Math Tables (used by `MyVector` / `MyQuaternion`)
- `FastSinCosDeg` / `FAST_SINCOS_DEG`: sine and cosine of an angle in **degrees**, interpolated, one lookup
- `FastACosAccurate` / `FAST_ACOS_ACCURATE`: arccosine that stays accurate for nearly parallel / opposite vectors
- `GetUnitCircle` / `FAST_UNIT_CIRCLE`: precalculated circle points for building prisms, cones and discs
- `GetUnitSphereDirection` / `FAST_UNIT_SPHERE`: a random direction on the sphere in one table load (explosion bursts)
- `RegisterPowerCurve` + `FastPowCurve`: `t^exponent` easing from an interpolated table instead of `std::pow`
- `FastInvInt` / `FAST_INV_INT`: `1/n` from a compile-time table (per-face averages, per-segment steps)
- See [Vector-Math Functions (new)](#vector-math-functions-new) and `Vectors-Example-Usage.md`

## Basic Usage Examples

### Initializing the MathPrecalculation System

```cpp
#include "MathPrecalculation.h"
#include "Debug.h"

// Initialize the MathPrecalculation singleton
if (!FAST_MATH.Initialize())
{
    debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Failed to initialize MathPrecalculation!");
    return -1;
}

// Validate lookup tables before usage
if (!FAST_MATH.ValidateTables())
{
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Lookup table validation failed!");
    return -2;
}
```

### Trigonometric Functions

```cpp
void DemonstrateTrigonometricFunctions()
{
    // Test angles for demonstration
    std::vector<float> testAngles = { 0.0f, XM_PIDIV4, XM_PIDIV2, XM_PI, 3.0f * XM_PIDIV2, 2.0f * XM_PI };
    
    for (float angle : testAngles)
    {
        // Using the convenient macros for fast trigonometric calculations
        float fastSin = FAST_SIN(angle);        // Fast sine lookup
        float fastCos = FAST_COS(angle);        // Fast cosine lookup
        float fastTan = FAST_TAN(angle);        // Fast tangent lookup
        
        // Using the class methods directly for inverse trigonometric functions
        float fastAsin = FAST_ASIN(fastSin);    // Fast arcsine lookup
        float fastAcos = FAST_ACOS(fastCos);    // Fast arccosine lookup
        float fastAtan = FAST_ATAN(fastTan);    // Fast arctangent lookup
        
        // Demonstrate simultaneous sin/cos calculation for efficiency
        float sinResult, cosResult;
        FAST_MATH.FastSinCos(angle, sinResult, cosResult);  // Get both values in single call
    }
    
    // Demonstrate ATan2 for angle calculation from coordinates
    std::vector<XMFLOAT2> testPoints = { 
        XMFLOAT2(1.0f, 1.0f),   // 45 degrees
        XMFLOAT2(-1.0f, 1.0f),  // 135 degrees
        XMFLOAT2(-1.0f, -1.0f), // -135 degrees
        XMFLOAT2(1.0f, -1.0f)   // -45 degrees
    };
    
    for (const XMFLOAT2& point : testPoints)
    {
        float angle = FAST_ATAN2(point.y, point.x);     // Calculate angle from coordinates
        float degrees = angle * 180.0f / XM_PI;         // Convert to degrees for display
    }
}
```

### Color Conversion Functions

```cpp
void DemonstrateColorConversions()
{
    // Test color values for demonstration
    std::vector<std::tuple<uint8_t, uint8_t, uint8_t>> testColors = {
        {255, 0, 0},    // Pure red
        {0, 255, 0},    // Pure green
        {0, 0, 255},    // Pure blue
        {255, 255, 255}, // White
        {128, 128, 128}, // Gray
        {0, 0, 0}       // Black
    };
    
    for (const auto& color : testColors)
    {
        uint8_t r = std::get<0>(color);
        uint8_t g = std::get<1>(color);
        uint8_t b = std::get<2>(color);
        
        // Convert RGB to YUV using fast conversion
        uint8_t y, u, v;
        FAST_MATH.FastRgbToYuv(r, g, b, y, u, v);
        
        // Convert back from YUV to RGB to verify accuracy
        uint8_t convertedR, convertedG, convertedB;
        FAST_MATH.FastYuvToRgb(y, u, v, convertedR, convertedG, convertedB);
        
        // Demonstrate gamma correction
        uint8_t gammaCorrectedR = FAST_MATH.FastGammaCorrect(r, 2.2f);
        uint8_t gammaCorrectedG = FAST_MATH.FastGammaCorrect(g, 2.2f);
        uint8_t gammaCorrectedB = FAST_MATH.FastGammaCorrect(b, 2.2f);
    }
    
    // Demonstrate float-based YUV to RGB conversion for shader usage
    XMFLOAT4 floatColor = FAST_MATH.FastYuvToRgbFloat(0.5f, 0.3f, 0.7f);
}
```

### Interpolation Functions

```cpp
void DemonstrateInterpolationFunctions()
{
    const float startValue = 0.0f;
    const float endValue = 100.0f;
    const int steps = 11;  // 0% to 100% in 10% increments
    
    for (int i = 0; i <= steps; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(steps);
        
        // Demonstrate all interpolation types using the class methods
        float linearResult = FAST_LERP(startValue, endValue, t);                    // Linear interpolation macro
        float smoothResult = FAST_MATH.FastSmoothStep(startValue, endValue, t);     // Smooth step interpolation
        float smootherResult = FAST_MATH.FastSmootherStep(startValue, endValue, t); // Smoother step interpolation
        float easeInResult = FAST_MATH.FastEaseIn(startValue, endValue, t);         // Ease-in interpolation
        float easeOutResult = FAST_MATH.FastEaseOut(startValue, endValue, t);       // Ease-out interpolation
        float easeInOutResult = FAST_MATH.FastEaseInOut(startValue, endValue, t);   // Ease-in-out interpolation
    }
}
```

### Vector-Math Functions (new)

Six lookup facilities serve the vector toolkit in `Vectors.h` / `Vectors.cpp` (`MyVector`, `MyQuaternion`, the explosion effects) and the vector demo. The tables are built in `Initialize()` (`InitializeVectorMathTables()`) - and on the first `GetInstance()` call, so they always exist - and are read-only afterwards, so lookups need no locking. (Power curves are registered on demand; see below. `FastInvInt` is built by the compiler.)

| Function | Macro | Domain | Result |
|---|---|---|---|
| `FastSinCosDeg(deg, s, c)` | `FAST_SINCOS_DEG(deg, s, c)` | any angle in degrees (negative and > 360 allowed) | sine and cosine, error about 1e-9 |
| `FastACosAccurate(v)` | `FAST_ACOS_ACCURATE(v)` | `v` in [-1, 1] (clamped) | radians in [0, PI] |
| `GetUnitCircle(n)` | `FAST_UNIT_CIRCLE(n)` | n = 3,4,5,6,8,10,12,16,20,24,32,48,64 | `const std::vector<XMFLOAT2>&` of `(cos, sin)` |
| `GetUnitSphereDirection(rnd)` | `FAST_UNIT_SPHERE(rnd)` | any `uint32_t` (bits 10..19 pick the entry) | unit `Vector3`, 1024 evenly spread directions |
| `RegisterPowerCurve(e)` / `FastPowCurve(id, t)` | - | `t` clamped to [0, 1]; up to 8 exponents | `t^e` (interpolated table, error < ~1e-6) |
| `FastInvInt(n)` (static, inline) | `FAST_INV_INT(n)` | `n` = 1..64 from a table, other `n` divides | `1/n` (0 when `n <= 0`) |

#### FastSinCosDeg - degree-domain sine and cosine

```cpp
// One lookup returns both values, no degree-to-radian multiply, no normalisation loop.
float s, c;
FAST_SINCOS_DEG(45.0f, s, c);            // s = c = 0.7071...
FAST_SINCOS_DEG(-90.0f, s, c);           // s = -1, c = 0
FAST_SINCOS_DEG(720.0f + 30.0f, s, c);   // wraps in O(1): same as 30 degrees

// Rotating a 2D point by an angle that changes every frame:
void RotatePoint(float& x, float& y, float degrees)
{
    float s, c;
    FAST_SINCOS_DEG(degrees, s, c);
    const float nx = x * c - y * s;
    const float ny = x * s + y * c;
    x = nx;
    y = ny;
}
```

**Why not the existing `FastSinCos`?** The radian path truncates to the nearest table entry (about 1e-4 rad of error) and `NormalizeAngle` loops once per revolution, which is slow for an angle that keeps accumulating. `FastSinCosDeg` interpolates between entries and wraps with one `floor`.

**Hoist the lookup out of loops.** Every lookup increments an atomic statistics counter, so rotate a whole array with a single call:

```cpp
float s, c;
FAST_SINCOS_DEG(angleDegrees, s, c);               // ONE lookup
for (size_t i = 0; i < count; ++i)                 // pure multiply-add inside
{
    const float x = pts[i].x, y = pts[i].y;
    pts[i].x = x * c - y * s;
    pts[i].y = x * s + y * c;
}
// MyVector::RotateAroundBatch(...) does exactly this for 3D vectors.
```

#### FastACosAccurate - angle from a dot product

`FastACos` indexes its table directly by the input value, so near +-1 (where acos is steepest) one table step is about 0.6 degrees. `FastACosAccurate` evaluates `acos(|v|) = 2 * asin(sqrt((1 - |v|) / 2))` over an interpolated table whose argument never leaves [0, 0.7071], where asin is gently sloped.

```cpp
// Angle between two unit vectors
float dot = a.Dot(b);                                   // a, b normalised
float radians = FAST_ACOS_ACCURATE(dot);                // safe: out-of-range input is clamped
float degrees = radians * (180.0f / XM_PI);

// Nearly parallel vectors - the case FastACos gets wrong:
float tiny = FAST_ACOS_ACCURATE(0.99999f);              // ~0.00447 rad (0.256 degrees)
float old  = FAST_ACOS(0.99999f);                       // coarse: quantised to the table step

// Field-of-view test: is the target inside a 60 degree view cone?
bool InCone(const MyVector& forward, const MyVector& toTarget, float coneDegrees)
{
    const float dot = forward.Dot(toTarget.Normalized());
    return FAST_ACOS_ACCURATE(dot) * (180.0f / XM_PI) <= coneDegrees * 0.5f;
}
// Cheaper still when you only need a comparison: dot >= cos(halfAngle), with cos computed once outside the loop.
```

`MyVector::Angle`, `Slerp`, `RotateTowards` and `MyQuaternion::Angle` / `Slerp` / `ToAngleAxis` all use it.

#### GetUnitCircle - precalculated circle points

```cpp
// Cylinder / prism / cone / disc cross-sections with no trig at mesh-build time.
const std::vector<XMFLOAT2>& ring = FAST_UNIT_CIRCLE(16);
if (ring.empty())
{
    // Unsupported segment count (only the counts in the table above are stored).
}

std::vector<XMFLOAT3> vertices;
for (const XMFLOAT2& p : ring)
{
    vertices.push_back(XMFLOAT3(p.x * radius, 0.0f, p.y * radius));        // base circle of a cone
}
vertices.push_back(XMFLOAT3(0.0f, height, 0.0f));                          // apex

// Particle burst on a fixed number of rays:
const auto& rays = FAST_UNIT_CIRCLE(24);
for (const XMFLOAT2& dir : rays)
    SpawnParticle(position, XMFLOAT2(dir.x * speed, dir.y * speed));
```

The returned reference stays valid until `Cleanup()`. Point `k` is at angle `2*PI*k/n`, counter-clockwise, starting at `(1, 0)`.

#### GetUnitSphereDirection - a random direction in one load

The table holds 1024 points of a Fibonacci spiral over the sphere: `y` steps from +1 to -1 in equal-area bands and the azimuth advances by the golden angle, so the spread is even with no clumping at the poles. Picking a direction is a shift, a mask and a load - no `sin` / `cos`, no `sqrt`, and none of the rejection-sampling loop (three random numbers plus a square root per attempt) that a "random point in a cube" approach needs.

```cpp
// Any random number works: bits 10..19 choose the entry, so a plain LCG (weak low bits) is fine.
static uint32_t rng = 12345u;
rng = rng * 1664525u + 1013904223u;
Vector3 dir = FAST_UNIT_SPHERE(rng);                     // unit length, uniformly spread

// A burst of sparks: every particle flies off in its own direction.
void SpawnSparks(const MyVector& origin, int count, float speed)
{
    for (int i = 0; i < count; ++i)
    {
        rng = rng * 1664525u + 1013904223u;              // step the generator for EVERY particle
        const Vector3 d = FAST_UNIT_SPHERE(rng);
        Spawn(origin, MyVector(d) * (speed * 0.35f + speed * 0.65f * Rand01()));
    }
}
```

`MyCubeExplosion` and `MyExplosionFX` use it for every debris cube, spark and smoke puff. Directions are quantised to the 1024 table points, which is far finer than a burst of a few dozen particles can show; do not use it where you need a continuous distribution (for example Monte-Carlo integration).

#### RegisterPowerCurve / FastPowCurve - easing without std::pow

`std::pow` costs tens of nanoseconds a call, and easing curves are evaluated per particle per frame. Register each exponent **once** (the table is built then, in double precision) and look it up afterwards: `FastPowCurve` clamps `t` to [0, 1], then interpolates between 1024 table steps.

```cpp
// Register once - a function-local static is the usual place (thread-safe in C++11).
static const MathPrecalculation::PowerCurveId kEaseOut = FAST_MATH.RegisterPowerCurve(2.2f);

float ShockwaveRadius(float t, float r0, float r1)       // t = 0 at birth, 1 at death
{
    const float eased = 1.0f - FAST_MATH.FastPowCurve(kEaseOut, 1.0f - t);   // fast out of the blast, slowing as it stretches
    return r0 + (r1 - r0) * eased;
}

// The same exponent registered twice shares one table:
const auto a = FAST_MATH.RegisterPowerCurve(1.5f);
const auto b = FAST_MATH.RegisterPowerCurve(1.5f);       // a == b
```

Limits: up to `kMaxPowerCurves` (8) distinct exponents; when the table is full the last curve's id is returned and a warning is logged. An unknown id behaves as a plain linear ramp. Tables are never discarded (not even by `Cleanup()`), so an id cached in a static stays valid. The error is below about 1e-6 for exponents of 1 or more; for exponents below 1 the curve is steep near 0, so use `std::pow` there if you need precision at tiny `t`. `MyExplosionFX::Radius` / `Alpha` use curves for the 2.2, 1.5, 1.2 and 1.3 exponents.

#### FastInvInt - integer reciprocals from a compile-time table

`FastInvInt(n)` is `static` and inline (no singleton access); the table `kInverseInt` is built by the compiler, so it needs no `Initialize()`.

```cpp
// Average depth of a polygon: one multiply instead of a divide per face.
float zSum = 0.0f;
for (int k = 0; k < cnt; ++k) zSum += vertexZ[k];
const float averageZ = zSum * FAST_INV_INT(cnt);         // cnt = 3..32 vertices

// Step through a ring of `segments` points:
const float step = 360.0f * FAST_INV_INT(segments);      // degrees per segment

FAST_INV_INT(0);       // 0 - never divides by zero
FAST_INV_INT(100);     // outside the table: a real 1.0f / 100.0f
```

#### Using them through MyVector

You rarely call these functions directly: `MyVector` and `MyQuaternion` already do.

```cpp
FAST_MATH.Initialize();                                          // once, before any MyVector rotation or angle

MyVector v(0, 0, 1);
MyVector r = v.RotatedY(90.0f);                                  // FastSinCosDeg inside
float    a = MyVector::Angle(v, r);                              // FastACosAccurate inside
MyQuaternion q = MyQuaternion::AngleAxis(45.0f, MyVector::Up()); // FastSinCosDeg inside
```

If `FastSinCosDeg` or `FastACosAccurate` is called before `Initialize()`, it falls back to the C runtime (`std::sin`, `std::cos`, `std::acos`) without logging, so results stay correct but the speed benefit is lost. (The older `FastSin` / `FastCos`, which `MyVector::Slerp` also uses, log a warning in that situation.)

#### Memory cost

| Table | Entries | Size |
|---|---|---|
| Half-angle asin table | 8192 floats | 32 KB |
| Unit circles | 13 rings, 3 to 64 points | about 2 KB |
| Unit-sphere directions | 1024 `Vector3` | 12 KB |
| Power curves | 1025 floats per registered exponent (the engine registers 4) | about 4 KB each |
| Inverse integers | 65 floats, compile-time constant | 260 bytes (in the executable, not built at start-up) |

All except the inverse-integer table are included in `GetMemoryUsage()`. `Cleanup()` releases them, except the power curves, whose ids may be cached in statics and which therefore stay valid.

## Advanced Gaming Scenarios

### Particle System Implementation

```cpp
// Simple particle structure for demonstration
struct GameParticle {
    XMFLOAT2 position;      // 2D position
    XMFLOAT2 velocity;      // 2D velocity vector
    float life;             // Remaining life (0.0 to 1.0)
    float size;             // Particle size
    XMFLOAT4 color;         // Particle color with alpha
    float rotationAngle;    // Current rotation angle
    float rotationSpeed;    // Rotation speed in radians/second
};

void DemonstrateParticleSystem()
{
    const int particleCount = 16;                   // Number of particles in explosion
    const float explosionSpeed = 150.0f;           // Explosion speed in units/second
    const XMFLOAT2 explosionCenter(400.0f, 300.0f); // Center of explosion
    
    // Create particle array for demonstration
    std::vector<GameParticle> particles(particleCount);
    
    // Initialize particles using precalculated directions
    for (int i = 0; i < particleCount; ++i)
    {
        GameParticle& particle = particles[i];
        
        // Get precalculated direction from the MathPrecalculation class
        XMFLOAT2 direction = FAST_MATH.GetParticleDirection(i, particleCount);
        
        // Set particle properties
        particle.position = explosionCenter;
        particle.velocity = XMFLOAT2(direction.x * explosionSpeed, direction.y * explosionSpeed);
        particle.life = 1.0f;                       // Full life at start
        particle.size = 2.0f + (static_cast<float>(rand()) / RAND_MAX) * 3.0f; // Random size 2-5
        particle.rotationSpeed = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 6.28f; // Random rotation speed
        
        // Set color based on particle index for variety
        float hue = static_cast<float>(i) / static_cast<float>(particleCount) * 2.0f * XM_PI;
        particle.color.x = (FAST_SIN(hue) + 1.0f) * 0.5f;          // Red component using fast sine
        particle.color.y = (FAST_SIN(hue + 2.09f) + 1.0f) * 0.5f;  // Green component (120° offset)
        particle.color.z = (FAST_SIN(hue + 4.18f) + 1.0f) * 0.5f;  // Blue component (240° offset)
        particle.color.w = 1.0f;                                    // Full alpha initially
    }
    
    // Simulate particle update for a few frames
    const float deltaTime = 1.0f / 60.0f;          // 60 FPS simulation
    const int simulationFrames = 5;
    
    for (int frame = 0; frame < simulationFrames; ++frame)
    {
        for (int i = 0; i < particleCount; ++i)
        {
            GameParticle& particle = particles[i];
            
            // Update particle position using velocity
            particle.position.x += particle.velocity.x * deltaTime;
            particle.position.y += particle.velocity.y * deltaTime;
            
            // Update particle rotation
            particle.rotationAngle += particle.rotationSpeed * deltaTime;
            
            // Update particle life (linear decay)
            particle.life -= deltaTime * 0.5f;     // 2 second lifespan
            
            // Update particle alpha based on remaining life using fast interpolation
            particle.color.w = FAST_LERP(0.0f, 1.0f, particle.life);
            
            // Calculate distance from explosion center using fast distance calculation
            XMFLOAT2 currentPos(particle.position.x, particle.position.y);
            float distanceFromCenter = FAST_MATH.FastDistance(explosionCenter, currentPos);
        }
    }
}
```

### Matrix Transformations for 3D Objects

```cpp
// Simple game entity structure
struct GameEntity {
    XMFLOAT3 position;      // World position (x, y, z)
    XMFLOAT3 rotation;      // Rotation angles in radians
    XMFLOAT3 scale;         // Scale factors
    XMFLOAT4 color;         // RGBA color
    float animationTime;    // Current animation time
    bool isActive;          // Entity active state
};

void DemonstrateMatrixTransformations()
{
    // Create test game entities with different properties
    std::vector<GameEntity> entities(5);
    
    // Initialize entities with different transformations
    entities[0].position = XMFLOAT3(10.0f, 5.0f, 0.0f);
    entities[0].rotation = XMFLOAT3(0.0f, 0.0f, XM_PIDIV4);    // 45° Z rotation
    entities[0].scale = XMFLOAT3(2.0f, 2.0f, 2.0f);            // Uniform scale
    
    entities[1].position = XMFLOAT3(-5.0f, 10.0f, 3.0f);
    entities[1].rotation = XMFLOAT3(XM_PIDIV6, 0.0f, 0.0f);    // 30° X rotation
    entities[1].scale = XMFLOAT3(1.5f, 1.0f, 0.5f);            // Non-uniform scale
    
    // Process each entity and create transformation matrices
    for (size_t i = 0; i < entities.size(); ++i)
    {
        GameEntity& entity = entities[i];
        
        // Get optimized transformation matrices using MathPrecalculation
        XMMATRIX scaleMatrix = FAST_MATH.GetScaleMatrix(
            entity.scale.x, entity.scale.y, entity.scale.z);
        
        XMMATRIX rotationMatrix = FAST_MATH.GetRotationMatrix(
            entity.rotation.x, entity.rotation.y, entity.rotation.z);
        
        // Create translation matrix (no precalculation needed for translation)
        XMMATRIX translationMatrix = XMMatrixTranslation(
            entity.position.x, entity.position.y, entity.position.z);
        
        // Combine matrices using fast matrix multiplication (Scale * Rotation * Translation)
        XMMATRIX worldMatrix = FAST_MATH.FastMatrixMultiply(scaleMatrix, rotationMatrix);
        worldMatrix = FAST_MATH.FastMatrixMultiply(worldMatrix, translationMatrix);
    }
    
    // Demonstrate camera view matrix calculation
    XMFLOAT3 cameraPos(0.0f, 5.0f, -10.0f);        // Camera position
    XMFLOAT3 lookAtTarget(0.0f, 0.0f, 0.0f);        // Look at world origin
    XMFLOAT3 upVector(0.0f, 1.0f, 0.0f);            // Up vector (Y-axis)
    
    XMMATRIX viewMatrix = FAST_MATH.GetViewMatrix(cameraPos, lookAtTarget, upVector);
}
```

### Text Rendering Optimizations

```cpp
void DemonstrateTextOptimizations()
{
    // Test string for width calculation
    const std::wstring testText = L"Hello Gaming World! 123";
    const float fontSize = 16.0f;
    
    // Calculate total text width using fast character width lookup
    float totalWidth = 0.0f;
    for (wchar_t ch : testText)
    {
        float charWidth = FAST_MATH.GetCharacterWidthFast(ch, fontSize);
        totalWidth += charWidth;
    }
    
    // Demonstrate text transparency calculation for scrolling text
    const float scrollRegionStart = 100.0f;
    const float scrollRegionEnd = 700.0f;
    const float fadeDistance = 50.0f;
    const int positions = 15;
    
    for (int i = 0; i <= positions; ++i)
    {
        float position = 0.0f + (static_cast<float>(i) / static_cast<float>(positions)) * 800.0f;
        float transparency = FAST_MATH.GetTextTransparencyFast(
            position, scrollRegionStart, scrollRegionEnd, fadeDistance);
    }
}
```

## Performance Considerations

### Performance Comparison Example

```cpp
void DemonstratePerformanceComparison()
{
    const int iterations = 10000;           // Number of calculations for timing
    const float testAngle = 1.23456f;      // Test angle for calculations
    
    // Variables to store results (prevent compiler optimization)
    volatile float standardResult = 0.0f;
    volatile float fastResult = 0.0f;
    
    // Simulate standard math function calls
    for (int i = 0; i < iterations; ++i)
    {
        float angle = testAngle + static_cast<float>(i) * 0.001f;
        standardResult += std::sin(angle) + std::cos(angle) + std::tan(angle);
    }
    
    // Simulate fast math function calls using macros
    for (int i = 0; i < iterations; ++i)
    {
        float angle = testAngle + static_cast<float>(i) * 0.001f;
        fastResult += FAST_SIN(angle) + FAST_COS(angle) + FAST_TAN(angle);
    }
    
    // Calculate accuracy comparison
    float accuracyDifference = std::abs(standardResult - fastResult);
}
```

### Memory Usage Information

```cpp
// Get memory usage statistics
size_t memoryUsage = FAST_MATH.GetMemoryUsage();

// Dump table statistics for debugging
FAST_MATH.DumpTableStatistics();
```

## Integration Examples

### Game Loop Integration

```cpp
void DemonstrateGameLoopIntegration()
{
    // Simulate game loop with multiple entities and effects
    const int maxEntities = 50;            // Maximum number of game entities
    const int maxParticles = 200;          // Maximum number of particles
    const float deltaTime = 1.0f / 60.0f;  // 60 FPS target
    const int simulationFrames = 10;       // Number of frames to simulate
    
    // Create entity and particle arrays
    std::vector<GameEntity> gameEntities(maxEntities);
    std::vector<GameParticle> gameParticles(maxParticles);
    
    // Initialize entities with random properties
    for (int i = 0; i < maxEntities; ++i)
    {
        GameEntity& entity = gameEntities[i];
        
        // Random position within game world
        entity.position.x = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 200.0f;
        entity.position.y = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 200.0f;
        entity.position.z = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 200.0f;
        
        // Random colors using fast trigonometric functions
        float hue = static_cast<float>(i) / static_cast<float>(maxEntities) * 2.0f * XM_PI;
        entity.color.x = (FAST_SIN(hue) + 1.0f) * 0.5f;
        entity.color.y = (FAST_SIN(hue + 2.094f) + 1.0f) * 0.5f;       // 120° phase shift
        entity.color.z = (FAST_SIN(hue + 4.188f) + 1.0f) * 0.5f;       // 240° phase shift
        entity.color.w = 1.0f;
    }
    
    // Simulate game loop frames
    for (int frame = 0; frame < simulationFrames; ++frame)
    {
        float frameTime = static_cast<float>(frame) * deltaTime;
        
        // Update all game entities
        for (int i = 0; i < maxEntities; ++i)
        {
            GameEntity& entity = gameEntities[i];
            
            // Update animation time
            entity.animationTime += deltaTime;
            
            // Animate rotation using smooth interpolation
            float rotationT = FAST_MATH.FastSmoothStep(0.0f, 1.0f, 
                fmod(entity.animationTime * 0.5f, 1.0f));
            
            // Animate scale using sine wave
            float scaleAnimation = FAST_SIN(entity.animationTime * 2.0f) * 0.2f + 1.0f;
            entity.scale.x = entity.scale.x * scaleAnimation;
            entity.scale.y = entity.scale.y * scaleAnimation;
            entity.scale.z = entity.scale.z * scaleAnimation;
            
            // Animate color using fast trigonometric functions
            entity.color.w = (FAST_SIN(entity.animationTime * 3.0f) + 1.0f) * 0.5f;
        }
    }
}
```

### Advanced Lighting Calculations

```cpp
void DemonstrateAdvancedColorCalculations()
{
    // Simulate lighting calculations for multiple light sources
    const int lightCount = 8;
    const int surfaceCount = 16;
    
    // Light structure
    struct Light {
        XMFLOAT3 position;
        XMFLOAT3 color;
        float intensity;
        float range;
    };
    
    // Surface structure
    struct Surface {
        XMFLOAT3 position;
        XMFLOAT3 normal;
        XMFLOAT3 albedo;
        float roughness;
    };
    
    // Create lights in circular formation
    std::vector<Light> lights(lightCount);
    for (int i = 0; i < lightCount; ++i)
    {
        float angle = static_cast<float>(i) / static_cast<float>(lightCount) * 2.0f * XM_PI;
        lights[i].position.x = FAST_COS(angle) * 20.0f;
        lights[i].position.y = 5.0f + FAST_SIN(angle * 2.0f) * 3.0f;
        lights[i].position.z = FAST_SIN(angle) * 20.0f;
        
        // Generate light colors using fast trigonometric functions
        float hue = angle;
        lights[i].color.x = (FAST_SIN(hue) + 1.0f) * 0.5f;
        lights[i].color.y = (FAST_SIN(hue + 2.094f) + 1.0f) * 0.5f;
        lights[i].color.z = (FAST_SIN(hue + 4.188f) + 1.0f) * 0.5f;
        
        lights[i].intensity = 0.8f + (static_cast<float>(rand()) / RAND_MAX) * 0.4f;
        lights[i].range = 15.0f + (static_cast<float>(rand()) / RAND_MAX) * 10.0f;
    }
    
    // Calculate lighting for each surface
    for (int s = 0; s < surfaceCount; ++s)
    {
        const Surface& surface = surfaces[s];
        XMFLOAT3 finalColor(0.0f, 0.0f, 0.0f);
        
        for (int l = 0; l < lightCount; ++l)
        {
            const Light& light = lights[l];
            
            // Calculate distance using fast distance calculation
            XMFLOAT2 surfacePos2D(surface.position.x, surface.position.z);
            XMFLOAT2 lightPos2D(light.position.x, light.position.z);
            float distance = FAST_MATH.FastDistance(surfacePos2D, lightPos2D);
            
            // Skip if light is out of range
            if (distance > light.range) continue;
            
            // Calculate attenuation using fast interpolation
            float attenuation = FAST_MATH.FastSmoothStep(1.0f, 0.0f, distance / light.range);
            
            // Apply lighting calculation
            float lightStrength = dotProduct * attenuation * light.intensity;
            
            finalColor.x += surface.albedo.x * light.color.x * lightStrength;
            finalColor.y += surface.albedo.y * light.color.y * lightStrength;
            finalColor.z += surface.albedo.z * light.color.z * lightStrength;
        }
        
        // Clamp final color values
        finalColor.x = std::min(1.0f, finalColor.x);
        finalColor.y = std::min(1.0f, finalColor.y);
        finalColor.z = std::min(1.0f, finalColor.z);
    }
}
```

## Complete Code Example

### Main Entry Point

```cpp
#include "Includes.h"
#include "MathPrecalculation.h"
#include "Debug.h"

// External reference for global debug instance
extern Debug debug;

// Main function demonstrating comprehensive usage of MathPrecalculation class
int MathPrecalculationUsageExample()
{
    debug.logLevelMessage(LogLevel::LOG_INFO, L"===============================================");
    debug.logLevelMessage(LogLevel::LOG_INFO, L"MathPrecalculation Comprehensive Usage Example");
    debug.logLevelMessage(LogLevel::LOG_INFO, L"===============================================");

    // Initialize the MathPrecalculation singleton
    if (!FAST_MATH.Initialize())
    {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Failed to initialize MathPrecalculation!");
        return -1;
    }

    debug.logDebugMessage(LogLevel::LOG_INFO, 
        L"MathPrecalculation initialized successfully - Memory usage: %zu bytes",
        FAST_MATH.GetMemoryUsage());

    // Validate lookup tables before usage
    if (!FAST_MATH.ValidateTables())
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Lookup table validation failed!");
        return -2;
    }

    try
    {
        // Run all demonstration functions
        DemonstrateTrigonometricFunctions();        // Basic trigonometric operations
        DemonstrateColorConversions();              // Color space conversions
        DemonstrateInterpolationFunctions();        // Animation interpolations
        DemonstrateParticleSystem();                // Particle system usage
        DemonstrateMatrixTransformations();         // 3D transformations
        DemonstrateTextOptimizations();             // Text rendering optimizations
        DemonstratePerformanceComparison();         // Performance comparison
        
        // Dump final statistics
        FAST_MATH.DumpTableStatistics();
        
        debug.logLevelMessage(LogLevel::LOG_INFO, L"All demonstrations completed successfully!");
        debug.logLevelMessage(LogLevel::LOG_INFO, L"===============================================");
        
        return 0;  // Success
    }
    catch (const std::exception& e)
    {
        // Convert exception message to wide string for logging
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, 
            L"Exception occurred: " + wErrorMsg);
        return -3;
    }
    catch (...)
    {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Unknown exception occurred during demonstration");
        return -4;
    }
}

// Complete example entry point that can be called from main()
int RunMathPrecalculationExample()
{
    // Initialize random seed for demonstrations
    srand(static_cast<unsigned int>(time(nullptr)));
    
    // Run the main example function
    int result = MathPrecalculationUsageExample();
    
    if (result == 0)
    {
        // Run advanced examples if basic examples succeeded
        DemonstrateGameLoopIntegration();
        DemonstrateAdvancedColorCalculations();
        DemonstrateCompleteGameScenario();
        
        debug.logLevelMessage(LogLevel::LOG_INFO, 
            L"All advanced demonstrations completed successfully!");
    }
    
    // Cleanup is handled automatically by the singleton destructor
    return result;
}
```

## Best Practices

### 1. Initialization
- Always initialize the MathPrecalculation system before use
- Validate lookup tables after initialization
- Check memory usage if needed for optimization

### 2. Error Handling
- Use try-catch blocks around mathematical operations
- Check return values from initialization functions
- Implement graceful fallbacks if fast math fails

### 3. Performance Optimization
- Use the macro versions (`FAST_SIN`, `FAST_COS`, etc.) for best performance
- Batch similar calculations when possible
- Consider memory usage vs. speed tradeoffs

### 4. Debugging
- Enable debug output using `_DEBUG_MATHPRECALC_` define
- Use table statistics for performance analysis
- Compare results with standard math functions for accuracy verification

### 5. Integration
- Initialize once at application startup
- Use throughout the game loop for consistent performance
- Cleanup is automatic via singleton destructor

## Conclusion

The MathPrecalculation class provides significant performance improvements for mathematical operations in gaming applications. By using lookup tables and optimized algorithms, it can reduce the computational overhead of trigonometric functions, color conversions, interpolations, and matrix operations while maintaining acceptable accuracy for real-time applications.

This example demonstrates comprehensive usage patterns that can be adapted to various gaming scenarios, from simple 2D particle effects to complex 3D lighting calculations. The class is designed to integrate seamlessly into existing game engines and provides both ease of use and high performance.