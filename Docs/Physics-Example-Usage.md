# Physics Class - Comprehensive Usage Guide

## Table of Contents

1.  [Introduction and Overview](#1-introduction-and-overview)
2.  [Initialization and Setup](#2-initialization-and-setup)
3.  [Basic Physics Vector Operations](#3-basic-physics-vector-operations)
4.  [Curved Path Calculations (2D and 3D)](#4-curved-path-calculations-2d-and-3d)
5.  [Reflection Path Calculations](#5-reflection-path-calculations)
6.  [Gravity System and Fields](#6-gravity-system-and-fields)
7.  [Bouncing Mechanics with Inertia](#7-bouncing-mechanics-with-inertia)
8.  [Collision Detection and Response](#8-collision-detection-and-response)
9.  [Ragdoll Physics System](#9-ragdoll-physics-system)
10. [Newtonian Motion and Projectile Physics](#10-newtonian-motion-and-projectile-physics)
11. [Audio Physics for 3D Sound](#11-audio-physics-for-3d-sound)
12. [Particle System Physics](#12-particle-system-physics)
13. [Physics-Based Animation](#13-physics-based-animation)
14. [Performance Optimization and Debugging](#14-performance-optimization-and-debugging)
15. [Advanced Integration Techniques](#15-advanced-integration-techniques)
16. [Best Practices and Common Pitfalls](#16-best-practices-and-common-pitfalls)
17. [Troubleshooting Guide](#17-troubleshooting-guide)
18. [Complete Example Projects](#18-complete-example-projects)

---

## 1. Introduction and Overview

The Physics class is a comprehensive physics simulation system designed for high-performance gaming applications. It provides advanced mathematical calculations for curved paths, reflections, gravity, collisions, ragdoll physics, and much more. The system is optimized for real-time performance and integrates seamlessly with the MathPrecalculation system for maximum efficiency.

### Key Features

- **2D and 3D Curved Path Calculations**: Create smooth spline-based paths with up to 1024 coordinates
- **Reflection Physics**: Calculate realistic bounces and reflections with customizable restitution and friction
- **Variable Gravity System**: Support for multiple gravity fields with distance-based intensity calculations
- **Advanced Collision Detection**: AABB, sphere, and continuous collision detection with response
- **Ragdoll Physics**: Complete joint-based ragdoll system with constraint solving
- **Newtonian Motion**: Full implementation of Newton's laws with projectile calculations
- **Audio Physics**: 3D sound propagation with Doppler effects and occlusion
- **Particle Systems**: Explosive effects and environmental particles with wind and gravity
- **Physics-Based Animation**: Blend physics simulation with keyframe animation

### System Requirements

- C++17 compliant compiler
- Windows, Linux, macOS, Android, or iOS 64-bit environment
- Visual Studio 2019/2022 compatibility
- Dependencies: Debug.h, MathPrecalculation.h, ExceptionHandler.h

### Architecture Overview

The Physics class follows a modular design pattern:

```
Physics System
├── Core Physics Engine
│   ├── Integration Methods (Euler, Verlet, RK4)
│   ├── Constraint Solving
│   └── Memory Management
├── Collision Detection
│   ├── Broad Phase (Spatial Hashing)
│   ├── Narrow Phase (Detailed Collision)
│   └── Response System
├── Specialized Systems
│   ├── Gravity Fields
│   ├── Ragdoll Physics
│   ├── Particle Systems
│   └── Audio Physics
└── Utility Systems
    ├── Debug Visualization
    ├── Performance Profiling
    └── Memory Optimization
```

### Performance Characteristics

The Physics system is designed for real-time performance with the following optimizations:

- **MathPrecalculation Integration**: Uses lookup tables for trigonometric functions and common calculations
- **Memory Pooling**: Efficient memory allocation for physics bodies and particles
- **Spatial Optimization**: Broad-phase collision detection reduces O(n²) complexity
- **Multi-threading Support**: Thread-safe operations with proper mutex protection
- **Vectorized Operations**: Optimized vector mathematics using DirectX Math library

### Thread Safety

The Physics class is thread-safe and uses the following synchronization mechanisms:

- `std::mutex m_physicsMutex` for protecting shared data structures
- `std::atomic` variables for performance counters and state flags
- Lock-free operations where possible for maximum performance
- Integration with ExceptionHandler for robust error handling

---

## 2. Initialization and Setup

### 2.1 Basic Initialization

The Physics class follows a singleton-like pattern but requires explicit initialization before use. Here's the basic setup process:

```cpp
#include "Physics.h"

// Create physics instance
Physics physics;

// Initialize the physics system
if (!physics.Initialize()) {
    // Handle initialization failure
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to initialize Physics system");
    return false;
}

// Physics system is now ready for use
```

### 2.2 Dependency Requirements

Before initializing the Physics system, ensure all dependencies are properly set up:

#### Required Headers
```cpp
#include "Debug.h"           // Debug logging system
#include "ExceptionHandler.h" // Exception handling
#include "MathPrecalculation.h" // Mathematical optimizations
#include "Physics.h"         // Physics system
```

#### Initialization Order
The Physics system depends on other systems, so initialize them in this order:

```cpp
// 1. Initialize Debug system first
extern Debug debug;

// 2. Initialize Exception Handler
ExceptionHandler& exceptionHandler = ExceptionHandler::GetInstance();
if (!exceptionHandler.Initialize()) {
    debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Failed to initialize ExceptionHandler");
    return false;
}

// 3. Initialize MathPrecalculation system
MathPrecalculation& mathPrecalc = MathPrecalculation::GetInstance();
if (!mathPrecalc.Initialize()) {
    debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Failed to initialize MathPrecalculation");
    return false;
}

// 4. Finally initialize Physics system
Physics physics;
if (!physics.Initialize()) {
    debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Failed to initialize Physics system");
    return false;
}
```

### 2.3 Global Physics Access

The Physics system provides a global pointer for convenient access:

```cpp
// Access global physics instance
if (g_pPhysics && g_pPhysics->IsInitialized()) {
    // Safe to use physics system
    g_pPhysics->Update(deltaTime);
}

// Using the convenience macro
PHYSICS_SAFE_CALL(Update(deltaTime));
```

### 2.4 Configuration Options

#### Debug Output Configuration

Enable specific debug output in Debug.h:

```cpp
// In Debug.h, add or uncomment:
#define _DEBUG_PHYSICS_              // Enable general physics debug output

// Conditional debug logging in your code:
#if defined(_DEBUG_PHYSICS_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Physics system initialized successfully");
#endif
```

#### Performance Tuning Constants

Modify constants in Physics.h for your specific needs:

```cpp
// Adjust these constants based on your game requirements
const int MAX_PATH_COORDINATES = 1024;        // Increase for more detailed paths
const int MAX_COLLISION_CONTACTS = 32;        // Increase for complex collisions
const int MAX_RAGDOLL_JOINTS = 64;           // Adjust for ragdoll complexity
const int MAX_PARTICLE_COUNT = 10000;        // Balance performance vs. effects

// Physics simulation parameters
const float DEFAULT_GRAVITY = 9.81f;         // Earth gravity or custom value
const float MIN_VELOCITY_THRESHOLD = 0.001f; // Sleeping threshold
const float DEFAULT_AIR_RESISTANCE = 0.01f;  // Air resistance factor
```

### 2.5 Memory Allocation Strategy

The Physics system pre-allocates memory pools for optimal performance:

```cpp
// The system automatically reserves memory during initialization
// You can check memory usage:
Physics physics;
physics.Initialize();

// Get memory statistics
int activeBodyCount, collisionCount, particleCount;
physics.GetPhysicsStatistics(activeBodyCount, collisionCount, particleCount);

debug.logDebugMessage(LogLevel::LOG_INFO, 
    L"Physics initialized - Bodies: %d, Collisions: %d, Particles: %d",
    activeBodyCount, collisionCount, particleCount);
```

### 2.6 Error Handling During Initialization

Always check for initialization failures and handle them appropriately:

```cpp
bool InitializePhysicsSystem() {
    try {
        // Create physics instance
        Physics physics;
        
        // Attempt initialization
        if (!physics.Initialize()) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, 
                L"Physics initialization failed - check dependencies");
            return false;
        }
        
        // Validate initialization
        if (!physics.IsInitialized()) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, 
                L"Physics reports not initialized after init call");
            return false;
        }
        
        debug.logLevelMessage(LogLevel::LOG_INFO, 
            L"Physics system initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        // Convert to wide string for logging
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, 
            L"Exception during physics initialization: " + wErrorMsg);
        return false;
    }
}
```

### 2.7 Cleanup and Shutdown

Proper cleanup is essential for memory management:

```cpp
void ShutdownPhysicsSystem(Physics& physics) {
    if (physics.IsInitialized()) {
        // Clear all physics data
        physics.ClearGravityFields();
        physics.ClearDebugLines();
        physics.ResetPerformanceCounters();
        
        // Cleanup the system
        physics.Cleanup();
        
        debug.logLevelMessage(LogLevel::LOG_INFO, 
            L"Physics system shutdown complete");
    }
}

// In your application shutdown:
ShutdownPhysicsSystem(physics);
```

### 2.8 Initialization Verification

Verify that all systems are working correctly:

```cpp
bool VerifyPhysicsInitialization(Physics& physics) {
    // Check initialization status
    if (!physics.IsInitialized()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Physics not initialized");
        return false;
    }
    
    // Test basic functionality
    PhysicsVector3D testVector(1.0f, 2.0f, 3.0f);
    float magnitude = testVector.Magnitude();
    
    if (magnitude < 0.1f) {  // Should be ~3.74
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Vector calculations not working");
        return false;
    }
    
    // Test math precalculation integration
    float sinValue = FAST_SIN(XM_PIDIV2);  // Should be ~1.0
    if (abs(sinValue - 1.0f) > 0.01f) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, 
            L"MathPrecalculation integration may have issues");
    }
    
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Physics initialization verified");
    return true;
}
```

### 2.9 Common Initialization Issues

#### Issue 1: Dependency Not Initialized
```cpp
// Problem: MathPrecalculation not initialized first
// Solution: Initialize dependencies in correct order
MathPrecalculation::GetInstance().Initialize();  // First
Physics physics;
physics.Initialize();  // Second
```

#### Issue 2: Debug System Not Available
```cpp
// Problem: Debug messages not appearing
// Solution: Ensure Debug.h is included and _DEBUG_PHYSICS_ is defined
#include "Debug.h"
extern Debug debug;  // Make sure debug instance is available

#if defined(_DEBUG_PHYSICS_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Debug output enabled");
#endif
```

#### Issue 3: Memory Allocation Failures
```cpp
// Problem: Insufficient memory for physics pools
// Solution: Check available memory before initialization
size_t availableMemory = GetAvailablePhysicalMemory();
if (availableMemory < MINIMUM_REQUIRED_MEMORY) {
    debug.logLevelMessage(LogLevel::LOG_WARNING, 
        L"Low memory - physics performance may be affected");
}
```

### 2.10 Platform-Specific Considerations

#### Windows
```cpp
// Ensure proper alignment for DirectX Math
#ifdef _WIN32
    // DirectX Math requires 16-byte alignment
    static_assert(alignof(PhysicsVector3D) >= 4, "Vector alignment issue");
#endif
```

#### Mobile Platforms (Android/iOS)
```cpp
// Reduce memory usage for mobile
#if defined(__ANDROID__) || defined(__APPLE__)
    const int MOBILE_MAX_PARTICLES = 1000;      // Reduced from 10000
    const int MOBILE_MAX_RAGDOLL_JOINTS = 16;   // Reduced from 64
#endif
```

---

## 3. Basic Physics Vector Operations

### 3.1 PhysicsVector2D Operations

The PhysicsVector2D structure provides essential 2D vector mathematics for physics calculations.

#### Creating and Initializing 2D Vectors

```cpp
// Default constructor (0, 0)
PhysicsVector2D zeroVector;

// Parameterized constructor
PhysicsVector2D position(10.0f, 20.0f);
PhysicsVector2D velocity(-5.0f, 15.0f);

// Copy construction
PhysicsVector2D newPosition(position);

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"Created 2D vector: (%.2f, %.2f)", position.x, position.y);
#endif
```

#### Basic 2D Vector Arithmetic

```cpp
PhysicsVector2D vectorA(10.0f, 5.0f);
PhysicsVector2D vectorB(3.0f, 7.0f);

// Addition
PhysicsVector2D sum = vectorA + vectorB;  // Result: (13.0f, 12.0f)

// Subtraction
PhysicsVector2D difference = vectorA - vectorB;  // Result: (7.0f, -2.0f)

// Scalar multiplication
PhysicsVector2D scaled = vectorA * 2.0f;  // Result: (20.0f, 10.0f)

// In-place operations
vectorA += vectorB;  // vectorA becomes (13.0f, 12.0f)
vectorA -= vectorB;  // vectorA becomes (10.0f, 5.0f)
vectorA *= 0.5f;     // vectorA becomes (5.0f, 2.5f)

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"Vector operations: sum(%.2f, %.2f), diff(%.2f, %.2f)", 
        sum.x, sum.y, difference.x, difference.y);
#endif
```

#### Advanced 2D Vector Functions

```cpp
PhysicsVector2D movement(30.0f, 40.0f);

// Calculate magnitude (length)
float magnitude = movement.Magnitude();  // Result: 50.0f

// Calculate squared magnitude (faster when you don't need exact length)
float magnitudeSquared = movement.MagnitudeSquared();  // Result: 2500.0f

// Normalize vector (unit vector in same direction)
PhysicsVector2D direction = movement.Normalized();  // Result: (0.6f, 0.8f)

// Dot product (useful for angle calculations)
PhysicsVector2D vectorA(1.0f, 0.0f);
PhysicsVector2D vectorB(0.0f, 1.0f);
float dotProduct = vectorA.Dot(vectorB);  // Result: 0.0f (perpendicular)

// Cross product (returns scalar in 2D)
float crossProduct = vectorA.Cross(vectorB);  // Result: 1.0f

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"2D Vector analysis: magnitude=%.2f, normalized(%.3f, %.3f), dot=%.2f", 
        magnitude, direction.x, direction.y, dotProduct);
#endif
```

### 3.2 PhysicsVector3D Operations

The PhysicsVector3D structure provides comprehensive 3D vector mathematics for spatial physics.

#### Creating and Initializing 3D Vectors

```cpp
// Default constructor (0, 0, 0)
PhysicsVector3D origin;

// Parameterized constructor
PhysicsVector3D position(10.0f, 20.0f, 30.0f);
PhysicsVector3D velocity(-5.0f, 15.0f, -25.0f);

// Construction from XMFLOAT3
XMFLOAT3 directxVector(1.0f, 2.0f, 3.0f);
PhysicsVector3D physicsVector(directxVector);

// Convert back to XMFLOAT3
XMFLOAT3 convertedBack = physicsVector.ToXMFLOAT3();

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"Created 3D vector: (%.2f, %.2f, %.2f)", 
        position.x, position.y, position.z);
#endif
```

#### Basic 3D Vector Arithmetic

```cpp
PhysicsVector3D vectorA(10.0f, 5.0f, 15.0f);
PhysicsVector3D vectorB(3.0f, 7.0f, -5.0f);

// Addition
PhysicsVector3D sum = vectorA + vectorB;  // Result: (13.0f, 12.0f, 10.0f)

// Subtraction  
PhysicsVector3D difference = vectorA - vectorB;  // Result: (7.0f, -2.0f, 20.0f)

// Scalar multiplication
PhysicsVector3D scaled = vectorA * 2.0f;  // Result: (20.0f, 10.0f, 30.0f)

// In-place operations
PhysicsVector3D working = vectorA;
working += vectorB;  // working becomes (13.0f, 12.0f, 10.0f)
working -= vectorB;  // working becomes (10.0f, 5.0f, 15.0f)
working *= 0.5f;     // working becomes (5.0f, 2.5f, 7.5f)

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"3D Vector operations: sum(%.2f, %.2f, %.2f)", 
        sum.x, sum.y, sum.z);
#endif
```

#### Advanced 3D Vector Functions

```cpp
PhysicsVector3D movement(30.0f, 40.0f, 0.0f);

// Calculate magnitude (length) using optimized square root
float magnitude = movement.Magnitude();  // Uses FAST_SQRT internally

// Calculate squared magnitude (no square root operation)
float magnitudeSquared = movement.MagnitudeSquared();

// Normalize vector (unit vector in same direction)
PhysicsVector3D direction = movement.Normalized();

// Dot product (projection, angle calculations)
PhysicsVector3D vectorA(1.0f, 0.0f, 0.0f);  // X-axis
PhysicsVector3D vectorB(0.0f, 1.0f, 0.0f);  // Y-axis
float dotProduct = vectorA.Dot(vectorB);  // Result: 0.0f (perpendicular)

// Cross product (perpendicular vector)
PhysicsVector3D crossProduct = vectorA.Cross(vectorB);  // Result: (0, 0, 1) Z-axis

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"3D Vector analysis: magnitude=%.2f, cross(%.2f, %.2f, %.2f)", 
        magnitude, crossProduct.x, crossProduct.y, crossProduct.z);
#endif
```

### 3.3 Practical Vector Applications in Physics

#### Distance Calculations

```cpp
// Calculate distance between two points
PhysicsVector3D pointA(10.0f, 20.0f, 30.0f);
PhysicsVector3D pointB(15.0f, 25.0f, 35.0f);

// Method 1: Manual calculation
PhysicsVector3D displacement = pointB - pointA;
float distance = displacement.Magnitude();

// Method 2: Using Physics class helper function
Physics& physics = PHYSICS_INSTANCE();
float distanceFast = physics.FastDistance(pointA, pointB);

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"Distance calculation: manual=%.2f, optimized=%.2f", 
        distance, distanceFast);
#endif
```

#### Velocity and Acceleration Calculations

```cpp
// Calculate velocity from position change
PhysicsVector3D previousPosition(0.0f, 0.0f, 0.0f);
PhysicsVector3D currentPosition(10.0f, 5.0f, 15.0f);
float deltaTime = 0.016f;  // 60 FPS

PhysicsVector3D velocity = (currentPosition - previousPosition) * (1.0f / deltaTime);

// Calculate acceleration from velocity change
PhysicsVector3D previousVelocity(0.0f, 0.0f, 0.0f);
PhysicsVector3D currentVelocity = velocity;

PhysicsVector3D acceleration = (currentVelocity - previousVelocity) * (1.0f / deltaTime);

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"Physics calculations: velocity(%.2f, %.2f, %.2f), acceleration(%.2f, %.2f, %.2f)", 
        velocity.x, velocity.y, velocity.z, acceleration.x, acceleration.y, acceleration.z);
#endif
```

#### Direction and Orientation Calculations

```cpp
// Calculate direction from one point to another
PhysicsVector3D origin(0.0f, 0.0f, 0.0f);
PhysicsVector3D target(100.0f, 50.0f, 75.0f);

PhysicsVector3D direction = (target - origin).Normalized();

// Calculate angle between two vectors
PhysicsVector3D vectorA(1.0f, 0.0f, 0.0f);
PhysicsVector3D vectorB(0.707f, 0.707f, 0.0f);  // 45 degrees from X-axis

float dotProduct = vectorA.Dot(vectorB);
float angle = FAST_ACOS(dotProduct);  // Result in radians

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"Direction vector: (%.3f, %.3f, %.3f), angle: %.2f radians", 
        direction.x, direction.y, direction.z, angle);
#endif
```

### 3.4 Vector Interpolation and Blending

#### Linear Interpolation (LERP)

```cpp
// Interpolate between two positions
PhysicsVector3D startPosition(0.0f, 0.0f, 0.0f);
PhysicsVector3D endPosition(100.0f, 50.0f, 25.0f);
float t = 0.5f;  // 50% interpolation

// Manual LERP
PhysicsVector3D interpolated = startPosition * (1.0f - t) + endPosition * t;

// Using Physics class fast interpolation
Physics& physics = PHYSICS_INSTANCE();
PhysicsVector3D fastInterpolated = startPosition + 
    (endPosition - startPosition) * physics.FastLerp(0.0f, 1.0f, t);

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"Interpolated position: (%.2f, %.2f, %.2f)", 
        interpolated.x, interpolated.y, interpolated.z);
#endif
```

#### Spherical Linear Interpolation (SLERP) for Directions

```cpp
// Interpolate between two direction vectors
PhysicsVector3D directionA(1.0f, 0.0f, 0.0f);    // X-axis
PhysicsVector3D directionB(0.0f, 1.0f, 0.0f);    // Y-axis
float t = 0.3f;

// Calculate angle between directions
float dot = directionA.Dot(directionB);
float angle = FAST_ACOS(std::clamp(dot, -1.0f, 1.0f));

// Perform SLERP
PhysicsVector3D slerpResult;
if (angle > 0.001f) {  // Avoid division by zero
    float sinAngle = FAST_SIN(angle);
    float factor1 = FAST_SIN((1.0f - t) * angle) / sinAngle;
    float factor2 = FAST_SIN(t * angle) / sinAngle;
    
    slerpResult = directionA * factor1 + directionB * factor2;
} else {
    // Vectors are nearly identical, use linear interpolation
    slerpResult = directionA * (1.0f - t) + directionB * t;
}

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"SLERP result: (%.3f, %.3f, %.3f)", 
        slerpResult.x, slerpResult.y, slerpResult.z);
#endif
```

### 3.5 Performance Optimizations for Vector Operations

#### Fast Vector Operations Using Physics Class

```cpp
Physics& physics = PHYSICS_INSTANCE();

// Use optimized physics class methods for better performance
PhysicsVector3D vectorA(10.0f, 20.0f, 30.0f);
PhysicsVector3D vectorB(5.0f, 15.0f, 25.0f);

// Fast magnitude calculation using lookup tables
float fastMagnitude = physics.FastMagnitude(vectorA);

// Fast normalization
PhysicsVector3D fastNormalized = physics.FastNormalize(vectorA);

// Fast dot product
float fastDot = physics.FastDotProduct(vectorA, vectorB);

// Fast cross product
PhysicsVector3D fastCross = physics.FastCrossProduct(vectorA, vectorB);

// Fast distance calculation
float fastDistance = physics.FastDistance(vectorA, vectorB);

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"Fast operations: magnitude=%.2f, dot=%.2f, distance=%.2f", 
        fastMagnitude, fastDot, fastDistance);
#endif
```

#### Batch Vector Operations

```cpp
// Process multiple vectors efficiently
std::vector<PhysicsVector3D> positions;
std::vector<PhysicsVector3D> velocities;
std::vector<PhysicsVector3D> results;

// Reserve memory for better performance
positions.reserve(1000);
velocities.reserve(1000);
results.reserve(1000);

// Fill with sample data
for (int i = 0; i < 1000; ++i) {
    positions.push_back(PhysicsVector3D(i * 1.0f, i * 2.0f, i * 3.0f));
    velocities.push_back(PhysicsVector3D(i * 0.1f, i * 0.2f, i * 0.3f));
}

// Batch process vectors
Physics& physics = PHYSICS_INSTANCE();
for (size_t i = 0; i < positions.size(); ++i) {
    // Apply velocity to position with time step
    float deltaTime = 0.016f;
    PhysicsVector3D newPosition = positions[i] + velocities[i] * deltaTime;
    
    // Normalize for direction calculations
    PhysicsVector3D direction = physics.FastNormalize(velocities[i]);
    
    results.push_back(newPosition + direction * 5.0f);
}

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"Processed %zu vectors in batch operation", results.size());
#endif
```

### 3.6 Common Vector Calculation Patterns

#### Reflect Vector Off Surface

```cpp
// Reflect incoming vector off a surface normal
PhysicsVector3D incomingVelocity(-10.0f, -20.0f, 0.0f);
PhysicsVector3D surfaceNormal(0.0f, 1.0f, 0.0f);  // Horizontal surface

// Calculate reflection: R = V - 2(V·N)N
float dotProduct = incomingVelocity.Dot(surfaceNormal);
PhysicsVector3D reflectedVelocity = incomingVelocity - surfaceNormal * (2.0f * dotProduct);

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"Reflection: incoming(%.2f, %.2f, %.2f) -> reflected(%.2f, %.2f, %.2f)", 
        incomingVelocity.x, incomingVelocity.y, incomingVelocity.z,
        reflectedVelocity.x, reflectedVelocity.y, reflectedVelocity.z);
#endif
```

#### Project Vector onto Plane

```cpp
// Project vector onto a plane defined by normal
PhysicsVector3D vector(10.0f, 20.0f, 30.0f);
PhysicsVector3D planeNormal(0.0f, 1.0f, 0.0f);  // XZ plane

// Project onto plane: projected = V - (V·N)N
float projection = vector.Dot(planeNormal);
PhysicsVector3D projectedVector = vector - planeNormal * projection;

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"Plane projection: original(%.2f, %.2f, %.2f) -> projected(%.2f, %.2f, %.2f)", 
        vector.x, vector.y, vector.z,
        projectedVector.x, projectedVector.y, projectedVector.z);
#endif
```

#### Calculate Centroid of Points

```cpp
// Calculate center point of multiple vectors
std::vector<PhysicsVector3D> points = {
    PhysicsVector3D(10.0f, 20.0f, 30.0f),
    PhysicsVector3D(15.0f, 25.0f, 35.0f),
    PhysicsVector3D(5.0f, 15.0f, 25.0f),
    PhysicsVector3D(20.0f, 30.0f, 40.0f)
};

PhysicsVector3D centroid(0.0f, 0.0f, 0.0f);
for (const auto& point : points) {
    centroid += point;
}
centroid *= (1.0f / static_cast<float>(points.size()));

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"Centroid of %zu points: (%.2f, %.2f, %.2f)", 
        points.size(), centroid.x, centroid.y, centroid.z);
#endif
```

---

**Chapter 3 Complete**

This completes Chapter 3 of the Physics Class usage guide. The chapter provides comprehensive coverage of physics vector operations including 2D and 3D vector mathematics, practical applications, performance optimizations, and common calculation patterns.

# Physics Class - Chapter 4: Curved Path Calculations (2D and 3D)

## 4. Curved Path Calculations (2D and 3D)

### 4.1 Introduction to Curved Paths

The Physics class provides sophisticated curved path generation using Catmull-Rom spline interpolation. This system allows you to create smooth, natural-looking paths from a series of control points, with automatic tangent and curvature calculations for realistic movement.

#### Key Features
- Support for up to 1024 coordinates per path
- Automatic tangent vector calculation
- Curvature analysis for realistic motion
- Distance-based point retrieval
- Loop support for closed paths
- High-performance interpolation using MathPrecalculation

### 4.2 Creating 2D Curved Paths

#### Basic 2D Path Creation

```cpp
#include "Physics.h"

Physics& physics = PHYSICS_INSTANCE();

// Define control points for a curved path
std::vector<PhysicsVector2D> controlPoints = {
    PhysicsVector2D(0.0f, 0.0f),      // Start point
    PhysicsVector2D(50.0f, 25.0f),    // Control point 1
    PhysicsVector2D(100.0f, -10.0f),  // Control point 2
    PhysicsVector2D(150.0f, 30.0f),   // Control point 3
    PhysicsVector2D(200.0f, 0.0f)     // End point
};

// Create curved path with default resolution (100 points)
CurvedPath2D smoothPath = physics.CreateCurvedPath2D(controlPoints);

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_INFO, 
        L"Created 2D curved path with %zu coordinates, total length: %.2f", 
        smoothPath.coordinates.size(), smoothPath.totalLength);
#endif
```

#### Advanced 2D Path Creation with Custom Resolution

```cpp
// Create high-resolution path for detailed movement
int highResolution = 500;  // More interpolated points
CurvedPath2D detailedPath = physics.CreateCurvedPath2D(controlPoints, highResolution);

// Create low-resolution path for performance
int lowResolution = 50;   // Fewer interpolated points
CurvedPath2D performancePath = physics.CreateCurvedPath2D(controlPoints, lowResolution);

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"High-res path: %zu points, Low-res path: %zu points", 
        detailedPath.coordinates.size(), performancePath.coordinates.size());
#endif
```

#### Working with 2D Path Properties

```cpp
CurvedPath2D gamePath = physics.CreateCurvedPath2D(controlPoints);

// Get path information
float totalLength = gamePath.totalLength;
size_t pointCount = gamePath.coordinates.size();
bool isLooped = gamePath.isLooped;

// Access tangent angles at specific points
if (!gamePath.tangents.empty()) {
    float startTangent = gamePath.tangents[0];           // First point tangent
    float endTangent = gamePath.tangents.back();        // Last point tangent
    
    #if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, 
            L"Path tangents: start=%.2f radians, end=%.2f radians", 
            startTangent, endTangent);
    #endif
}

// Access curvature values for physics calculations
if (!gamePath.curvatures.empty()) {
    float maxCurvature = *std::max_element(gamePath.curvatures.begin(), gamePath.curvatures.end());
    
    #if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, 
            L"Maximum curvature in path: %.4f", maxCurvature);
    #endif
}
```

### 4.3 Creating 3D Curved Paths

#### Basic 3D Path Creation

```cpp
// Define 3D control points for spatial movement
std::vector<PhysicsVector3D> controlPoints3D = {
    PhysicsVector3D(0.0f, 0.0f, 0.0f),        // Start point
    PhysicsVector3D(50.0f, 25.0f, 10.0f),     // Control point 1
    PhysicsVector3D(100.0f, -10.0f, 30.0f),   // Control point 2
    PhysicsVector3D(150.0f, 30.0f, -5.0f),    // Control point 3
    PhysicsVector3D(200.0f, 0.0f, 20.0f)      // End point
};

// Create 3D curved path
CurvedPath3D spatialPath = physics.CreateCurvedPath3D(controlPoints3D);

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_INFO, 
        L"Created 3D curved path with %zu coordinates, total length: %.2f", 
        spatialPath.coordinates.size(), spatialPath.totalLength);
#endif
```

#### Complex 3D Path for Flight Trajectories

```cpp
// Create a complex flight path for aircraft or projectiles
std::vector<PhysicsVector3D> flightPath = {
    PhysicsVector3D(0.0f, 100.0f, 0.0f),      // Takeoff point
    PhysicsVector3D(200.0f, 200.0f, 50.0f),   // Climb
    PhysicsVector3D(500.0f, 300.0f, 100.0f),  // Cruise altitude
    PhysicsVector3D(800.0f, 250.0f, 80.0f),   // Begin descent
    PhysicsVector3D(1000.0f, 150.0f, 30.0f),  // Approach
    PhysicsVector3D(1200.0f, 100.0f, 0.0f)    // Landing
};

int flightResolution = 200;  // Smooth flight path
CurvedPath3D aircraftPath = physics.CreateCurvedPath3D(flightPath, flightResolution);

// Analyze path for flight dynamics
for (size_t i = 0; i < aircraftPath.curvatures.size(); ++i) {
    float curvature = aircraftPath.curvatures[i];
    
    // High curvature areas require reduced speed
    if (curvature > 0.01f) {
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"High curvature detected at point %zu: %.4f - reduce speed", 
                i, curvature);
        #endif
    }
}
```

### 4.4 Retrieving Points on Curved Paths

#### Time-Based Point Retrieval

```cpp
CurvedPath2D gamePath = physics.CreateCurvedPath2D(controlPoints);

// Get points at specific time parameters (0.0 to 1.0)
PhysicsVector2D startPoint = physics.GetCurvePoint2D(gamePath, 0.0f);    // Beginning
PhysicsVector2D midPoint = physics.GetCurvePoint2D(gamePath, 0.5f);      // Middle
PhysicsVector2D endPoint = physics.GetCurvePoint2D(gamePath, 1.0f);      // End

// Animate along path
float animationTime = 0.0f;
float animationSpeed = 0.5f;  // Complete path in 2 seconds
float deltaTime = 0.016f;     // 60 FPS

while (animationTime <= 1.0f) {
    PhysicsVector2D currentPosition = physics.GetCurvePoint2D(gamePath, animationTime);
    PhysicsVector2D currentTangent = physics.GetCurveTangent2D(gamePath, animationTime);
    
    // Use position and tangent for object movement and orientation
    #if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, 
            L"Animation t=%.3f: pos(%.2f, %.2f), tangent(%.3f, %.3f)", 
            animationTime, currentPosition.x, currentPosition.y, 
            currentTangent.x, currentTangent.y);
    #endif
    
    animationTime += animationSpeed * deltaTime;
}
```

#### Distance-Based Point Retrieval for Consistent Speed

```cpp
CurvedPath3D racingTrack = physics.CreateCurvedPath3D(controlPoints3D);

float constantSpeed = 50.0f;  // Units per second
float currentDistance = 0.0f;
float maxDistance = racingTrack.totalLength;

while (currentDistance < maxDistance) {
    // Get position at specific distance along path
    PhysicsVector3D carPosition = racingTrack.GetPointAtDistance(currentDistance);
    PhysicsVector3D carDirection = racingTrack.GetTangentAtDistance(currentDistance);
    
    // Calculate banking angle based on curvature
    float t = currentDistance / maxDistance;
    size_t curveIndex = static_cast<size_t>(t * (racingTrack.curvatures.size() - 1));
    float curvature = racingTrack.curvatures[curveIndex];
    float bankingAngle = curvature * 30.0f;  // Convert curvature to degrees
    
    #if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, 
            L"Car at distance %.2f: pos(%.2f, %.2f, %.2f), banking: %.1f degrees", 
            currentDistance, carPosition.x, carPosition.y, carPosition.z, bankingAngle);
    #endif
    
    currentDistance += constantSpeed * deltaTime;
}
```

### 4.5 Path Manipulation and Modification

#### Dynamic Path Modification

```cpp
// Start with basic path
CurvedPath2D dynamicPath;

// Add points dynamically
dynamicPath.AddPoint(PhysicsVector2D(0.0f, 0.0f));
dynamicPath.AddPoint(PhysicsVector2D(25.0f, 50.0f));
dynamicPath.AddPoint(PhysicsVector2D(75.0f, 25.0f));
dynamicPath.AddPoint(PhysicsVector2D(100.0f, 75.0f));

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_INFO, 
        L"Dynamic path created with %zu points, length: %.2f", 
        dynamicPath.coordinates.size(), dynamicPath.totalLength);
#endif

// Modify path by adding more points
for (int i = 0; i < 10; ++i) {
    float x = 100.0f + i * 20.0f;
    float y = 75.0f + FAST_SIN(i * 0.5f) * 25.0f;  // Sinusoidal variation
    
    dynamicPath.AddPoint(PhysicsVector2D(x, y));
}

// Path automatically recalculates tangents and curvatures
#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_INFO, 
        L"Extended path now has %zu points, new length: %.2f", 
        dynamicPath.coordinates.size(), dynamicPath.totalLength);
#endif
```

#### Creating Looped Paths

```cpp
// Create circular racing track
std::vector<PhysicsVector2D> trackPoints;
int numPoints = 8;
float radius = 100.0f;

for (int i = 0; i < numPoints; ++i) {
    float angle = (i * 2.0f * XM_PI) / numPoints;
    float x = radius * FAST_COS(angle);
    float y = radius * FAST_SIN(angle);
    trackPoints.push_back(PhysicsVector2D(x, y));
}

// Add the first point again to close the loop
trackPoints.push_back(trackPoints[0]);

CurvedPath2D racingTrack = physics.CreateCurvedPath2D(trackPoints);
racingTrack.isLooped = true;  // Mark as looped path

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_INFO, 
        L"Created looped racing track with %zu points", 
        racingTrack.coordinates.size());
#endif
```

#### Path Segment Analysis

```cpp
CurvedPath3D analyzePath = physics.CreateCurvedPath3D(controlPoints3D);

// Calculate length of specific segments
float segment1Length = physics.CalculateCurveLength3D(analyzePath, 0.0f, 0.3f);    // First 30%
float segment2Length = physics.CalculateCurveLength3D(analyzePath, 0.3f, 0.7f);    // Middle 40%
float segment3Length = physics.CalculateCurveLength3D(analyzePath, 0.7f, 1.0f);    // Last 30%

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"Path segments: first=%.2f, middle=%.2f, last=%.2f", 
        segment1Length, segment2Length, segment3Length);
#endif

// Find points of maximum curvature for gameplay mechanics
float maxCurvature = 0.0f;
size_t maxCurvatureIndex = 0;

for (size_t i = 0; i < analyzePath.curvatures.size(); ++i) {
    if (analyzePath.curvatures[i] > maxCurvature) {
        maxCurvature = analyzePath.curvatures[i];
        maxCurvatureIndex = i;
    }
}

PhysicsVector3D tightestTurn = analyzePath.coordinates[maxCurvatureIndex];

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_INFO, 
        L"Tightest turn at (%.2f, %.2f, %.2f) with curvature %.4f", 
        tightestTurn.x, tightestTurn.y, tightestTurn.z, maxCurvature);
#endif
```

### 4.6 Practical Applications of Curved Paths

#### Camera Movement Along Paths

```cpp
// Create smooth camera path for cinematics
std::vector<PhysicsVector3D> cameraKeyframes = {
    PhysicsVector3D(0.0f, 50.0f, 100.0f),     // Wide shot
    PhysicsVector3D(50.0f, 30.0f, 50.0f),     // Medium shot
    PhysicsVector3D(80.0f, 10.0f, 20.0f),     // Close-up
    PhysicsVector3D(100.0f, 20.0f, 30.0f),    // Pull back
    PhysicsVector3D(120.0f, 60.0f, 80.0f)     // Final wide
};

CurvedPath3D cameraPath = physics.CreateCurvedPath3D(cameraKeyframes, 300);

// Animate camera with smooth motion
class CinematicCamera {
public:
    void Update(float deltaTime) {
        cinematicTime += cameraSpeed * deltaTime;
        cinematicTime = std::clamp(cinematicTime, 0.0f, 1.0f);
        
        // Get smooth camera position and direction
        PhysicsVector3D cameraPos = physics.GetCurvePoint3D(cameraPath, cinematicTime);
        PhysicsVector3D cameraDir = physics.GetCurveTangent3D(cameraPath, cinematicTime);
        
        // Apply to camera system
        SetCameraPosition(cameraPos);
        SetCameraDirection(cameraDir.Normalized());
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"Camera cinematic t=%.3f: pos(%.2f, %.2f, %.2f)", 
                cinematicTime, cameraPos.x, cameraPos.y, cameraPos.z);
        #endif
    }
    
private:
    float cinematicTime = 0.0f;
    float cameraSpeed = 0.2f;  // Complete path in 5 seconds
    Physics& physics = PHYSICS_INSTANCE();
    CurvedPath3D cameraPath;
};
```

#### AI Pathfinding with Curved Paths

```cpp
// Create AI patrol route with smooth curves
class AIPatrolAgent {
public:
    void SetPatrolRoute(const std::vector<PhysicsVector3D>& waypoints) {
        patrolPath = physics.CreateCurvedPath3D(waypoints, 150);
        patrolPath.isLooped = true;  // Continuous patrol
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"AI patrol route set with %zu waypoints, total length: %.2f", 
                waypoints.size(), patrolPath.totalLength);
        #endif
    }
    
    void UpdatePatrol(float deltaTime) {
        // Move along path at constant speed
        currentDistance += patrolSpeed * deltaTime;
        
        // Loop back to start when reaching end
        if (currentDistance >= patrolPath.totalLength) {
            currentDistance = 0.0f;
        }
        
        // Get current position and movement direction
        PhysicsVector3D targetPosition = patrolPath.GetPointAtDistance(currentDistance);
        PhysicsVector3D moveDirection = patrolPath.GetTangentAtDistance(currentDistance);
        
        // Update AI agent
        SetAgentPosition(targetPosition);
        SetAgentFacing(moveDirection.Normalized());
        
        // Adjust speed based on path curvature
        float t = currentDistance / patrolPath.totalLength;
        size_t curveIndex = static_cast<size_t>(t * (patrolPath.curvatures.size() - 1));
        float curvature = patrolPath.curvatures[curveIndex];
        
        float speedMultiplier = 1.0f - (curvature * 2.0f);  // Slow down on tight curves
        speedMultiplier = std::clamp(speedMultiplier, 0.3f, 1.0f);
        
        currentSpeed = patrolSpeed * speedMultiplier;
    }
    
private:
    CurvedPath3D patrolPath;
    float currentDistance = 0.0f;
    float patrolSpeed = 25.0f;    // Base patrol speed
    float currentSpeed = 25.0f;   // Current adjusted speed
    Physics& physics = PHYSICS_INSTANCE();
};
```

#### Projectile Trajectory Curves

```cpp
// Create curved projectile paths for non-ballistic weapons
class CurvedProjectile {
public:
    void FireProjectile(const PhysicsVector3D& startPos, const PhysicsVector3D& targetPos) {
        // Create control points for curved trajectory
        PhysicsVector3D midPoint1 = startPos + (targetPos - startPos) * 0.33f;
        midPoint1.y += 50.0f;  // Add arc height
        
        PhysicsVector3D midPoint2 = startPos + (targetPos - startPos) * 0.66f;
        midPoint2.y += 30.0f;  // Descending arc
        
        std::vector<PhysicsVector3D> trajectoryPoints = {
            startPos, midPoint1, midPoint2, targetPos
        };
        
        projectilePath = physics.CreateCurvedPath3D(trajectoryPoints, 100);
        isFlying = true;
        flightTime = 0.0f;
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Projectile fired with curved trajectory, length: %.2f", 
                projectilePath.totalLength);
        #endif
    }
    
    void UpdateProjectile(float deltaTime) {
        if (!isFlying) return;
        
        flightTime += deltaTime;
        float t = flightTime / totalFlightTime;
        
        if (t >= 1.0f) {
            // Projectile reached target
            isFlying = false;
            OnProjectileImpact();
            return;
        }
        
        // Get projectile position and orientation
        PhysicsVector3D position = physics.GetCurvePoint3D(projectilePath, t);
        PhysicsVector3D direction = physics.GetCurveTangent3D(projectilePath, t);
        
        // Update projectile visual representation
        SetProjectilePosition(position);
        SetProjectileRotation(direction.Normalized());
    }
    
private:
    CurvedPath3D projectilePath;
    bool isFlying = false;
    float flightTime = 0.0f;
    float totalFlightTime = 2.0f;  // 2 seconds flight time
    Physics& physics = PHYSICS_INSTANCE();
};
```

### 4.7 Performance Optimization for Curved Paths

#### Path Caching Strategy

```cpp
class PathCache {
public:
    static PathCache& GetInstance() {
        static PathCache instance;
        return instance;
    }
    
    CurvedPath3D GetCachedPath(const std::string& pathName) {
        auto it = cachedPaths.find(pathName);
        if (it != cachedPaths.end()) {
            #if defined(_DEBUG_PHYSICS_)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                    L"Retrieved cached path: %S", pathName.c_str());
            #endif
            return it->second;
        }
        
        // Return empty path if not found
        return CurvedPath3D();
    }
    
    void CachePath(const std::string& pathName, const CurvedPath3D& path) {
        cachedPaths[pathName] = path;
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Cached path '%S' with %zu points", 
                pathName.c_str(), path.coordinates.size());
        #endif
    }
    
    void ClearCache() {
        cachedPaths.clear();
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Path cache cleared");
        #endif
    }
    
private:
    std::unordered_map<std::string, CurvedPath3D> cachedPaths;
};

// Usage example
PathCache& cache = PathCache::GetInstance();
CurvedPath3D racingTrack = cache.GetCachedPath("main_track");

if (racingTrack.coordinates.empty()) {
    // Create and cache the path
    racingTrack = physics.CreateCurvedPath3D(trackControlPoints, 200);
    cache.CachePath("main_track", racingTrack);
}
```

#### Level-of-Detail (LOD) for Paths

```cpp
class AdaptivePathLOD {
public:
    CurvedPath3D GetPathForDistance(const std::vector<PhysicsVector3D>& controlPoints, 
                                   float viewerDistance) {
        int resolution;
        
        if (viewerDistance < 50.0f) {
            resolution = 300;  // High detail for close viewing
        } else if (viewerDistance < 200.0f) {
            resolution = 150;  // Medium detail
        } else {
            resolution = 50;   // Low detail for distant paths
        }
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"Creating path with LOD resolution %d for distance %.2f", 
                resolution, viewerDistance);
        #endif
        
        return physics.CreateCurvedPath3D(controlPoints, resolution);
    }
    
private:
    Physics& physics = PHYSICS_INSTANCE();
};
```

### 4.8 Error Handling and Validation

#### Path Validation Functions

```cpp
bool ValidatePath2D(const CurvedPath2D& path) {
    // Check if path has minimum required points
    if (path.coordinates.size() < 2) {
        #if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, 
                L"Invalid 2D path: insufficient coordinates");
        #endif
        return false;
    }
    
    // Check for valid total length
    if (path.totalLength <= 0.0f) {
        #if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, 
                L"Invalid 2D path: zero or negative length");
        #endif
        return false;
    }
    
    // Check tangent and curvature arrays match coordinates
    if (path.tangents.size() != path.coordinates.size() ||
        path.curvatures.size() != path.coordinates.size()) {
        #if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, 
                L"2D path: tangent/curvature arrays size mismatch");
        #endif
    }
    
    return true;
}

bool ValidatePath3D(const CurvedPath3D& path) {
    // Check if path has minimum required points
    if (path.coordinates.size() < 2) {
        #if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, 
                L"Invalid 3D path: insufficient coordinates");
        #endif
        return false;
    }
    
    // Check for valid total length
    if (path.totalLength <= 0.0f) {
        #if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, 
                L"Invalid 3D path: zero or negative length");
        #endif
        return false;
    }
    
    // Validate that no consecutive points are identical
    for (size_t i = 1; i < path.coordinates.size(); ++i) {
        PhysicsVector3D diff = path.coordinates[i] - path.coordinates[i-1];
        if (diff.MagnitudeSquared() < MIN_VELOCITY_THRESHOLD) {
            #if defined(_DEBUG_PHYSICS_)
                debug.logDebugMessage(LogLevel::LOG_WARNING, 
                    L"3D path has duplicate consecutive points at index %zu", i);
            #endif
        }
    }
    
    return true;
}
```

---

**Chapter 4 Complete**

This completes Chapter 4 of the Physics Class usage guide covering comprehensive curved path calculations for both 2D and 3D scenarios, including practical applications, performance optimization, and error handling.

# Physics Class - Chapter 5: Reflection Path Calculations

## 5. Reflection Path Calculations

### 5.1 Introduction to Reflection Physics

The Physics class provides comprehensive reflection calculations for realistic bouncing and collision responses. The reflection system supports customizable restitution (bounce) coefficients, friction effects, and energy loss calculations, making it ideal for ball physics, projectile impacts, and collision responses in gaming environments.

#### Key Features
- Accurate reflection calculations using surface normals
- Customizable restitution coefficients (0.0 = no bounce, 1.0 = perfect bounce)
- Friction modeling for realistic surface interactions
- Energy loss tracking for realistic physics simulation
- Support for both 2D and 3D reflection calculations
- Multiple bounce trajectory prediction
- Integration with MathPrecalculation for optimized performance

### 5.2 Basic 3D Reflection Calculations

#### Simple Reflection with Default Parameters

```cpp
#include "Physics.h"

Physics& physics = PHYSICS_INSTANCE();

// Define incoming velocity and surface normal
PhysicsVector3D incomingVelocity(-10.0f, -20.0f, -5.0f);  // Ball moving down and left
PhysicsVector3D surfaceNormal(0.0f, 1.0f, 0.0f);          // Horizontal ground surface

// Calculate reflection with default restitution and friction
ReflectionData reflection = physics.CalculateReflection(incomingVelocity, surfaceNormal);

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_INFO, 
        L"Reflection calculated: incoming(%.2f, %.2f, %.2f) -> reflected(%.2f, %.2f, %.2f)",
        reflection.incomingVelocity.x, reflection.incomingVelocity.y, reflection.incomingVelocity.z,
        reflection.reflectedVelocity.x, reflection.reflectedVelocity.y, reflection.reflectedVelocity.z);
    
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"Reflection properties: restitution=%.2f, friction=%.2f, energy_loss=%.3f",
        reflection.restitution, reflection.friction, reflection.energyLoss);
#endif

// Apply reflected velocity to physics body
PhysicsBody ball;
ball.velocity = reflection.reflectedVelocity;
```

#### Advanced Reflection with Custom Parameters

```cpp
// Define surface properties
float restitution = 0.7f;  // 70% bounce efficiency
float friction = 0.3f;     // 30% friction coefficient

PhysicsVector3D ballVelocity(15.0f, -25.0f, 10.0f);
PhysicsVector3D wallNormal(-1.0f, 0.0f, 0.0f);  // Right-facing wall

// Calculate reflection with custom surface properties
ReflectionData wallBounce = physics.CalculateReflection(ballVelocity, wallNormal, restitution, friction);

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_INFO, 
        L"Wall reflection: energy loss = %.1f%%, speed reduction = %.2f -> %.2f",
        wallBounce.energyLoss * 100.0f,
        ballVelocity.Magnitude(), wallBounce.reflectedVelocity.Magnitude());
#endif

// Analyze reflection quality
float speedReduction = 1.0f - (wallBounce.reflectedVelocity.Magnitude() / ballVelocity.Magnitude());
if (speedReduction > 0.5f) {
    #if defined(_DEBUG_PHYSICS_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, 
            L"High energy loss in reflection - check surface parameters");
    #endif
}
```

### 5.3 2D Reflection Calculations

#### Basic 2D Reflection for Side-Scrolling Games

```cpp
// 2D ball bouncing off platforms
PhysicsVector2D ballVelocity2D(20.0f, -30.0f);    // Moving right and down
PhysicsVector2D platformNormal(0.0f, 1.0f);       // Upward-facing platform

float platformRestitution = 0.8f;  // Bouncy platform

PhysicsVector2D reflected2D = physics.CalculateReflection2D(ballVelocity2D, platformNormal, platformRestitution);

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"2D Reflection: (%.2f, %.2f) -> (%.2f, %.2f)",
        ballVelocity2D.x, ballVelocity2D.y, reflected2D.x, reflected2D.y);
#endif

// Apply to 2D physics body
PhysicsBody ball2D;
ball2D.velocity = PhysicsVector3D(reflected2D.x, reflected2D.y, 0.0f);
```

#### Angled Surface Reflections in 2D

```cpp
// Ball hitting angled ramp
PhysicsVector2D ballVelocity(25.0f, -15.0f);

// 45-degree ramp surface normal
float rampAngle = XM_PI / 4.0f;  // 45 degrees
PhysicsVector2D rampNormal(FAST_SIN(rampAngle), FAST_COS(rampAngle));

float rampRestitution = 0.6f;  // Moderate bounce
PhysicsVector2D rampReflection = physics.CalculateReflection2D(ballVelocity, rampNormal, rampRestitution);

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_INFO, 
        L"Ramp reflection: angle=%.1f degrees, reflection(%.2f, %.2f)",
        rampAngle * 180.0f / XM_PI, rampReflection.x, rampReflection.y);
#endif

// Calculate reflection angle for gameplay mechanics
float incidentAngle = FAST_ATAN2(ballVelocity.y, ballVelocity.x);
float reflectedAngle = FAST_ATAN2(rampReflection.y, rampReflection.x);
float angleDifference = reflectedAngle - incidentAngle;

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"Angle analysis: incident=%.1f°, reflected=%.1f°, difference=%.1f°",
        incidentAngle * 180.0f / XM_PI, reflectedAngle * 180.0f / XM_PI, 
        angleDifference * 180.0f / XM_PI);
#endif
```

### 5.4 Multiple Bounce Trajectory Prediction

#### Predicting Bounce Sequences

```cpp
// Starting conditions for bouncing ball
PhysicsVector3D startPosition(0.0f, 50.0f, 0.0f);
PhysicsVector3D initialVelocity(30.0f, -10.0f, 20.0f);

// Define surface normals for different collision surfaces
std::vector<PhysicsVector3D> surfaceNormals = {
    PhysicsVector3D(0.0f, 1.0f, 0.0f),   // Ground
    PhysicsVector3D(-1.0f, 0.0f, 0.0f),  // Right wall
    PhysicsVector3D(0.0f, 1.0f, 0.0f),   // Ground again
    PhysicsVector3D(0.0f, 0.0f, -1.0f),  // Front wall
    PhysicsVector3D(0.0f, 1.0f, 0.0f)    // Ground final
};

int maxBounces = 5;

// Calculate complete bounce trajectory
std::vector<PhysicsVector3D> bounceTrajectory = physics.CalculateMultipleBounces(
    startPosition, initialVelocity, surfaceNormals, maxBounces);

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_INFO, 
        L"Calculated bounce trajectory with %zu positions over %d potential bounces",
        bounceTrajectory.size(), maxBounces);
#endif

// Analyze trajectory for gameplay purposes
for (size_t i = 1; i < bounceTrajectory.size(); ++i) {
    PhysicsVector3D segmentVector = bounceTrajectory[i] - bounceTrajectory[i-1];
    float segmentLength = segmentVector.Magnitude();
    
    #if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, 
            L"Bounce %zu: position(%.2f, %.2f, %.2f), segment_length=%.2f",
            i, bounceTrajectory[i].x, bounceTrajectory[i].y, bounceTrajectory[i].z, segmentLength);
    #endif
}
```

#### Advanced Multi-Surface Bounce Prediction

```cpp
class AdvancedBouncePredictor {
public:
    struct BounceEvent {
        PhysicsVector3D position;
        PhysicsVector3D velocity;
        PhysicsVector3D surfaceNormal;
        float timeOfImpact;
        float energyRemaining;
        int bounceNumber;
    };
    
    std::vector<BounceEvent> PredictComplexBounces(
        const PhysicsVector3D& startPos, 
        const PhysicsVector3D& startVel,
        const std::vector<SurfaceData>& surfaces,
        float timeStep = 0.016f,
        float minEnergy = 0.01f) {
        
        std::vector<BounceEvent> bounceEvents;
        
        PhysicsVector3D currentPos = startPos;
        PhysicsVector3D currentVel = startVel;
        float currentTime = 0.0f;
        int bounceCount = 0;
        float currentEnergy = startVel.MagnitudeSquared() * 0.5f;  // Kinetic energy
        
        while (currentEnergy > minEnergy && bounceCount < 20) {
            // Simulate movement until next collision
            BounceEvent nextBounce = SimulateToNextCollision(currentPos, currentVel, surfaces, currentTime, timeStep);
            
            if (nextBounce.bounceNumber == -1) {
                break;  // No more collisions
            }
            
            // Calculate reflection
            ReflectionData reflection = physics.CalculateReflection(
                nextBounce.velocity, nextBounce.surfaceNormal, 0.8f, 0.2f);
            
            // Update for next iteration
            currentPos = nextBounce.position;
            currentVel = reflection.reflectedVelocity;
            currentTime = nextBounce.timeOfImpact;
            currentEnergy *= (1.0f - reflection.energyLoss);
            bounceCount++;
            
            // Store bounce event
            nextBounce.velocity = currentVel;
            nextBounce.energyRemaining = currentEnergy;
            nextBounce.bounceNumber = bounceCount;
            bounceEvents.push_back(nextBounce);
            
            #if defined(_DEBUG_PHYSICS_)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                    L"Bounce %d at t=%.3f: pos(%.2f, %.2f, %.2f), energy=%.2f",
                    bounceCount, currentTime, currentPos.x, currentPos.y, currentPos.z, currentEnergy);
            #endif
        }
        
        return bounceEvents;
    }
    
private:
    Physics& physics = PHYSICS_INSTANCE();
    
    struct SurfaceData {
        PhysicsVector3D normal;
        PhysicsVector3D point;
        float restitution;
        float friction;
    };
    
    BounceEvent SimulateToNextCollision(const PhysicsVector3D& pos, const PhysicsVector3D& vel,
                                       const std::vector<SurfaceData>& surfaces,
                                       float startTime, float timeStep) {
        // Simplified collision detection - in real implementation,
        // this would use proper ray-surface intersection
        BounceEvent event;
        event.bounceNumber = -1;  // No collision found
        
        // This is a placeholder - implement actual collision detection
        return event;
    }
};
```

### 5.5 Surface Material Properties

#### Defining Different Surface Types

```cpp
enum class SurfaceType {
    CONCRETE,
    RUBBER,
    ICE,
    CARPET,
    METAL,
    WOOD,
    GLASS
};

struct SurfaceMaterial {
    float restitution;
    float friction;
    float energyAbsorption;
    std::wstring name;
};

class SurfaceManager {
public:
    SurfaceManager() {
        // Initialize surface materials
        materials[SurfaceType::CONCRETE] = {0.2f, 0.8f, 0.7f, L"Concrete"};
        materials[SurfaceType::RUBBER] = {0.9f, 0.9f, 0.1f, L"Rubber"};
        materials[SurfaceType::ICE] = {0.1f, 0.05f, 0.8f, L"Ice"};
        materials[SurfaceType::CARPET] = {0.1f, 0.95f, 0.9f, L"Carpet"};
        materials[SurfaceType::METAL] = {0.3f, 0.2f, 0.6f, L"Metal"};
        materials[SurfaceType::WOOD] = {0.4f, 0.6f, 0.5f, L"Wood"};
        materials[SurfaceType::GLASS] = {0.1f, 0.1f, 0.8f, L"Glass"};
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_INFO, 
                L"Surface materials initialized with 7 surface types");
        #endif
    }
    
    ReflectionData CalculateReflectionForSurface(const PhysicsVector3D& velocity,
                                               const PhysicsVector3D& normal,
                                               SurfaceType surfaceType) {
        auto it = materials.find(surfaceType);
        if (it == materials.end()) {
            #if defined(_DEBUG_PHYSICS_)
                debug.logLevelMessage(LogLevel::LOG_WARNING, 
                    L"Unknown surface type - using default material");
            #endif
            return physics.CalculateReflection(velocity, normal);
        }
        
        const SurfaceMaterial& material = it->second;
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"Calculating reflection for surface: %s (rest=%.2f, fric=%.2f)",
                material.name.c_str(), material.restitution, material.friction);
        #endif
        
        return physics.CalculateReflection(velocity, normal, material.restitution, material.friction);
    }
    
private:
    std::unordered_map<SurfaceType, SurfaceMaterial> materials;
    Physics& physics = PHYSICS_INSTANCE();
};
```

#### Using Surface Materials in Game Logic

```cpp
// Example: Ball bouncing on different surfaces
class BouncingBall {
public:
    void HandleSurfaceCollision(SurfaceType surface, const PhysicsVector3D& collisionNormal) {
        SurfaceManager& surfaceManager = SurfaceManager::GetInstance();
        
        // Calculate reflection based on surface material
        ReflectionData reflection = surfaceManager.CalculateReflectionForSurface(
            physicsBody.velocity, collisionNormal, surface);
        
        // Apply reflection to ball
        physicsBody.velocity = reflection.reflectedVelocity;
        
        // Handle special surface effects
        HandleSurfaceEffects(surface, reflection);
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Ball collision with surface, new velocity: (%.2f, %.2f, %.2f)",
                physicsBody.velocity.x, physicsBody.velocity.y, physicsBody.velocity.z);
        #endif
    }
    
private:
    void HandleSurfaceEffects(SurfaceType surface, const ReflectionData& reflection) {
        switch (surface) {
            case SurfaceType::ICE:
                // Ice creates sliding effect - reduce friction further
                physicsBody.velocity *= 1.1f;  // Slight speed boost on ice
                PlaySoundEffect("ice_slide");
                break;
                
            case SurfaceType::RUBBER:
                // Rubber gives extra bounce
                if (reflection.reflectedVelocity.y > 0) {
                    physicsBody.velocity.y *= 1.2f;  // Extra vertical bounce
                }
                PlaySoundEffect("rubber_bounce");
                break;
                
            case SurfaceType::CARPET:
                // Carpet dampens movement significantly
                physicsBody.velocity *= 0.7f;  // Additional dampening
                PlaySoundEffect("soft_thud");
                break;
                
            case SurfaceType::GLASS:
                // Glass might shatter on high-energy impacts
                float impactEnergy = reflection.incomingVelocity.MagnitudeSquared();
                if (impactEnergy > 100.0f) {
                    TriggerGlassShatter();
                }
                PlaySoundEffect("glass_impact");
                break;
        }
    }
    
    PhysicsBody physicsBody;
};
```

### 5.6 Realistic Reflection Scenarios

#### Pool/Billiards Ball Physics

```cpp
class BilliardsBall {
public:
    void HandleCushionBounce(const PhysicsVector3D& cushionNormal) {
        // Billiards cushions have specific properties
        float cushionRestitution = 0.85f;  // High bounce efficiency
        float cushionFriction = 0.15f;     // Low friction for smooth bounces
        
        ReflectionData cushionBounce = physics.CalculateReflection(
            velocity, cushionNormal, cushionRestitution, cushionFriction);
        
        // Apply reflection
        velocity = cushionBounce.reflectedVelocity;
        
        // Add slight random variation for realism
        float randomFactor = 0.02f;  // 2% random variation
        velocity.x += (static_cast<float>(rand()) / RAND_MAX - 0.5f) * randomFactor * velocity.Magnitude();
        velocity.z += (static_cast<float>(rand()) / RAND_MAX - 0.5f) * randomFactor * velocity.Magnitude();
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"Billiards cushion bounce: energy_loss=%.3f, new_speed=%.2f",
                cushionBounce.energyLoss, velocity.Magnitude());
        #endif
        
        PlaySoundEffect("cushion_bounce", velocity.Magnitude() / 50.0f);  // Volume based on speed
    }
    
    void HandleBallToBallCollision(BilliardsBall& otherBall) {
        // Calculate collision normal
        PhysicsVector3D collisionNormal = (otherBall.position - position).Normalized();
        
        // Calculate relative velocity
        PhysicsVector3D relativeVelocity = velocity - otherBall.velocity;
        float velocityAlongNormal = relativeVelocity.Dot(collisionNormal);
        
        // Do not resolve if velocities are separating
        if (velocityAlongNormal > 0) return;
        
        // Calculate collision response (elastic collision)
        float restitution = 0.95f;  // Near-perfect elastic collision
        float impulse = -(1 + restitution) * velocityAlongNormal;
        impulse /= (1.0f / mass + 1.0f / otherBall.mass);
        
        PhysicsVector3D impulseVector = collisionNormal * impulse;
        
        // Apply impulse to both balls
        velocity += impulseVector / mass;
        otherBall.velocity -= impulseVector / otherBall.mass;
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Ball collision: impulse=%.2f, ball1_speed=%.2f, ball2_speed=%.2f",
                impulse, velocity.Magnitude(), otherBall.velocity.Magnitude());
        #endif
        
        PlaySoundEffect("ball_click", impulse / 10.0f);
    }
    
private:
    PhysicsVector3D position;
    PhysicsVector3D velocity;
    float mass = 0.17f;  // Standard billiard ball mass in kg
    Physics& physics = PHYSICS_INSTANCE();
};
```

#### Projectile Ricochet System

```cpp
class ProjectileRicochet {
public:
    bool AttemptRicochet(const PhysicsVector3D& impactNormal, float impactSpeed) {
        // Check if ricochet is possible based on impact angle
        float impactAngle = CalculateImpactAngle(projectileVelocity, impactNormal);
        
        // Shallow angles are more likely to ricochet
        float ricochetProbability = CalculateRicochetProbability(impactAngle, impactSpeed);
        
        float randomValue = static_cast<float>(rand()) / RAND_MAX;
        
        if (randomValue < ricochetProbability) {
            PerformRicochet(impactNormal, impactSpeed);
            return true;
        } else {
            HandleProjectileStop();
            return false;
        }
    }
    
private:
    float CalculateImpactAngle(const PhysicsVector3D& velocity, const PhysicsVector3D& normal) {
        float dotProduct = velocity.Normalized().Dot(normal);
        return FAST_ACOS(std::abs(dotProduct));  // Angle from surface normal
    }
    
    float CalculateRicochetProbability(float impactAngle, float impactSpeed) {
        // Shallow angles (close to 90 degrees) have higher ricochet chance
        float angleFactor = FAST_SIN(impactAngle);  // Higher for shallow angles
        
        // Higher speeds increase ricochet chance
        float speedFactor = std::clamp(impactSpeed / 100.0f, 0.0f, 1.0f);
        
        float baseProbability = 0.7f;  // 70% base chance
        float probability = baseProbability * angleFactor * speedFactor;
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"Ricochet calc: angle=%.1f°, speed=%.1f, probability=%.3f",
                impactAngle * 180.0f / XM_PI, impactSpeed, probability);
        #endif
        
        return std::clamp(probability, 0.0f, 0.95f);  // Max 95% chance
    }
    
    void PerformRicochet(const PhysicsVector3D& normal, float impactSpeed) {
        // Calculate ricochet with reduced energy
        float ricochetRestitution = 0.4f;  // Significant energy loss
        float surfaceFriction = 0.2f;
        
        ReflectionData ricochet = physics.CalculateReflection(
            projectileVelocity, normal, ricochetRestitution, surfaceFriction);
        
        // Apply ricochet velocity
        projectileVelocity = ricochet.reflectedVelocity;
        
        // Add slight random deviation for realism
        float deviation = 0.1f;  // 10% random deviation
        PhysicsVector3D randomDeviation(
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * deviation,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * deviation,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * deviation
        );
        
        projectileVelocity += randomDeviation * projectileVelocity.Magnitude();
        
        // Reduce remaining bounces
        remainingRicochets--;
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Projectile ricochet: new_speed=%.2f, remaining_ricochets=%d",
                projectileVelocity.Magnitude(), remainingRicochets);
        #endif
        
        PlaySoundEffect("ricochet", impactSpeed / 50.0f);
        CreateSparkEffect(lastImpactPosition);
    }
    
    PhysicsVector3D projectileVelocity;
    PhysicsVector3D lastImpactPosition;
    int remainingRicochets = 3;  // Maximum number of ricochets
    Physics& physics = PHYSICS_INSTANCE();
};
```

### 5.7 Performance Optimization for Reflection Calculations

#### Cached Surface Normal Calculations

```cpp
class ReflectionCache {
public:
    static ReflectionCache& GetInstance() {
        static ReflectionCache instance;
        return instance;
    }
    
    PhysicsVector3D GetCachedReflection(const PhysicsVector3D& velocity,
                                       const PhysicsVector3D& normal,
                                       float restitution) {
        // Create cache key
        ReflectionKey key = CreateCacheKey(velocity, normal, restitution);
        
        auto it = reflectionCache.find(key);
        if (it != reflectionCache.end()) {
            cacheHits++;
            return it->second;
        }
        
        // Calculate and cache new reflection
        ReflectionData reflection = physics.CalculateReflection(velocity, normal, restitution);
        reflectionCache[key] = reflection.reflectedVelocity;
        cacheMisses++;
        
        // Limit cache size
        if (reflectionCache.size() > MAX_CACHE_SIZE) {
            ClearOldestEntries();
        }
        
        return reflection.reflectedVelocity;
    }
    
    void PrintCacheStatistics() {
        float hitRate = static_cast<float>(cacheHits) / (cacheHits + cacheMisses);
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Reflection cache: hits=%d, misses=%d, hit_rate=%.2f%%, size=%zu",
                cacheHits, cacheMisses, hitRate * 100.0f, reflectionCache.size());
        #endif
    }
    
private:
    struct ReflectionKey {
        int velocityHash;
        int normalHash;
        int restitutionHash;
        
        bool operator==(const ReflectionKey& other) const {
            return velocityHash == other.velocityHash &&
                   normalHash == other.normalHash &&
                   restitutionHash == other.restitutionHash;
        }
    };
    
    struct ReflectionKeyHash {
        size_t operator()(const ReflectionKey& key) const {
            return std::hash<int>()(key.velocityHash) ^
                   std::hash<int>()(key.normalHash) ^
                   std::hash<int>()(key.restitutionHash);
        }
    };
    
    ReflectionKey CreateCacheKey(const PhysicsVector3D& velocity,
                                const PhysicsVector3D& normal,
                                float restitution) {
        // Quantize values for cache key generation
        ReflectionKey key;
        key.velocityHash = static_cast<int>(velocity.x * 100) ^ 
                          static_cast<int>(velocity.y * 100) ^ 
                          static_cast<int>(velocity.z * 100);
        key.normalHash = static_cast<int>(normal.x * 1000) ^ 
                        static_cast<int>(normal.y * 1000) ^ 
                        static_cast<int>(normal.z * 1000);
        key.restitutionHash = static_cast<int>(restitution * 1000);
        
        return key;
    }
    
    void ClearOldestEntries() {
        // Simple cache eviction - clear half the cache
        size_t targetSize = MAX_CACHE_SIZE / 2;
        auto it = reflectionCache.begin();
        
        while (reflectionCache.size() > targetSize && it != reflectionCache.end()) {
            it = reflectionCache.erase(it);
        }
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"Reflection cache cleared to %zu entries", reflectionCache.size());
        #endif
    }
    
    std::unordered_map<ReflectionKey, PhysicsVector3D, ReflectionKeyHash> reflectionCache;
    static const size_t MAX_CACHE_SIZE = 1000;
    int cacheHits = 0;
    int cacheMisses = 0;
    Physics& physics = PHYSICS_INSTANCE();
};
```

### 5.8 Advanced Reflection Applications

#### Water Surface Reflections

```cpp
class WaterSurfaceReflection {
public:
    ReflectionData CalculateWaterReflection(const PhysicsVector3D& velocity,
                                           const PhysicsVector3D& waterNormal,
                                           float waterDepth,
                                           float waveHeight = 0.0f) {
        // Water has dynamic properties based on conditions
        float baseRestitution = 0.1f;   // Water absorbs most energy
        float baseFriction = 0.8f;      // High resistance
        
        // Adjust for wave conditions
        if (waveHeight > 0.5f) {
            baseRestitution += waveHeight * 0.1f;  // Rougher water reflects more
            baseFriction += waveHeight * 0.1f;     // More turbulence
        }
        
        // Deep water absorbs more energy
        float depthFactor = std::clamp(1.0f - (waterDepth / 10.0f), 0.2f, 1.0f);
        baseRestitution *= depthFactor;
        
        ReflectionData waterReflection = physics.CalculateReflection(
            velocity, waterNormal, baseRestitution, baseFriction);
        
        // Add water-specific effects
        AddWaterSplashEffect(waterReflection.incomingVelocity.Magnitude());
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"Water reflection: depth=%.1f, waves=%.1f, energy_loss=%.3f",
                waterDepth, waveHeight, waterReflection.energyLoss);
        #endif
        
        return waterReflection;
    }
    
private:
    void AddWaterSplashEffect(float impactSpeed) {
        if (impactSpeed > 5.0f) {
            // Create splash particles based on impact speed
            int particleCount = static_cast<int>(impactSpeed * 2.0f);
            particleCount = std::clamp(particleCount, 10, 100);
            
            #if defined(_DEBUG_PHYSICS_)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                    L"Creating water splash with %d particles", particleCount);
            #endif
            
            CreateSplashParticles(particleCount);
        }
    }
    
    Physics& physics = PHYSICS_INSTANCE();
};
```

#### Elastic vs Inelastic Collisions

```cpp
class CollisionTypeManager {
public:
    enum class CollisionType {
        PERFECTLY_ELASTIC,    // e = 1.0 (no energy loss)
        ELASTIC,             // e = 0.8-0.95 (minimal energy loss)
        INELASTIC,           // e = 0.3-0.7 (moderate energy loss)
        PERFECTLY_INELASTIC  // e = 0.0 (maximum energy loss, objects stick)
    };
    
    ReflectionData CalculateCollisionByType(const PhysicsVector3D& velocity1,
                                          const PhysicsVector3D& velocity2,
                                          const PhysicsVector3D& normal,
                                          float mass1, float mass2,
                                          CollisionType collisionType) {
        float restitution = GetRestitutionForType(collisionType);
        float friction = GetFrictionForType(collisionType);
        
        // Calculate relative velocity
        PhysicsVector3D relativeVelocity = velocity1 - velocity2;
        
        // For perfectly inelastic collisions, calculate combined velocity
        if (collisionType == CollisionType::PERFECTLY_INELASTIC) {
            return CalculatePerfectlyInelasticCollision(velocity1, velocity2, mass1, mass2);
        }
        
        // Standard reflection calculation
        ReflectionData reflection = physics.CalculateReflection(relativeVelocity, normal, restitution, friction);
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Collision type %d: restitution=%.2f, energy_loss=%.3f",
                static_cast<int>(collisionType), restitution, reflection.energyLoss);
        #endif
        
        return reflection;
    }
    
private:
    float GetRestitutionForType(CollisionType type) {
        switch (type) {
            case CollisionType::PERFECTLY_ELASTIC: return 1.0f;
            case CollisionType::ELASTIC: return 0.9f;
            case CollisionType::INELASTIC: return 0.5f;
            case CollisionType::PERFECTLY_INELASTIC: return 0.0f;
            default: return 0.7f;
        }
    }
    
    float GetFrictionForType(CollisionType type) {
        switch (type) {
            case CollisionType::PERFECTLY_ELASTIC: return 0.0f;
            case CollisionType::ELASTIC: return 0.1f;
            case CollisionType::INELASTIC: return 0.3f;
            case CollisionType::PERFECTLY_INELASTIC: return 0.8f;
            default: return 0.2f;
        }
    }
    
    ReflectionData CalculatePerfectlyInelasticCollision(const PhysicsVector3D& v1,
                                                       const PhysicsVector3D& v2,
                                                       float m1, float m2) {
        // Conservation of momentum: m1*v1 + m2*v2 = (m1+m2)*vf
        PhysicsVector3D combinedVelocity = (v1 * m1 + v2 * m2) * (1.0f / (m1 + m2));
        
        ReflectionData result;
        result.incomingVelocity = v1;
        result.reflectedVelocity = combinedVelocity;
        result.restitution = 0.0f;
        result.friction = 0.8f;
        
        // Calculate energy loss
        float initialEnergy = 0.5f * m1 * v1.MagnitudeSquared() + 0.5f * m2 * v2.MagnitudeSquared();
        float finalEnergy = 0.5f * (m1 + m2) * combinedVelocity.MagnitudeSquared();
        result.energyLoss = (initialEnergy - finalEnergy) / initialEnergy;
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"Perfectly inelastic collision: energy_loss=%.3f, combined_velocity=(%.2f, %.2f, %.2f)",
                result.energyLoss, combinedVelocity.x, combinedVelocity.y, combinedVelocity.z);
        #endif
        
        return result;
    }
    
    Physics& physics = PHYSICS_INSTANCE();
};
```

### 5.9 Debugging and Visualization Tools

#### Reflection Debug Visualization

```cpp
class ReflectionDebugger {
public:
    void VisualizeMeflection(const PhysicsVector3D& position,
                           const PhysicsVector3D& incomingVelocity,
                           const PhysicsVector3D& reflectedVelocity,
                           const PhysicsVector3D& surfaceNormal) {
        // Add debug lines for visualization
        Physics& physics = PHYSICS_INSTANCE();
        
        // Incoming velocity vector (red)
        PhysicsVector3D incomingStart = position - incomingVelocity.Normalized() * 2.0f;
        physics.AddDebugLine(incomingStart, position);
        
        // Reflected velocity vector (green)
        PhysicsVector3D reflectedEnd = position + reflectedVelocity.Normalized() * 2.0f;
        physics.AddDebugLine(position, reflectedEnd);
        
        // Surface normal (blue)
        PhysicsVector3D normalEnd = position + surfaceNormal * 1.5f;
        physics.AddDebugLine(position, normalEnd);
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"Reflection visualization: incoming_angle=%.1f°, reflected_angle=%.1f°",
                CalculateAngleFromNormal(incomingVelocity, surfaceNormal) * 180.0f / XM_PI,
                CalculateAngleFromNormal(reflectedVelocity, surfaceNormal) * 180.0f / XM_PI);
        #endif
    }
    
    void LogReflectionDetails(const ReflectionData& reflection) {
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"=== Reflection Analysis ===");
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Incoming: (%.2f, %.2f, %.2f) speed=%.2f",
                reflection.incomingVelocity.x, reflection.incomingVelocity.y, 
                reflection.incomingVelocity.z, reflection.incomingVelocity.Magnitude());
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Reflected: (%.2f, %.2f, %.2f) speed=%.2f",
                reflection.reflectedVelocity.x, reflection.reflectedVelocity.y, 
                reflection.reflectedVelocity.z, reflection.reflectedVelocity.Magnitude());
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Surface Normal: (%.3f, %.3f, %.3f)",
                reflection.surfaceNormal.x, reflection.surfaceNormal.y, reflection.surfaceNormal.z);
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Properties: restitution=%.2f, friction=%.2f, energy_loss=%.3f",
                reflection.restitution, reflection.friction, reflection.energyLoss);
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Speed change: %.2f -> %.2f (%.1f%% of original)",
                reflection.incomingVelocity.Magnitude(), reflection.reflectedVelocity.Magnitude(),
                (reflection.reflectedVelocity.Magnitude() / reflection.incomingVelocity.Magnitude()) * 100.0f);
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"========================");
        #endif
    }
    
    bool ValidateReflection(const ReflectionData& reflection) {
        bool isValid = true;
        
        // Check for reasonable energy conservation
        float speedRatio = reflection.reflectedVelocity.Magnitude() / reflection.incomingVelocity.Magnitude();
        if (speedRatio > 1.1f) {  // Allow 10% tolerance for numerical errors
            #if defined(_DEBUG_PHYSICS_)
                debug.logLevelMessage(LogLevel::LOG_WARNING, 
                    L"Reflection validation failed: speed increased unrealistically");
            #endif
            isValid = false;
        }
        
        // Check restitution bounds
        if (reflection.restitution < 0.0f || reflection.restitution > 1.0f) {
            #if defined(_DEBUG_PHYSICS_)
                debug.logLevelMessage(LogLevel::LOG_ERROR, 
                    L"Reflection validation failed: invalid restitution value");
            #endif
            isValid = false;
        }
        
        // Check friction bounds
        if (reflection.friction < 0.0f || reflection.friction > 1.0f) {
            #if defined(_DEBUG_PHYSICS_)
                debug.logLevelMessage(LogLevel::LOG_ERROR, 
                    L"Reflection validation failed: invalid friction value");
            #endif
            isValid = false;
        }
        
        // Check surface normal
        float normalMagnitude = reflection.surfaceNormal.Magnitude();
        if (std::abs(normalMagnitude - 1.0f) > 0.1f) {
            #if defined(_DEBUG_PHYSICS_)
                debug.logLevelMessage(LogLevel::LOG_WARNING, 
                    L"Reflection validation warning: surface normal not normalized");
            #endif
        }
        
        return isValid;
    }
    
private:
    float CalculateAngleFromNormal(const PhysicsVector3D& velocity, const PhysicsVector3D& normal) {
        float dotProduct = velocity.Normalized().Dot(normal);
        return FAST_ACOS(std::clamp(std::abs(dotProduct), 0.0f, 1.0f));
    }
};
```

### 5.10 Complete Reflection System Example

#### Comprehensive Bouncing Ball Game

```cpp
class BouncingBallGame {
public:
    struct Ball {
        PhysicsVector3D position;
        PhysicsVector3D velocity;
        float radius;
        float mass;
        SurfaceType lastSurfaceHit;
        int bounceCount;
        float totalEnergyLost;
        bool isActive;
    };
    
    struct GameSurface {
        PhysicsVector3D normal;
        PhysicsVector3D point;
        SurfaceType materialType;
        float area;
        bool isMoving;
        PhysicsVector3D surfaceVelocity;  // For moving platforms
    };
    
    void InitializeGame() {
        // Create game surfaces
        gameSurfaces = {
            // Ground
            {PhysicsVector3D(0.0f, 1.0f, 0.0f), PhysicsVector3D(0.0f, 0.0f, 0.0f), 
             SurfaceType::CONCRETE, 100.0f, false, PhysicsVector3D()},
            
            // Left wall  
            {PhysicsVector3D(1.0f, 0.0f, 0.0f), PhysicsVector3D(-50.0f, 0.0f, 0.0f), 
             SurfaceType::METAL, 50.0f, false, PhysicsVector3D()},
             
            // Right wall
            {PhysicsVector3D(-1.0f, 0.0f, 0.0f), PhysicsVector3D(50.0f, 0.0f, 0.0f), 
             SurfaceType::RUBBER, 50.0f, false, PhysicsVector3D()},
             
            // Moving platform
            {PhysicsVector3D(0.0f, 1.0f, 0.0f), PhysicsVector3D(0.0f, 20.0f, 0.0f), 
             SurfaceType::WOOD, 20.0f, true, PhysicsVector3D(10.0f, 0.0f, 0.0f)}
        };
        
        // Create test ball
        Ball testBall;
        testBall.position = PhysicsVector3D(0.0f, 30.0f, 0.0f);
        testBall.velocity = PhysicsVector3D(15.0f, -5.0f, 0.0f);
        testBall.radius = 1.0f;
        testBall.mass = 0.5f;
        testBall.lastSurfaceHit = SurfaceType::CONCRETE;
        testBall.bounceCount = 0;
        testBall.totalEnergyLost = 0.0f;
        testBall.isActive = true;
        
        gameBalls.push_back(testBall);
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Bouncing ball game initialized with %zu surfaces and %zu balls",
                gameSurfaces.size(), gameBalls.size());
        #endif
    }
    
    void UpdateGame(float deltaTime) {
        for (auto& ball : gameBalls) {
            if (!ball.isActive) continue;
            
            // Apply gravity
            ball.velocity.y -= 9.81f * deltaTime;
            
            // Update position
            PhysicsVector3D newPosition = ball.position + ball.velocity * deltaTime;
            
            // Check for collisions with all surfaces
            for (auto& surface : gameSurfaces) {
                if (CheckBallSurfaceCollision(ball, surface, newPosition)) {
                    HandleBallSurfaceCollision(ball, surface);
                }
            }
            
            // Update ball position
            ball.position = newPosition;
            
            // Check if ball has lost too much energy
            float currentSpeed = ball.velocity.Magnitude();
            if (currentSpeed < 0.5f && ball.position.y < 2.0f) {
                ball.isActive = false;
                
                #if defined(_DEBUG_PHYSICS_)
                    debug.logDebugMessage(LogLevel::LOG_INFO, 
                        L"Ball stopped after %d bounces, total energy lost: %.3f",
                        ball.bounceCount, ball.totalEnergyLost);
                #endif
            }
        }
        
        // Update moving surfaces
        UpdateMovingSurfaces(deltaTime);
    }
    
private:
    bool CheckBallSurfaceCollision(const Ball& ball, const GameSurface& surface, 
                                  const PhysicsVector3D& newPosition) {
        // Simplified sphere-plane collision detection
        PhysicsVector3D ballToSurface = newPosition - surface.point;
        float distanceToSurface = ballToSurface.Dot(surface.normal);
        
        return (distanceToSurface <= ball.radius && distanceToSurface >= 0.0f);
    }
    
    void HandleBallSurfaceCollision(Ball& ball, const GameSurface& surface) {
        // Account for moving surfaces
        PhysicsVector3D relativeVelocity = ball.velocity - surface.surfaceVelocity;
        
        // Calculate reflection using surface manager
        SurfaceManager& surfaceManager = SurfaceManager::GetInstance();
        ReflectionData reflection = surfaceManager.CalculateReflectionForSurface(
            relativeVelocity, surface.normal, surface.materialType);
        
        // Apply reflection
        ball.velocity = reflection.reflectedVelocity + surface.surfaceVelocity;
        
        // Update ball statistics
        ball.bounceCount++;
        ball.totalEnergyLost += reflection.energyLoss;
        ball.lastSurfaceHit = surface.materialType;
        
        // Adjust position to prevent penetration
        PhysicsVector3D ballToSurface = ball.position - surface.point;
        float penetration = ball.radius - ballToSurface.Dot(surface.normal);
        if (penetration > 0.0f) {
            ball.position += surface.normal * penetration;
        }
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"Ball bounce #%d on surface type %d: speed %.2f -> %.2f",
                ball.bounceCount, static_cast<int>(surface.materialType),
                reflection.incomingVelocity.Magnitude(), ball.velocity.Magnitude());
        #endif
        
        // Add visual/audio feedback
        CreateBounceEffect(ball.position, surface.materialType, ball.velocity.Magnitude());
    }
    
    void UpdateMovingSurfaces(float deltaTime) {
        for (auto& surface : gameSurfaces) {
            if (surface.isMoving) {
                // Simple back-and-forth movement
                static float movementTime = 0.0f;
                movementTime += deltaTime;
                
                float oscillation = FAST_SIN(movementTime * 2.0f);
                surface.point.x = oscillation * 20.0f;
                surface.surfaceVelocity.x = FAST_COS(movementTime * 2.0f) * 40.0f;
            }
        }
    }
    
    void CreateBounceEffect(const PhysicsVector3D& position, SurfaceType surface, float intensity) {
        // Create particle effects and play sounds based on surface type and intensity
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"Bounce effect at (%.1f, %.1f, %.1f) intensity %.2f",
                position.x, position.y, position.z, intensity);
        #endif
        
        // Implementation would create visual/audio effects here
    }
    
    std::vector<Ball> gameBalls;
    std::vector<GameSurface> gameSurfaces;
    Physics& physics = PHYSICS_INSTANCE();
    ReflectionDebugger debugger;
};
```

---

**Chapter 5 Complete**

This completes Chapter 5 of the Physics Class usage guide covering comprehensive reflection path calculations including:

- **Basic 3D and 2D Reflections**: Simple and advanced reflection calculations with custom parameters
- **Multiple Bounce Prediction**: Trajectory prediction for complex bounce sequences
- **Surface Material Properties**: Different surface types with realistic material behavior
- **Realistic Scenarios**: Pool/billiards physics, projectile ricochet systems
- **Water Surface Reflections**: Dynamic water physics with wave conditions
- **Collision Types**: Elastic vs inelastic collision handling
- **Performance Optimization**: Caching systems for reflection calculations
- **Debugging Tools**: Comprehensive visualization and validation systems
- **Complete Game Example**: Full bouncing ball game implementation

All examples include proper debug logging, error handling, and real-world gaming applications.

# Physics Class - Chapter 6: Gravity System and Fields

## 6. Gravity System and Fields

### 6.1 Introduction to Gravity Systems

The Physics class provides a comprehensive gravity system supporting multiple gravity fields with variable intensity, distance-based calculations, and special handling for astronomical bodies. This system enables realistic gravitational effects in space games, planetary simulations, and multi-body physics scenarios.

#### Key Features
- Multiple simultaneous gravity fields
- Distance-based gravity intensity calculations
- Orbital velocity and escape velocity calculations
- Black hole physics with event horizon effects
- Thread-safe gravity field operations

### 6.2 Basic Gravity Field Creation

#### Simple Gravity Field Setup

```cpp
#include "Physics.h"

Physics& physics = PHYSICS_INSTANCE();

// Create a basic gravity field (Earth-like planet)
GravityField earthGravity;
earthGravity.center = PhysicsVector3D(0.0f, 0.0f, 0.0f);
earthGravity.mass = 5.972e24f;                    // Earth's mass in kg
earthGravity.radius = 6.371e6f;                   // Earth's radius in meters
earthGravity.intensity = 1.0f;                    // Standard intensity
earthGravity.isBlackHole = false;

// Add the gravity field to the physics system
physics.AddGravityField(earthGravity);

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_INFO, 
        L"Earth gravity field created: mass=%.2e, radius=%.2e",
        earthGravity.mass, earthGravity.radius);
#endif
```

#### Multiple Gravity Fields

```cpp
void CreateSolarSystemGravity() {
    Physics& physics = PHYSICS_INSTANCE();
    
    // Sun gravity field
    GravityField sun;
    sun.center = PhysicsVector3D(0.0f, 0.0f, 0.0f);
    sun.mass = 1.989e30f;                         // Solar mass
    sun.radius = 6.96e8f;                         // Solar radius
    sun.intensity = 1.0f;
    sun.isBlackHole = false;
    physics.AddGravityField(sun);
    
    // Earth gravity field
    GravityField earth;
    earth.center = PhysicsVector3D(1.496e11f, 0.0f, 0.0f);  // 1 AU from sun
    earth.mass = 5.972e24f;
    earth.radius = 6.371e6f;
    earth.intensity = 1.0f;
    earth.isBlackHole = false;
    physics.AddGravityField(earth);
    
    #if defined(_DEBUG_PHYSICS_)
        debug.logLevelMessage(LogLevel::LOG_INFO, 
            L"Solar system gravity fields created: Sun and Earth");
    #endif
}
```

### 6.3 Gravity Calculations

#### Basic Gravity Force Calculation

```cpp
void CalculateGravityEffects(const PhysicsVector3D& position) {
    Physics& physics = PHYSICS_INSTANCE();
    
    // Get total gravitational force at position
    PhysicsVector3D gravityForce = physics.CalculateGravityAtPosition(position);
    
    // Apply to physics body
    PhysicsBody spacecraft;
    spacecraft.position = position;
    spacecraft.SetMass(1000.0f);  // 1 ton spacecraft
    spacecraft.ApplyForce(gravityForce);
    
    #if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, 
            L"Gravity at position: force(%.3f, %.3f, %.3f), magnitude=%.6f",
            gravityForce.x, gravityForce.y, gravityForce.z, gravityForce.Magnitude());
    #endif
}
```

### 6.4 Orbital Mechanics

#### Orbital Velocity Calculations

```cpp
void CalculateOrbitalParameters() {
    Physics& physics = PHYSICS_INSTANCE();
    
    // Define Earth-like planet
    GravityField planet;
    planet.center = PhysicsVector3D(0.0f, 0.0f, 0.0f);
    planet.mass = 5.972e24f;    // Earth mass
    planet.radius = 6.371e6f;   // Earth radius
    planet.intensity = 1.0f;
    planet.isBlackHole = false;
    
    // Calculate orbital parameters at different altitudes
    std::vector<float> altitudes = {200e3f, 400e3f, 800e3f};  // Low Earth orbits
    
    for (float altitude : altitudes) {
        PhysicsVector3D orbitPosition(planet.radius + altitude, 0.0f, 0.0f);
        
        // Calculate required orbital velocity
        float orbitalVelocity = physics.CalculateOrbitalVelocity(orbitPosition, planet);
        
        // Calculate escape velocity
        float escapeVelocity = physics.CalculateEscapeVelocity(orbitPosition, planet);
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Orbit at %.0f km: velocity=%.1f m/s, escape=%.1f m/s",
                altitude / 1000.0f, orbitalVelocity, escapeVelocity);
        #endif
    }
}
```

#### Spacecraft Orbital Insertion

```cpp
class SpacecraftOrbitalMechanics {
public:
    bool AttemptOrbitalInsertion(PhysicsBody& spacecraft, const GravityField& planet,
                                float targetAltitude) {
        Physics& physics = PHYSICS_INSTANCE();
        
        // Current spacecraft state
        PhysicsVector3D currentPosition = spacecraft.position;
        PhysicsVector3D radiusVector = currentPosition - planet.center;
        float currentRadius = radiusVector.Magnitude();
        float targetRadius = planet.radius + targetAltitude;
        
        // Check if at correct altitude
        if (std::abs(currentRadius - targetRadius) > 1000.0f) {
            #if defined(_DEBUG_PHYSICS_)
                debug.logLevelMessage(LogLevel::LOG_WARNING, 
                    L"Spacecraft not at target altitude for orbital insertion");
            #endif
            return false;
        }
        
        // Calculate required orbital velocity
        float requiredVelocity = physics.CalculateOrbitalVelocity(currentPosition, planet);
        
        // Calculate tangent direction for circular orbit
        PhysicsVector3D upVector(0.0f, 1.0f, 0.0f);
        PhysicsVector3D tangentDirection = radiusVector.Cross(upVector).Normalized();
        
        // Apply orbital velocity
        spacecraft.velocity = tangentDirection * requiredVelocity;
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Orbital insertion successful: velocity=%.1f m/s", requiredVelocity);
        #endif
        
        return true;
    }
};
```

### 6.5 Black Hole Physics

#### Black Hole Gravity Field

```cpp
void CreateBlackHoleGravityField() {
    Physics& physics = PHYSICS_INSTANCE();
    
    // Stellar mass black hole
    GravityField blackHole;
    blackHole.center = PhysicsVector3D(0.0f, 0.0f, 0.0f);
    blackHole.mass = 1.989e31f;                   // 10 solar masses
    blackHole.radius = 29.5e3f;                   // Schwarzschild radius
    blackHole.intensity = 2.0f;                   // Enhanced gravity
    blackHole.isBlackHole = true;                 // Enable black hole physics
    
    physics.AddGravityField(blackHole);
    
    // Test distances from black hole
    std::vector<float> testDistances = {100e3f, 50e3f, 30e3f};
    
    for (float distance : testDistances) {
        PhysicsVector3D testPosition(distance, 0.0f, 0.0f);
        
        float gravityForce = blackHole.CalculateGravityForce(distance);
        float escapeVelocity = physics.CalculateEscapeVelocity(testPosition, blackHole);
        bool insideEventHorizon = (distance < blackHole.radius);
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"Black hole at %.1f km: gravity=%.2e, escape_v=%.0f m/s, event_horizon=%s",
                distance / 1000.0f, gravityForce, escapeVelocity, 
                insideEventHorizon ? L"YES" : L"NO");
        #endif
    }
}
```

### 6.6 Gravity Field Management

#### Dynamic Gravity Fields

```cpp
class DynamicGravityManager {
public:
    struct MovingGravityField {
        GravityField field;
        PhysicsVector3D velocity;
        bool isActive;
        int fieldId;
    };
    
    int AddDynamicGravityField(const GravityField& field, const PhysicsVector3D& velocity) {
        MovingGravityField movingField;
        movingField.field = field;
        movingField.velocity = velocity;
        movingField.isActive = true;
        movingField.fieldId = nextFieldId++;
        
        dynamicFields.push_back(movingField);
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Added dynamic gravity field %d: mass=%.2e",
                movingField.fieldId, field.mass);
        #endif
        
        return movingField.fieldId;
    }
    
    void UpdateDynamicGravityFields(float deltaTime) {
        Physics& physics = PHYSICS_INSTANCE();
        
        for (auto& movingField : dynamicFields) {
            if (!movingField.isActive) continue;
            
            // Update position
            movingField.field.center += movingField.velocity * deltaTime;
        }
        
        // Update physics system with new positions
        physics.ClearGravityFields();
        for (const auto& movingField : dynamicFields) {
            if (movingField.isActive) {
                physics.AddGravityField(movingField.field);
            }
        }
    }
    
private:
    std::vector<MovingGravityField> dynamicFields;
    int nextFieldId = 1;
};
```

### 6.7 Practical Applications

#### Planetary Landing System

```cpp
class PlanetaryLandingSystem {
public:
    enum class LandingPhase {
        APPROACH, DEORBIT_BURN, POWERED_DESCENT, LANDING_BURN, LANDED
    };
    
    struct LandingData {
        LandingPhase currentPhase;
        float altitudeAboveSurface;
        float verticalVelocity;
        float fuelRemaining;
        bool landingSuccessful;
    };
    
    LandingData ExecuteLandingSequence(PhysicsBody& spacecraft, const GravityField& planet, 
                                     float deltaTime) {
        LandingData data = {};
        
        // Calculate current state
        PhysicsVector3D surfaceVector = spacecraft.position - planet.center;
        data.altitudeAboveSurface = surfaceVector.Magnitude() - planet.radius;
        
        PhysicsVector3D surfaceNormal = surfaceVector.Normalized();
        data.verticalVelocity = spacecraft.velocity.Dot(surfaceNormal);
        data.fuelRemaining = spacecraftFuel;
        
        // Determine landing phase
        if (data.altitudeAboveSurface > 50000.0f) {
            data.currentPhase = LandingPhase::APPROACH;
        } else if (data.altitudeAboveSurface > 10000.0f) {
            data.currentPhase = LandingPhase::DEORBIT_BURN;
            ApplyRetrogradeBurn(spacecraft, deltaTime);
        } else if (data.altitudeAboveSurface > 1000.0f) {
            data.currentPhase = LandingPhase::POWERED_DESCENT;
            ApplyDescentThrust(spacecraft, planet, data, deltaTime);
        } else if (data.altitudeAboveSurface > 10.0f) {
            data.currentPhase = LandingPhase::LANDING_BURN;
            ApplyLandingThrust(spacecraft, planet, data, deltaTime);
        } else {
            data.currentPhase = LandingPhase::LANDED;
            spacecraft.velocity = PhysicsVector3D(0.0f, 0.0f, 0.0f);
            data.landingSuccessful = true;
        }
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"Landing phase %d: altitude=%.1f m, v_vert=%.1f m/s",
                static_cast<int>(data.currentPhase), data.altitudeAboveSurface, data.verticalVelocity);
        #endif
        
        return data;
    }
    
private:
    void ApplyRetrogradeBurn(PhysicsBody& spacecraft, float deltaTime) {
        PhysicsVector3D thrustDirection = spacecraft.velocity.Normalized() * -1.0f;
        PhysicsVector3D thrustForce = thrustDirection * 500.0f;  // 500N thrust
        spacecraft.ApplyForce(thrustForce);
        ConsumeFuel(0.5f * deltaTime);
    }
    
    void ApplyDescentThrust(PhysicsBody& spacecraft, const GravityField& planet, 
                           const LandingData& data, float deltaTime) {
        Physics& physics = PHYSICS_INSTANCE();
        PhysicsVector3D gravity = physics.CalculateGravityAtPosition(spacecraft.position);
        
        // Apply thrust to counteract gravity and control descent
        PhysicsVector3D surfaceNormal = (spacecraft.position - planet.center).Normalized();
        float thrustMagnitude = gravity.Magnitude() * spacecraft.mass * 1.2f;  // 120% of weight
        
        spacecraft.ApplyForce(surfaceNormal * thrustMagnitude);
        ConsumeFuel(1.0f * deltaTime);
    }
    
    void ApplyLandingThrust(PhysicsBody& spacecraft, const GravityField& planet, 
                           const LandingData& data, float deltaTime) {
        // Final landing burn for soft touchdown
        PhysicsVector3D surfaceNormal = (spacecraft.position - planet.center).Normalized();
        float landingThrust = 1000.0f;  // Strong thrust for final landing
        
        spacecraft.ApplyForce(surfaceNormal * landingThrust);
        ConsumeFuel(2.0f * deltaTime);
    }
    
    void ConsumeFuel(float amount) {
        spacecraftFuel -= amount;
        spacecraftFuel = std::max(0.0f, spacecraftFuel);
    }
    
    float spacecraftFuel = 1000.0f;  // kg
};
```

### 6.8 Complete Gravity System Example

```cpp
class SpaceGameGravitySystem {
public:
    void InitializeSpaceGame() {
        Physics& physics = PHYSICS_INSTANCE();
        physics.ClearGravityFields();
        
        // Create solar system
        CreateSun();
        CreateEarth();
        CreateMoon();
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_INFO, 
                L"Space game gravity system initialized");
        #endif
    }
    
    void UpdateSpaceGame(float deltaTime) {
        // Update orbital positions
        UpdateOrbitalBodies(deltaTime);
        
        // Update spacecraft
        for (auto& spacecraft : spacecraftList) {
            UpdateSpacecraftGravity(spacecraft, deltaTime);
        }
    }
    
    void AddSpacecraft(const PhysicsBody& spacecraft) {
        spacecraftList.push_back(spacecraft);
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Spacecraft added at position (%.2e, %.2e, %.2e)", 
                spacecraft.position.x, spacecraft.position.y, spacecraft.position.z);
        #endif
    }
    
private:
    void CreateSun() {
        Physics& physics = PHYSICS_INSTANCE();
        
        GravityField sun;
        sun.center = PhysicsVector3D(0.0f, 0.0f, 0.0f);
        sun.mass = 1.989e30f;
        sun.radius = 6.96e8f;
        sun.intensity = 1.0f;
        sun.isBlackHole = false;
        
        physics.AddGravityField(sun);
    }
    
    void CreateEarth() {
        Physics& physics = PHYSICS_INSTANCE();
        
        GravityField earth;
        earth.center = PhysicsVector3D(1.496e11f, 0.0f, 0.0f);  // 1 AU
        earth.mass = 5.972e24f;
        earth.radius = 6.371e6f;
        earth.intensity = 1.0f;
        earth.isBlackHole = false;
        
        physics.AddGravityField(earth);
    }
    
    void CreateMoon() {
        Physics& physics = PHYSICS_INSTANCE();
        
        GravityField moon;
        moon.center = PhysicsVector3D(1.496e11f + 3.844e8f, 0.0f, 0.0f);
        moon.mass = 7.342e22f;
        moon.radius = 1.737e6f;
        moon.intensity = 1.0f;
        moon.isBlackHole = false;
        
        physics.AddGravityField(moon);
    }
    
    void UpdateOrbitalBodies(float deltaTime) {
        // Simplified orbital mechanics - bodies move in circular orbits
        static float gameTime = 0.0f;
        gameTime += deltaTime;
        
        // Update Earth position (1 year orbit)
        float earthAngle = (gameTime * 2.0f * XM_PI) / (365.25f * 24.0f * 3600.0f);
        PhysicsVector3D earthPos(1.496e11f * FAST_COS(earthAngle), 0.0f, 
                                1.496e11f * FAST_SIN(earthAngle));
        
        // Update Moon position relative to Earth
        float moonAngle = (gameTime * 2.0f * XM_PI) / (27.3f * 24.0f * 3600.0f);
        PhysicsVector3D moonOffset(3.844e8f * FAST_COS(moonAngle), 0.0f, 
                                  3.844e8f * FAST_SIN(moonAngle));
        
        // Update gravity fields with new positions
        UpdateGravityFieldPositions(earthPos, earthPos + moonOffset);
    }
    
    void UpdateGravityFieldPositions(const PhysicsVector3D& earthPos, 
                                    const PhysicsVector3D& moonPos) {
        Physics& physics = PHYSICS_INSTANCE();
        
        // Clear and recreate gravity fields with updated positions
        physics.ClearGravityFields();
        
        // Re-add sun, earth, and moon with updated positions
        CreateSun();
        
        GravityField earth;
        earth.center = earthPos;
        earth.mass = 5.972e24f;
        earth.radius = 6.371e6f;
        earth.intensity = 1.0f;
        earth.isBlackHole = false;
        physics.AddGravityField(earth);
        
        GravityField moon;
        moon.center = moonPos;
        moon.mass = 7.342e22f;
        moon.radius = 1.737e6f;
        moon.intensity = 1.0f;
        moon.isBlackHole = false;
        physics.AddGravityField(moon);
    }
    
    void UpdateSpacecraftGravity(PhysicsBody& spacecraft, float deltaTime) {
        Physics& physics = PHYSICS_INSTANCE();
        
        if (!spacecraft.isActive) return;
        
        // Apply gravitational forces
        PhysicsVector3D totalGravity = physics.CalculateGravityAtPosition(spacecraft.position);
        spacecraft.ApplyForce(totalGravity);
        
        #if defined(_DEBUG_PHYSICS_)
            static int logCounter = 0;
            if (++logCounter % 300 == 0) {  // Log every 5 seconds at 60 FPS
                debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                    L"Spacecraft gravity force: (%.2e, %.2e, %.2e)",
                    totalGravity.x, totalGravity.y, totalGravity.z);
            }
        #endif
    }
    
    std::vector<PhysicsBody> spacecraftList;
    DynamicGravityManager dynamicManager;
    PlanetaryLandingSystem landingSystem;
};
```

---

**Chapter 6 Complete**

This completes Chapter 6 covering gravity systems including basic gravity field creation, orbital mechanics, black hole physics, dynamic gravity management, and practical space game applications. All examples include proper debug logging and real-world physics applications.

# Physics Class - Chapter 7: Bouncing Mechanics with Inertia

## 7. Bouncing Mechanics with Inertia

### 7.1 Introduction to Bouncing Physics

The Physics class provides comprehensive bouncing mechanics that accurately simulate object behavior with inertia, energy conservation, and realistic trajectory calculations. This system handles complex bouncing scenarios including multiple surface interactions, energy loss over time, and final resting position predictions.

#### Key Features
- Realistic bouncing trajectory calculations
- Energy loss simulation with customizable restitution
- Air resistance and drag effects
- Multiple bounce prediction
- Final resting position calculation
- Thread-safe bouncing calculations

### 7.2 Basic Bouncing Trajectory Calculation

#### Simple Bouncing Ball

```cpp
#include "Physics.h"

Physics& physics = PHYSICS_INSTANCE();

// Calculate bouncing trajectory for a ball
PhysicsVector3D startPosition(0.0f, 10.0f, 0.0f);     // 10m high
PhysicsVector3D initialVelocity(5.0f, 0.0f, 3.0f);    // Moving forward and right
float groundHeight = 0.0f;                             // Ground level
float restitution = 0.8f;                             // 80% bounce efficiency
float drag = 0.02f;                                    // Air resistance
int maxBounces = 10;

// Calculate complete bouncing trajectory
std::vector<PhysicsVector3D> trajectory = physics.CalculateBouncingTrajectory(
    startPosition, initialVelocity, groundHeight, restitution, drag, maxBounces);

#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_INFO, 
        L"Bouncing trajectory calculated with %zu positions over %d bounces",
        trajectory.size(), maxBounces);
#endif

// Analyze trajectory points
for (size_t i = 1; i < trajectory.size(); ++i) {
    PhysicsVector3D segment = trajectory[i] - trajectory[i-1];
    float segmentLength = segment.Magnitude();
    
    #if defined(_DEBUG_PHYSICS_)
        if (trajectory[i].y <= groundHeight + 0.1f && trajectory[i-1].y > groundHeight + 0.1f) {
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"Bounce detected at position (%.2f, %.2f, %.2f)",
                trajectory[i].x, trajectory[i].y, trajectory[i].z);
        }
    #endif
}
```

#### Advanced Bouncing with Custom Parameters

```cpp
class BouncingBallSimulator {
public:
    struct BallProperties {
        float mass;
        float radius;
        float restitution;
        float airResistance;
        float spinDecay;
        bool hasRollingFriction;
    };
    
    struct BounceResult {
        std::vector<PhysicsVector3D> trajectory;
        std::vector<float> velocities;
        std::vector<float> energyLevels;
        int totalBounces;
        float totalFlightTime;
        PhysicsVector3D finalPosition;
        bool reachedRest;
    };
    
    BounceResult SimulateBouncingBall(const PhysicsVector3D& startPos, 
                                     const PhysicsVector3D& startVel,
                                     const BallProperties& ballProps,
                                     float groundHeight = 0.0f) {
        Physics& physics = PHYSICS_INSTANCE();
        BounceResult result = {};
        
        // Calculate basic trajectory
        result.trajectory = physics.CalculateBouncingTrajectory(
            startPos, startVel, groundHeight, ballProps.restitution, 
            ballProps.airResistance, 20);
        
        // Analyze trajectory for detailed physics
        AnalyzeTrajectoryPhysics(result, ballProps, startVel);
        
        // Calculate final resting position
        result.finalPosition = physics.CalculateRestingPosition(
            startPos, startVel, groundHeight, ballProps.restitution, ballProps.airResistance);
        
        result.reachedRest = (result.finalPosition - startPos).Magnitude() > 0.1f;
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Ball simulation: %d bounces, %.2f sec flight time, rest at (%.2f, %.2f, %.2f)",
                result.totalBounces, result.totalFlightTime,
                result.finalPosition.x, result.finalPosition.y, result.finalPosition.z);
        #endif
        
        return result;
    }
    
private:
    void AnalyzeTrajectoryPhysics(BounceResult& result, const BallProperties& props,
                                 const PhysicsVector3D& initialVel) {
        result.velocities.reserve(result.trajectory.size());
        result.energyLevels.reserve(result.trajectory.size());
        
        float currentSpeed = initialVel.Magnitude();
        float initialEnergy = 0.5f * props.mass * currentSpeed * currentSpeed;
        
        for (size_t i = 0; i < result.trajectory.size(); ++i) {
            result.velocities.push_back(currentSpeed);
            
            // Calculate current energy (simplified)
            float currentEnergy = initialEnergy * std::pow(props.restitution, result.totalBounces);
            result.energyLevels.push_back(currentEnergy);
            
            // Detect bounces (simplified)
            if (i > 0 && result.trajectory[i].y <= 0.1f && result.trajectory[i-1].y > 0.1f) {
                result.totalBounces++;
                currentSpeed *= props.restitution;  // Reduce speed on bounce
            }
            
            // Apply air resistance
            currentSpeed *= (1.0f - props.airResistance * 0.016f);  // Per frame
        }
        
        // Estimate total flight time
        result.totalFlightTime = result.trajectory.size() * 0.016f;  // 60 FPS assumption
    }
};
```

### 7.3 Energy Conservation and Loss

#### Energy Analysis During Bouncing

```cpp
class BounceEnergyAnalyzer {
public:
    struct EnergyData {
        float kineticEnergy;
        float potentialEnergy;
        float totalEnergy;
        float energyLoss;
        float energyEfficiency;
    };
    
    EnergyData AnalyzeBounceEnergy(const PhysicsVector3D& velocity, 
                                  const PhysicsVector3D& position,
                                  float mass, float groundHeight = 0.0f) {
        EnergyData energy = {};
        
        // Calculate kinetic energy: KE = 0.5 * m * v²
        float speed = velocity.Magnitude();
        energy.kineticEnergy = 0.5f * mass * speed * speed;
        
        // Calculate potential energy: PE = m * g * h
        float height = position.y - groundHeight;
        energy.potentialEnergy = mass * DEFAULT_GRAVITY * std::max(0.0f, height);
        
        // Total mechanical energy
        energy.totalEnergy = energy.kineticEnergy + energy.potentialEnergy;
        
        // Track energy loss (requires initial energy for comparison)
        if (initialTotalEnergy > 0.0f) {
            energy.energyLoss = initialTotalEnergy - energy.totalEnergy;
            energy.energyEfficiency = energy.totalEnergy / initialTotalEnergy;
        } else {
            initialTotalEnergy = energy.totalEnergy;
            energy.energyLoss = 0.0f;
            energy.energyEfficiency = 1.0f;
        }
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"Energy analysis: KE=%.2f J, PE=%.2f J, total=%.2f J, efficiency=%.3f",
                energy.kineticEnergy, energy.potentialEnergy, energy.totalEnergy, energy.energyEfficiency);
        #endif
        
        return energy;
    }
    
    void ResetEnergyTracking() {
        initialTotalEnergy = 0.0f;
    }
    
private:
    float initialTotalEnergy = 0.0f;
};
```

#### Realistic Energy Loss Modeling

```cpp
class RealisticBouncePhysics {
public:
    struct SurfaceProperties {
        float restitution;          // Coefficient of restitution
        float friction;             // Surface friction
        float deformation;          // Surface deformation factor
        float soundAbsorption;      // Energy lost to sound
        float heatGeneration;       // Energy lost to heat
    };
    
    struct BounceEnergyLoss {
        float restitutionLoss;      // Energy lost due to imperfect bounce
        float frictionLoss;         // Energy lost to friction
        float deformationLoss;      // Energy lost to object deformation
        float airResistanceLoss;    // Energy lost to air resistance
        float totalEnergyLoss;      // Sum of all losses
        float remainingEnergy;      // Energy remaining after bounce
    };
    
    BounceEnergyLoss CalculateRealisticEnergyLoss(float impactVelocity, 
                                                 float objectMass,
                                                 const SurfaceProperties& surface) {
        BounceEnergyLoss losses = {};
        
        // Initial kinetic energy
        float initialEnergy = 0.5f * objectMass * impactVelocity * impactVelocity;
        
        // Energy loss due to imperfect restitution
        float restitutionFactor = surface.restitution * surface.restitution;  // e² for energy
        losses.restitutionLoss = initialEnergy * (1.0f - restitutionFactor);
        
        // Energy loss due to friction (during contact)
        losses.frictionLoss = initialEnergy * surface.friction * 0.1f;  // 10% of friction coefficient
        
        // Energy loss due to deformation
        losses.deformationLoss = initialEnergy * surface.deformation * 0.05f;  // 5% of deformation factor
        
        // Energy loss due to air resistance (minimal during bounce)
        losses.airResistanceLoss = initialEnergy * 0.001f;  // 0.1% for air resistance
        
        // Total energy loss
        losses.totalEnergyLoss = losses.restitutionLoss + losses.frictionLoss + 
                                losses.deformationLoss + losses.airResistanceLoss;
        
        // Remaining energy
        losses.remainingEnergy = initialEnergy - losses.totalEnergyLoss;
        losses.remainingEnergy = std::max(0.0f, losses.remainingEnergy);
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"Energy loss analysis: initial=%.2f J, lost=%.2f J (%.1f%%), remaining=%.2f J",
                initialEnergy, losses.totalEnergyLoss, 
                (losses.totalEnergyLoss / initialEnergy) * 100.0f, losses.remainingEnergy);
        #endif
        
        return losses;
    }
    
    PhysicsVector3D ApplyEnergyLossToVelocity(const PhysicsVector3D& incomingVelocity,
                                             const BounceEnergyLoss& energyLoss,
                                             float objectMass) {
        // Calculate new speed based on remaining energy
        float newSpeed = FAST_SQRT((2.0f * energyLoss.remainingEnergy) / objectMass);
        
        // Maintain direction (simplified - in reality would depend on surface angle)
        PhysicsVector3D newVelocity = incomingVelocity.Normalized() * newSpeed;
        
        // Flip Y component for bounce (assuming horizontal ground)
        newVelocity.y = -newVelocity.y;
        
        return newVelocity;
    }
};
```

### 7.4 Multiple Surface Bouncing

#### Multi-Surface Bounce Simulation

```cpp
class MultiSurfaceBounceSystem {
public:
    struct BounceSurface {
        PhysicsVector3D normal;
        PhysicsVector3D point;
        RealisticBouncePhysics::SurfaceProperties properties;
        std::string surfaceName;
    };
    
    struct MultiSurfaceBounceResult {
        std::vector<PhysicsVector3D> trajectory;
        std::vector<int> surfaceHitIndices;
        std::vector<float> bounceEnergies;
        int totalBounces;
        float totalEnergyLoss;
        PhysicsVector3D finalVelocity;
    };
    
    MultiSurfaceBounceResult SimulateMultiSurfaceBouncing(
        const PhysicsVector3D& startPos,
        const PhysicsVector3D& startVel,
        const std::vector<BounceSurface>& surfaces,
        float objectMass,
        int maxBounces = 10) {
        
        MultiSurfaceBounceResult result = {};
        result.trajectory.reserve(maxBounces * 10);
        result.surfaceHitIndices.reserve(maxBounces);
        result.bounceEnergies.reserve(maxBounces);
        
        PhysicsVector3D currentPos = startPos;
        PhysicsVector3D currentVel = startVel;
        float timeStep = 0.016f;  // 60 FPS
        int bounceCount = 0;
        
        result.trajectory.push_back(currentPos);
        
        // Simulation loop
        for (int step = 0; step < 1000 && bounceCount < maxBounces; ++step) {
            // Apply gravity
            currentVel.y -= DEFAULT_GRAVITY * timeStep;
            
            // Update position
            PhysicsVector3D nextPos = currentPos + currentVel * timeStep;
            
            // Check for surface collisions
            int hitSurfaceIndex = CheckSurfaceCollisions(currentPos, nextPos, surfaces);
            
            if (hitSurfaceIndex >= 0) {
                // Process bounce
                ProcessSurfaceBounce(currentPos, currentVel, surfaces[hitSurfaceIndex], 
                                   objectMass, result);
                bounceCount++;
                
                #if defined(_DEBUG_PHYSICS_)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                        L"Bounce %d on surface '%S' at position (%.2f, %.2f, %.2f)",
                        bounceCount, surfaces[hitSurfaceIndex].surfaceName.c_str(),
                        currentPos.x, currentPos.y, currentPos.z);
                #endif
                
                // Check if energy is too low to continue
                if (currentVel.Magnitude() < 0.5f) {
                    break;
                }
            }
            
            currentPos = nextPos;
            result.trajectory.push_back(currentPos);
        }
        
        result.totalBounces = bounceCount;
        result.finalVelocity = currentVel;
        
        // Calculate total energy loss
        float initialEnergy = 0.5f * objectMass * startVel.MagnitudeSquared();
        float finalEnergy = 0.5f * objectMass * currentVel.MagnitudeSquared();
        result.totalEnergyLoss = initialEnergy - finalEnergy;
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Multi-surface simulation complete: %d bounces, %.1f%% energy lost",
                result.totalBounces, (result.totalEnergyLoss / initialEnergy) * 100.0f);
        #endif
        
        return result;
    }
    
private:
    int CheckSurfaceCollisions(const PhysicsVector3D& currentPos, 
                              const PhysicsVector3D& nextPos,
                              const std::vector<BounceSurface>& surfaces) {
        // Simplified collision detection - check if trajectory crosses surface
        for (size_t i = 0; i < surfaces.size(); ++i) {
            const BounceSurface& surface = surfaces[i];
            
            // Calculate distances from surface plane
            PhysicsVector3D currentToSurface = currentPos - surface.point;
            PhysicsVector3D nextToSurface = nextPos - surface.point;
            
            float currentDist = currentToSurface.Dot(surface.normal);
            float nextDist = nextToSurface.Dot(surface.normal);
            
            // Check if trajectory crosses surface (signs different)
            if ((currentDist > 0.0f && nextDist <= 0.0f) || 
                (currentDist < 0.0f && nextDist >= 0.0f)) {
                return static_cast<int>(i);
            }
        }
        
        return -1;  // No collision
    }
    
    void ProcessSurfaceBounce(PhysicsVector3D& position, PhysicsVector3D& velocity,
                             const BounceSurface& surface, float objectMass,
                             MultiSurfaceBounceResult& result) {
        Physics& physics = PHYSICS_INSTANCE();
        
        // Calculate reflection using Physics class
        ReflectionData reflection = physics.CalculateReflection(
            velocity, surface.normal, surface.properties.restitution, surface.properties.friction);
        
        // Apply realistic energy loss
        RealisticBouncePhysics energyPhysics;
        RealisticBouncePhysics::BounceEnergyLoss energyLoss = 
            energyPhysics.CalculateRealisticEnergyLoss(velocity.Magnitude(), objectMass, surface.properties);
        
        // Update velocity with energy loss
        velocity = energyPhysics.ApplyEnergyLossToVelocity(velocity, energyLoss, objectMass);
        
        // Store bounce data
        result.surfaceHitIndices.push_back(result.totalBounces);
        result.bounceEnergies.push_back(energyLoss.remainingEnergy);
        
        // Adjust position to prevent penetration
        PhysicsVector3D toSurface = position - surface.point;
        float penetration = -toSurface.Dot(surface.normal);
        if (penetration > 0.0f) {
            position += surface.normal * penetration;
        }
    }
};
```

### 7.5 Inertia and Momentum Effects

#### Rotational Inertia in Bouncing

```cpp
class RotationalBouncePhysics {
public:
    struct RotatingObject {
        PhysicsVector3D position;
        PhysicsVector3D velocity;
        PhysicsVector3D angularVelocity;
        float mass;
        float momentOfInertia;
        float radius;
    };
    
    struct SpinBounceResult {
        PhysicsVector3D newVelocity;
        PhysicsVector3D newAngularVelocity;
        float energyLostToSpin;
        float spinEffect;  // How much spin affected the bounce
    };
    
    SpinBounceResult CalculateSpinBounce(const RotatingObject& object, 
                                        const PhysicsVector3D& surfaceNormal,
                                        float surfaceFriction) {
        SpinBounceResult result = {};
        
        // Calculate surface contact velocity (includes spin)
        PhysicsVector3D contactVelocity = CalculateContactVelocity(object);
        
        // Normal and tangential velocity components
        float normalVelocity = contactVelocity.Dot(surfaceNormal);
        PhysicsVector3D tangentialVelocity = contactVelocity - surfaceNormal * normalVelocity;
        
        // Process normal component (standard bounce)
        float restitution = 0.8f;  // Could be parameterized
        PhysicsVector3D newNormalVelocity = surfaceNormal * (-normalVelocity * restitution);
        
        // Process tangential component with friction and spin
        ProcessSpinFrictionInteraction(object, tangentialVelocity, surfaceFriction, result);
        
        // Combine velocity components
        result.newVelocity = newNormalVelocity + tangentialVelocity;
        
        // Calculate energy lost to spin effects
        float initialEnergy = 0.5f * object.mass * object.velocity.MagnitudeSquared() +
                             0.5f * object.momentOfInertia * object.angularVelocity.MagnitudeSquared();
        float finalEnergy = 0.5f * object.mass * result.newVelocity.MagnitudeSquared() +
                           0.5f * object.momentOfInertia * result.newAngularVelocity.MagnitudeSquared();
        
        result.energyLostToSpin = initialEnergy - finalEnergy;
        result.spinEffect = result.energyLostToSpin / initialEnergy;
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"Spin bounce: energy lost to spin=%.2f J (%.1f%%), new angular velocity magnitude=%.2f rad/s",
                result.energyLostToSpin, result.spinEffect * 100.0f, result.newAngularVelocity.Magnitude());
        #endif
        
        return result;
    }
    
private:
    PhysicsVector3D CalculateContactVelocity(const RotatingObject& object) {
        // Contact velocity = linear velocity + (angular velocity × radius vector)
        PhysicsVector3D radiusVector(0.0f, -object.radius, 0.0f);  // Bottom of sphere
        PhysicsVector3D spinVelocity = object.angularVelocity.Cross(radiusVector);
        
        return object.velocity + spinVelocity;
    }
    
    void ProcessSpinFrictionInteraction(const RotatingObject& object,
                                       PhysicsVector3D& tangentialVelocity,
                                       float surfaceFriction,
                                       SpinBounceResult& result) {
        // Simplified spin-friction interaction
        PhysicsVector3D radiusVector(0.0f, -object.radius, 0.0f);
        PhysicsVector3D surfaceAngularVel = object.angularVelocity.Cross(radiusVector);
        
        // Calculate relative sliding velocity
        PhysicsVector3D slidingVelocity = tangentialVelocity - surfaceAngularVel;
        float slidingSpeed = slidingVelocity.Magnitude();
        
        if (slidingSpeed > 0.01f) {
            // Apply friction force
            PhysicsVector3D frictionForce = slidingVelocity.Normalized() * (-surfaceFriction);
            
            // Modify tangential velocity
            tangentialVelocity += frictionForce * 0.1f;  // Simplified friction application
            
            // Modify angular velocity due to friction torque
            PhysicsVector3D torque = radiusVector.Cross(frictionForce);
            PhysicsVector3D angularAcceleration = torque * (1.0f / object.momentOfInertia);
            result.newAngularVelocity = object.angularVelocity + angularAcceleration * 0.016f;  // One frame
        } else {
            // No sliding - pure rolling
            result.newAngularVelocity = object.angularVelocity;
        }
    }
};
```

### 7.6 Practical Bouncing Applications

#### Game Ball Physics

```cpp
class GameBallPhysics {
public:
    enum class BallType {
        BASKETBALL,
        TENNIS_BALL,
        RUBBER_BALL,
        PING_PONG_BALL,
        BOWLING_BALL
    };
    
    struct BallTypeProperties {
        float mass;              // kg
        float radius;            // m
        float restitution;       // bounce coefficient
        float airResistance;     // drag coefficient
        float spinDecay;         // angular velocity decay
        float rollingFriction;   // rolling resistance
    };
    
    void InitializeBallTypes() {
        // Basketball
        ballProperties[BallType::BASKETBALL] = {
            0.62f,    // mass (kg)
            0.12f,    // radius (m)
            0.85f,    // restitution
            0.02f,    // air resistance
            0.95f,    // spin decay
            0.01f     // rolling friction
        };
        
        // Tennis ball
        ballProperties[BallType::TENNIS_BALL] = {
            0.057f,   // mass
            0.033f,   // radius
            0.75f,    // restitution
            0.03f,    // air resistance
            0.90f,    // spin decay
            0.015f    // rolling friction
        };
        
        // Rubber ball
        ballProperties[BallType::RUBBER_BALL] = {
            0.1f,     // mass
            0.05f,    // radius
            0.95f,    // restitution (very bouncy)
            0.01f,    // air resistance
            0.98f,    // spin decay
            0.005f    // rolling friction
        };
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_INFO, 
                L"Game ball physics initialized with 3 ball types");
        #endif
    }
    
    BouncingBallSimulator::BounceResult SimulateGameBall(
        BallType ballType,
        const PhysicsVector3D& startPos,
        const PhysicsVector3D& startVel,
        float courtSurfaceRestitution = 0.9f) {
        
        auto it = ballProperties.find(ballType);
        if (it == ballProperties.end()) {
            #if defined(_DEBUG_PHYSICS_)
                debug.logLevelMessage(LogLevel::LOG_ERROR, 
                    L"Unknown ball type for simulation");
            #endif
            return {};
        }
        
        const BallTypeProperties& props = it->second;
        
        // Convert to BallProperties structure
        BouncingBallSimulator::BallProperties ballProps;
        ballProps.mass = props.mass;
        ballProps.radius = props.radius;
        ballProps.restitution = props.restitution * courtSurfaceRestitution;  // Combined effect
        ballProps.airResistance = props.airResistance;
        ballProps.spinDecay = props.spinDecay;
        ballProps.hasRollingFriction = true;
        
        // Simulate bouncing
        BouncingBallSimulator simulator;
        auto result = simulator.SimulateBouncingBall(startPos, startVel, ballProps);
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Game ball simulation complete: %d bounces, final position (%.2f, %.2f, %.2f)",
                result.totalBounces, result.finalPosition.x, result.finalPosition.y, result.finalPosition.z);
        #endif
        
        return result;
    }
    
    PhysicsVector3D PredictBallLanding(BallType ballType,
                                      const PhysicsVector3D& startPos,
                                      const PhysicsVector3D& startVel) {
        Physics& physics = PHYSICS_INSTANCE();
        
        auto it = ballProperties.find(ballType);
        if (it == ballProperties.end()) {
            return startPos;  // Fallback
        }
        
        const BallTypeProperties& props = it->second;
        
        // Use Physics class to predict resting position
        PhysicsVector3D restingPos = physics.CalculateRestingPosition(
            startPos, startVel, 0.0f, props.restitution, props.airResistance);
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"Ball landing prediction: from (%.2f, %.2f, %.2f) to (%.2f, %.2f, %.2f)",
                startPos.x, startPos.y, startPos.z, restingPos.x, restingPos.y, restingPos.z);
        #endif
        
        return restingPos;
    }
    
private:
    std::unordered_map<BallType, BallTypeProperties> ballProperties;
};
```

#### Destructible Environment Bouncing

```cpp
class DestructibleEnvironmentBouncer {
public:
    struct DestructibleSurface {
        PhysicsVector3D normal;
        PhysicsVector3D position;
        float durability;           // How much impact it can take before breaking
        float currentDamage;        // Accumulated damage
        bool isBroken;              // Whether surface is destroyed
        float restitution;          // Bounce properties
        float friction;
    };
    
    struct ImpactResult {
        bool surfaceBroken;
        float damageDealt;
        PhysicsVector3D newVelocity;
        std::vector<PhysicsVector3D> debrisPositions;
    };
    
    ImpactResult ProcessDestructibleImpact(PhysicsBody& bouncingObject,
                                          DestructibleSurface& surface,
                                          float impactVelocity) {
        ImpactResult result = {};
        
        // Calculate impact force based on mass and velocity
        float impactForce = bouncingObject.mass * impactVelocity * impactVelocity * 0.5f;
        
        // Apply damage to surface
        result.damageDealt = impactForce / 1000.0f;  // Scale factor
        surface.currentDamage += result.damageDealt;
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"Destructible impact: force=%.1f N, damage=%.2f, total_damage=%.2f/%.2f",
                impactForce, result.damageDealt, surface.currentDamage, surface.durability);
        #endif
        
        // Check if surface breaks
        if (surface.currentDamage >= surface.durability && !surface.isBroken) {
            result.surfaceBroken = true;
            surface.isBroken = true;
            
            // Generate debris
            GenerateDebris(surface, result);
            
            // Different bounce properties for broken surface
            surface.restitution *= 0.3f;  // Much less bouncy when broken
            surface.friction *= 2.0f;     // More friction from debris
            
            #if defined(_DEBUG_PHYSICS_)
                debug.logLevelMessage(LogLevel::LOG_INFO, 
                    L"Surface destroyed! Generated debris at impact site");
            #endif
        }
        
        // Calculate bounce with modified surface properties
        Physics& physics = PHYSICS_INSTANCE();
        ReflectionData reflection = physics.CalculateReflection(
            bouncingObject.velocity, surface.normal, surface.restitution, surface.friction);
        
        result.newVelocity = reflection.reflectedVelocity;
        
        return result;
    }
    
private:
    void GenerateDebris(const DestructibleSurface& surface, ImpactResult& result) {
        // Generate random debris positions around impact point
        int debrisCount = 5 + (rand() % 10);  // 5-15 debris pieces
        result.debrisPositions.reserve(debrisCount);
        
        for (int i = 0; i < debrisCount; ++i) {
            PhysicsVector3D debrisPos = surface.position;
            
            // Add random offset
            debrisPos.x += (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f;
            debrisPos.y += (static_cast<float>(rand()) / RAND_MAX) * 1.0f;
            debrisPos.z += (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f;
            
            result.debrisPositions.push_back(debrisPos);
        }
    }
};
```

### 7.7 Advanced Bouncing Scenarios

#### Chain Reaction Bouncing

```cpp
class ChainReactionBouncer {
public:
    struct ChainObject {
        PhysicsBody physicsBody;
        float triggerRadius;        // How close other objects need to be to trigger
        bool hasTriggered;          // Whether this object has been activated
        float activationDelay;      // Time delay before activation
        float currentDelay;         // Current delay timer
    };
    
    void UpdateChainReaction(std::vector<ChainObject>& objects, float deltaTime) {
        Physics& physics = PHYSICS_INSTANCE();
        
        for (auto& obj : objects) {
            if (!obj.physicsBody.isActive) continue;
            
            // Update delay timers
            if (obj.hasTriggered && obj.currentDelay > 0.0f) {
                obj.currentDelay -= deltaTime;
                
                if (obj.currentDelay <= 0.0f) {
                    // Activate this object
                    ActivateChainObject(obj);
                }
            }
            
            // Check for triggers from nearby objects
            if (!obj.hasTriggered) {
                CheckForChainTriggers(obj, objects);
            }
            
            // Apply physics updates
            PhysicsVector3D gravity = physics.CalculateGravityAtPosition(obj.physicsBody.position);
            obj.physicsBody.ApplyForce(gravity);
        }
    }
    
    void StartChainReaction(std::vector<ChainObject>& objects, int startIndex) {
        if (startIndex >= 0 && startIndex < static_cast<int>(objects.size())) {
            TriggerObject(objects[startIndex]);
            
            #if defined(_DEBUG_PHYSICS_)
                debug.logDebugMessage(LogLevel::LOG_INFO, 
                    L"Chain reaction started at object %d", startIndex);
            #endif
        }
    }
    
private:
    void CheckForChainTriggers(ChainObject& obj, const std::vector<ChainObject>& objects) {
        for (const auto& otherObj : objects) {
            if (&otherObj == &obj || !otherObj.hasTriggered) continue;
            
            // Check if triggered object is close enough
            float distance = (obj.physicsBody.position - otherObj.physicsBody.position).Magnitude();
            
            if (distance <= obj.triggerRadius) {
                TriggerObject(obj);
                break;
            }
        }
    }
    
    void TriggerObject(ChainObject& obj) {
        if (obj.hasTriggered) return;
        
        obj.hasTriggered = true;
        obj.currentDelay = obj.activationDelay;
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"Object triggered, will activate in %.2f seconds", obj.activationDelay);
        #endif
    }
    
    void ActivateChainObject(ChainObject& obj) {
        // Launch object upward with some random velocity
        PhysicsVector3D launchVelocity;
        launchVelocity.x = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 10.0f;
        launchVelocity.y = 5.0f + (static_cast<float>(rand()) / RAND_MAX) * 10.0f;
        launchVelocity.z = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 10.0f;
        
        obj.physicsBody.velocity = launchVelocity;
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"Chain object activated with velocity (%.2f, %.2f, %.2f)",
                launchVelocity.x, launchVelocity.y, launchVelocity.z);
        #endif
    }
};
```

#### Bouncing Particle Systems

```cpp
class BouncingParticleSystem {
public:
    struct BouncingParticle {
        PhysicsParticle baseParticle;
        float restitution;
        float minBounceVelocity;    // Stop bouncing below this speed
        int bounceCount;
        float totalBounceEnergy;
    };
    
    void CreateBouncingExplosion(const PhysicsVector3D& center, int particleCount, 
                                float explosionForce) {
        Physics& physics = PHYSICS_INSTANCE();
        
        // Clear existing particles
        bouncingParticles.clear();
        bouncingParticles.reserve(particleCount);
        
        for (int i = 0; i < particleCount; ++i) {
            BouncingParticle particle = {};
            
            // Set base particle properties
            particle.baseParticle.position = center;
            particle.baseParticle.life = 5.0f + (static_cast<float>(rand()) / RAND_MAX) * 3.0f;
            particle.baseParticle.mass = 0.01f + (static_cast<float>(rand()) / RAND_MAX) * 0.02f;
            particle.baseParticle.drag = 0.01f;
            particle.baseParticle.isActive = true;
            
            // Random explosion direction
            float theta = static_cast<float>(rand()) / RAND_MAX * 2.0f * XM_PI;
            float phi = static_cast<float>(rand()) / RAND_MAX * XM_PI;
            
            PhysicsVector3D direction(
                FAST_SIN(phi) * FAST_COS(theta),
                FAST_COS(phi),
                FAST_SIN(phi) * FAST_SIN(theta)
            );
            
            float speed = explosionForce * (0.5f + (static_cast<float>(rand()) / RAND_MAX) * 0.5f);
            particle.baseParticle.velocity = direction * speed;
            
            // Set bouncing properties
            particle.restitution = 0.3f + (static_cast<float>(rand()) / RAND_MAX) * 0.4f;
            particle.minBounceVelocity = 1.0f;
            particle.bounceCount = 0;
            particle.totalBounceEnergy = 0.0f;
            
            bouncingParticles.push_back(particle);
        }
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Bouncing explosion created with %d particles", particleCount);
        #endif
    }
    
    void UpdateBouncingParticles(float deltaTime, float groundHeight = 0.0f) {
        Physics& physics = PHYSICS_INSTANCE();
        
        int activeParticles = 0;
        
        for (auto& particle : bouncingParticles) {
            if (!particle.baseParticle.isActive) continue;
            
            // Apply gravity
            particle.baseParticle.acceleration.y = -DEFAULT_GRAVITY;
            
            // Update particle physics
            particle.baseParticle.Update(deltaTime);
            
            // Check for ground bounce
            if (particle.baseParticle.position.y <= groundHeight && 
                particle.baseParticle.velocity.y < 0.0f) {
                
                ProcessParticleBounce(particle, groundHeight);
            }
            
            if (particle.baseParticle.isActive) {
                activeParticles++;
            }
        }
        
        #if defined(_DEBUG_PHYSICS_)
            static int logCounter = 0;
            if (++logCounter % 60 == 0) {  // Log every second at 60 FPS
                debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                    L"Bouncing particle system: %d active particles", activeParticles);
            }
        #endif
    }
    
    std::vector<PhysicsVector3D> GetParticlePositions() const {
        std::vector<PhysicsVector3D> positions;
        positions.reserve(bouncingParticles.size());
        
        for (const auto& particle : bouncingParticles) {
            if (particle.baseParticle.isActive) {
                positions.push_back(particle.baseParticle.position);
            }
        }
        
        return positions;
    }
    
private:
    void ProcessParticleBounce(BouncingParticle& particle, float groundHeight) {
        // Calculate bounce
        float impactSpeed = std::abs(particle.baseParticle.velocity.y);
        
        // Check if speed is above minimum bounce threshold
        if (impactSpeed < particle.minBounceVelocity) {
            // Stop bouncing, particle comes to rest
            particle.baseParticle.velocity = PhysicsVector3D(0.0f, 0.0f, 0.0f);
            particle.baseParticle.position.y = groundHeight;
            
            #if defined(_DEBUG_PHYSICS_)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                    L"Particle stopped bouncing after %d bounces", particle.bounceCount);
            #endif
            return;
        }
        
        // Apply bounce
        particle.baseParticle.velocity.y = -particle.baseParticle.velocity.y * particle.restitution;
        particle.baseParticle.position.y = groundHeight;
        
        // Apply horizontal friction
        particle.baseParticle.velocity.x *= 0.9f;
        particle.baseParticle.velocity.z *= 0.9f;
        
        // Track bounce statistics
        particle.bounceCount++;
        particle.totalBounceEnergy += 0.5f * particle.baseParticle.mass * impactSpeed * impactSpeed;
        
        // Reduce restitution slightly with each bounce (realistic degradation)
        particle.restitution *= 0.98f;
    }
    
    std::vector<BouncingParticle> bouncingParticles;
};
```

### 7.8 Performance Optimization

#### Bouncing Physics Optimization

```cpp
class BounceOptimization {
public:
    struct OptimizedBounceData {
        PhysicsVector3D position;
        PhysicsVector3D velocity;
        float energy;
        int bounceCount;
        bool isActive;
        float lastBounceTime;
    };
    
    // Use object pooling for better performance
    class BounceObjectPool {
    public:
        BounceObjectPool(int poolSize) {
            pool.reserve(poolSize);
            available.reserve(poolSize);
            
            for (int i = 0; i < poolSize; ++i) {
                pool.emplace_back();
                available.push_back(i);
            }
            
            #if defined(_DEBUG_PHYSICS_)
                debug.logDebugMessage(LogLevel::LOG_INFO, 
                    L"Bounce object pool created with %d objects", poolSize);
            #endif
        }
        
        OptimizedBounceData* AcquireObject() {
            if (available.empty()) {
                #if defined(_DEBUG_PHYSICS_)
                    debug.logLevelMessage(LogLevel::LOG_WARNING, 
                        L"Bounce object pool exhausted");
                #endif
                return nullptr;
            }
            
            int index = available.back();
            available.pop_back();
            
            OptimizedBounceData* obj = &pool[index];
            obj->isActive = true;
            return obj;
        }
        
        void ReleaseObject(OptimizedBounceData* obj) {
            if (!obj) return;
            
            obj->isActive = false;
            
            // Find index and return to available pool
            int index = static_cast<int>(obj - &pool[0]);
            if (index >= 0 && index < static_cast<int>(pool.size())) {
                available.push_back(index);
            }
        }
        
    private:
        std::vector<OptimizedBounceData> pool;
        std::vector<int> available;
    };
    
    // Spatial partitioning for collision optimization
    class BounceSpatialGrid {
    public:
        BounceSpatialGrid(float cellSize, int gridWidth, int gridHeight) 
            : cellSize(cellSize), gridWidth(gridWidth), gridHeight(gridHeight) {
            
            grid.resize(gridWidth * gridHeight);
            
            #if defined(_DEBUG_PHYSICS_)
                debug.logDebugMessage(LogLevel::LOG_INFO, 
                    L"Bounce spatial grid created: %dx%d cells, %.2f cell size",
                    gridWidth, gridHeight, cellSize);
            #endif
        }
        
        void ClearGrid() {
            for (auto& cell : grid) {
                cell.clear();
            }
        }
        
        void AddObject(OptimizedBounceData* obj) {
            int cellIndex = GetCellIndex(obj->position);
            if (cellIndex >= 0 && cellIndex < static_cast<int>(grid.size())) {
                grid[cellIndex].push_back(obj);
            }
        }
        
        std::vector<OptimizedBounceData*> GetNearbyObjects(const PhysicsVector3D& position) {
            std::vector<OptimizedBounceData*> nearby;
            
            int cellIndex = GetCellIndex(position);
            if (cellIndex >= 0 && cellIndex < static_cast<int>(grid.size())) {
                nearby = grid[cellIndex];
                
                // Also check neighboring cells
                int x = cellIndex % gridWidth;
                int y = cellIndex / gridWidth;
                
                for (int dx = -1; dx <= 1; ++dx) {
                    for (int dy = -1; dy <= 1; ++dy) {
                        if (dx == 0 && dy == 0) continue;
                        
                        int nx = x + dx;
                        int ny = y + dy;
                        
                        if (nx >= 0 && nx < gridWidth && ny >= 0 && ny < gridHeight) {
                            int neighborIndex = ny * gridWidth + nx;
                            const auto& neighborCell = grid[neighborIndex];
                            nearby.insert(nearby.end(), neighborCell.begin(), neighborCell.end());
                        }
                    }
                }
            }
            
            return nearby;
        }
        
    private:
        int GetCellIndex(const PhysicsVector3D& position) {
            int x = static_cast<int>(position.x / cellSize);
            int y = static_cast<int>(position.z / cellSize);  // Use Z for 2D grid
            
            x = std::clamp(x, 0, gridWidth - 1);
            y = std::clamp(y, 0, gridHeight - 1);
            
            return y * gridWidth + x;
        }
        
        float cellSize;
        int gridWidth, gridHeight;
        std::vector<std::vector<OptimizedBounceData*>> grid;
    };
    
    // Level-of-detail bouncing
    float CalculateLODBounceAccuracy(const PhysicsVector3D& position, 
                                    const PhysicsVector3D& viewerPosition) {
        float distance = (position - viewerPosition).Magnitude();
        
        if (distance < 10.0f) {
            return 1.0f;      // Full accuracy
        } else if (distance < 50.0f) {
            return 0.5f;      // Medium accuracy
        } else {
            return 0.25f;     // Low accuracy
        }
    }
};
```

### 7.9 Complete Bouncing System Example

```cpp
class CompleteBounceGameSystem {
public:
    void InitializeBounceGame() {
        Physics& physics = PHYSICS_INSTANCE();
        
        // Initialize all subsystems
        gameBallPhysics.InitializeBallTypes();
        
        // Create bouncing surfaces
        CreateGameSurfaces();
        
        // Initialize object pool
        bouncePool = std::make_unique<BounceOptimization::BounceObjectPool>(1000);
        
        // Initialize spatial grid
        spatialGrid = std::make_unique<BounceOptimization::BounceSpatialGrid>(5.0f, 20, 20);
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_INFO, 
                L"Complete bounce game system initialized");
        #endif
    }
    
    void UpdateBounceGame(float deltaTime) {
        // Clear spatial grid
        spatialGrid->ClearGrid();
        
        // Update all bouncing objects
        UpdateBouncingObjects(deltaTime);
        
        // Update particle systems
        particleSystem.UpdateBouncingParticles(deltaTime);
        
        // Update chain reactions
        chainReaction.UpdateChainReaction(chainObjects, deltaTime);
        
        // Process collision detection using spatial optimization
        ProcessOptimizedCollisions(deltaTime);
    }
    
    void CreateBouncingBall(GameBallPhysics::BallType ballType, 
                           const PhysicsVector3D& position,
                           const PhysicsVector3D& velocity) {
        auto result = gameBallPhysics.SimulateGameBall(ballType, position, velocity);
        
        // Store result for visualization/gameplay
        activeBounceResults.push_back(result);
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Created bouncing ball with %d predicted bounces", result.totalBounces);
        #endif
    }
    
    void TriggerBouncingExplosion(const PhysicsVector3D& center, float force) {
        particleSystem.CreateBouncingExplosion(center, 100, force);
        
        #if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Triggered bouncing explosion at (%.2f, %.2f, %.2f)",
                center.x, center.y, center.z);
        #endif
    }
    
    PhysicsVector3D PredictBallLanding(GameBallPhysics::BallType ballType,
                                      const PhysicsVector3D& position,
                                      const PhysicsVector3D& velocity) {
        return gameBallPhysics.PredictBallLanding(ballType, position, velocity);
    }
    
private:
    void CreateGameSurfaces() {
        // Create different surface types for varied bouncing
        MultiSurfaceBounceSystem::BounceSurface ground;
        ground.normal = PhysicsVector3D(0.0f, 1.0f, 0.0f);
        ground.point = PhysicsVector3D(0.0f, 0.0f, 0.0f);
        ground.properties.restitution = 0.8f;
        ground.properties.friction = 0.3f;
        ground.surfaceName = "Ground";
        
        MultiSurfaceBounceSystem::BounceSurface wall;
        wall.normal = PhysicsVector3D(-1.0f, 0.0f, 0.0f);
        wall.point = PhysicsVector3D(50.0f, 0.0f, 0.0f);
        wall.properties.restitution = 0.6f;
        wall.properties.friction = 0.5f;
        wall.surfaceName = "Wall";
        
        gameSurfaces.push_back(ground);
        gameSurfaces.push_back(wall);
    }
    
    void UpdateBouncingObjects(float deltaTime) {
        for (auto& result : activeBounceResults) {
            // Update object positions along calculated trajectory
            // This is simplified - in real implementation would track time
            
            #if defined(_DEBUG_PHYSICS_)
                static int logCounter = 0;
                if (++logCounter % 300 == 0) {  // Log every 5 seconds
                    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                        L"Updating %zu active bouncing objects", activeBounceResults.size());
                }
            #endif
        }
    }
    
    void ProcessOptimizedCollisions(float deltaTime) {
        // Use spatial grid for optimized collision detection
        // This would contain the actual collision processing logic
        
        #if defined(_DEBUG_PHYSICS_)
            static int logCounter = 0;
            if (++logCounter % 180 == 0) {  // Log every 3 seconds
                debug.logLevelMessage(LogLevel::LOG_DEBUG, 
                    L"Processed optimized collisions for bounce objects");
            }
        #endif
    }
    
    GameBallPhysics gameBallPhysics;
    BouncingParticleSystem particleSystem;
    ChainReactionBouncer chainReaction;
    MultiSurfaceBounceSystem multiSurfaceBouncer;
    
    std::vector<MultiSurfaceBounceSystem::BounceSurface> gameSurfaces;
    std::vector<BouncingBallSimulator::BounceResult> activeBounceResults;
    std::vector<ChainReactionBouncer::ChainObject> chainObjects;
    
    std::unique_ptr<BounceOptimization::BounceObjectPool> bouncePool;
    std::unique_ptr<BounceOptimization::BounceSpatialGrid> spatialGrid;
};
```

---

**Chapter 7 Complete**

This completes Chapter 7 covering comprehensive bouncing mechanics with inertia including:

- **Basic Bouncing Trajectory**: Simple and advanced bouncing calculations with energy analysis
- **Energy Conservation**: Realistic energy loss modeling and conservation tracking
- **Multiple Surface Bouncing**: Complex multi-surface interactions and destructible environments
- **Inertia and Momentum**: Rotational inertia effects and spin-bounce interactions
- **Practical Applications**: Game ball physics, chain reactions, and particle systems
- **Advanced Scenarios**: Destructible environments and complex bouncing sequences
- **Performance Optimization**: Object pooling, spatial partitioning, and LOD systems
- **Complete Example**: Full bouncing game system with all features integrated

All examples include proper debug logging, performance considerations, and real-world gaming applications.


## Chapter 8: Collision Detection and Response

### Overview
The Physics system provides comprehensive collision detection and response capabilities for gaming applications. This chapter covers AABB, sphere, and ray-based collision detection, along with advanced collision response mechanics.

### Key Features
- Multiple collision detection algorithms (AABB, Sphere, Ray-Sphere)
- Continuous collision detection to prevent tunneling
- Collision manifold generation and resolution
- Physics-based impulse and friction calculations
- Position correction for penetration resolution

---

### 8.1 Basic Collision Detection

#### AABB (Axis-Aligned Bounding Box) Collision
```cpp
#include "Physics.h"

void CheckAABBCollisions()
{
    // Initialize physics system
    Physics physics;
    if (!physics.Initialize()) {
        return; // Handle initialization failure
    }
    
    // Define two AABB boxes
    PhysicsVector3D boxA_min(-1.0f, -1.0f, -1.0f);
    PhysicsVector3D boxA_max(1.0f, 1.0f, 1.0f);
    
    PhysicsVector3D boxB_min(0.5f, 0.5f, 0.5f);
    PhysicsVector3D boxB_max(2.0f, 2.0f, 2.0f);
    
    // Check collision between boxes
    bool isColliding = physics.CheckAABBCollision(boxA_min, boxA_max, boxB_min, boxB_max);
    
    if (isColliding) {
        // Handle collision response
        // Boxes are overlapping
    }
    
    physics.Cleanup();
}
```

#### Sphere Collision Detection
```cpp
void CheckSphereCollisions()
{
    Physics physics;
    physics.Initialize();
    
    // Define two spheres
    PhysicsVector3D sphereA_center(0.0f, 0.0f, 0.0f);
    float sphereA_radius = 1.0f;
    
    PhysicsVector3D sphereB_center(1.5f, 0.0f, 0.0f);
    float sphereB_radius = 1.0f;
    
    // Check collision
    bool isColliding = physics.CheckSphereCollision(
        sphereA_center, sphereA_radius,
        sphereB_center, sphereB_radius
    );
    
    if (isColliding) {
        // Spheres are intersecting
        // Calculate collision response
    }
    
    physics.Cleanup();
}
```

---

### 8.2 Ray-Sphere Intersection

#### Basic Ray Casting
```cpp
void PerformRayCasting()
{
    Physics physics;
    physics.Initialize();
    
    // Define ray
    PhysicsVector3D rayOrigin(0.0f, 0.0f, -5.0f);
    PhysicsVector3D rayDirection(0.0f, 0.0f, 1.0f); // Normalized forward direction
    
    // Define target sphere
    PhysicsVector3D sphereCenter(0.0f, 0.0f, 0.0f);
    float sphereRadius = 1.0f;
    
    float hitDistance;
    bool rayHit = physics.RaySphereIntersection(
        rayOrigin, rayDirection,
        sphereCenter, sphereRadius,
        hitDistance
    );
    
    if (rayHit) {
        // Calculate hit position
        PhysicsVector3D hitPosition = rayOrigin + rayDirection * hitDistance;
        // Process hit result
    }
    
    physics.Cleanup();
}
```

---

### 8.3 Collision Manifold Generation

#### Creating and Processing Collision Manifolds
```cpp
void HandleCollisionManifolds()
{
    Physics physics;
    physics.Initialize();
    
    // Create two physics bodies
    PhysicsBody bodyA;
    bodyA.position = PhysicsVector3D(-1.0f, 0.0f, 0.0f);
    bodyA.velocity = PhysicsVector3D(2.0f, 0.0f, 0.0f);
    bodyA.SetMass(1.0f);
    bodyA.restitution = 0.8f;
    bodyA.friction = 0.3f;
    
    PhysicsBody bodyB;
    bodyB.position = PhysicsVector3D(1.0f, 0.0f, 0.0f);
    bodyB.velocity = PhysicsVector3D(-1.0f, 0.0f, 0.0f);
    bodyB.SetMass(2.0f);
    bodyB.restitution = 0.6f;
    bodyB.friction = 0.4f;
    
    // Generate collision manifold
    CollisionManifold manifold = physics.GenerateCollisionManifold(bodyA, bodyB);
    
    if (!manifold.contacts.empty()) {
        // Process each contact point
        for (const auto& contact : manifold.contacts) {
            // Access contact information
            PhysicsVector3D contactPos = contact.position;
            PhysicsVector3D contactNormal = contact.normal;
            float penetration = contact.penetrationDepth;
            
            // Handle contact-specific logic
        }
        
        // Resolve collision
        physics.ResolveCollisionResponse(manifold);
    }
    
    physics.Cleanup();
}
```

---

### 8.4 Continuous Collision Detection

#### Preventing Tunneling with CCD
```cpp
void UseContinuousCollisionDetection()
{
    Physics physics;
    physics.Initialize();
    
    // Create fast-moving bodies
    PhysicsBody fastBody;
    fastBody.position = PhysicsVector3D(-10.0f, 0.0f, 0.0f);
    fastBody.velocity = PhysicsVector3D(50.0f, 0.0f, 0.0f); // Very fast
    fastBody.SetMass(1.0f);
    
    PhysicsBody staticBody;
    staticBody.position = PhysicsVector3D(0.0f, 0.0f, 0.0f);
    staticBody.velocity = PhysicsVector3D(0.0f, 0.0f, 0.0f);
    staticBody.SetMass(1.0f);
    staticBody.isStatic = true;
    
    float deltaTime = 0.016f; // 60 FPS
    
    // Check for potential tunneling
    bool willCollide = physics.ContinuousCollisionDetection(fastBody, staticBody, deltaTime);
    
    if (willCollide) {
        // Handle collision before tunneling occurs
        // Adjust positions or velocities as needed
    }
    
    physics.Cleanup();
}
```

---

### 8.5 Advanced Collision Response

#### Custom Collision Resolution
```cpp
void AdvancedCollisionHandling()
{
    Physics physics;
    physics.Initialize();
    
    // Create collision scenario
    PhysicsBody movingBody;
    movingBody.position = PhysicsVector3D(0.0f, 5.0f, 0.0f);
    movingBody.velocity = PhysicsVector3D(0.0f, -10.0f, 0.0f);
    movingBody.SetMass(1.0f);
    movingBody.restitution = 0.7f; // Bouncy
    movingBody.friction = 0.2f;    // Low friction
    
    PhysicsBody groundBody;
    groundBody.position = PhysicsVector3D(0.0f, 0.0f, 0.0f);
    groundBody.SetMass(1000.0f); // Very heavy (near-static)
    groundBody.restitution = 0.8f;
    groundBody.friction = 0.5f;
    groundBody.isStatic = true;
    
    // Generate and resolve collision
    CollisionManifold manifold = physics.GenerateCollisionManifold(movingBody, groundBody);
    
    if (!manifold.contacts.empty()) {
        // Apply custom pre-collision logic
        for (auto& contact : manifold.contacts) {
            // Modify contact properties if needed
            contact.restitution *= 0.9f; // Reduce bounce slightly
            contact.friction += 0.1f;    // Increase friction
        }
        
        // Resolve with modified parameters
        physics.ResolveCollisionResponse(manifold);
        
        // Apply post-collision effects
        // Add particle effects, sound, etc.
    }
    
    physics.Cleanup();
}
```

---

### 8.6 Multiple Body Collision System

#### Managing Multiple Colliding Bodies
```cpp
void MultiBodyCollisionSystem()
{
    Physics physics;
    physics.Initialize();
    
    // Create multiple physics bodies
    std::vector<PhysicsBody> bodies;
    bodies.reserve(5);
    
    // Initialize bodies in a cluster
    for (int i = 0; i < 5; ++i) {
        PhysicsBody body;
        body.position = PhysicsVector3D(
            static_cast<float>(i) * 0.5f,
            5.0f,
            0.0f
        );
        body.velocity = PhysicsVector3D(
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f,
            -2.0f,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f
        );
        body.SetMass(1.0f + static_cast<float>(i) * 0.2f);
        body.restitution = 0.6f + static_cast<float>(i) * 0.1f;
        body.friction = 0.3f;
        
        bodies.push_back(body);
    }
    
    // Simulate collision detection for all pairs
    float deltaTime = 0.016f;
    
    for (size_t i = 0; i < bodies.size(); ++i) {
        for (size_t j = i + 1; j < bodies.size(); ++j) {
            // Check collision between bodies i and j
            CollisionManifold manifold = physics.GenerateCollisionManifold(bodies[i], bodies[j]);
            
            if (!manifold.contacts.empty()) {
                // Resolve collision
                physics.ResolveCollisionResponse(manifold);
                
                // Update body references after collision resolution
                bodies[i] = *manifold.bodyA;
                bodies[j] = *manifold.bodyB;
            }
        }
    }
    
    physics.Cleanup();
}
```

---

### 8.7 Collision Filtering and Groups

#### Implementing Collision Layers
```cpp
enum CollisionLayers {
    LAYER_PLAYER = 1 << 0,     // 1
    LAYER_ENEMY = 1 << 1,      // 2
    LAYER_PROJECTILE = 1 << 2, // 4
    LAYER_ENVIRONMENT = 1 << 3, // 8
    LAYER_PICKUP = 1 << 4      // 16
};

struct ExtendedPhysicsBody : public PhysicsBody {
    uint32_t collisionLayer = LAYER_ENVIRONMENT;
    uint32_t collisionMask = 0xFFFFFFFF; // Collides with all by default
    
    bool ShouldCollideWith(const ExtendedPhysicsBody& other) const {
        return (collisionLayer & other.collisionMask) && 
               (other.collisionLayer & collisionMask);
    }
};

void CollisionFilteringExample()
{
    Physics physics;
    physics.Initialize();
    
    // Create player body
    ExtendedPhysicsBody player;
    player.position = PhysicsVector3D(0.0f, 0.0f, 0.0f);
    player.SetMass(1.0f);
    player.collisionLayer = LAYER_PLAYER;
    player.collisionMask = LAYER_ENEMY | LAYER_ENVIRONMENT | LAYER_PICKUP;
    
    // Create enemy body
    ExtendedPhysicsBody enemy;
    enemy.position = PhysicsVector3D(2.0f, 0.0f, 0.0f);
    enemy.SetMass(1.0f);
    enemy.collisionLayer = LAYER_ENEMY;
    enemy.collisionMask = LAYER_PLAYER | LAYER_PROJECTILE | LAYER_ENVIRONMENT;
    
    // Create projectile
    ExtendedPhysicsBody projectile;
    projectile.position = PhysicsVector3D(1.0f, 0.0f, 0.0f);
    projectile.SetMass(0.1f);
    projectile.collisionLayer = LAYER_PROJECTILE;
    projectile.collisionMask = LAYER_ENEMY | LAYER_ENVIRONMENT;
    
    // Check if bodies should collide
    if (player.ShouldCollideWith(enemy)) {
        // Perform collision detection and response
        CollisionManifold manifold = physics.GenerateCollisionManifold(player, enemy);
        if (!manifold.contacts.empty()) {
            physics.ResolveCollisionResponse(manifold);
        }
    }
    
    if (projectile.ShouldCollideWith(enemy)) {
        // Handle projectile-enemy collision
        CollisionManifold manifold = physics.GenerateCollisionManifold(projectile, enemy);
        if (!manifold.contacts.empty()) {
            physics.ResolveCollisionResponse(manifold);
            // Apply damage, effects, etc.
        }
    }
    
    physics.Cleanup();
}
```

---

### 8.8 Performance Considerations

#### Optimized Collision Detection
```cpp
void OptimizedCollisionDetection()
{
    Physics physics;
    physics.Initialize();
    
    // Use spatial partitioning for large numbers of objects
    const int GRID_SIZE = 10;
    const float CELL_SIZE = 10.0f;
    
    std::vector<PhysicsBody> bodies;
    // ... populate bodies
    
    // Broad-phase collision detection (simplified)
    std::vector<std::pair<int, int>> potentialCollisions;
    
    for (size_t i = 0; i < bodies.size(); ++i) {
        for (size_t j = i + 1; j < bodies.size(); ++j) {
            // Quick distance check
            PhysicsVector3D diff = bodies[j].position - bodies[i].position;
            float distanceSquared = diff.MagnitudeSquared();
            float maxRadius = 2.0f; // Assume maximum object radius
            
            if (distanceSquared <= (maxRadius * maxRadius * 4.0f)) {
                potentialCollisions.push_back({static_cast<int>(i), static_cast<int>(j)});
            }
        }
    }
    
    // Narrow-phase collision detection only for potential collisions
    for (const auto& pair : potentialCollisions) {
        CollisionManifold manifold = physics.GenerateCollisionManifold(
            bodies[pair.first], 
            bodies[pair.second]
        );
        
        if (!manifold.contacts.empty()) {
            physics.ResolveCollisionResponse(manifold);
        }
    }
    
    physics.Cleanup();
}
```

---

### 8.9 Debugging and Visualization

#### Collision Debug Visualization
```cpp
void CollisionDebugging()
{
    Physics physics;
    physics.Initialize();
    
    PhysicsBody bodyA, bodyB;
    // ... initialize bodies
    
    // Generate collision manifold
    CollisionManifold manifold = physics.GenerateCollisionManifold(bodyA, bodyB);
    
    if (!manifold.contacts.empty()) {
        // Add debug visualization
        for (const auto& contact : manifold.contacts) {
            // Add debug line for contact normal
            PhysicsVector3D normalEnd = contact.position + contact.normal * 2.0f;
            physics.AddDebugLine(contact.position, normalEnd);
            
            // Add debug sphere at contact point
            // (Implementation depends on your rendering system)
        }
        
        // Resolve collision
        physics.ResolveCollisionResponse(manifold);
    }
    
    // Get debug lines for rendering
    std::vector<PhysicsVector3D> debugLines = physics.GetDebugLines();
    
    // Render debug lines in your graphics system
    // for (size_t i = 0; i < debugLines.size(); i += 2) {
    //     RenderLine(debugLines[i], debugLines[i + 1]);
    // }
    
    physics.Cleanup();
}
```

---

### Best Practices for Collision Detection

1. **Layer Management**: Use collision layers to filter unnecessary collision checks
2. **Broad-Phase Optimization**: Implement spatial partitioning for large object counts
3. **Continuous Detection**: Use CCD for fast-moving objects to prevent tunneling
4. **Contact Persistence**: Cache collision manifolds across frames for stability
5. **Debug Visualization**: Always implement collision debug rendering for development

### Common Issues and Solutions

- **Tunneling**: Objects passing through each other → Use continuous collision detection
- **Jittering**: Objects vibrating at contact → Adjust position correction parameters
- **Performance**: Too many collision checks → Implement spatial partitioning
- **Penetration**: Objects sinking into each other → Increase position correction strength

---

**Chapter 8 Complete**

## Chapter 9: Ragdoll Physics System

### Overview
The Physics system provides comprehensive ragdoll physics capabilities for realistic character animation and death sequences. This chapter covers ragdoll creation, joint constraints, animation blending, and real-time physics simulation.

### Key Features
- Dynamic ragdoll creation from joint hierarchies
- Configurable joint constraints with angle limits
- Physics-animation blending for smooth transitions
- Real-time constraint solving and stabilization
- Performance-optimized joint management

---

### 9.1 Basic Ragdoll Creation

#### Creating a Simple Ragdoll
```cpp
#include "Physics.h"

void CreateBasicRagdoll()
{
    Physics physics;
    if (!physics.Initialize()) {
        return; // Handle initialization failure
    }
    
    // Define joint positions for a basic humanoid
    std::vector<PhysicsVector3D> jointPositions = {
        PhysicsVector3D(0.0f, 1.8f, 0.0f),  // Head
        PhysicsVector3D(0.0f, 1.5f, 0.0f),  // Neck
        PhysicsVector3D(0.0f, 1.2f, 0.0f),  // Upper torso
        PhysicsVector3D(0.0f, 0.8f, 0.0f),  // Lower torso
        PhysicsVector3D(-0.3f, 1.2f, 0.0f), // Left shoulder
        PhysicsVector3D(-0.6f, 0.9f, 0.0f), // Left elbow
        PhysicsVector3D(-0.9f, 0.6f, 0.0f), // Left hand
        PhysicsVector3D(0.3f, 1.2f, 0.0f),  // Right shoulder
        PhysicsVector3D(0.6f, 0.9f, 0.0f),  // Right elbow
        PhysicsVector3D(0.9f, 0.6f, 0.0f),  // Right hand
        PhysicsVector3D(-0.1f, 0.4f, 0.0f), // Left hip
        PhysicsVector3D(-0.1f, 0.0f, 0.0f), // Left knee
        PhysicsVector3D(-0.1f, -0.4f, 0.0f),// Left foot
        PhysicsVector3D(0.1f, 0.4f, 0.0f),  // Right hip
        PhysicsVector3D(0.1f, 0.0f, 0.0f),  // Right knee
        PhysicsVector3D(0.1f, -0.4f, 0.0f)  // Right foot
    };
    
    // Define joint connections (parent-child relationships)
    std::vector<std::pair<int, int>> connections = {
        {0, 1},   // Head to neck
        {1, 2},   // Neck to upper torso
        {2, 3},   // Upper to lower torso
        {2, 4},   // Upper torso to left shoulder
        {4, 5},   // Left shoulder to elbow
        {5, 6},   // Left elbow to hand
        {2, 7},   // Upper torso to right shoulder
        {7, 8},   // Right shoulder to elbow
        {8, 9},   // Right elbow to hand
        {3, 10},  // Lower torso to left hip
        {10, 11}, // Left hip to knee
        {11, 12}, // Left knee to foot
        {3, 13},  // Lower torso to right hip
        {13, 14}, // Right hip to knee
        {14, 15}  // Right knee to foot
    };
    
    // Create ragdoll
    std::vector<PhysicsBody> ragdollBodies = physics.CreateRagdoll(jointPositions, connections);
    
    // Configure individual body properties
    for (auto& body : ragdollBodies) {
        body.restitution = 0.1f; // Low bounce for realism
        body.friction = 0.8f;    // High friction for stability
        body.drag = 0.1f;        // Air resistance
    }
    
    physics.Cleanup();
}
```

---

### 9.2 Advanced Joint Configuration

#### Configuring Joint Constraints
```cpp
void ConfigureAdvancedJoints()
{
    Physics physics;
    physics.Initialize();
    
    // Create basic ragdoll first
    std::vector<PhysicsVector3D> jointPositions = {
        PhysicsVector3D(0.0f, 1.0f, 0.0f),  // Torso
        PhysicsVector3D(-0.3f, 1.0f, 0.0f), // Left shoulder
        PhysicsVector3D(-0.6f, 0.7f, 0.0f)  // Left elbow
    };
    
    std::vector<std::pair<int, int>> connections = {
        {0, 1}, // Torso to shoulder
        {1, 2}  // Shoulder to elbow
    };
    
    std::vector<PhysicsBody> ragdollBodies = physics.CreateRagdoll(jointPositions, connections);
    
    // Configure shoulder joint (ball joint with limits)
    RagdollJoint shoulderJoint;
    shoulderJoint.bodyA = &ragdollBodies[0]; // Torso
    shoulderJoint.bodyB = &ragdollBodies[1]; // Shoulder
    shoulderJoint.anchorA = PhysicsVector3D(-0.3f, 0.0f, 0.0f); // Local anchor on torso
    shoulderJoint.anchorB = PhysicsVector3D(0.0f, 0.0f, 0.0f);  // Local anchor on shoulder
    shoulderJoint.minAngle = -XM_PI * 0.7f; // -126 degrees
    shoulderJoint.maxAngle = XM_PI * 0.7f;  // +126 degrees
    shoulderJoint.stiffness = 800.0f;       // Medium stiffness
    shoulderJoint.damping = 40.0f;          // Moderate damping
    shoulderJoint.isActive = true;
    
    physics.AddRagdollJoint(shoulderJoint);
    
    // Configure elbow joint (hinge joint with limits)
    RagdollJoint elbowJoint;
    elbowJoint.bodyA = &ragdollBodies[1]; // Shoulder
    elbowJoint.bodyB = &ragdollBodies[2]; // Elbow
    elbowJoint.anchorA = PhysicsVector3D(-0.3f, -0.3f, 0.0f);
    elbowJoint.anchorB = PhysicsVector3D(0.0f, 0.0f, 0.0f);
    elbowJoint.minAngle = -XM_PI * 0.1f;  // Slight extension
    elbowJoint.maxAngle = XM_PI * 0.8f;   // Full bend
    elbowJoint.stiffness = 1200.0f;       // Higher stiffness for stability
    elbowJoint.damping = 60.0f;           // Higher damping
    elbowJoint.isActive = true;
    
    physics.AddRagdollJoint(elbowJoint);
    
    physics.Cleanup();
}
```

---

### 9.3 Ragdoll Animation Blending

#### Blending Physics with Animation
```cpp
void RagdollAnimationBlending()
{
    Physics physics;
    physics.Initialize();
    
    // Create ragdoll
    std::vector<PhysicsVector3D> jointPositions = {
        PhysicsVector3D(0.0f, 1.0f, 0.0f),
        PhysicsVector3D(0.0f, 0.5f, 0.0f),
        PhysicsVector3D(0.0f, 0.0f, 0.0f)
    };
    
    std::vector<std::pair<int, int>> connections = {
        {0, 1}, {1, 2}
    };
    
    std::vector<PhysicsBody> ragdollBodies = physics.CreateRagdoll(jointPositions, connections);
    
    // Animation keyframe positions
    std::vector<PhysicsVector3D> animationPositions = {
        PhysicsVector3D(0.2f, 1.1f, 0.0f),  // Animated head position
        PhysicsVector3D(0.1f, 0.6f, 0.0f),  // Animated torso position
        PhysicsVector3D(0.0f, 0.1f, 0.0f)   // Animated hip position
    };
    
    // Blend between physics and animation
    float blendFactor = 0.7f; // 70% animation, 30% physics
    
    physics.BlendRagdollWithAnimation(ragdollBodies, animationPositions, blendFactor);
    
    // The ragdoll bodies now contain blended positions
    for (size_t i = 0; i < ragdollBodies.size(); ++i) {
        PhysicsVector3D blendedPos = ragdollBodies[i].position;
        // Use blended position for rendering
    }
    
    physics.Cleanup();
}
```

---

### 9.4 Real-Time Ragdoll Simulation

#### Complete Ragdoll Update Loop
```cpp
class RagdollCharacter
{
private:
    Physics* m_physics;
    std::vector<PhysicsBody> m_ragdollBodies;
    bool m_isRagdollActive;
    float m_ragdollBlendFactor;
    
public:
    RagdollCharacter() : m_physics(nullptr), m_isRagdollActive(false), m_ragdollBlendFactor(0.0f) {}
    
    bool Initialize(Physics* physics)
    {
        m_physics = physics;
        
        // Create full body ragdoll
        std::vector<PhysicsVector3D> jointPositions = {
            PhysicsVector3D(0.0f, 1.8f, 0.0f),  // Head
            PhysicsVector3D(0.0f, 1.5f, 0.0f),  // Neck
            PhysicsVector3D(0.0f, 1.2f, 0.0f),  // Upper torso
            PhysicsVector3D(0.0f, 0.8f, 0.0f),  // Lower torso
            PhysicsVector3D(-0.3f, 1.2f, 0.0f), // Left shoulder
            PhysicsVector3D(-0.6f, 0.9f, 0.0f), // Left elbow
            PhysicsVector3D(0.3f, 1.2f, 0.0f),  // Right shoulder
            PhysicsVector3D(0.6f, 0.9f, 0.0f),  // Right elbow
            PhysicsVector3D(-0.1f, 0.4f, 0.0f), // Left hip
            PhysicsVector3D(-0.1f, 0.0f, 0.0f), // Left knee
            PhysicsVector3D(0.1f, 0.4f, 0.0f),  // Right hip
            PhysicsVector3D(0.1f, 0.0f, 0.0f)   // Right knee
        };
        
        std::vector<std::pair<int, int>> connections = {
            {0, 1}, {1, 2}, {2, 3},     // Spine
            {2, 4}, {4, 5},             // Left arm
            {2, 6}, {6, 7},             // Right arm
            {3, 8}, {8, 9},             // Left leg
            {3, 10}, {10, 11}           // Right leg
        };
        
        m_ragdollBodies = m_physics->CreateRagdoll(jointPositions, connections);
        
        // Configure body properties
        for (auto& body : m_ragdollBodies) {
            body.restitution = 0.05f; // Very low bounce
            body.friction = 0.9f;     // High friction
            body.drag = 0.15f;        // Moderate air resistance
        }
        
        return true;
    }
    
    void Update(float deltaTime)
    {
        if (!m_physics || m_ragdollBodies.empty()) {
            return;
        }
        
        if (m_isRagdollActive) {
            // Update ragdoll physics
            m_physics->UpdateRagdoll(m_ragdollBodies, deltaTime);
            
            // Gradually increase ragdoll influence
            m_ragdollBlendFactor = std::min(1.0f, m_ragdollBlendFactor + deltaTime * 2.0f);
        } else {
            // Gradually decrease ragdoll influence
            m_ragdollBlendFactor = std::max(0.0f, m_ragdollBlendFactor - deltaTime * 3.0f);
        }
    }
    
    void ActivateRagdoll()
    {
        m_isRagdollActive = true;
        
        // Apply initial forces for dramatic effect
        for (auto& body : m_ragdollBodies) {
            PhysicsVector3D randomForce(
                (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 10.0f,
                static_cast<float>(rand()) / RAND_MAX * 5.0f,
                (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 10.0f
            );
            body.ApplyImpulse(randomForce);
        }
    }
    
    void DeactivateRagdoll()
    {
        m_isRagdollActive = false;
    }
    
    void ApplyExplosionForce(const PhysicsVector3D& explosionCenter, float force)
    {
        for (auto& body : m_ragdollBodies) {
            PhysicsVector3D direction = body.position - explosionCenter;
            float distance = direction.Magnitude();
            
            if (distance > 0.1f) {
                direction = direction.Normalized();
                float forceMagnitude = force / (1.0f + distance * distance * 0.1f);
                body.ApplyImpulse(direction * forceMagnitude);
            }
        }
    }
    
    std::vector<PhysicsVector3D> GetBonePositions() const
    {
        std::vector<PhysicsVector3D> positions;
        positions.reserve(m_ragdollBodies.size());
        
        for (const auto& body : m_ragdollBodies) {
            positions.push_back(body.position);
        }
        
        return positions;
    }
    
    float GetRagdollBlendFactor() const { return m_ragdollBlendFactor; }
    bool IsRagdollActive() const { return m_isRagdollActive; }
};
```

---

### 9.5 Ragdoll Performance Optimization

#### Optimized Ragdoll Management
```cpp
void OptimizedRagdollSystem()
{
    Physics physics;
    physics.Initialize();
    
    // Create multiple ragdolls with LOD system
    std::vector<RagdollCharacter> characters;
    characters.resize(10);
    
    for (auto& character : characters) {
        character.Initialize(&physics);
    }
    
    // Update loop with distance-based LOD
    PhysicsVector3D cameraPosition(0.0f, 2.0f, -10.0f);
    float deltaTime = 0.016f;
    
    for (size_t i = 0; i < characters.size(); ++i) {
        // Calculate distance from camera
        std::vector<PhysicsVector3D> bonePositions = characters[i].GetBonePositions();
        if (!bonePositions.empty()) {
            float distance = (bonePositions[0] - cameraPosition).Magnitude();
            
            // Apply LOD based on distance
            if (distance < 20.0f) {
                // Full update for close characters
                characters[i].Update(deltaTime);
            } else if (distance < 50.0f) {
                // Reduced update frequency for medium distance
                if (i % 2 == 0) { // Update every other frame
                    characters[i].Update(deltaTime * 2.0f);
                }
            } else {
                // No physics update for distant characters
                // Keep in animated state only
                characters[i].DeactivateRagdoll();
            }
        }
    }
    
    physics.Cleanup();
}
```

---

### 9.6 Ragdoll Interaction Systems

#### Environmental Interactions
```cpp
void RagdollEnvironmentInteraction()
{
    Physics physics;
    physics.Initialize();
    
    RagdollCharacter character;
    character.Initialize(&physics);
    character.ActivateRagdoll();
    
    // Create environmental obstacles
    std::vector<PhysicsBody> obstacles;
    
    // Ground plane
    PhysicsBody ground;
    ground.position = PhysicsVector3D(0.0f, -0.5f, 0.0f);
    ground.SetMass(1000.0f);
    ground.isStatic = true;
    ground.restitution = 0.2f;
    ground.friction = 0.8f;
    obstacles.push_back(ground);
    
    // Wall
    PhysicsBody wall;
    wall.position = PhysicsVector3D(5.0f, 1.0f, 0.0f);
    wall.SetMass(1000.0f);
    wall.isStatic = true;
    wall.restitution = 0.1f;
    wall.friction = 0.9f;
    obstacles.push_back(wall);
    
    // Simulation loop
    float deltaTime = 0.016f;
    int maxIterations = 1000; // Prevent infinite loop
    
    for (int frame = 0; frame < maxIterations; ++frame) {
        // Update ragdoll
        character.Update(deltaTime);
        
        // Check collisions with environment
        std::vector<PhysicsVector3D> bonePositions = character.GetBonePositions();
        
        for (size_t boneIndex = 0; boneIndex < bonePositions.size(); ++boneIndex) {
            for (const auto& obstacle : obstacles) {
                // Simple collision check (sphere vs AABB)
                PhysicsVector3D diff = bonePositions[boneIndex] - obstacle.position;
                float distance = diff.Magnitude();
                
                if (distance < 1.0f) { // Collision detected
                    // Apply collision response
                    PhysicsVector3D normal = diff.Normalized();
                    PhysicsVector3D separationImpulse = normal * 2.0f;
                    
                    // Apply to ragdoll (simplified)
                    // In real implementation, access ragdoll bodies directly
                }
            }
        }
        
        // Check if ragdoll has settled
        bool isSettled = true;
        for (const auto& pos : bonePositions) {
            if (pos.y > -0.4f && std::abs(pos.x) < 10.0f) {
                isSettled = false;
                break;
            }
        }
        
        if (isSettled) {
            break; // Ragdoll has come to rest
        }
    }
    
    physics.Cleanup();
}
```

---

### 9.7 Debugging Ragdoll Systems

#### Ragdoll Debug Visualization
```cpp
void DebugRagdollVisualization()
{
    Physics physics;
    physics.Initialize();
    
    RagdollCharacter character;
    character.Initialize(&physics);
    character.ActivateRagdoll();
    
    // Update for a few frames
    for (int i = 0; i < 10; ++i) {
        character.Update(0.016f);
        
        // Get bone positions for debug rendering
        std::vector<PhysicsVector3D> bonePositions = character.GetBonePositions();
        
        // Add debug lines between connected bones
        std::vector<std::pair<int, int>> debugConnections = {
            {0, 1}, {1, 2}, {2, 3},     // Spine
            {2, 4}, {4, 5},             // Left arm
            {2, 6}, {6, 7},             // Right arm
            {3, 8}, {8, 9},             // Left leg
            {3, 10}, {10, 11}           // Right leg
        };
        
        for (const auto& connection : debugConnections) {
            if (connection.first < static_cast<int>(bonePositions.size()) && 
                connection.second < static_cast<int>(bonePositions.size())) {
                physics.AddDebugLine(
                    bonePositions[connection.first],
                    bonePositions[connection.second]
                );
            }
        }
    }
    
    // Get debug lines for rendering
    std::vector<PhysicsVector3D> debugLines = physics.GetDebugLines();
    
    // Render debug visualization
    // for (size_t i = 0; i < debugLines.size(); i += 2) {
    //     RenderDebugLine(debugLines[i], debugLines[i + 1], Color::Green);
    // }
    
    physics.Cleanup();
}
```

---

### Best Practices for Ragdoll Physics

1. **Joint Configuration**: Use realistic joint limits based on human anatomy
2. **Mass Distribution**: Set appropriate mass values for different body parts
3. **Constraint Tuning**: Balance stiffness and damping for stability
4. **LOD Implementation**: Use distance-based level of detail for performance
5. **Blending Transitions**: Smoothly transition between animation and ragdoll states

### Common Issues and Solutions

- **Instability**: Ragdoll shaking or exploding → Reduce constraint stiffness, increase damping
- **Unrealistic Movement**: Joints bending wrong way → Check joint angle limits
- **Performance Issues**: Too many ragdolls → Implement LOD system and spatial culling
- **Poor Blending**: Sudden transitions → Use gradual blend factor changes

---

**Chapter 9 Complete**

## Chapter 10: Newtonian Motion and Projectile Physics

### Overview
The Physics system provides comprehensive Newtonian motion simulation and projectile physics for realistic ballistics, trajectory calculations, and motion dynamics. This chapter covers force application, projectile motion, trajectory prediction, and advanced ballistics.

### Key Features
- Newton's laws of motion implementation
- Projectile trajectory calculation with gravity and drag
- Target trajectory solving for accurate aiming
- Ballistic trajectory prediction
- Force-based motion simulation
- Optimized mathematical calculations using MathPrecalculation

---

### 10.1 Basic Newtonian Motion

#### Applying Forces to Physics Bodies
```cpp
#include "Physics.h"

void BasicNewtonianMotion()
{
    Physics physics;
    if (!physics.Initialize()) {
        return; // Handle initialization failure
    }
    
    // Create physics body
    PhysicsBody projectile;
    projectile.position = PhysicsVector3D(0.0f, 1.0f, 0.0f);
    projectile.velocity = PhysicsVector3D(0.0f, 0.0f, 0.0f);
    projectile.SetMass(1.0f);
    projectile.drag = 0.05f;
    projectile.restitution = 0.8f;
    projectile.friction = 0.3f;
    
    // Apply constant force (Newton's Second Law: F = ma)
    PhysicsVector3D appliedForce(10.0f, 0.0f, 0.0f); // 10N to the right
    float deltaTime = 0.016f; // 60 FPS
    
    // Simulate motion for 5 seconds
    for (int frame = 0; frame < 300; ++frame) {
        // Apply Newtonian motion
        physics.ApplyNewtonianMotion(projectile, appliedForce, deltaTime);
        
        // Log position every 60 frames (1 second)
        if (frame % 60 == 0) {
            float time = frame * deltaTime;
            // Position data available in projectile.position
        }
    }
    
    physics.Cleanup();
}
```

#### Multiple Force Application
```cpp
void MultipleForceApplication()
{
    Physics physics;
    physics.Initialize();
    
    PhysicsBody body;
    body.position = PhysicsVector3D(0.0f, 5.0f, 0.0f);
    body.SetMass(2.0f);
    body.drag = 0.1f;
    
    float deltaTime = 0.016f;
    
    for (int frame = 0; frame < 180; ++frame) { // 3 seconds
        // Apply gravity
        PhysicsVector3D gravity(0.0f, -9.81f * body.mass, 0.0f);
        
        // Apply wind force
        PhysicsVector3D wind(2.0f, 0.0f, 1.0f);
        
        // Apply thrust (first 1 second only)
        PhysicsVector3D thrust(0.0f, 0.0f, 0.0f);
        if (frame < 60) {
            thrust = PhysicsVector3D(0.0f, 15.0f, 0.0f);
        }
        
        // Combine all forces
        PhysicsVector3D totalForce = gravity + wind + thrust;
        
        // Apply combined force
        physics.ApplyNewtonianMotion(body, totalForce, deltaTime);
    }
    
    physics.Cleanup();
}
```

---

### 10.2 Projectile Motion Simulation

#### Basic Projectile Trajectory
```cpp
void BasicProjectileMotion()
{
    Physics physics;
    physics.Initialize();
    
    // Launch parameters
    PhysicsVector3D startPosition(0.0f, 1.0f, 0.0f);
    PhysicsVector3D initialVelocity(20.0f, 15.0f, 0.0f); // 20 m/s horizontal, 15 m/s vertical
    float gravity = 9.81f;
    float drag = 0.02f;
    float timeStep = 0.01f; // High precision
    float maxTime = 10.0f;
    
    // Calculate trajectory
    std::vector<PhysicsVector3D> trajectory = physics.CalculateProjectileMotion(
        startPosition, initialVelocity, gravity, drag, timeStep, maxTime
    );
    
    // Analyze trajectory
    float maxHeight = 0.0f;
    float range = 0.0f;
    float flightTime = 0.0f;
    
    for (size_t i = 0; i < trajectory.size(); ++i) {
        const PhysicsVector3D& point = trajectory[i];
        
        // Track maximum height
        if (point.y > maxHeight) {
            maxHeight = point.y;
        }
        
        // Check for ground impact
        if (point.y <= 0.0f && i > 0) {
            range = point.x;
            flightTime = i * timeStep;
            break;
        }
    }
    
    // Results available: maxHeight, range, flightTime
    physics.Cleanup();
}
```

#### Advanced Projectile with Environmental Factors
```cpp
void AdvancedProjectileMotion()
{
    Physics physics;
    physics.Initialize();
    
    // Create projectile with realistic properties
    PhysicsBody projectile;
    projectile.position = PhysicsVector3D(0.0f, 2.0f, 0.0f);
    projectile.velocity = PhysicsVector3D(50.0f, 30.0f, 0.0f); // High-speed projectile
    projectile.SetMass(0.5f); // 500g projectile
    projectile.drag = 0.1f;   // Significant air resistance
    
    // Environmental forces
    PhysicsVector3D windForce(5.0f, 0.0f, 2.0f); // Crosswind
    PhysicsVector3D gravityField(0.0f, -9.81f, 0.0f);
    
    std::vector<PhysicsVector3D> trajectory;
    trajectory.reserve(1000);
    
    float deltaTime = 0.01f;
    int maxIterations = 1000;
    
    for (int i = 0; i < maxIterations; ++i) {
        // Record position
        trajectory.push_back(projectile.position);
        
        // Apply environmental forces
        PhysicsVector3D totalForce = gravityField * projectile.mass + windForce;
        
        // Update projectile
        physics.ApplyNewtonianMotion(projectile, totalForce, deltaTime);
        
        // Check for ground collision
        if (projectile.position.y <= 0.0f) {
            trajectory.push_back(projectile.position); // Final position
            break;
        }
    }
    
    // Trajectory analysis and visualization data ready
    physics.Cleanup();
}
```

---

### 10.3 Trajectory Prediction and Targeting

#### Calculate Trajectory to Hit Target
```cpp
void TrajectoryTargeting()
{
    Physics physics;
    physics.Initialize();
    
    // Firing position and target
    PhysicsVector3D firingPosition(0.0f, 2.0f, 0.0f);
    PhysicsVector3D targetPosition(30.0f, 1.5f, 10.0f);
    float launchSpeed = 25.0f;
    float gravity = 9.81f;
    
    // Calculate required launch velocity
    PhysicsVector3D launchVelocity = physics.CalculateTrajectoryToTarget(
        firingPosition, targetPosition, gravity, launchSpeed
    );
    
    // Verify trajectory hits target
    std::vector<PhysicsVector3D> predictedPath = physics.CalculateProjectileMotion(
        firingPosition, launchVelocity, gravity, 0.01f, 0.01f, 10.0f
    );
    
    // Find closest point to target
    float closestDistance = FLT_MAX;
    PhysicsVector3D closestPoint;
    
    for (const auto& point : predictedPath) {
        float distance = (point - targetPosition).Magnitude();
        if (distance < closestDistance) {
            closestDistance = distance;
            closestPoint = point;
        }
    }
    
    // Check accuracy (should be close to target)
    bool accurateShot = closestDistance < 1.0f; // Within 1 meter
    
    physics.Cleanup();
}
```

#### Ballistic Calculator with Multiple Solutions
```cpp
void BallisticCalculator()
{
    Physics physics;
    physics.Initialize();
    
    PhysicsVector3D origin(0.0f, 0.0f, 0.0f);
    PhysicsVector3D target(100.0f, -10.0f, 0.0f); // Target below firing position
    float muzzleVelocity = 40.0f;
    float gravity = 9.81f;
    
    // Calculate high and low angle solutions
    PhysicsVector3D displacement = target - origin;
    float horizontalDistance = sqrt(displacement.x * displacement.x + displacement.z * displacement.z);
    float verticalDistance = displacement.y;
    
    // Ballistic formula: tan(θ) = (v²± √(v⁴ - g(gx² + 2yv²))) / (gx)
    float v2 = muzzleVelocity * muzzleVelocity;
    float v4 = v2 * v2;
    float gx2 = gravity * horizontalDistance * horizontalDistance;
    float discriminant = v4 - gravity * (gx2 + 2 * verticalDistance * v2);
    
    if (discriminant >= 0.0f) {
        float sqrtDiscriminant = sqrt(discriminant);
        
        // High angle solution
        float highAngle = atan((v2 + sqrtDiscriminant) / (gravity * horizontalDistance));
        
        // Low angle solution
        float lowAngle = atan((v2 - sqrtDiscriminant) / (gravity * horizontalDistance));
        
        // Create launch velocities for both solutions
        PhysicsVector3D horizontalDir = PhysicsVector3D(displacement.x, 0.0f, displacement.z).Normalized();
        
        PhysicsVector3D highAngleVelocity = horizontalDir * (muzzleVelocity * cos(highAngle));
        highAngleVelocity.y = muzzleVelocity * sin(highAngle);
        
        PhysicsVector3D lowAngleVelocity = horizontalDir * (muzzleVelocity * cos(lowAngle));
        lowAngleVelocity.y = muzzleVelocity * sin(lowAngle);
        
        // Test both trajectories
        std::vector<PhysicsVector3D> highTrajectory = physics.CalculateProjectileMotion(
            origin, highAngleVelocity, gravity, 0.0f, 0.01f, 15.0f
        );
        
        std::vector<PhysicsVector3D> lowTrajectory = physics.CalculateProjectileMotion(
            origin, lowAngleVelocity, gravity, 0.0f, 0.01f, 15.0f
        );
        
        // Both trajectories should hit near the target
    }
    
    physics.Cleanup();
}
```

---

### 10.4 Weapon Systems and Ballistics

#### Artillery System
```cpp
class ArtillerySystem
{
private:
    Physics* m_physics;
    PhysicsVector3D m_position;
    float m_muzzleVelocity;
    float m_elevation;
    float m_azimuth;
    
public:
    ArtillerySystem(Physics* physics) : m_physics(physics), m_muzzleVelocity(100.0f), 
                                       m_elevation(45.0f), m_azimuth(0.0f) {}
    
    void SetPosition(const PhysicsVector3D& position) { m_position = position; }
    void SetMuzzleVelocity(float velocity) { m_muzzleVelocity = velocity; }
    void SetElevation(float degrees) { m_elevation = degrees; }
    void SetAzimuth(float degrees) { m_azimuth = degrees; }
    
    std::vector<PhysicsVector3D> FireAt(const PhysicsVector3D& target)
    {
        // Calculate required launch velocity
        PhysicsVector3D launchVelocity = m_physics->CalculateTrajectoryToTarget(
            m_position, target, 9.81f, m_muzzleVelocity
        );
        
        // Apply weapon inaccuracy
        float accuracyFactor = 0.05f; // 5% spread
        launchVelocity.x += (static_cast<float>(rand()) / RAND_MAX - 0.5f) * m_muzzleVelocity * accuracyFactor;
        launchVelocity.y += (static_cast<float>(rand()) / RAND_MAX - 0.5f) * m_muzzleVelocity * accuracyFactor;
        launchVelocity.z += (static_cast<float>(rand()) / RAND_MAX - 0.5f) * m_muzzleVelocity * accuracyFactor;
        
        // Calculate trajectory with air resistance
        return m_physics->CalculateProjectileMotion(
            m_position, launchVelocity, 9.81f, 0.05f, 0.02f, 30.0f
        );
    }
    
    std::vector<PhysicsVector3D> GetTrajectoryPreview(const PhysicsVector3D& target)
    {
        // Preview trajectory without firing
        PhysicsVector3D launchVelocity = m_physics->CalculateTrajectoryToTarget(
            m_position, target, 9.81f, m_muzzleVelocity
        );
        
        return m_physics->CalculateProjectileMotion(
            m_position, launchVelocity, 9.81f, 0.05f, 0.02f, 30.0f
        );
    }
    
    float GetTimeToTarget(const PhysicsVector3D& target)
    {
        std::vector<PhysicsVector3D> trajectory = GetTrajectoryPreview(target);
        
        // Find impact time
        for (size_t i = 0; i < trajectory.size(); ++i) {
            if (trajectory[i].y <= target.y) {
                return i * 0.02f; // timeStep from CalculateProjectileMotion
            }
        }
        
        return -1.0f; // Target unreachable
    }
};
```

#### Grenade Physics
```cpp
void GrenadePhysics()
{
    Physics physics;
    physics.Initialize();
    
    // Grenade throw parameters
    PhysicsVector3D throwPosition(0.0f, 1.8f, 0.0f); // Player hand height
    PhysicsVector3D throwVelocity(15.0f, 8.0f, 5.0f); // Moderate throw
    float grenadeMass = 0.4f; // 400g grenade
    float fuseTime = 3.0f;    // 3-second fuse
    
    // Create grenade body
    PhysicsBody grenade;
    grenade.position = throwPosition;
    grenade.velocity = throwVelocity;
    grenade.SetMass(grenadeMass);
    grenade.drag = 0.08f;     // Air resistance
    grenade.restitution = 0.6f; // Bouncy
    grenade.friction = 0.7f;   // Surface friction
    
    std::vector<PhysicsVector3D> trajectory;
    float deltaTime = 0.016f;
    float elapsedTime = 0.0f;
    bool hasExploded = false;
    
    while (elapsedTime < 10.0f && !hasExploded) {
        // Record position
        trajectory.push_back(grenade.position);
        
        // Apply gravity and drag
        PhysicsVector3D gravity(0.0f, -9.81f * grenade.mass, 0.0f);
        physics.ApplyNewtonianMotion(grenade, gravity, deltaTime);
        
        // Check for ground bounce
        if (grenade.position.y <= 0.0f && grenade.velocity.y < 0.0f) {
            // Bounce off ground
            grenade.position.y = 0.0f;
            grenade.velocity.y *= -grenade.restitution;
            grenade.velocity.x *= (1.0f - grenade.friction);
            grenade.velocity.z *= (1.0f - grenade.friction);
        }
        
        // Check fuse timer
        if (elapsedTime >= fuseTime) {
            hasExploded = true;
            PhysicsVector3D explosionPosition = grenade.position;
            // Handle explosion at explosionPosition
        }
        
        elapsedTime += deltaTime;
    }
    
    physics.Cleanup();
}
```

---

### 10.5 Advanced Motion Dynamics

#### Orbital Mechanics
```cpp
void OrbitalMechanics()
{
    Physics physics;
    physics.Initialize();
    
    // Create central body (planet)
    GravityField planet;
    planet.center = PhysicsVector3D(0.0f, 0.0f, 0.0f);
    planet.mass = 1000000.0f;  // Massive central body
    planet.radius = 100.0f;
    planet.intensity = 1.0f;
    planet.isBlackHole = false;
    
    physics.AddGravityField(planet);
    
    // Create orbiting body
    PhysicsBody satellite;
    satellite.position = PhysicsVector3D(200.0f, 0.0f, 0.0f); // 200 units from center
    satellite.SetMass(1.0f);
    
    // Calculate orbital velocity
    float orbitalVelocity = physics.CalculateOrbitalVelocity(satellite.position, planet);
    satellite.velocity = PhysicsVector3D(0.0f, 0.0f, orbitalVelocity);
    
    // Simulate orbit
    std::vector<PhysicsVector3D> orbitPath;
    orbitPath.reserve(1000);
    
    float deltaTime = 0.1f; // Larger time step for orbital simulation
    
    for (int i = 0; i < 1000; ++i) {
        orbitPath.push_back(satellite.position);
        
        // Apply gravitational force
        PhysicsVector3D gravityForce = physics.CalculateGravityAtPosition(satellite.position);
        physics.ApplyNewtonianMotion(satellite, gravityForce, deltaTime);
    }
    
    physics.Cleanup();
}
```

#### Rocket Propulsion
```cpp
void RocketPropulsion()
{
    Physics physics;
    physics.Initialize();
    
    // Create rocket
    PhysicsBody rocket;
    rocket.position = PhysicsVector3D(0.0f, 0.0f, 0.0f);
    rocket.velocity = PhysicsVector3D(0.0f, 0.0f, 0.0f);
    rocket.SetMass(1000.0f); // Initial mass with fuel
    rocket.drag = 0.02f;
    
    // Rocket parameters
    float thrustForce = 15000.0f;  // 15kN thrust
    float fuelConsumption = 10.0f; // kg/s
    float burnTime = 60.0f;        // 60 seconds burn
    PhysicsVector3D thrustDirection(0.0f, 1.0f, 0.0f); // Upward thrust
    
    std::vector<PhysicsVector3D> flightPath;
    float deltaTime = 0.1f;
    float elapsedTime = 0.0f;
    
    while (elapsedTime < 300.0f) { // 5 minute simulation
        flightPath.push_back(rocket.position);
        
        // Calculate forces
        PhysicsVector3D gravity(0.0f, -9.81f * rocket.mass, 0.0f);
        PhysicsVector3D thrust(0.0f, 0.0f, 0.0f);
        
        // Apply thrust during burn time
        if (elapsedTime < burnTime) {
            thrust = thrustDirection * thrustForce;
            
            // Reduce mass due to fuel consumption
            rocket.SetMass(rocket.mass - fuelConsumption * deltaTime);
            
            // Rocket equation: thrust direction can change
            if (elapsedTime > 30.0f) {
                // Gravity turn - gradually tip rocket
                float tiltAngle = (elapsedTime - 30.0f) / 30.0f * 0.5f; // 0.5 radians over 30 seconds
                thrustDirection.x = sin(tiltAngle);
                thrustDirection.y = cos(tiltAngle);
                thrustDirection = thrustDirection.Normalized();
            }
        }
        
        // Apply total force
        PhysicsVector3D totalForce = gravity + thrust;
        physics.ApplyNewtonianMotion(rocket, totalForce, deltaTime);
        
        elapsedTime += deltaTime;
    }
    
    physics.Cleanup();
}
```

---

### 10.6 Collision and Impact Physics

#### High-Velocity Impact Simulation
```cpp
void HighVelocityImpact()
{
    Physics physics;
    physics.Initialize();
    
    // High-speed projectile
    PhysicsBody bullet;
    bullet.position = PhysicsVector3D(-50.0f, 1.0f, 0.0f);
    bullet.velocity = PhysicsVector3D(800.0f, 0.0f, 0.0f); // 800 m/s
    bullet.SetMass(0.01f); // 10g bullet
    bullet.drag = 0.001f;  // Minimal air resistance
    
    // Target object
    PhysicsBody target;
    target.position = PhysicsVector3D(0.0f, 1.0f, 0.0f);
    target.velocity = PhysicsVector3D(0.0f, 0.0f, 0.0f);
    target.SetMass(10.0f); // 10kg target
    target.isStatic = false;
    
    std::vector<PhysicsVector3D> bulletPath;
    bool impactOccurred = false;
    float deltaTime = 0.0001f; // Very small time step for high velocity
    
    for (int i = 0; i < 1000 && !impactOccurred; ++i) {
        bulletPath.push_back(bullet.position);
        
        // Check for collision
        float distance = (bullet.position - target.position).Magnitude();
        if (distance < 0.5f) { // Impact detected
            impactOccurred = true;
            
            // Calculate impact energy
            float bulletEnergy = 0.5f * bullet.mass * bullet.velocity.MagnitudeSquared();
            
            // Apply impulse to target
            PhysicsVector3D impactDirection = bullet.velocity.Normalized();
            PhysicsVector3D transferredMomentum = bullet.velocity * bullet.mass * 0.8f; // 80% transfer
            
            target.ApplyImpulse(transferredMomentum * (1.0f / target.mass));
            
            // Bullet loses most velocity
            bullet.velocity *= 0.1f;
            
            break;
        }
        
        // Update bullet position
        PhysicsVector3D gravity(0.0f, -9.81f * bullet.mass, 0.0f);
        physics.ApplyNewtonianMotion(bullet, gravity, deltaTime);
    }
    
    physics.Cleanup();
}
```

---

### Best Practices for Newtonian Motion

1. **Time Step Selection**: Use appropriate time steps for different velocity ranges
2. **Force Accumulation**: Combine multiple forces before applying to bodies
3. **Mass Considerations**: Set realistic mass values for proper force responses
4. **Drag Implementation**: Apply air resistance for realistic motion
5. **Energy Conservation**: Monitor energy in closed systems for validation

### Common Issues and Solutions

- **Numerical Instability**: Large time steps with high velocities → Reduce time step or use adaptive stepping
- **Unrealistic Trajectories**: Missing drag forces → Apply appropriate air resistance
- **Poor Targeting**: Inaccurate ballistic calculations → Verify trajectory solver implementation
- **Energy Gain**: Forces applied incorrectly → Check force application order and magnitude

---

**Chapter 10 Complete**

## Chapter 11: Audio Physics for 3D Sound

### Overview
The Physics system provides comprehensive audio physics calculations for realistic 3D sound simulation including Doppler effects, sound occlusion, reverb calculation, and distance-based audio properties. This chapter covers spatial audio, environmental acoustics, and performance optimization.

### Key Features
- 3D positional audio calculations
- Doppler effect simulation for moving sound sources
- Sound occlusion and obstruction calculations
- Environmental reverb and acoustic modeling
- Distance-based volume falloff and filtering
- Optimized audio physics using MathPrecalculation

---

### 11.1 Basic 3D Audio Physics

#### Simple Positional Audio
```cpp
#include "Physics.h"

void Basic3DAudio()
{
    Physics physics;
    if (!physics.Initialize()) {
        return; // Handle initialization failure
    }
    
    // Define listener and sound source positions
    PhysicsVector3D listenerPosition(0.0f, 1.8f, 0.0f);  // Player head height
    PhysicsVector3D sourcePosition(10.0f, 1.5f, 5.0f);   // Sound source
    PhysicsVector3D sourceVelocity(0.0f, 0.0f, 0.0f);    // Stationary source
    
    // Calculate audio physics
    AudioPhysicsData audioData = physics.CalculateAudioPhysics(
        listenerPosition, sourcePosition, sourceVelocity
    );
    
    // Access calculated properties
    float distance = audioData.distance;           // Distance between listener and source
    float volumeFalloff = audioData.volumeFalloff; // Volume attenuation (0.0 to 1.0)
    float dopplerShift = audioData.dopplerShift;   // Frequency multiplier
    float occlusion = audioData.occlusion;         // Occlusion factor (0.0 to 1.0)
    float reverb = audioData.reverb;               // Reverb intensity
    
    // Apply to audio system
    // SetAudioVolume(baseVolume * volumeFalloff);
    // SetAudioPitch(basePitch * dopplerShift);
    // SetAudioReverb(reverb);
    
    physics.Cleanup();
}
```

#### Moving Sound Source
```cpp
void MovingSoundSource()
{
    Physics physics;
    physics.Initialize();
    
    // Listener (stationary)
    PhysicsVector3D listenerPosition(0.0f, 1.8f, 0.0f);
    PhysicsVector3D listenerVelocity(0.0f, 0.0f, 0.0f);
    
    // Moving sound source (car engine)
    PhysicsVector3D sourcePosition(-50.0f, 1.0f, 0.0f);
    PhysicsVector3D sourceVelocity(25.0f, 0.0f, 0.0f); // 25 m/s (90 km/h)
    
    // Simulate sound over time
    float deltaTime = 0.1f; // 10 Hz update rate for audio
    
    for (int frame = 0; frame < 100; ++frame) {
        // Update source position
        sourcePosition += sourceVelocity * deltaTime;
        
        // Calculate audio properties
        AudioPhysicsData audioData = physics.CalculateAudioPhysics(
            listenerPosition, sourcePosition, sourceVelocity
        );
        
        // Apply Doppler effect
        float basePitch = 440.0f; // A4 note
        float dopplerPitch = basePitch * audioData.dopplerShift;
        
        // Apply distance attenuation
        float baseVolume = 1.0f;
        float attenuatedVolume = baseVolume * audioData.volumeFalloff;
        
        // Log audio changes every 10 frames
        if (frame % 10 == 0) {
            float time = frame * deltaTime;
            // Audio properties available for processing
        }
    }
    
    physics.Cleanup();
}
```

---

### 11.2 Doppler Effect Calculation

#### Advanced Doppler Implementation
```cpp
void AdvancedDopplerEffect()
{
    Physics physics;
    physics.Initialize();
    
    // Racing car scenario
    PhysicsVector3D listenerPos(0.0f, 1.8f, 10.0f);    // Spectator position
    PhysicsVector3D listenerVel(0.0f, 0.0f, 0.0f);     // Stationary spectator
    
    PhysicsVector3D carPosition(-100.0f, 0.5f, 0.0f);  // Car starting position
    PhysicsVector3D carVelocity(60.0f, 0.0f, 0.0f);    // 60 m/s (216 km/h)
    
    float speedOfSound = 343.0f; // m/s at 20°C
    float deltaTime = 0.05f;     // 20 Hz audio update
    
    std::vector<float> dopplerHistory;
    dopplerHistory.reserve(200);
    
    for (int frame = 0; frame < 200; ++frame) {
        // Update car position
        carPosition += carVelocity * deltaTime;
        
        // Calculate direction from source to listener
        PhysicsVector3D direction = listenerPos - carPosition;
        float distance = direction.Magnitude();
        
        if (distance > 0.1f) {
            direction = direction.Normalized();
            
            // Calculate Doppler shift
            float dopplerShift = physics.CalculateDopplerShift(
                carVelocity, listenerVel, direction, speedOfSound
            );
            
            dopplerHistory.push_back(dopplerShift);
            
            // Apply to audio engine
            float engineFreq = 200.0f; // Base engine frequency
            float dopplerFreq = engineFreq * dopplerShift;
            
            // Calculate volume based on distance
            float volume = 1.0f / (1.0f + distance * distance * 0.001f);
            
            // Audio system calls would go here
            // PlayEngineSound(dopplerFreq, volume);
        }
    }
    
    physics.Cleanup();
}
```

#### Multi-Source Doppler
```cpp
void MultiSourceDoppler()
{
    Physics physics;
    physics.Initialize();
    
    struct MovingSource {
        PhysicsVector3D position;
        PhysicsVector3D velocity;
        float baseFrequency;
        float volume;
        int sourceId;
    };
    
    // Create multiple moving sources
    std::vector<MovingSource> sources = {
        { PhysicsVector3D(-20.0f, 2.0f, 0.0f), PhysicsVector3D(15.0f, 0.0f, 0.0f), 800.0f, 1.0f, 0 }, // Car 1
        { PhysicsVector3D(30.0f, 2.0f, 10.0f), PhysicsVector3D(-20.0f, 0.0f, -5.0f), 600.0f, 0.8f, 1 }, // Car 2
        { PhysicsVector3D(0.0f, 50.0f, 0.0f), PhysicsVector3D(0.0f, -10.0f, 0.0f), 400.0f, 1.2f, 2 }  // Helicopter
    };
    
    PhysicsVector3D listenerPos(0.0f, 1.8f, 0.0f);
    PhysicsVector3D listenerVel(0.0f, 0.0f, 0.0f);
    
    float deltaTime = 0.1f;
    
    for (int frame = 0; frame < 100; ++frame) {
        for (auto& source : sources) {
            // Update source position
            source.position += source.velocity * deltaTime;
            
            // Calculate direction and distance
            PhysicsVector3D direction = listenerPos - source.position;
            float distance = direction.Magnitude();
            
            if (distance > 0.1f) {
                direction = direction.Normalized();
                
                // Calculate Doppler effect
                float dopplerShift = physics.CalculateDopplerShift(
                    source.velocity, listenerVel, direction, 343.0f
                );
                
                // Calculate distance attenuation
                float attenuation = 1.0f / (1.0f + distance * distance * 0.01f);
                
                // Final audio properties
                float finalFreq = source.baseFrequency * dopplerShift;
                float finalVolume = source.volume * attenuation;
                
                // Apply to audio system per source
                // UpdateAudioSource(source.sourceId, finalFreq, finalVolume);
            }
        }
    }
    
    physics.Cleanup();
}
```

---

### 11.3 Sound Occlusion and Obstruction

#### Basic Occlusion Calculation
```cpp
void SoundOcclusion()
{
    Physics physics;
    physics.Initialize();
    
    // Audio source and listener
    PhysicsVector3D sourcePos(0.0f, 1.5f, 0.0f);
    PhysicsVector3D listenerPos(10.0f, 1.8f, 0.0f);
    
    // Define obstacles in the environment
    std::vector<PhysicsVector3D> obstacles = {
        PhysicsVector3D(3.0f, 1.0f, 0.0f),  // Wall 1
        PhysicsVector3D(5.0f, 2.0f, 0.0f),  // Wall 2
        PhysicsVector3D(7.0f, 1.5f, 0.0f)   // Wall 3
    };
    
    // Calculate occlusion
    float occlusion = physics.CalculateSoundOcclusion(sourcePos, listenerPos, obstacles);
    
    // Apply occlusion to audio
    float baseVolume = 1.0f;
    float occludedVolume = baseVolume * (1.0f - occlusion);
    
    // Apply low-pass filtering based on occlusion
    float cutoffFrequency = 20000.0f * (1.0f - occlusion * 0.8f); // Reduce high frequencies
    
    // Audio processing
    // SetAudioVolume(occludedVolume);
    // SetLowPassFilter(cutoffFrequency);
    
    physics.Cleanup();
}
```

#### Dynamic Occlusion System
```cpp
class AudioOcclusionSystem
{
private:
    Physics* m_physics;
    std::vector<PhysicsVector3D> m_staticObstacles;
    std::vector<PhysicsBody*> m_dynamicObstacles;
    
public:
    AudioOcclusionSystem(Physics* physics) : m_physics(physics) {}
    
    void AddStaticObstacle(const PhysicsVector3D& position)
    {
        m_staticObstacles.push_back(position);
    }
    
    void AddDynamicObstacle(PhysicsBody* body)
    {
        m_dynamicObstacles.push_back(body);
    }
    
    float CalculateOcclusion(const PhysicsVector3D& sourcePos, const PhysicsVector3D& listenerPos)
    {
        std::vector<PhysicsVector3D> allObstacles = m_staticObstacles;
        
        // Add dynamic obstacle positions
        for (const auto& body : m_dynamicObstacles) {
            if (body && body->isActive) {
                allObstacles.push_back(body->position);
            }
        }
        
        return m_physics->CalculateSoundOcclusion(sourcePos, listenerPos, allObstacles);
    }
    
    struct OcclusionResult {
        float directOcclusion;
        float indirectOcclusion;
        float totalOcclusion;
        std::vector<PhysicsVector3D> reflectionPoints;
    };
    
    OcclusionResult CalculateAdvancedOcclusion(const PhysicsVector3D& sourcePos, 
                                               const PhysicsVector3D& listenerPos)
    {
        OcclusionResult result;
        
        // Calculate direct path occlusion
        result.directOcclusion = CalculateOcclusion(sourcePos, listenerPos);
        
        // Calculate indirect paths (simplified reflection model)
        result.indirectOcclusion = 0.0f;
        
        // Find potential reflection points
        for (const auto& obstacle : m_staticObstacles) {
            // Simplified reflection calculation
            PhysicsVector3D toObstacle = obstacle - sourcePos;
            PhysicsVector3D fromObstacle = listenerPos - obstacle;
            
            float reflectionDistance = toObstacle.Magnitude() + fromObstacle.Magnitude();
            float directDistance = (listenerPos - sourcePos).Magnitude();
            
            // Check if reflection path is reasonable
            if (reflectionDistance < directDistance * 2.0f) {
                result.reflectionPoints.push_back(obstacle);
                
                // Calculate occlusion for this reflection path
                float pathOcclusion = CalculateOcclusion(sourcePos, obstacle) * 0.5f +
                                     CalculateOcclusion(obstacle, listenerPos) * 0.5f;
                result.indirectOcclusion += pathOcclusion * 0.3f; // Weighted contribution
            }
        }
        
        // Combine direct and indirect occlusion
        result.totalOcclusion = std::min(1.0f, result.directOcclusion + result.indirectOcclusion * 0.5f);
        
        return result;
    }
};
```

---

### 11.4 Environmental Reverb

#### Room Acoustics Simulation
```cpp
void RoomAcoustics()
{
    Physics physics;
    physics.Initialize();
    
    // Define different room types
    struct RoomProperties {
        float size;              // Room volume factor
        float absorption;        // Surface absorption coefficient
        float reverbTime;        // RT60 reverb time
        std::string name;
    };
    
    std::vector<RoomProperties> rooms = {
        { 10.0f, 0.8f, 0.5f, "Small Bedroom" },
        { 50.0f, 0.6f, 1.2f, "Living Room" },
        { 200.0f, 0.3f, 2.5f, "Large Hall" },
        { 1000.0f, 0.1f, 4.0f, "Cathedral" },
        { 5.0f, 0.9f, 0.2f, "Bathroom" }
    };
    
    PhysicsVector3D listenerPos(0.0f, 1.8f, 0.0f);
    PhysicsVector3D sourcePos(5.0f, 1.5f, 0.0f);
    
    for (const auto& room : rooms) {
        // Calculate reverb for this room
        float reverb = physics.CalculateReverb(listenerPos, room.size, room.absorption);
        
        // Apply room-specific processing
        float reverbMix = reverb * 0.3f;     // 30% max reverb mix
        float highFreqDamping = room.absorption;
        float earlyReflections = (1.0f - room.absorption) * 0.2f;
        
        // Audio system configuration for this room
        // SetReverbMix(reverbMix);
        // SetReverbTime(room.reverbTime);
        // SetHighFrequencyDamping(highFreqDamping);
        // SetEarlyReflections(earlyReflections);
    }
    
    physics.Cleanup();
}
```

#### Dynamic Environmental Audio
```cpp
void DynamicEnvironmentalAudio()
{
    Physics physics;
    physics.Initialize();
    
    // Player movement through different environments
    PhysicsVector3D playerPos(0.0f, 1.8f, 0.0f);
    PhysicsVector3D playerVel(2.0f, 0.0f, 0.0f); // Walking speed
    
    // Define environment zones
    struct EnvironmentZone {
        PhysicsVector3D center;
        float radius;
        float roomSize;
        float absorption;
        float dampingFactor;
        std::string type;
    };
    
    std::vector<EnvironmentZone> zones = {
        { PhysicsVector3D(0.0f, 0.0f, 0.0f), 10.0f, 20.0f, 0.7f, 0.8f, "Indoor" },
        { PhysicsVector3D(30.0f, 0.0f, 0.0f), 20.0f, 1000.0f, 0.1f, 0.2f, "Outdoor" },
        { PhysicsVector3D(70.0f, 0.0f, 0.0f), 8.0f, 15.0f, 0.9f, 0.9f, "Cave" },
        { PhysicsVector3D(100.0f, 0.0f, 0.0f), 15.0f, 500.0f, 0.2f, 0.3f, "Large Hall" }
    };
    
    float deltaTime = 0.1f;
    std::string currentEnvironment = "None";
    float environmentBlend = 0.0f;
    
    for (int frame = 0; frame < 200; ++frame) {
        // Update player position
        playerPos += playerVel * deltaTime;
        
        // Find current environment
        std::string newEnvironment = "None";
        float newRoomSize = 50.0f;
        float newAbsorption = 0.5f;
        
        for (const auto& zone : zones) {
            float distance = (playerPos - zone.center).Magnitude();
            if (distance <= zone.radius) {
                newEnvironment = zone.type;
                newRoomSize = zone.roomSize;
                newAbsorption = zone.absorption;
                break;
            }
        }
        
        // Smooth environment transitions
        if (newEnvironment != currentEnvironment) {
            currentEnvironment = newEnvironment;
            environmentBlend = 0.0f; // Start transition
        }
        
        environmentBlend = std::min(1.0f, environmentBlend + deltaTime * 2.0f); // 0.5 second transition
        
        // Calculate current reverb
        float reverb = physics.CalculateReverb(playerPos, newRoomSize, newAbsorption);
        float blendedReverb = reverb * environmentBlend;
        
        // Apply environmental audio effects
        // SetEnvironmentalReverb(blendedReverb);
        // SetAmbientSound(currentEnvironment);
    }
    
    physics.Cleanup();
}
```

---

### 11.5 Performance Optimization

#### Optimized Audio Physics Update
```cpp
class OptimizedAudioSystem
{
private:
    Physics* m_physics;
    struct AudioSource {
        PhysicsVector3D position;
        PhysicsVector3D velocity;
        float importance;    // Priority for LOD
        float lastUpdate;    // Time since last full update
        float cachedVolume;  // Cached volume calculation
        float cachedDoppler; // Cached Doppler shift
        bool isActive;
    };
    
    std::vector<AudioSource> m_audioSources;
    PhysicsVector3D m_listenerPos;
    PhysicsVector3D m_listenerVel;
    float m_audioUpdateRate;
    
public:
    OptimizedAudioSystem(Physics* physics) : m_physics(physics), m_audioUpdateRate(0.1f) {}
    
    void AddAudioSource(const PhysicsVector3D& pos, const PhysicsVector3D& vel, float importance)
    {
        AudioSource source;
        source.position = pos;
        source.velocity = vel;
        source.importance = importance;
        source.lastUpdate = 0.0f;
        source.cachedVolume = 1.0f;
        source.cachedDoppler = 1.0f;
        source.isActive = true;
        
        m_audioSources.push_back(source);
    }
    
    void UpdateListener(const PhysicsVector3D& pos, const PhysicsVector3D& vel)
    {
        m_listenerPos = pos;
        m_listenerVel = vel;
    }
    
    void Update(float deltaTime)
    {
        // Sort sources by importance and distance
        std::sort(m_audioSources.begin(), m_audioSources.end(),
            [this](const AudioSource& a, const AudioSource& b) {
                float distA = (a.position - m_listenerPos).Magnitude();
                float distB = (b.position - m_listenerPos).Magnitude();
                float scoreA = a.importance / (1.0f + distA);
                float scoreB = b.importance / (1.0f + distB);
                return scoreA > scoreB;
            });
        
        // Update high-priority sources every frame, others less frequently
        for (size_t i = 0; i < m_audioSources.size(); ++i) {
            AudioSource& source = m_audioSources[i];
            
            if (!source.isActive) continue;
            
            source.lastUpdate += deltaTime;
            
            // Determine update frequency based on priority
            float updateInterval = (i < 5) ? 0.05f :   // Top 5: 20 Hz
                                  (i < 15) ? 0.1f :    // Next 10: 10 Hz
                                  0.2f;                // Others: 5 Hz
            
            if (source.lastUpdate >= updateInterval) {
                // Calculate full audio physics
                AudioPhysicsData audioData = m_physics->CalculateAudioPhysics(
                    m_listenerPos, source.position, source.velocity
                );
                
                source.cachedVolume = audioData.volumeFalloff;
                source.cachedDoppler = audioData.dopplerShift;
                source.lastUpdate = 0.0f;
                
                // Apply to audio system
                // UpdateAudioSource(i, source.cachedVolume, source.cachedDoppler);
            }
        }
    }
    
    void SetAudioUpdateRate(float rate) { m_audioUpdateRate = rate; }
    
    size_t GetActiveSourceCount() const
    {
        return std::count_if(m_audioSources.begin(), m_audioSources.end(),
            [](const AudioSource& source) { return source.isActive; });
    }
};
```

#### Audio Culling System
```cpp
void AudioCullingSystem()
{
    Physics physics;
    physics.Initialize();
    
    OptimizedAudioSystem audioSystem(&physics);
    
    // Add multiple audio sources
    for (int i = 0; i < 50; ++i) {
        PhysicsVector3D pos(
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 200.0f,
            1.0f,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 200.0f
        );
        PhysicsVector3D vel(
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 10.0f,
            0.0f,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 10.0f
        );
        float importance = static_cast<float>(rand()) / RAND_MAX;
        
        audioSystem.AddAudioSource(pos, vel, importance);
    }
    
    // Simulate player movement
    PhysicsVector3D playerPos(0.0f, 1.8f, 0.0f);
    PhysicsVector3D playerVel(5.0f, 0.0f, 0.0f);
    
    float deltaTime = 0.016f; // 60 FPS
    
    for (int frame = 0; frame < 600; ++frame) { // 10 seconds
        // Update player position
        playerPos += playerVel * deltaTime;
        audioSystem.UpdateListener(playerPos, playerVel);
        
        // Update audio system with LOD
        audioSystem.Update(deltaTime);
        
        // Log performance every 60 frames
        if (frame % 60 == 0) {
            size_t activeCount = audioSystem.GetActiveSourceCount();
            // Performance metrics available
        }
    }
    
    physics.Cleanup();
}
```

---

### 11.6 Advanced Audio Features

#### Binaural Audio Simulation
```cpp
void BinauralAudioSimulation()
{
    Physics physics;
    physics.Initialize();
    
    // Head-related transfer function simulation (simplified)
    struct BinauralData {
        float leftEarDelay;
        float rightEarDelay;
        float leftEarGain;
        float rightEarGain;
        float interauralTimeDifference;
        float interauralLevelDifference;
    };
    
    PhysicsVector3D headPos(0.0f, 1.8f, 0.0f);
    PhysicsVector3D headForward(0.0f, 0.0f, 1.0f);
    PhysicsVector3D headRight(1.0f, 0.0f, 0.0f);
    float headRadius = 0.085f; // Average head radius
    
    // Sound source positions
    std::vector<PhysicsVector3D> sources = {
        PhysicsVector3D(5.0f, 1.8f, 0.0f),   // Right side
        PhysicsVector3D(-5.0f, 1.8f, 0.0f),  // Left side
        PhysicsVector3D(0.0f, 1.8f, 5.0f),   // Front
        PhysicsVector3D(0.0f, 1.8f, -5.0f),  // Behind
        PhysicsVector3D(0.0f, 3.0f, 0.0f)    // Above
    };
    
    for (const auto& sourcePos : sources) {
        // Calculate direction from head center to source
        PhysicsVector3D toSource = sourcePos - headPos;
        float distance = toSource.Magnitude();
        toSource = toSource.Normalized();
        
        // Calculate azimuth and elevation
        float azimuth = atan2(toSource.Dot(headRight), toSource.Dot(headForward));
        float elevation = asin(toSource.y);
        
        // Calculate ear positions
        PhysicsVector3D leftEarPos = headPos + headRight * (-headRadius);
        PhysicsVector3D rightEarPos = headPos + headRight * headRadius;
        
        // Calculate distances to each ear
        float leftEarDistance = (sourcePos - leftEarPos).Magnitude();
        float rightEarDistance = (sourcePos - rightEarPos).Magnitude();
        
        // Calculate binaural cues
        BinauralData binaural;
        binaural.interauralTimeDifference = (rightEarDistance - leftEarDistance) / 343.0f; // Speed of sound
        binaural.interauralLevelDifference = 20.0f * log10(leftEarDistance / rightEarDistance);
        
        // Apply head shadow effect (simplified)
        float shadowFactor = 1.0f;
        if (abs(azimuth) > XM_PI * 0.25f) { // Source to the side
            shadowFactor = 0.7f + 0.3f * cos(azimuth * 2.0f);
        }
        
        binaural.leftEarGain = (azimuth < 0) ? 1.0f : shadowFactor;
        binaural.rightEarGain = (azimuth > 0) ? 1.0f : shadowFactor;
        
        // Apply to audio processing
        // SetBinauralDelay(binaural.interauralTimeDifference);
        // SetBinauralGains(binaural.leftEarGain, binaural.rightEarGain);
    }
    
    physics.Cleanup();
}
```

---

### Best Practices for Audio Physics

1. **Update Frequency**: Use appropriate update rates for different audio priorities
2. **Distance Culling**: Disable audio processing for very distant sources
3. **LOD Systems**: Implement level-of-detail for audio complexity
4. **Caching**: Cache expensive calculations like reverb and occlusion
5. **Spatial Partitioning**: Use spatial data structures for occlusion queries

### Common Issues and Solutions

- **Audio Pops**: Sudden volume changes → Use smooth interpolation for all audio parameters
- **Performance Issues**: Too many audio sources → Implement priority-based culling
- **Unrealistic Doppler**: Incorrect velocity calculations → Verify velocity vectors and directions
- **Poor Occlusion**: Inaccurate obstacle detection → Improve obstacle representation and ray casting

---

**Chapter 11 Complete**


## Chapter 12: Particle System Physics

### Overview
The Physics system provides comprehensive particle physics simulation for visual effects including explosions, fire, smoke, water, and environmental particles. This chapter covers particle creation, simulation, environmental forces, and performance optimization techniques.

### Key Features
- Large-scale particle simulation (up to 10,000 particles)
- Explosion and debris systems
- Environmental force application (wind, gravity, magnetic fields)
- Particle collision and interaction
- Optimized update loops with spatial partitioning
- Integration with MathPrecalculation for performance

---

### 12.1 Basic Particle Creation

#### Simple Explosion Effect
```cpp
#include "Physics.h"

void CreateBasicExplosion()
{
    Physics physics;
    if (!physics.Initialize()) {
        return; // Handle initialization failure
    }
    
    // Explosion parameters
    PhysicsVector3D explosionCenter(0.0f, 2.0f, 0.0f);
    int particleCount = 100;
    float explosionForce = 20.0f;
    float particleLifetime = 3.0f;
    
    // Create explosion particles
    std::vector<PhysicsParticle> particles = physics.CreateExplosion(
        explosionCenter, particleCount, explosionForce, particleLifetime
    );
    
    // Simulate particle system
    float deltaTime = 0.016f; // 60 FPS
    
    for (int frame = 0; frame < 180; ++frame) { // 3 seconds
        // Update all particles
        physics.UpdateParticleSystem(particles, deltaTime);
        
        // Count active particles
        int activeCount = 0;
        for (const auto& particle : particles) {
            if (particle.isActive) {
                activeCount++;
                // Particle position available in particle.position
                // Use for rendering or other effects
            }
        }
        
        // Stop simulation when all particles are dead
        if (activeCount == 0) {
            break;
        }
    }
    
    physics.Cleanup();
}
```

#### Custom Particle Properties
```cpp
void CustomParticleSystem()
{
    Physics physics;
    physics.Initialize();
    
    // Create custom particle system
    std::vector<PhysicsParticle> particles;
    particles.reserve(500);
    
    PhysicsVector3D emitterPosition(0.0f, 5.0f, 0.0f);
    
    for (int i = 0; i < 500; ++i) {
        PhysicsParticle particle;
        
        // Random spawn position around emitter
        particle.position = emitterPosition + PhysicsVector3D(
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f,
            static_cast<float>(rand()) / RAND_MAX * 1.0f,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f
        );
        
        // Random velocity (fountain effect)
        particle.velocity = PhysicsVector3D(
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 8.0f,
            static_cast<float>(rand()) / RAND_MAX * 15.0f + 5.0f, // Upward bias
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 8.0f
        );
        
        // Particle properties
        particle.life = 2.0f + static_cast<float>(rand()) / RAND_MAX * 3.0f; // 2-5 seconds
        particle.mass = 0.05f + static_cast<float>(rand()) / RAND_MAX * 0.1f; // 0.05-0.15 kg
        particle.drag = 0.1f + static_cast<float>(rand()) / RAND_MAX * 0.2f;  // 0.1-0.3 drag
        particle.isActive = true;
        
        particles.push_back(particle);
    }
    
    // Simulation loop
    float deltaTime = 0.016f;
    
    for (int frame = 0; frame < 300; ++frame) { // 5 seconds
        // Apply gravity to all particles
        physics.ApplyGravityToParticles(particles, 9.81f);
        
        // Update particle physics
        physics.UpdateParticleSystem(particles, deltaTime);
    }
    
    physics.Cleanup();
}
```

---

### 12.2 Environmental Forces

#### Wind and Atmospheric Effects
```cpp
void WindEffectsSimulation()
{
    Physics physics;
    physics.Initialize();
    
    // Create particle system (smoke/dust)
    std::vector<PhysicsParticle> particles = physics.CreateExplosion(
        PhysicsVector3D(0.0f, 1.0f, 0.0f), 200, 5.0f, 8.0f
    );
    
    // Configure particles for wind effects
    for (auto& particle : particles) {
        particle.mass = 0.01f;  // Very light particles
        particle.drag = 0.3f;   // High drag for wind sensitivity
    }
    
    // Wind parameters
    PhysicsVector3D baseWind(3.0f, 0.5f, 1.0f);
    float windVariation = 2.0f;
    float deltaTime = 0.016f;
    
    for (int frame = 0; frame < 500; ++frame) { // 8+ seconds
        // Calculate dynamic wind with turbulence
        float time = frame * deltaTime;
        PhysicsVector3D currentWind = baseWind + PhysicsVector3D(
            sin(time * 0.5f) * windVariation,
            cos(time * 0.7f) * windVariation * 0.5f,
            sin(time * 0.3f) * windVariation
        );
        
        // Apply environmental forces
        physics.ApplyWindForce(particles, currentWind);
        physics.ApplyGravityToParticles(particles, 9.81f);
        
        // Update particles
        physics.UpdateParticleSystem(particles, deltaTime);
    }
    
    physics.Cleanup();
}
```

#### Magnetic Field Effects
```cpp
void MagneticFieldSimulation()
{
    Physics physics;
    physics.Initialize();
    
    // Create charged particles
    struct ChargedParticle {
        PhysicsParticle particle;
        float charge; // Electric charge
        float chargeToMassRatio;
    };
    
    std::vector<ChargedParticle> chargedParticles;
    chargedParticles.reserve(100);
    
    // Create particles with random charges
    for (int i = 0; i < 100; ++i) {
        ChargedParticle cp;
        cp.particle.position = PhysicsVector3D(
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 10.0f,
            static_cast<float>(rand()) / RAND_MAX * 2.0f,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 10.0f
        );
        cp.particle.velocity = PhysicsVector3D(
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 20.0f,
            static_cast<float>(rand()) / RAND_MAX * 10.0f,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 20.0f
        );
        cp.particle.mass = 0.1f;
        cp.particle.drag = 0.01f; // Low drag for electromagnetic effects
        cp.particle.life = 10.0f;
        cp.particle.isActive = true;
        
        // Random charge
        cp.charge = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f; // -1.0 to +1.0
        cp.chargeToMassRatio = cp.charge / cp.particle.mass;
        
        chargedParticles.push_back(cp);
    }
    
    // Magnetic field (pointing upward)
    PhysicsVector3D magneticField(0.0f, 5.0f, 0.0f);
    float deltaTime = 0.01f; // Higher precision for electromagnetic simulation
    
    for (int frame = 0; frame < 1000; ++frame) { // 10 seconds
        for (auto& cp : chargedParticles) {
            if (!cp.particle.isActive) continue;
            
            // Calculate Lorentz force: F = q(v × B)
            PhysicsVector3D velocity = cp.particle.velocity;
            PhysicsVector3D crossProduct = velocity.Cross(magneticField);
            PhysicsVector3D lorentzForce = crossProduct * cp.charge;
            
            // Apply force
            cp.particle.ApplyForce(lorentzForce);
            
            // Update particle
            cp.particle.Update(deltaTime);
        }
    }
    
    physics.Cleanup();
}
```

---

### 12.3 Particle Collision Systems

#### Particle-Environment Collision
```cpp
void ParticleEnvironmentCollision()
{
    Physics physics;
    physics.Initialize();
    
    // Create bouncing particles
    std::vector<PhysicsParticle> particles = physics.CreateExplosion(
        PhysicsVector3D(0.0f, 10.0f, 0.0f), 150, 15.0f, 5.0f
    );
    
    // Configure for bouncing
    for (auto& particle : particles) {
        particle.mass = 0.2f;
        particle.drag = 0.05f;
    }
    
    // Environment boundaries
    float groundLevel = 0.0f;
    float wallLeft = -20.0f;
    float wallRight = 20.0f;
    float wallFront = 20.0f;
    float wallBack = -20.0f;
    float ceiling = 15.0f;
    
    float restitution = 0.7f; // Bounce factor
    float friction = 0.8f;    // Surface friction
    float deltaTime = 0.016f;
    
    for (int frame = 0; frame < 600; ++frame) { // 10 seconds
        // Apply gravity
        physics.ApplyGravityToParticles(particles, 9.81f);
        
        // Update particles
        physics.UpdateParticleSystem(particles, deltaTime);
        
        // Check collisions with environment
        for (auto& particle : particles) {
            if (!particle.isActive) continue;
            
            // Ground collision
            if (particle.position.y <= groundLevel) {
                particle.position.y = groundLevel;
                if (particle.velocity.y < 0.0f) {
                    particle.velocity.y *= -restitution;
                    particle.velocity.x *= friction;
                    particle.velocity.z *= friction;
                }
            }
            
            // Wall collisions
            if (particle.position.x <= wallLeft) {
                particle.position.x = wallLeft;
                if (particle.velocity.x < 0.0f) {
                    particle.velocity.x *= -restitution;
                }
            }
            if (particle.position.x >= wallRight) {
                particle.position.x = wallRight;
                if (particle.velocity.x > 0.0f) {
                    particle.velocity.x *= -restitution;
                }
            }
            if (particle.position.z <= wallBack) {
                particle.position.z = wallBack;
                if (particle.velocity.z < 0.0f) {
                    particle.velocity.z *= -restitution;
                }
            }
            if (particle.position.z >= wallFront) {
                particle.position.z = wallFront;
                if (particle.velocity.z > 0.0f) {
                    particle.velocity.z *= -restitution;
                }
            }
            
            // Ceiling collision
            if (particle.position.y >= ceiling) {
                particle.position.y = ceiling;
                if (particle.velocity.y > 0.0f) {
                    particle.velocity.y *= -restitution;
                }
            }
        }
    }
    
    physics.Cleanup();
}
```

#### Particle-Particle Interaction
```cpp
void ParticleInteractionSystem()
{
    Physics physics;
    physics.Initialize();
    
    // Create two different particle types
    std::vector<PhysicsParticle> fireParticles = physics.CreateExplosion(
        PhysicsVector3D(-5.0f, 3.0f, 0.0f), 50, 8.0f, 4.0f
    );
    
    std::vector<PhysicsParticle> waterParticles = physics.CreateExplosion(
        PhysicsVector3D(5.0f, 3.0f, 0.0f), 50, 8.0f, 6.0f
    );
    
    // Configure particle properties
    for (auto& particle : fireParticles) {
        particle.mass = 0.05f;  // Light fire particles
        particle.drag = 0.15f;
    }
    
    for (auto& particle : waterParticles) {
        particle.mass = 0.15f;  // Heavier water particles
        particle.drag = 0.1f;
    }
    
    float deltaTime = 0.016f;
    float interactionRadius = 1.0f;
    
    for (int frame = 0; frame < 400; ++frame) { // ~6.5 seconds
        // Apply environmental forces
        physics.ApplyGravityToParticles(fireParticles, 9.81f);
        physics.ApplyGravityToParticles(waterParticles, 9.81f);
        
        // Apply upward force to fire particles (buoyancy)
        PhysicsVector3D buoyancy(0.0f, 2.0f, 0.0f);
        physics.ApplyWindForce(fireParticles, buoyancy);
        
        // Check fire-water interactions
        for (auto& fireParticle : fireParticles) {
            if (!fireParticle.isActive) continue;
            
            for (auto& waterParticle : waterParticles) {
                if (!waterParticle.isActive) continue;
                
                float distance = (fireParticle.position - waterParticle.position).Magnitude();
                
                if (distance < interactionRadius) {
                    // Water extinguishes fire
                    fireParticle.life *= 0.9f; // Reduce fire particle life
                    
                    // Create steam effect (modify water particle)
                    waterParticle.velocity.y += 3.0f; // Steam rises
                    waterParticle.mass *= 0.95f;      // Evaporation
                }
            }
        }
        
        // Update both particle systems
        physics.UpdateParticleSystem(fireParticles, deltaTime);
        physics.UpdateParticleSystem(waterParticles, deltaTime);
    }
    
    physics.Cleanup();
}
```

---

### 12.4 Advanced Particle Effects

#### Fireworks System
```cpp
class FireworksSystem
{
private:
    Physics* m_physics;
    
    struct Firework {
        PhysicsParticle shell;
        std::vector<PhysicsParticle> sparks;
        bool hasExploded;
        float fuseTime;
        PhysicsVector3D color; // RGB color for rendering
    };
    
    std::vector<Firework> m_fireworks;
    
public:
    FireworksSystem(Physics* physics) : m_physics(physics) {}
    
    void LaunchFirework(const PhysicsVector3D& launchPos, const PhysicsVector3D& targetPos)
    {
        Firework firework;
        
        // Calculate launch velocity to reach target
        PhysicsVector3D launchVel = m_physics->CalculateTrajectoryToTarget(
            launchPos, targetPos, 9.81f, 30.0f
        );
        
        // Setup shell
        firework.shell.position = launchPos;
        firework.shell.velocity = launchVel;
        firework.shell.SetMass(0.5f);
        firework.shell.drag = 0.02f;
        firework.shell.life = 10.0f;
        firework.shell.isActive = true;
        
        firework.hasExploded = false;
        firework.fuseTime = 3.0f; // 3 second fuse
        
        // Random color
        firework.color = PhysicsVector3D(
            static_cast<float>(rand()) / RAND_MAX,
            static_cast<float>(rand()) / RAND_MAX,
            static_cast<float>(rand()) / RAND_MAX
        );
        
        m_fireworks.push_back(firework);
    }
    
    void Update(float deltaTime)
    {
        for (auto& firework : m_fireworks) {
            if (!firework.hasExploded && firework.shell.isActive) {
                // Update shell trajectory
                PhysicsVector3D gravity(0.0f, -9.81f * firework.shell.mass, 0.0f);
                m_physics->ApplyNewtonianMotion(firework.shell, gravity, deltaTime);
                
                // Check for explosion
                firework.fuseTime -= deltaTime;
                if (firework.fuseTime <= 0.0f || firework.shell.velocity.y < 0.0f) {
                    ExplodeFirework(firework);
                }
            } else if (firework.hasExploded) {
                // Update sparks
                m_physics->UpdateParticleSystem(firework.sparks, deltaTime);
                m_physics->ApplyGravityToParticles(firework.sparks, 9.81f);
                
                // Apply slight wind
                PhysicsVector3D wind(1.0f, 0.0f, 0.5f);
                m_physics->ApplyWindForce(firework.sparks, wind);
            }
        }
        
        // Remove dead fireworks
        m_fireworks.erase(
            std::remove_if(m_fireworks.begin(), m_fireworks.end(),
                [](const Firework& fw) {
                    if (!fw.hasExploded) return false;
                    return std::all_of(fw.sparks.begin(), fw.sparks.end(),
                        [](const PhysicsParticle& p) { return !p.isActive; });
                }),
            m_fireworks.end()
        );
    }
    
private:
    void ExplodeFirework(Firework& firework)
    {
        firework.hasExploded = true;
        firework.shell.isActive = false;
        
        // Create explosion sparks
        int sparkCount = 80 + rand() % 40; // 80-120 sparks
        firework.sparks = m_physics->CreateExplosion(
            firework.shell.position, sparkCount, 25.0f, 4.0f
        );
        
        // Customize sparks
        for (auto& spark : firework.sparks) {
            spark.mass = 0.02f;  // Very light
            spark.drag = 0.2f;   // High drag for trailing effect
            
            // Add some randomness to lifetime
            spark.life += (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f;
        }
    }
};
```

#### Continuous Emitter System
```cpp
void ContinuousEmitterSystem()
{
    Physics physics;
    physics.Initialize();
    
    // Waterfall emitter
    struct ParticleEmitter {
        PhysicsVector3D position;
        PhysicsVector3D direction;
        float emissionRate;     // particles per second
        float velocityVariance;
        float lastEmission;
        int maxParticles;
    };
    
    ParticleEmitter waterfall;
    waterfall.position = PhysicsVector3D(0.0f, 10.0f, 0.0f);
    waterfall.direction = PhysicsVector3D(0.0f, -1.0f, 0.0f);
    waterfall.emissionRate = 50.0f; // 50 particles per second
    waterfall.velocityVariance = 2.0f;
    waterfall.lastEmission = 0.0f;
    waterfall.maxParticles = 500;
    
    std::vector<PhysicsParticle> waterParticles;
    waterParticles.reserve(waterfall.maxParticles);
    
    float deltaTime = 0.016f;
    float simulationTime = 0.0f;
    
    for (int frame = 0; frame < 1000; ++frame) { // ~16 seconds
        simulationTime += deltaTime;
        waterfall.lastEmission += deltaTime;
        
        // Emit new particles
        float emissionInterval = 1.0f / waterfall.emissionRate;
        while (waterfall.lastEmission >= emissionInterval && 
               waterParticles.size() < static_cast<size_t>(waterfall.maxParticles)) {
            
            PhysicsParticle newParticle;
            newParticle.position = waterfall.position + PhysicsVector3D(
                (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 0.5f,
                0.0f,
                (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 0.5f
            );
            
            newParticle.velocity = waterfall.direction * 8.0f + PhysicsVector3D(
                (static_cast<float>(rand()) / RAND_MAX - 0.5f) * waterfall.velocityVariance,
                (static_cast<float>(rand()) / RAND_MAX - 0.5f) * waterfall.velocityVariance,
                (static_cast<float>(rand()) / RAND_MAX - 0.5f) * waterfall.velocityVariance
            );
            
            newParticle.mass = 0.1f;
            newParticle.drag = 0.05f;
            newParticle.life = 5.0f;
            newParticle.isActive = true;
            
            waterParticles.push_back(newParticle);
            waterfall.lastEmission -= emissionInterval;
        }
        
        // Update existing particles
        physics.ApplyGravityToParticles(waterParticles, 9.81f);
        physics.UpdateParticleSystem(waterParticles, deltaTime);
        
        // Remove particles that hit ground
        for (auto& particle : waterParticles) {
            if (particle.position.y <= 0.0f) {
                particle.isActive = false;
            }
        }
        
        // Remove dead particles
        waterParticles.erase(
            std::remove_if(waterParticles.begin(), waterParticles.end(),
                [](const PhysicsParticle& p) { return !p.isActive; }),
            waterParticles.end()
        );
    }
    
    physics.Cleanup();
}
```

---

### 12.5 Performance Optimization

#### Spatial Partitioning for Particles
```cpp
class OptimizedParticleSystem
{
private:
    Physics* m_physics;
    
    struct ParticleGrid {
        static const int GRID_SIZE = 32;
        static const float CELL_SIZE;
        std::vector<std::vector<PhysicsParticle*>> cells;
        
        ParticleGrid() : cells(GRID_SIZE * GRID_SIZE * GRID_SIZE) {}
        
        int GetCellIndex(const PhysicsVector3D& pos) const {
            int x = std::clamp(static_cast<int>((pos.x + 160.0f) / CELL_SIZE), 0, GRID_SIZE - 1);
            int y = std::clamp(static_cast<int>((pos.y + 160.0f) / CELL_SIZE), 0, GRID_SIZE - 1);
            int z = std::clamp(static_cast<int>((pos.z + 160.0f) / CELL_SIZE), 0, GRID_SIZE - 1);
            return x * GRID_SIZE * GRID_SIZE + y * GRID_SIZE + z;
        }
        
        void Clear() {
            for (auto& cell : cells) {
                cell.clear();
            }
        }
        
        void AddParticle(PhysicsParticle* particle) {
            int index = GetCellIndex(particle->position);
            cells[index].push_back(particle);
        }
    };
    
    std::vector<PhysicsParticle> m_particles;
    ParticleGrid m_grid;
    
public:
    OptimizedParticleSystem(Physics* physics) : m_physics(physics) {
        m_particles.reserve(10000);
    }
    
    void AddParticles(const std::vector<PhysicsParticle>& newParticles) {
        for (const auto& particle : newParticles) {
            if (m_particles.size() < 10000) { // Max particle limit
                m_particles.push_back(particle);
            }
        }
    }
    
    void Update(float deltaTime) {
        // Clear spatial grid
        m_grid.Clear();
        
        // Update particles and populate grid
        for (auto& particle : m_particles) {
            if (particle.isActive) {
                particle.Update(deltaTime);
                m_grid.AddParticle(&particle);
            }
        }
        
        // Perform optimized inter-particle interactions
        PerformGridBasedInteractions();
        
        // Remove dead particles
        m_particles.erase(
            std::remove_if(m_particles.begin(), m_particles.end(),
                [](const PhysicsParticle& p) { return !p.isActive; }),
            m_particles.end()
        );
    }
    
    void ApplyGlobalForces(const PhysicsVector3D& gravity, const PhysicsVector3D& wind) {
        for (auto& particle : m_particles) {
            if (particle.isActive) {
                PhysicsVector3D totalForce = gravity * particle.mass + wind;
                particle.acceleration += totalForce * (1.0f / particle.mass);
            }
        }
    }
    
    size_t GetActiveParticleCount() const {
        return std::count_if(m_particles.begin(), m_particles.end(),
            [](const PhysicsParticle& p) { return p.isActive; });
    }
    
private:
    void PerformGridBasedInteractions() {
        const float interactionRadius = 0.5f;
        
        for (const auto& cell : m_grid.cells) {
            // Check interactions within cell
            for (size_t i = 0; i < cell.size(); ++i) {
                for (size_t j = i + 1; j < cell.size(); ++j) {
                    PhysicsParticle* p1 = cell[i];
                    PhysicsParticle* p2 = cell[j];
                    
                    float distance = (p1->position - p2->position).Magnitude();
                    if (distance < interactionRadius && distance > 0.01f) {
                        // Simple repulsion force
                        PhysicsVector3D direction = (p1->position - p2->position).Normalized();
                        float force = 1.0f / (distance * distance);
                        
                        p1->velocity += direction * force * 0.1f;
                        p2->velocity -= direction * force * 0.1f;
                    }
                }
            }
        }
    }
};

const float OptimizedParticleSystem::ParticleGrid::CELL_SIZE = 10.0f;
```

---

### 12.6 Specialized Particle Effects

#### Liquid Simulation (Simplified)
```cpp
void SimpleLiquidSimulation()
{
    Physics physics;
    physics.Initialize();
    
    OptimizedParticleSystem liquidSystem(&physics);
    
    // Create liquid drop
    std::vector<PhysicsParticle> liquidParticles;
    
    // Dense packing of particles
    PhysicsVector3D dropCenter(0.0f, 5.0f, 0.0f);
    float dropRadius = 1.0f;
    int particlesPerAxis = 8;
    float spacing = (dropRadius * 2.0f) / particlesPerAxis;
    
    for (int x = 0; x < particlesPerAxis; ++x) {
        for (int y = 0; y < particlesPerAxis; ++y) {
            for (int z = 0; z < particlesPerAxis; ++z) {
                PhysicsVector3D pos = dropCenter + PhysicsVector3D(
                    (x - particlesPerAxis * 0.5f) * spacing,
                    (y - particlesPerAxis * 0.5f) * spacing,
                    (z - particlesPerAxis * 0.5f) * spacing
                );
                
                // Only create particles within sphere
                if ((pos - dropCenter).Magnitude() <= dropRadius) {
                    PhysicsParticle particle;
                    particle.position = pos;
                    particle.velocity = PhysicsVector3D(0.0f, 0.0f, 0.0f);
                    particle.mass = 0.2f;     // Heavy for liquid
                    particle.drag = 0.8f;     // High internal friction
                    particle.life = 20.0f;    // Long-lived
                    particle.isActive = true;
                    
                    liquidParticles.push_back(particle);
                }
            }
        }
    }
    
    liquidSystem.AddParticles(liquidParticles);
    
    // Simulate liquid behavior
    float deltaTime = 0.01f; // Higher precision for liquid
    PhysicsVector3D gravity(0.0f, -9.81f, 0.0f);
    PhysicsVector3D noWind(0.0f, 0.0f, 0.0f);
    
    for (int frame = 0; frame < 2000; ++frame) { // 20 seconds
        liquidSystem.ApplyGlobalForces(gravity, noWind);
        liquidSystem.Update(deltaTime);
        
        // Log particle count every 100 frames
        if (frame % 100 == 0) {
            size_t activeCount = liquidSystem.GetActiveParticleCount();
            // Liquid particle count available for monitoring
        }
    }
    
    physics.Cleanup();
}
```

---

### Best Practices for Particle Systems

1. **Memory Management**: Pre-allocate particle pools to avoid runtime allocations
2. **Spatial Optimization**: Use spatial partitioning for large particle counts
3. **LOD Systems**: Reduce particle density at distance
4. **Lifetime Management**: Efficiently remove dead particles
5. **Force Batching**: Apply global forces in batches for performance

### Common Issues and Solutions

- **Performance Drops**: Too many active particles → Implement particle pooling and LOD systems
- **Memory Fragmentation**: Frequent allocation/deallocation → Use pre-allocated particle pools
- **Unrealistic Movement**: Poor force application → Balance mass, drag, and force magnitudes
- **Visual Artifacts**: Particles popping in/out → Use smooth fade transitions for lifetime
- **Collision Issues**: Particles tunneling through surfaces → Use smaller time steps or continuous collision detection

---

**Chapter 12 Complete**


## Chapter 13: Physics-Based Animation

### Overview
The Physics system provides comprehensive physics-based animation capabilities for realistic character movement, object interactions, and procedural animations. This chapter covers physics-animation blending, recoil effects, secondary motion, and dynamic animation systems.

### Key Features
- Seamless physics-animation blending
- Realistic recoil and impact animations
- Secondary motion simulation (cloth, hair, accessories)
- Procedural animation generation
- Dynamic response to environmental forces
- Optimized calculations using MathPrecalculation

---

### 13.1 Basic Physics-Animation Blending

#### Simple Blend Between Physics and Animation
```cpp
#include "Physics.h"

void BasicAnimationBlending()
{
    Physics physics;
    if (!physics.Initialize()) {
        return; // Handle initialization failure
    }
    
    // Animation keyframe position
    PhysicsVector3D animatedPosition(5.0f, 2.0f, 0.0f);
    
    // Physics simulation position (after collision or force)
    PhysicsVector3D physicsPosition(4.2f, 1.8f, 0.3f);
    
    // Blend factors for different scenarios
    float blendFactors[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    
    for (float blendFactor : blendFactors) {
        // Blend physics with animation
        PhysicsVector3D blendedPosition = physics.BlendPhysicsWithAnimation(
            physicsPosition, animatedPosition, blendFactor
        );
        
        // blendFactor = 0.0f: Pure physics
        // blendFactor = 1.0f: Pure animation
        // blendFactor = 0.5f: 50/50 blend
        
        // Use blendedPosition for rendering
    }
    
    physics.Cleanup();
}
```

#### Dynamic Blend Factor Control
```cpp
void DynamicBlendControl()
{
    Physics physics;
    physics.Initialize();
    
    struct AnimatedCharacter {
        PhysicsVector3D animatedPos;
        PhysicsVector3D physicsPos;
        PhysicsVector3D finalPos;
        float blendFactor;
        bool isHit;
        float hitRecoveryTime;
    };
    
    AnimatedCharacter character;
    character.animatedPos = PhysicsVector3D(0.0f, 1.8f, 0.0f);
    character.physicsPos = PhysicsVector3D(0.0f, 1.8f, 0.0f);
    character.blendFactor = 1.0f; // Start with pure animation
    character.isHit = false;
    character.hitRecoveryTime = 0.0f;
    
    float deltaTime = 0.016f; // 60 FPS
    
    for (int frame = 0; frame < 300; ++frame) { // 5 seconds
        float time = frame * deltaTime;
        
        // Update animation position (simple walking cycle)
        character.animatedPos.x = sin(time * 2.0f) * 2.0f;
        character.animatedPos.y = 1.8f + abs(sin(time * 4.0f)) * 0.1f; // Slight bounce
        
        // Simulate hit at 2 seconds
        if (time >= 2.0f && time <= 2.1f && !character.isHit) {
            character.isHit = true;
            character.hitRecoveryTime = 1.5f; // 1.5 second recovery
            
            // Apply physics impulse
            character.physicsPos = character.animatedPos + PhysicsVector3D(2.0f, 0.5f, 1.0f);
        }
        
        // Handle hit recovery
        if (character.isHit && character.hitRecoveryTime > 0.0f) {
            character.hitRecoveryTime -= deltaTime;
            
            // Gradually blend back to animation
            float recoveryProgress = 1.0f - (character.hitRecoveryTime / 1.5f);
            character.blendFactor = recoveryProgress; // 0.0 = physics, 1.0 = animation
            
            // Physics position settles back towards animation
            PhysicsVector3D toAnimation = character.animatedPos - character.physicsPos;
            character.physicsPos += toAnimation * deltaTime * 2.0f; // Smooth return
        } else {
            character.blendFactor = 1.0f; // Pure animation when not hit
            character.physicsPos = character.animatedPos;
        }
        
        // Calculate final blended position
        character.finalPos = physics.BlendPhysicsWithAnimation(
            character.physicsPos, character.animatedPos, character.blendFactor
        );
        
        // Use character.finalPos for rendering
    }
    
    physics.Cleanup();
}
```

---

### 13.2 Recoil and Impact Animation

#### Weapon Recoil System
```cpp
void WeaponRecoilAnimation()
{
    Physics physics;
    physics.Initialize();
    
    struct WeaponRecoil {
        PhysicsVector3D basePosition;
        PhysicsVector3D recoilOffset;
        PhysicsVector3D currentPosition;
        float recoilMagnitude;
        float recoverySpeed;
        bool isFiring;
    };
    
    WeaponRecoil weapon;
    weapon.basePosition = PhysicsVector3D(0.0f, 0.0f, 0.0f);
    weapon.recoilOffset = PhysicsVector3D(0.0f, 0.0f, 0.0f);
    weapon.currentPosition = weapon.basePosition;
    weapon.recoilMagnitude = 0.15f; // 15cm recoil
    weapon.recoverySpeed = 8.0f;    // Fast recovery
    weapon.isFiring = false;
    
    // Weapon properties
    float weaponMass = 3.5f;      // 3.5kg rifle
    float bulletMass = 0.008f;    // 8g bullet
    float muzzleVelocity = 800.0f; // 800 m/s
    float dampening = 0.7f;
    
    float deltaTime = 0.016f;
    
    for (int frame = 0; frame < 600; ++frame) { // 10 seconds
        float time = frame * deltaTime;
        
        // Simulate firing every 0.5 seconds
        if (fmod(time, 0.5f) < deltaTime && time > 0.1f) {
            weapon.isFiring = true;
            
            // Calculate recoil force (Newton's third law)
            PhysicsVector3D bulletMomentum(0.0f, 0.0f, muzzleVelocity * bulletMass);
            PhysicsVector3D recoilForce = bulletMomentum * -1.0f; // Opposite direction
            
            // Calculate recoil animation
            PhysicsVector3D recoilDisplacement = physics.CalculateRecoilAnimation(
                recoilForce, weaponMass, dampening
            );
            
            // Apply recoil with some randomness
            weapon.recoilOffset = recoilDisplacement * weapon.recoilMagnitude + PhysicsVector3D(
                (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 0.02f, // Horizontal spread
                (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 0.02f, // Vertical spread
                0.0f
            );
        }
        
        // Recover from recoil
        if (weapon.recoilOffset.Magnitude() > 0.001f) {
            weapon.recoilOffset *= (1.0f - weapon.recoverySpeed * deltaTime);
            
            // Stop recovery when very small
            if (weapon.recoilOffset.Magnitude() < 0.001f) {
                weapon.recoilOffset = PhysicsVector3D(0.0f, 0.0f, 0.0f);
                weapon.isFiring = false;
            }
        }
        
        // Calculate final weapon position
        weapon.currentPosition = weapon.basePosition + weapon.recoilOffset;
        
        // Use weapon.currentPosition for rendering
    }
    
    physics.Cleanup();
}
```

#### Impact Response Animation
```cpp
void ImpactResponseAnimation()
{
    Physics physics;
    physics.Initialize();
    
    struct ImpactData {
        PhysicsVector3D impactPoint;
        PhysicsVector3D impactDirection;
        float impactForce;
        float timestamp;
    };
    
    struct AnimatedObject {
        PhysicsVector3D basePosition;
        PhysicsVector3D currentPosition;
        PhysicsVector3D velocity;
        float mass;
        float damping;
        std::vector<ImpactData> activeImpacts;
    };
    
    AnimatedObject obj;
    obj.basePosition = PhysicsVector3D(0.0f, 2.0f, 0.0f);
    obj.currentPosition = obj.basePosition;
    obj.velocity = PhysicsVector3D(0.0f, 0.0f, 0.0f);
    obj.mass = 50.0f; // 50kg object
    obj.damping = 5.0f;
    
    float deltaTime = 0.016f;
    
    for (int frame = 0; frame < 480; ++frame) { // 8 seconds
        float time = frame * deltaTime;
        
        // Create impacts at regular intervals
        if (frame % 120 == 60) { // Every 2 seconds, offset by 1 second
            ImpactData impact;
            impact.impactPoint = obj.currentPosition + PhysicsVector3D(
                (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f,
                (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f,
                (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f
            );
            impact.impactDirection = PhysicsVector3D(
                static_cast<float>(rand()) / RAND_MAX - 0.5f,
                static_cast<float>(rand()) / RAND_MAX - 0.5f,
                static_cast<float>(rand()) / RAND_MAX - 0.5f
            ).Normalized();
            impact.impactForce = 100.0f + static_cast<float>(rand()) / RAND_MAX * 200.0f; // 100-300N
            impact.timestamp = time;
            
            obj.activeImpacts.push_back(impact);
        }
        
        // Process active impacts
        PhysicsVector3D totalForce(0.0f, 0.0f, 0.0f);
        
        for (auto it = obj.activeImpacts.begin(); it != obj.activeImpacts.end();) {
            float impactAge = time - it->timestamp;
            
            if (impactAge > 0.5f) { // Impact effects last 0.5 seconds
                it = obj.activeImpacts.erase(it);
            } else {
                // Calculate diminishing force over time
                float forceFalloff = 1.0f - (impactAge / 0.5f);
                forceFalloff = forceFalloff * forceFalloff; // Quadratic falloff
                
                PhysicsVector3D impactForceVector = it->impactDirection * it->impactForce * forceFalloff;
                totalForce += impactForceVector;
                ++it;
            }
        }
        
        // Apply physics
        PhysicsVector3D acceleration = totalForce * (1.0f / obj.mass);
        obj.velocity += acceleration * deltaTime;
        
        // Apply damping
        obj.velocity *= (1.0f - obj.damping * deltaTime);
        
        // Update position
        obj.currentPosition += obj.velocity * deltaTime;
        
        // Gravity and ground constraint
        obj.velocity.y -= 9.81f * deltaTime;
        if (obj.currentPosition.y < 0.5f) {
            obj.currentPosition.y = 0.5f;
            obj.velocity.y = std::max(0.0f, obj.velocity.y);
        }
        
        // Use obj.currentPosition for rendering
    }
    
    physics.Cleanup();
}
```

---

### 13.3 Secondary Motion Systems

#### Cloth and Accessory Physics
```cpp
void SecondaryMotionSimulation()
{
    Physics physics;
    physics.Initialize();
    
    struct SecondaryMotionNode {
        PhysicsVector3D currentPosition;
        PhysicsVector3D previousPosition;
        PhysicsVector3D targetPosition;
        float stiffness;
        float damping;
        float mass;
    };
    
    // Character with cape/cloth simulation
    struct Character {
        PhysicsVector3D position;
        PhysicsVector3D velocity;
        std::vector<SecondaryMotionNode> capeNodes;
    };
    
    Character character;
    character.position = PhysicsVector3D(0.0f, 1.8f, 0.0f);
    character.velocity = PhysicsVector3D(0.0f, 0.0f, 0.0f);
    
    // Create cape chain (5 nodes)
    character.capeNodes.resize(5);
    for (int i = 0; i < 5; ++i) {
        SecondaryMotionNode& node = character.capeNodes[i];
        node.currentPosition = character.position + PhysicsVector3D(0.0f, -0.3f * i, -0.2f * i);
        node.previousPosition = node.currentPosition;
        node.targetPosition = node.currentPosition;
        node.stiffness = 0.8f - i * 0.15f; // Decreasing stiffness down the chain
        node.damping = 0.9f;
        node.mass = 0.1f;
    }
    
    float deltaTime = 0.016f;
    
    for (int frame = 0; frame < 600; ++frame) { // 10 seconds
        float time = frame * deltaTime;
        
        // Character movement (figure-8 pattern)
        character.velocity = PhysicsVector3D(
            cos(time * 0.5f) * 3.0f,
            sin(time * 1.0f) * 0.5f,
            sin(time * 0.5f) * 2.0f
        );
        character.position += character.velocity * deltaTime;
        
        // Update cape nodes
        for (int i = 0; i < static_cast<int>(character.capeNodes.size()); ++i) {
            SecondaryMotionNode& node = character.capeNodes[i];
            
            // Calculate target position based on parent
            if (i == 0) {
                // First node follows character
                node.targetPosition = character.position + PhysicsVector3D(0.0f, -0.3f, -0.2f);
            } else {
                // Subsequent nodes follow previous node
                PhysicsVector3D parentPos = character.capeNodes[i - 1].currentPosition;
                PhysicsVector3D offset(0.0f, -0.3f, -0.2f);
                node.targetPosition = parentPos + offset;
            }
            
            // Apply secondary motion
            PhysicsVector3D newPosition = physics.ApplySecondaryMotion(
                node.targetPosition,
                node.currentPosition,
                node.stiffness,
                node.damping
            );
            
            // Store previous position for next frame
            node.previousPosition = node.currentPosition;
            node.currentPosition = newPosition;
            
            // Apply gravity to lower nodes
            if (i > 0) {
                node.currentPosition.y -= 9.81f * deltaTime * deltaTime * (i * 0.1f);
            }
        }
        
        // Use character.position and character.capeNodes for rendering
    }
    
    physics.Cleanup();
}
```

#### Hair Simulation
```cpp
void HairPhysicsSimulation()
{
    Physics physics;
    physics.Initialize();
    
    struct HairStrand {
        std::vector<PhysicsVector3D> segments;
        std::vector<PhysicsVector3D> velocities;
        float segmentLength;
        float stiffness;
        float damping;
    };
    
    // Create multiple hair strands
    std::vector<HairStrand> hairStrands;
    int strandCount = 20;
    int segmentsPerStrand = 8;
    
    PhysicsVector3D headPosition(0.0f, 2.0f, 0.0f);
    
    for (int s = 0; s < strandCount; ++s) {
        HairStrand strand;
        strand.segmentLength = 0.15f; // 15cm per segment
        strand.stiffness = 0.3f + (static_cast<float>(s) / strandCount) * 0.4f; // Varying stiffness
        strand.damping = 0.85f;
        
        // Initialize strand segments
        for (int i = 0; i < segmentsPerStrand; ++i) {
            // Random initial positions around head
            float angle = (static_cast<float>(s) / strandCount) * 2.0f * XM_PI;
            PhysicsVector3D offset(
                cos(angle) * 0.1f + (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 0.05f,
                -i * strand.segmentLength,
                sin(angle) * 0.1f + (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 0.05f
            );
            
            strand.segments.push_back(headPosition + offset);
            strand.velocities.push_back(PhysicsVector3D(0.0f, 0.0f, 0.0f));
        }
        
        hairStrands.push_back(strand);
    }
    
    // Head movement and hair simulation
    float deltaTime = 0.016f;
    
    for (int frame = 0; frame < 600; ++frame) { // 10 seconds
        float time = frame * deltaTime;
        
        // Head movement (nodding and turning)
        PhysicsVector3D headMovement(
            sin(time * 1.5f) * 0.3f,  // Head turning
            cos(time * 2.0f) * 0.1f,  // Head nodding
            0.0f
        );
        PhysicsVector3D newHeadPos = headPosition + headMovement;
        
        // Wind force
        PhysicsVector3D wind(
            sin(time * 0.8f) * 2.0f,
            0.0f,
            cos(time * 0.6f) * 1.5f
        );
        
        // Update each hair strand
        for (auto& strand : hairStrands) {
            // Update root position (attached to head)
            strand.segments[0] = newHeadPos + (strand.segments[0] - headPosition);
            
            // Update remaining segments
            for (int i = 1; i < static_cast<int>(strand.segments.size()); ++i) {
                PhysicsVector3D& currentSeg = strand.segments[i];
                PhysicsVector3D& currentVel = strand.velocities[i];
                PhysicsVector3D& parentSeg = strand.segments[i - 1];
                
                // Calculate target position (constraint)
                PhysicsVector3D toParent = parentSeg - currentSeg;
                float currentLength = toParent.Magnitude();
                
                if (currentLength > 0.001f) {
                    PhysicsVector3D targetPos = parentSeg - toParent.Normalized() * strand.segmentLength;
                    
                    // Apply secondary motion
                    PhysicsVector3D newPos = physics.ApplySecondaryMotion(
                        targetPos, currentSeg, strand.stiffness, strand.damping
                    );
                    
                    // Calculate velocity
                    currentVel = (newPos - currentSeg) / deltaTime;
                    
                    // Apply external forces
                    currentVel += wind * deltaTime * 0.1f; // Wind effect
                    currentVel.y -= 9.81f * deltaTime * 0.5f; // Gravity
                    
                    // Apply damping
                    currentVel *= 0.98f;
                    
                    // Update position
                    currentSeg = newPos + currentVel * deltaTime;
                }
            }
        }
        
        // Use hairStrands for rendering
    }
    
    physics.Cleanup();
}
```

---

### 13.4 Procedural Animation Generation

#### Dynamic Walk Cycle
```cpp
void ProceduralWalkCycle()
{
    Physics physics;
    physics.Initialize();
    
    struct LegIK {
        PhysicsVector3D hipPosition;
        PhysicsVector3D kneePosition;
        PhysicsVector3D footPosition;
        PhysicsVector3D footTarget;
        float upperLegLength;
        float lowerLegLength;
        bool isPlanted;
        float stepProgress;
    };
    
    struct Character {
        PhysicsVector3D position;
        PhysicsVector3D velocity;
        float walkSpeed;
        float stepHeight;
        float stepLength;
        LegIK leftLeg;
        LegIK rightLeg;
    };
    
    Character character;
    character.position = PhysicsVector3D(0.0f, 0.0f, 0.0f);
    character.velocity = PhysicsVector3D(2.0f, 0.0f, 0.0f); // Walking forward
    character.walkSpeed = 2.0f;
    character.stepHeight = 0.3f;
    character.stepLength = 1.0f;
    
    // Initialize legs
    character.leftLeg.hipPosition = character.position + PhysicsVector3D(-0.2f, 1.0f, 0.0f);
    character.leftLeg.upperLegLength = 0.5f;
    character.leftLeg.lowerLegLength = 0.5f;
    character.leftLeg.isPlanted = true;
    character.leftLeg.stepProgress = 0.0f;
    
    character.rightLeg.hipPosition = character.position + PhysicsVector3D(0.2f, 1.0f, 0.0f);
    character.rightLeg.upperLegLength = 0.5f;
    character.rightLeg.lowerLegLength = 0.5f;
    character.rightLeg.isPlanted = false;
    character.rightLeg.stepProgress = 0.5f; // Start with opposite phase
    
    float deltaTime = 0.016f;
    float walkCycleFrequency = 2.0f; // 2 Hz walk cycle
    
    for (int frame = 0; frame < 600; ++frame) { // 10 seconds
        float time = frame * deltaTime;
        
        // Update character position
        character.position += character.velocity * deltaTime;
        
        // Update hip positions
        character.leftLeg.hipPosition = character.position + PhysicsVector3D(-0.2f, 1.0f, 0.0f);
        character.rightLeg.hipPosition = character.position + PhysicsVector3D(0.2f, 1.0f, 0.0f);
        
        // Update leg step cycles
        LegIK* legs[] = { &character.leftLeg, &character.rightLeg };
        
        for (int i = 0; i < 2; ++i) {
            LegIK* leg = legs[i];
            
            // Update step progress
            leg->stepProgress += walkCycleFrequency * deltaTime;
            if (leg->stepProgress >= 1.0f) {
                leg->stepProgress -= 1.0f;
            }
            
            // Calculate foot target position
            float stepPhase = leg->stepProgress * 2.0f * XM_PI;
            
            if (leg->stepProgress < 0.5f) {
                // Swing phase - foot in air
                leg->isPlanted = false;
                
                float swingProgress = leg->stepProgress * 2.0f; // 0 to 1 during swing
                
                // Calculate foot target during swing
                PhysicsVector3D startPos = leg->hipPosition + PhysicsVector3D(
                    -character.stepLength * 0.5f, -1.0f, 0.0f
                );
                PhysicsVector3D endPos = leg->hipPosition + PhysicsVector3D(
                    character.stepLength * 0.5f, -1.0f, 0.0f
                );
                
                // Interpolate foot position with arc
                leg->footTarget = startPos + (endPos - startPos) * swingProgress;
                leg->footTarget.y += sin(swingProgress * XM_PI) * character.stepHeight;
                
            } else {
                // Stance phase - foot on ground
                leg->isPlanted = true;
                
                // Keep foot planted relative to world
                leg->footTarget.y = 0.0f; // Ground level
            }
            
            // Simple IK solver for leg
            SolveLegIK(*leg);
        }
        
        // Use character leg positions for rendering
    }
    
    physics.Cleanup();
}

// Helper function for inverse kinematics
void SolveLegIK(LegIK& leg)
{
    PhysicsVector3D hipToFoot = leg.footTarget - leg.hipPosition;
    float totalLegLength = leg.upperLegLength + leg.lowerLegLength;
    float hipToFootDistance = hipToFoot.Magnitude();
    
    // Clamp to reachable distance
    if (hipToFootDistance > totalLegLength * 0.99f) {
        hipToFootDistance = totalLegLength * 0.99f;
        hipToFoot = hipToFoot.Normalized() * hipToFootDistance;
        leg.footTarget = leg.hipPosition + hipToFoot;
    }
    
    // Calculate knee position using law of cosines
    float a = leg.upperLegLength;
    float b = leg.lowerLegLength;
    float c = hipToFootDistance;
    
    float cosKneeAngle = (a * a + b * b - c * c) / (2.0f * a * b);
    cosKneeAngle = std::clamp(cosKneeAngle, -1.0f, 1.0f);
    
    float kneeAngle = acos(cosKneeAngle);
    
    // Position knee
    PhysicsVector3D hipToFootDir = hipToFoot.Normalized();
    PhysicsVector3D kneeOffset = PhysicsVector3D(-hipToFootDir.z, 0.0f, hipToFootDir.x) * 0.3f; // Side offset
    
    leg.kneePosition = leg.hipPosition + hipToFootDir * (a * cos(kneeAngle * 0.5f)) + kneeOffset;
    leg.footPosition = leg.footTarget;
}
```

---

### 13.5 Environmental Response Animation

#### Wind and Force Response
```cpp
void EnvironmentalResponseAnimation()
{
    Physics physics;
    physics.Initialize();
    
    struct EnvironmentallyResponsiveObject {
        PhysicsVector3D basePosition;
        PhysicsVector3D currentPosition;
        PhysicsVector3D restPosition;
        float flexibility;        // How much object bends
        float responsiveness;     // How quickly it responds
        float recoveryRate;       // How quickly it returns to rest
        float mass;
    };
    
    // Create trees swaying in wind
    std::vector<EnvironmentallyResponsiveObject> trees;
    
    for (int i = 0; i < 10; ++i) {
        EnvironmentallyResponsiveObject tree;
        tree.basePosition = PhysicsVector3D(i * 5.0f - 25.0f, 0.0f, 0.0f);
        tree.currentPosition = tree.basePosition;
        tree.restPosition = tree.basePosition;
        tree.flexibility = 0.5f + (static_cast<float>(rand()) / RAND_MAX) * 0.5f; // 0.5-1.0
        tree.responsiveness = 2.0f + (static_cast<float>(rand()) / RAND_MAX) * 3.0f; // 2.0-5.0
        tree.recoveryRate = 1.0f + (static_cast<float>(rand()) / RAND_MAX) * 2.0f; // 1.0-3.0
        tree.mass = 100.0f + (static_cast<float>(rand()) / RAND_MAX) * 200.0f; // 100-300kg
        
        trees.push_back(tree);
    }
    
    float deltaTime = 0.016f;
    
    for (int frame = 0; frame < 900; ++frame) { // 15 seconds
        float time = frame * deltaTime;
        
        // Dynamic wind system
        PhysicsVector3D wind(
            sin(time * 0.3f) * 8.0f + cos(time * 0.7f) * 4.0f,  // Variable X wind
            0.0f,
            sin(time * 0.5f) * 3.0f + cos(time * 1.1f) * 2.0f   // Variable Z wind
        );
        
        // Add wind gusts every 3 seconds
        if (fmod(time, 3.0f) < 0.2f) {
            wind *= 2.0f; // Double wind strength during gusts
        }
        
        // Update each tree
        for (auto& tree : trees) {
            // Calculate wind force effect
            PhysicsVector3D windForce = wind * tree.flexibility;
            
            // Apply force to create bending displacement
            PhysicsVector3D displacement = windForce * (1.0f / tree.mass) * deltaTime;
            tree.currentPosition += displacement;
            
            // Apply recovery force towards rest position
            PhysicsVector3D toRest = tree.restPosition - tree.currentPosition;
            PhysicsVector3D recoveryForce = toRest * tree.recoveryRate;
            tree.currentPosition += recoveryForce * deltaTime;
            
            // Apply damping
            PhysicsVector3D velocity = (tree.currentPosition - tree.basePosition) / deltaTime;
            velocity *= 0.95f; // Damping factor
            tree.currentPosition = tree.basePosition + velocity * deltaTime;
            
            // Limit maximum displacement
            PhysicsVector3D totalDisplacement = tree.currentPosition - tree.restPosition;
            float maxDisplacement = 2.0f; // 2 meter max bend
            if (totalDisplacement.Magnitude() > maxDisplacement) {
                totalDisplacement = totalDisplacement.Normalized() * maxDisplacement;
                tree.currentPosition = tree.restPosition + totalDisplacement;
            }
        }
        
        // Use tree.currentPosition for rendering each tree
    }
    
    physics.Cleanup();
}
```

---

### 13.6 Character Animation Enhancement

#### Dynamic Balance and Foot Placement
```cpp
void DynamicBalanceSystem()
{
    Physics physics;
    physics.Initialize();
    
    struct BalanceSystem {
        PhysicsVector3D centerOfMass;
        PhysicsVector3D supportBase;     // Average foot position
        PhysicsVector3D balanceOffset;   // Adjustment for balance
        float balanceThreshold;          // When to trigger balance response
        float recoveryStrength;          // How strong the balance correction is
        bool isBalancing;
    };
    
    struct Character {
        PhysicsVector3D position;
        PhysicsVector3D velocity;
        PhysicsVector3D leftFootPos;
        PhysicsVector3D rightFootPos;
        BalanceSystem balance;
        float bodyHeight;
    };
    
    Character character;
    character.position = PhysicsVector3D(0.0f, 1.8f, 0.0f);
    character.velocity = PhysicsVector3D(0.0f, 0.0f, 0.0f);
    character.bodyHeight = 1.8f;
    character.leftFootPos = PhysicsVector3D(-0.2f, 0.0f, 0.0f);
    character.rightFootPos = PhysicsVector3D(0.2f, 0.0f, 0.0f);
    
    character.balance.balanceThreshold = 0.3f; // 30cm from support base
    character.balance.recoveryStrength = 5.0f;
    character.balance.isBalancing = false;
    
    float deltaTime = 0.016f;
    
    for (int frame = 0; frame < 600; ++frame) { // 10 seconds
        float time = frame * deltaTime;
        
        // Simulate external forces (pushes, wind, etc.)
        PhysicsVector3D externalForce(0.0f, 0.0f, 0.0f);
        
        // Random pushes every 2-4 seconds
        if (frame % 180 == 0) { // Every 3 seconds
            externalForce = PhysicsVector3D(
                (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 100.0f,
                0.0f,
                (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 100.0f
            );
        }
        
        // Apply external forces to velocity
        character.velocity += externalForce * deltaTime * 0.01f; // Scale down force
        
        // Calculate center of mass
        character.balance.centerOfMass = character.position;
        
        // Calculate support base (average foot position)
        character.balance.supportBase = (character.leftFootPos + character.rightFootPos) * 0.5f;
        
        // Check balance
        PhysicsVector3D comProjection = character.balance.centerOfMass;
        comProjection.y = 0.0f; // Project to ground level
        
        PhysicsVector3D balanceError = comProjection - character.balance.supportBase;
        float balanceDistance = balanceError.Magnitude();
        
        if (balanceDistance > character.balance.balanceThreshold) {
            character.balance.isBalancing = true;
            
            // Calculate balance correction
            PhysicsVector3D correctionDirection = balanceError.Normalized();
            
            // Step adjustment - move feet towards COM
            PhysicsVector3D stepAdjustment = correctionDirection * 0.1f;
            character.leftFootPos += stepAdjustment;
            character.rightFootPos += stepAdjustment;
            
            // Body lean correction
            character.balance.balanceOffset = correctionDirection * -0.2f;
            
            // Apply recovery force
            PhysicsVector3D recoveryForce = correctionDirection * character.balance.recoveryStrength;
            character.velocity -= recoveryForce * deltaTime;
        } else {
            character.balance.isBalancing = false;
            
            // Gradually return to neutral pose
            character.balance.balanceOffset *= 0.95f;
        }
        
        // Apply damping to velocity
        character.velocity *= 0.9f;
        
        // Update position
        character.position += character.velocity * deltaTime;
        
        // Apply balance offset to final position
        PhysicsVector3D finalPosition = character.position + character.balance.balanceOffset;
        
        // Use finalPosition and foot positions for rendering
    }
    
    physics.Cleanup();
}
```

#### Adaptive Animation Blending
```cpp
void AdaptiveAnimationBlending()
{
    Physics physics;
    physics.Initialize();
    
    struct AnimationState {
        std::string name;
        PhysicsVector3D position;
        float weight;
        float priority;
    };
    
    struct AdaptiveCharacter {
        PhysicsVector3D position;
        PhysicsVector3D velocity;
        std::vector<AnimationState> animationStates;
        PhysicsVector3D finalPosition;
        float environmentalInfluence;
        float physicsInfluence;
    };
    
    AdaptiveCharacter character;
    character.position = PhysicsVector3D(0.0f, 1.8f, 0.0f);
    character.velocity = PhysicsVector3D(0.0f, 0.0f, 0.0f);
    character.environmentalInfluence = 0.0f;
    character.physicsInfluence = 0.0f;
    
    // Initialize animation states
    character.animationStates = {
        { "Idle", PhysicsVector3D(0.0f, 1.8f, 0.0f), 1.0f, 1.0f },
        { "Walk", PhysicsVector3D(0.0f, 1.8f, 0.0f), 0.0f, 2.0f },
        { "Run", PhysicsVector3D(0.0f, 1.8f, 0.0f), 0.0f, 3.0f },
        { "Hit", PhysicsVector3D(0.0f, 1.8f, 0.0f), 0.0f, 5.0f },
        { "Balance", PhysicsVector3D(0.0f, 1.8f, 0.0f), 0.0f, 4.0f }
    };
    
    float deltaTime = 0.016f;
    
    for (int frame = 0; frame < 900; ++frame) { // 15 seconds
        float time = frame * deltaTime;
        
        // Simulate different scenarios
        float speed = character.velocity.Magnitude();
        bool isHit = (frame % 300 == 100); // Hit every 5 seconds
        bool needsBalance = character.environmentalInfluence > 0.3f;
        
        // Update animation state weights based on conditions
        for (auto& state : character.animationStates) {
            if (state.name == "Idle") {
                state.weight = (speed < 0.5f && !isHit && !needsBalance) ? 1.0f : 0.0f;
                state.position = PhysicsVector3D(0.0f, 1.8f, 0.0f);
                
            } else if (state.name == "Walk") {
                state.weight = (speed >= 0.5f && speed < 3.0f && !isHit) ? 1.0f : 0.0f;
                state.position = character.position + PhysicsVector3D(sin(time * 4.0f) * 0.05f, 0.0f, 0.0f);
                
            } else if (state.name == "Run") {
                state.weight = (speed >= 3.0f && !isHit) ? 1.0f : 0.0f;
                state.position = character.position + PhysicsVector3D(sin(time * 8.0f) * 0.1f, 0.0f, 0.0f);
                
            } else if (state.name == "Hit") {
                state.weight = isHit ? 1.0f : std::max(0.0f, state.weight - deltaTime * 2.0f);
                if (isHit) {
                    state.position = character.position + PhysicsVector3D(0.3f, -0.2f, 0.0f);
                }
                
            } else if (state.name == "Balance") {
                state.weight = needsBalance ? 0.8f : std::max(0.0f, state.weight - deltaTime * 3.0f);
                state.position = character.position + PhysicsVector3D(
                    sin(time * 6.0f) * 0.2f, 0.0f, cos(time * 6.0f) * 0.1f
                );
            }
        }
        
        // Normalize weights by priority
        float totalWeight = 0.0f;
        for (const auto& state : character.animationStates) {
            totalWeight += state.weight * state.priority;
        }
        
        // Calculate blended position
        PhysicsVector3D blendedAnimPosition(0.0f, 0.0f, 0.0f);
        if (totalWeight > 0.0f) {
            for (const auto& state : character.animationStates) {
                float normalizedWeight = (state.weight * state.priority) / totalWeight;
                blendedAnimPosition += state.position * normalizedWeight;
            }
        } else {
            blendedAnimPosition = character.position;
        }
        
        // Physics influence
        character.physicsInfluence = isHit ? 1.0f : std::max(0.0f, character.physicsInfluence - deltaTime * 2.0f);
        
        // Environmental influence (wind, slopes, etc.)
        character.environmentalInfluence = sin(time * 0.5f) * 0.5f + 0.5f; // 0 to 1
        
        // Final blend between animation and physics
        float physicsBlend = character.physicsInfluence;
        character.finalPosition = physics.BlendPhysicsWithAnimation(
            character.position, blendedAnimPosition, 1.0f - physicsBlend
        );
        
        // Update character state
        character.position = character.finalPosition;
        
        // Simulate movement based on animation state
        if (speed < 0.5f) {
            character.velocity *= 0.9f; // Idle - slow down
        } else {
            character.velocity = PhysicsVector3D(cos(time * 0.3f) * speed, 0.0f, sin(time * 0.3f) * speed);
        }
        
        // Apply environmental effects
        if (character.environmentalInfluence > 0.5f) {
            PhysicsVector3D wind(sin(time) * 2.0f, 0.0f, cos(time * 1.3f) * 1.5f);
            character.velocity += wind * deltaTime * 0.1f;
        }
        
        // Use character.finalPosition for rendering
    }
    
    physics.Cleanup();
}
```

---

### Best Practices for Physics-Based Animation

1. **Smooth Transitions**: Use gradual blend factor changes to avoid jerky motion
2. **State Management**: Implement proper animation state machines for complex behaviors
3. **Performance Optimization**: Cache expensive calculations and use LOD for distant objects
4. **Constraint Systems**: Implement proper constraints for realistic secondary motion
5. **Environmental Integration**: Consider environmental forces in all animation systems

### Common Issues and Solutions

- **Animation Popping**: Sudden blend changes → Use smooth interpolation for all blend factors
- **Unrealistic Secondary Motion**: Poor constraint setup → Tune stiffness and damping parameters
- **Performance Issues**: Too many calculations → Implement LOD and update frequency scaling
- **Inconsistent Blending**: Poor state management → Use normalized weights and priority systems
- **Jittery Motion**: High frequency oscillations → Apply appropriate damping and smoothing

---

**Chapter 13 Complete**


## Chapter 14: Performance Optimization and Debugging

### Overview
The Physics system provides comprehensive performance optimization tools and debugging capabilities for maintaining optimal performance in gaming applications. This chapter covers profiling, memory management, spatial optimization, and debugging visualization techniques.

### Key Features
- Performance profiling and timing analysis
- Memory usage optimization and pooling
- Spatial partitioning for collision optimization
- Debug visualization and logging systems
- LOD (Level of Detail) implementation
- Integration with Debug.h and ExceptionHandler.h for robust error handling

---

### 14.1 Performance Profiling and Monitoring

#### Basic Performance Monitoring
```cpp
#include "Physics.h"
#include "Debug.h"
#include "ExceptionHandler.h"

void BasicPerformanceMonitoring()
{
    Physics physics;
    if (!physics.Initialize()) {
        return; // Handle initialization failure
    }
    
    // Performance monitoring variables
    float totalUpdateTime = 0.0f;
    int frameCount = 0;
    int activeBodyCount = 0;
    int collisionCount = 0;
    int particleCount = 0;
    
    // Create test physics bodies
    std::vector<PhysicsBody> bodies;
    for (int i = 0; i < 100; ++i) {
        PhysicsBody body;
        body.position = PhysicsVector3D(
            static_cast<float>(i % 10) * 2.0f,
            5.0f + static_cast<float>(i / 10) * 2.0f,
            0.0f
        );
        body.SetMass(1.0f);
        body.isActive = true;
        bodies.push_back(body);
    }
    
    float deltaTime = 0.016f; // 60 FPS target
    
    for (int frame = 0; frame < 600; ++frame) { // 10 seconds
        auto startTime = std::chrono::high_resolution_clock::now();
        
        // Physics update
        physics.Update(deltaTime);
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        float frameTime = duration.count() / 1000.0f; // Convert to milliseconds
        
        totalUpdateTime += frameTime;
        frameCount++;
        
        // Get physics statistics every 60 frames
        if (frame % 60 == 0) {
            physics.GetPhysicsStatistics(activeBodyCount, collisionCount, particleCount);
            
            float averageFrameTime = totalUpdateTime / frameCount;
            float fps = 1000.0f / averageFrameTime; // FPS calculation
            
#if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"[Physics] Frame %d - Avg Time: %.3fms, FPS: %.1f, Bodies: %d, Collisions: %d, Particles: %d",
                frame, averageFrameTime, fps, activeBodyCount, collisionCount, particleCount);
#endif
            
            // Reset counters
            totalUpdateTime = 0.0f;
            frameCount = 0;
        }
        
        // Performance warning if frame time exceeds budget
        if (frameTime > 16.67f) { // Above 60 FPS budget
#if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_WARNING,
                L"[Physics] Performance warning: Frame time %.3fms exceeds 16.67ms budget", frameTime);
#endif
        }
    }
    
    physics.Cleanup();
}
```

#### Advanced Performance Profiler
```cpp
class PhysicsProfiler
{
private:
    struct ProfileData {
        std::string functionName;
        double totalTime;
        int callCount;
        double minTime;
        double maxTime;
        
        ProfileData() : totalTime(0.0), callCount(0), minTime(DBL_MAX), maxTime(0.0) {}
        
        double GetAverageTime() const {
            return callCount > 0 ? totalTime / callCount : 0.0;
        }
    };
    
    std::unordered_map<std::string, ProfileData> m_profileData;
    std::chrono::high_resolution_clock::time_point m_startTime;
    std::string m_currentFunction;
    bool m_isEnabled;
    
public:
    PhysicsProfiler() : m_isEnabled(true) {}
    
    void StartProfiling(const std::string& functionName) {
        if (!m_isEnabled) return;
        
        m_currentFunction = functionName;
        m_startTime = std::chrono::high_resolution_clock::now();
    }
    
    void EndProfiling() {
        if (!m_isEnabled || m_currentFunction.empty()) return;
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - m_startTime);
        double timeMs = duration.count() / 1000.0;
        
        ProfileData& data = m_profileData[m_currentFunction];
        data.functionName = m_currentFunction;
        data.totalTime += timeMs;
        data.callCount++;
        data.minTime = std::min(data.minTime, timeMs);
        data.maxTime = std::max(data.maxTime, timeMs);
        
        m_currentFunction.clear();
    }
    
    void PrintReport() {
#if defined(_DEBUG_PHYSICS_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[Physics] Performance Profile Report:");
        debug.logLevelMessage(LogLevel::LOG_INFO, L"========================================");
        
        for (const auto& pair : m_profileData) {
            const ProfileData& data = pair.second;
            
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"Function: %s\n  Calls: %d\n  Total: %.3fms\n  Avg: %.3fms\n  Min: %.3fms\n  Max: %.3fms",
                std::wstring(data.functionName.begin(), data.functionName.end()).c_str(),
                data.callCount, data.totalTime, data.GetAverageTime(), data.minTime, data.maxTime);
        }
        
        debug.logLevelMessage(LogLevel::LOG_INFO, L"========================================");
#endif
    }
    
    void Reset() {
        m_profileData.clear();
    }
    
    void SetEnabled(bool enabled) { m_isEnabled = enabled; }
};

// Macro for easy profiling
#define PROFILE_PHYSICS_FUNCTION(profiler, name) \
    profiler.StartProfiling(name); \
    auto profileGuard = [&profiler]() { profiler.EndProfiling(); }; \
    std::unique_ptr<void, decltype(profileGuard)> guard(nullptr, profileGuard);
```

---

### 14.2 Memory Optimization

#### Memory Pool Management
```cpp
class PhysicsMemoryPool
{
private:
    struct MemoryBlock {
        void* data;
        size_t size;
        bool isUsed;
        MemoryBlock* next;
        
        MemoryBlock() : data(nullptr), size(0), isUsed(false), next(nullptr) {}
    };
    
    MemoryBlock* m_freeBlocks;
    MemoryBlock* m_usedBlocks;
    size_t m_totalAllocated;
    size_t m_totalUsed;
    std::mutex m_poolMutex;
    
public:
    PhysicsMemoryPool() : m_freeBlocks(nullptr), m_usedBlocks(nullptr), 
                         m_totalAllocated(0), m_totalUsed(0) {}
    
    ~PhysicsMemoryPool() {
        Cleanup();
    }
    
    bool Initialize(size_t initialSize = 1024 * 1024) { // 1MB default
        std::lock_guard<std::mutex> lock(m_poolMutex);
        
        try {
            // Allocate initial memory block
            void* memory = malloc(initialSize);
            if (!memory) {
#if defined(_DEBUG_PHYSICS_)
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Failed to allocate memory pool");
#endif
                return false;
            }
            
            MemoryBlock* block = new MemoryBlock();
            block->data = memory;
            block->size = initialSize;
            block->isUsed = false;
            block->next = nullptr;
            
            m_freeBlocks = block;
            m_totalAllocated = initialSize;
            
#if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"[Physics] Memory pool initialized with %zu bytes", initialSize);
#endif
            
            return true;
        }
        catch (const std::exception& e) {
            std::string errorMsg = e.what();
            std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
            
#if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, 
                L"[Physics] Memory pool initialization failed: " + wErrorMsg);
#endif
            return false;
        }
    }
    
    void* Allocate(size_t size) {
        std::lock_guard<std::mutex> lock(m_poolMutex);
        
        // Find suitable free block
        MemoryBlock* current = m_freeBlocks;
        MemoryBlock* previous = nullptr;
        
        while (current) {
            if (!current->isUsed && current->size >= size) {
                // Found suitable block
                current->isUsed = true;
                
                // Remove from free list
                if (previous) {
                    previous->next = current->next;
                } else {
                    m_freeBlocks = current->next;
                }
                
                // Add to used list
                current->next = m_usedBlocks;
                m_usedBlocks = current;
                
                m_totalUsed += current->size;
                
                return current->data;
            }
            
            previous = current;
            current = current->next;
        }
        
        // No suitable block found - allocate new one
        return AllocateNewBlock(size);
    }
    
    void Deallocate(void* ptr) {
        if (!ptr) return;
        
        std::lock_guard<std::mutex> lock(m_poolMutex);
        
        // Find block in used list
        MemoryBlock* current = m_usedBlocks;
        MemoryBlock* previous = nullptr;
        
        while (current) {
            if (current->data == ptr) {
                current->isUsed = false;
                
                // Remove from used list
                if (previous) {
                    previous->next = current->next;
                } else {
                    m_usedBlocks = current->next;
                }
                
                // Add to free list
                current->next = m_freeBlocks;
                m_freeBlocks = current;
                
                m_totalUsed -= current->size;
                return;
            }
            
            previous = current;
            current = current->next;
        }
        
#if defined(_DEBUG_PHYSICS_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[Physics] Attempted to deallocate unknown pointer");
#endif
    }
    
    void GetMemoryStats(size_t& totalAllocated, size_t& totalUsed, size_t& totalFree) {
        std::lock_guard<std::mutex> lock(m_poolMutex);
        totalAllocated = m_totalAllocated;
        totalUsed = m_totalUsed;
        totalFree = m_totalAllocated - m_totalUsed;
    }
    
private:
    void* AllocateNewBlock(size_t size) {
        try {
            size_t blockSize = std::max(size, static_cast<size_t>(64 * 1024)); // Min 64KB
            void* memory = malloc(blockSize);
            
            if (!memory) {
                return nullptr;
            }
            
            MemoryBlock* block = new MemoryBlock();
            block->data = memory;
            block->size = blockSize;
            block->isUsed = true;
            block->next = m_usedBlocks;
            
            m_usedBlocks = block;
            m_totalAllocated += blockSize;
            m_totalUsed += blockSize;
            
            return memory;
        }
        catch (...) {
            return nullptr;
        }
    }
    
    void Cleanup() {
        std::lock_guard<std::mutex> lock(m_poolMutex);
        
        // Free all blocks
        MemoryBlock* current = m_freeBlocks;
        while (current) {
            MemoryBlock* next = current->next;
            free(current->data);
            delete current;
            current = next;
        }
        
        current = m_usedBlocks;
        while (current) {
            MemoryBlock* next = current->next;
            free(current->data);
            delete current;
            current = next;
        }
        
        m_freeBlocks = nullptr;
        m_usedBlocks = nullptr;
        m_totalAllocated = 0;
        m_totalUsed = 0;
    }
};
```

---

### 14.3 Spatial Optimization

#### Spatial Hash Grid for Collision Detection
```cpp
class SpatialHashGrid
{
private:
    struct GridCell {
        std::vector<PhysicsBody*> bodies;
        
        void Clear() { bodies.clear(); }
        void AddBody(PhysicsBody* body) { bodies.push_back(body); }
        size_t GetBodyCount() const { return bodies.size(); }
    };
    
    std::vector<GridCell> m_grid;
    float m_cellSize;
    int m_gridWidth;
    int m_gridHeight;
    int m_gridDepth;
    PhysicsVector3D m_worldMin;
    PhysicsVector3D m_worldMax;
    
public:
    SpatialHashGrid() : m_cellSize(5.0f), m_gridWidth(64), m_gridHeight(32), m_gridDepth(64) {
        m_worldMin = PhysicsVector3D(-160.0f, -80.0f, -160.0f);
        m_worldMax = PhysicsVector3D(160.0f, 80.0f, 160.0f);
        m_grid.resize(m_gridWidth * m_gridHeight * m_gridDepth);
    }
    
    void Initialize(float cellSize, const PhysicsVector3D& worldMin, const PhysicsVector3D& worldMax) {
        m_cellSize = cellSize;
        m_worldMin = worldMin;
        m_worldMax = worldMax;
        
        PhysicsVector3D worldSize = worldMax - worldMin;
        m_gridWidth = static_cast<int>(worldSize.x / cellSize) + 1;
        m_gridHeight = static_cast<int>(worldSize.y / cellSize) + 1;
        m_gridDepth = static_cast<int>(worldSize.z / cellSize) + 1;
        
        m_grid.clear();
        m_grid.resize(m_gridWidth * m_gridHeight * m_gridDepth);
        
#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"[Physics] Spatial grid initialized: %dx%dx%d cells, cell size: %.2f",
            m_gridWidth, m_gridHeight, m_gridDepth, cellSize);
#endif
    }
    
    void Clear() {
        for (auto& cell : m_grid) {
            cell.Clear();
        }
    }
    
    void AddBody(PhysicsBody* body) {
        if (!body || !body->isActive) return;
        
        int cellIndex = GetCellIndex(body->position);
        if (cellIndex >= 0 && cellIndex < static_cast<int>(m_grid.size())) {
            m_grid[cellIndex].AddBody(body);
        }
    }
    
    std::vector<std::pair<PhysicsBody*, PhysicsBody*>> GetPotentialCollisions() {
        std::vector<std::pair<PhysicsBody*, PhysicsBody*>> collisionPairs;
        
        for (const auto& cell : m_grid) {
            if (cell.GetBodyCount() < 2) continue;
            
            // Check all pairs within cell
            for (size_t i = 0; i < cell.bodies.size(); ++i) {
                for (size_t j = i + 1; j < cell.bodies.size(); ++j) {
                    PhysicsBody* bodyA = cell.bodies[i];
                    PhysicsBody* bodyB = cell.bodies[j];
                    
                    if (bodyA && bodyB && bodyA->isActive && bodyB->isActive) {
                        collisionPairs.push_back({bodyA, bodyB});
                    }
                }
            }
        }
        
        return collisionPairs;
    }
    
    std::vector<PhysicsBody*> GetBodiesInRadius(const PhysicsVector3D& center, float radius) {
        std::vector<PhysicsBody*> nearbyBodies;
        
        // Calculate cell range
        int minX = GetCellCoord(center.x - radius, 0);
        int maxX = GetCellCoord(center.x + radius, 0);
        int minY = GetCellCoord(center.y - radius, 1);
        int maxY = GetCellCoord(center.y + radius, 1);
        int minZ = GetCellCoord(center.z - radius, 2);
        int maxZ = GetCellCoord(center.z + radius, 2);
        
        // Check cells in range
        for (int x = minX; x <= maxX; ++x) {
            for (int y = minY; y <= maxY; ++y) {
                for (int z = minZ; z <= maxZ; ++z) {
                    int cellIndex = GetCellIndex(x, y, z);
                    if (cellIndex >= 0 && cellIndex < static_cast<int>(m_grid.size())) {
                        const GridCell& cell = m_grid[cellIndex];
                        
                        for (PhysicsBody* body : cell.bodies) {
                            if (body && body->isActive) {
                                float distance = (body->position - center).Magnitude();
                                if (distance <= radius) {
                                    nearbyBodies.push_back(body);
                                }
                            }
                        }
                    }
                }
            }
        }
        
        return nearbyBodies;
    }
    
private:
    int GetCellIndex(const PhysicsVector3D& position) {
        int x = GetCellCoord(position.x, 0);
        int y = GetCellCoord(position.y, 1);
        int z = GetCellCoord(position.z, 2);
        
        return GetCellIndex(x, y, z);
    }
    
    int GetCellIndex(int x, int y, int z) {
        if (x < 0 || x >= m_gridWidth || y < 0 || y >= m_gridHeight || z < 0 || z >= m_gridDepth) {
            return -1; // Out of bounds
        }
        
        return x * m_gridHeight * m_gridDepth + y * m_gridDepth + z;
    }
    
    int GetCellCoord(float worldCoord, int axis) {
        float minCoord = (axis == 0) ? m_worldMin.x : (axis == 1) ? m_worldMin.y : m_worldMin.z;
        int coord = static_cast<int>((worldCoord - minCoord) / m_cellSize);
        
        int maxCoord = (axis == 0) ? m_gridWidth - 1 : (axis == 1) ? m_gridHeight - 1 : m_gridDepth - 1;
        return std::clamp(coord, 0, maxCoord);
    }
};
```

---

### 14.4 Debug Visualization System

#### Comprehensive Debug Renderer
```cpp
class PhysicsDebugRenderer
{
private:
    Physics* m_physics;
    std::vector<PhysicsVector3D> m_debugLines;
    std::vector<PhysicsVector3D> m_debugSpheres;
    std::vector<PhysicsVector3D> m_debugBoxes;
    bool m_showCollisionBoxes;
    bool m_showVelocityVectors;
    bool m_showForceVectors;
    bool m_showContactPoints;
    bool m_showSpatialGrid;
    
public:
    PhysicsDebugRenderer(Physics* physics) : m_physics(physics),
        m_showCollisionBoxes(false), m_showVelocityVectors(false),
        m_showForceVectors(false), m_showContactPoints(false),
        m_showSpatialGrid(false) {}
    
    void SetDebugFlags(bool collisionBoxes, bool velocityVectors, bool forceVectors, 
                      bool contactPoints, bool spatialGrid) {
        m_showCollisionBoxes = collisionBoxes;
        m_showVelocityVectors = velocityVectors;
        m_showForceVectors = forceVectors;
        m_showContactPoints = contactPoints;
        m_showSpatialGrid = spatialGrid;
        
#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"[Physics] Debug flags set - Boxes: %d, Velocity: %d, Forces: %d, Contacts: %d, Grid: %d",
            collisionBoxes, velocityVectors, forceVectors, contactPoints, spatialGrid);
#endif
    }
    
    void UpdateDebugVisualization(const std::vector<PhysicsBody>& bodies, 
                                 const SpatialHashGrid& spatialGrid) {
        // Clear previous debug data
        m_debugLines.clear();
        m_debugSpheres.clear();
        m_debugBoxes.clear();
        
        // Generate debug geometry for active bodies
        for (const auto& body : bodies) {
            if (!body.isActive) continue;
            
            // Show collision boundaries
            if (m_showCollisionBoxes) {
                AddDebugSphere(body.position, 0.5f); // Assume 0.5m radius
            }
            
            // Show velocity vectors
            if (m_showVelocityVectors && body.velocity.Magnitude() > 0.1f) {
                PhysicsVector3D velocityEnd = body.position + body.velocity;
                AddDebugLine(body.position, velocityEnd);
            }
            
            // Show acceleration/force vectors
            if (m_showForceVectors && body.acceleration.Magnitude() > 0.1f) {
                PhysicsVector3D forceEnd = body.position + body.acceleration * 0.1f; // Scale for visibility
                AddDebugLine(body.position, forceEnd);
            }
        }
        
        // Show spatial grid
        if (m_showSpatialGrid) {
            DrawSpatialGrid();
        }
        
        // Get physics debug lines
        std::vector<PhysicsVector3D> physicsDebugLines = m_physics->GetDebugLines();
        for (const auto& line : physicsDebugLines) {
            m_debugLines.push_back(line);
        }
        
#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"[Physics] Debug visualization updated - Lines: %zu, Spheres: %zu, Boxes: %zu",
            m_debugLines.size() / 2, m_debugSpheres.size(), m_debugBoxes.size());
#endif
    }
    
    void AddDebugLine(const PhysicsVector3D& start, const PhysicsVector3D& end) {
        m_debugLines.push_back(start);
        m_debugLines.push_back(end);
    }
    
    void AddDebugSphere(const PhysicsVector3D& center, float radius) {
        m_debugSpheres.push_back(center);
        m_debugSpheres.push_back(PhysicsVector3D(radius, 0.0f, 0.0f)); // Store radius in x component
    }
    
    void AddDebugBox(const PhysicsVector3D& center, const PhysicsVector3D& size) {
        m_debugBoxes.push_back(center);
        m_debugBoxes.push_back(size);
    }
    
    // Getters for rendering system
    const std::vector<PhysicsVector3D>& GetDebugLines() const { return m_debugLines; }
    const std::vector<PhysicsVector3D>& GetDebugSpheres() const { return m_debugSpheres; }
    const std::vector<PhysicsVector3D>& GetDebugBoxes() const { return m_debugBoxes; }
    
private:
    void DrawSpatialGrid() {
        // Draw spatial grid boundaries (simplified)
        PhysicsVector3D gridMin(-160.0f, -80.0f, -160.0f);
        PhysicsVector3D gridMax(160.0f, 80.0f, 160.0f);
        float cellSize = 5.0f;
        
        // Draw vertical grid lines
        for (float x = gridMin.x; x <= gridMax.x; x += cellSize * 4) { // Every 4th line for clarity
            for (float z = gridMin.z; z <= gridMax.z; z += cellSize * 4) {
                AddDebugLine(PhysicsVector3D(x, gridMin.y, z), PhysicsVector3D(x, gridMax.y, z));
            }
        }
        
        // Draw horizontal grid lines
        for (float y = gridMin.y; y <= gridMax.y; y += cellSize * 2) { // Every 2nd level
            for (float x = gridMin.x; x <= gridMax.x; x += cellSize * 4) {
                AddDebugLine(PhysicsVector3D(x, y, gridMin.z), PhysicsVector3D(x, y, gridMax.z));
            }
            for (float z = gridMin.z; z <= gridMax.z; z += cellSize * 4) {
                AddDebugLine(PhysicsVector3D(gridMin.x, y, z), PhysicsVector3D(gridMax.x, y, z));
            }
        }
    }
};
```

---

### 14.5 LOD (Level of Detail) Implementation

#### Physics LOD System
```cpp
class PhysicsLODManager
{
private:
    struct LODLevel {
        float distance;
        float updateFrequency;    // Hz
        bool enableCollisions;
        bool enableConstraints;
        int maxParticles;
        
        LODLevel(float dist, float freq, bool collisions, bool constraints, int particles)
            : distance(dist), updateFrequency(freq), enableCollisions(collisions),
              enableConstraints(constraints), maxParticles(particles) {}
    };
    
    std::vector<LODLevel> m_lodLevels;
    PhysicsVector3D m_cameraPosition;
    float m_frameTime;
    std::unordered_map<PhysicsBody*, float> m_lastUpdateTimes;
    
public:
    PhysicsLODManager() : m_frameTime(0.016f) {
        // Initialize LOD levels
        m_lodLevels = {
            LODLevel(0.0f, 60.0f, true, true, 1000),    // High detail: 0-20m
            LODLevel(20.0f, 30.0f, true, false, 500),   // Medium detail: 20-50m
            LODLevel(50.0f, 10.0f, false, false, 100),  // Low detail: 50-100m
            LODLevel(100.0f, 2.0f, false, false, 0)     // Minimal detail: 100m+
        };
        
#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[Physics] LOD system initialized with %zu levels", m_lodLevels.size());
#endif
    }
    
    void SetCameraPosition(const PhysicsVector3D& position) {
        m_cameraPosition = position;
    }
    
    int GetLODLevel(PhysicsBody* body) {
        if (!body || !body->isActive) return -1;
        
        float distance = (body->position - m_cameraPosition).Magnitude();
        
        for (int i = static_cast<int>(m_lodLevels.size()) - 1; i >= 0; --i) {
            if (distance >= m_lodLevels[i].distance) {
                return i;
            }
        }
        
        return 0; // Highest detail
    }
    
    bool ShouldUpdateBody(PhysicsBody* body, float currentTime) {
        int lodLevel = GetLODLevel(body);
        if (lodLevel < 0) return false;
        
        const LODLevel& lod = m_lodLevels[lodLevel];
        float updateInterval = 1.0f / lod.updateFrequency;
        
        auto it = m_lastUpdateTimes.find(body);
        if (it == m_lastUpdateTimes.end()) {
            m_lastUpdateTimes[body] = currentTime;
            return true;
        }
        
        if (currentTime - it->second >= updateInterval) {
            it->second = currentTime;
            return true;
        }
        
        return false;
    }
    
    void OptimizePhysicsUpdate(std::vector<PhysicsBody>& bodies, float currentTime, float deltaTime) {
        int highDetailCount = 0;
        int mediumDetailCount = 0;
        int lowDetailCount = 0;
        int culledCount = 0;
        
        for (auto& body : bodies) {
            if (!body.isActive) continue;
            
            int lodLevel = GetLODLevel(&body);
            
            switch (lodLevel) {
                case 0: // High detail
                    highDetailCount++;
                    // Full physics update
                    break;
                    
                case 1: // Medium detail
                    mediumDetailCount++;
                    if (ShouldUpdateBody(&body, currentTime)) {
                        // Reduced frequency update
                        // body.IntegrateVelocity(deltaTime * 2.0f); // Compensate for reduced frequency
                        // body.IntegratePosition(deltaTime * 2.0f);
                    }
                    break;
                    
                case 2: // Low detail
                    lowDetailCount++;
                    if (ShouldUpdateBody(&body, currentTime)) {
                        // Basic integration only
                        body.position += body.velocity * (deltaTime * 5.0f); // Compensate for 10Hz update
                        body.velocity *= 0.99f; // Simple damping
                    }
                    break;
                    
                case 3: // Minimal detail
                    culledCount++;
                    // No physics update - freeze or use simple interpolation
                    break;
                    
                default:
                    break;
            }
        }
        
#if defined(_DEBUG_PHYSICS_)
        static int frameCounter = 0;
        if (++frameCounter % 60 == 0) { // Log every second
            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"[Physics] LOD Stats - High: %d, Medium: %d, Low: %d, Culled: %d",
                highDetailCount, mediumDetailCount, lowDetailCount, culledCount);
        }
#endif
    }
    
    void CleanupUnusedEntries() {
        // Remove entries for inactive bodies
        for (auto it = m_lastUpdateTimes.begin(); it != m_lastUpdateTimes.end();) {
            if (!it->first || !it->first->isActive) {
                it = m_lastUpdateTimes.erase(it);
            } else {
                ++it;
            }
        }
    }
};
```

---

### 14.6 Error Handling and Exception Safety

#### Robust Error Handling System
```cpp
class PhysicsErrorHandler
{
private:
    ExceptionHandler& m_exceptionHandler;
    std::vector<std::string> m_errorLog;
    std::mutex m_errorMutex;
    int m_errorCount;
    int m_warningCount;
    
public:
    PhysicsErrorHandler() : m_exceptionHandler(ExceptionHandler::GetInstance()), 
                           m_errorCount(0), m_warningCount(0) {}
    
    void HandlePhysicsError(const std::string& operation, const std::string& details) {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        
        m_errorCount++;
        std::string errorMsg = "Physics Error in " + operation + ": " + details;
        m_errorLog.push_back(errorMsg);
        
#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[Physics] %s", 
                             std::wstring(errorMsg.begin(), errorMsg.end()).c_str());
#endif
        
        // Record with exception handler
        m_exceptionHandler.LogCustomException(errorMsg.c_str(), operation.c_str());
    }
    
    void HandlePhysicsWarning(const std::string& operation, const std::string& details) {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        
        m_warningCount++;
        std::string warningMsg = "Physics Warning in " + operation + ": " + details;
        m_errorLog.push_back(warningMsg);
        
#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_WARNING, L"[Physics] %s", 
                             std::wstring(warningMsg.begin(), warningMsg.end()).c_str());
#endif
    }
    
    void ValidatePhysicsBody(const PhysicsBody& body, const std::string& context) {
        try {
            // Check for NaN values
            if (std::isnan(body.position.x) || std::isnan(body.position.y) || std::isnan(body.position.z)) {
                HandlePhysicsError(context, "Body position contains NaN values");
                return;
            }
            
            if (std::isnan(body.velocity.x) || std::isnan(body.velocity.y) || std::isnan(body.velocity.z)) {
                HandlePhysicsError(context, "Body velocity contains NaN values");
                return;
            }
            
            // Check for infinite values
            if (std::isinf(body.position.x) || std::isinf(body.position.y) || std::isinf(body.position.z)) {
                HandlePhysicsError(context, "Body position contains infinite values");
                return;
            }
            
            // Check for reasonable ranges
            float positionMagnitude = body.position.Magnitude();
            if (positionMagnitude > 10000.0f) {
                HandlePhysicsWarning(context, "Body position is very far from origin: " + std::to_string(positionMagnitude));
            }
            
            float velocityMagnitude = body.velocity.Magnitude();
            if (velocityMagnitude > 1000.0f) {
                HandlePhysicsWarning(context, "Body velocity is very high: " + std::to_string(velocityMagnitude));
            }
            
            // Check mass validity
            if (body.mass <= 0.0f && !body.isStatic) {
                HandlePhysicsError(context, "Dynamic body has non-positive mass: " + std::to_string(body.mass));
            }
            
        } catch (const std::exception& e) {
            HandlePhysicsError(context, "Exception during body validation: " + std::string(e.what()));
        }
    }
    
    void GetErrorStatistics(int& errorCount, int& warningCount) const {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        errorCount = m_errorCount;
        warningCount = m_warningCount;
    }
    
    void PrintErrorReport() const {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        
#if defined(_DEBUG_PHYSICS_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[Physics] Error Report Summary:");
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Total Errors: %d, Total Warnings: %d", m_errorCount, m_warningCount);
        
        if (!m_errorLog.empty()) {
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Recent Messages:");
            size_t start = m_errorLog.size() > 10 ? m_errorLog.size() - 10 : 0;
            for (size_t i = start; i < m_errorLog.size(); ++i) {
                debug.logDebugMessage(LogLevel::LOG_INFO, L"  %s", 
                                     std::wstring(m_errorLog[i].begin(), m_errorLog[i].end()).c_str());
            }
        }
#endif
    }
    
    void ClearErrors() {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_errorLog.clear();
        m_errorCount = 0;
        m_warningCount = 0;
    }
};
```

---

### 14.7 Complete Performance Optimization Example

#### Optimized Physics System Integration
```cpp
void ComprehensivePerformanceOptimization()
{
    // Initialize all optimization systems
    Physics physics;
    PhysicsProfiler profiler;
    PhysicsMemoryPool memoryPool;
    SpatialHashGrid spatialGrid;
    PhysicsLODManager lodManager;
    PhysicsDebugRenderer debugRenderer(&physics);
    PhysicsErrorHandler errorHandler;
    
    if (!physics.Initialize()) {
        errorHandler.HandlePhysicsError("Initialization", "Failed to initialize physics system");
        return;
    }
    
    if (!memoryPool.Initialize(2048 * 1024)) { // 2MB pool
        errorHandler.HandlePhysicsError("Memory", "Failed to initialize memory pool");
        return;
    }
    
    spatialGrid.Initialize(5.0f, PhysicsVector3D(-200.0f, -50.0f, -200.0f), 
                          PhysicsVector3D(200.0f, 50.0f, 200.0f));
    
    // Create test scenario with many bodies
    std::vector<PhysicsBody> bodies;
    bodies.reserve(1000);
    
    for (int i = 0; i < 1000; ++i) {
        PhysicsBody body;
        body.position = PhysicsVector3D(
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 300.0f,
            static_cast<float>(rand()) / RAND_MAX * 20.0f + 5.0f,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 300.0f
        );
        body.velocity = PhysicsVector3D(
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 10.0f,
            0.0f,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 10.0f
        );
        body.SetMass(0.5f + static_cast<float>(rand()) / RAND_MAX * 2.0f);
        body.isActive = true;
        
        bodies.push_back(body);
    }
    
    // Camera for LOD calculations
    PhysicsVector3D cameraPosition(0.0f, 10.0f, -50.0f);
    lodManager.SetCameraPosition(cameraPosition);
    
    // Debug visualization setup
    debugRenderer.SetDebugFlags(true, true, false, false, false); // Show boxes and velocity
    
    // Main simulation loop
    float deltaTime = 0.016f; // 60 FPS target
    float currentTime = 0.0f;
    
    for (int frame = 0; frame < 1800; ++frame) { // 30 seconds
        PROFILE_PHYSICS_FUNCTION(profiler, "MainUpdate");
        
        currentTime = frame * deltaTime;
        
        try {
            // Update camera position (moving in circle)
            cameraPosition.x = cos(currentTime * 0.1f) * 80.0f;
            cameraPosition.z = sin(currentTime * 0.1f) * 80.0f + 20.0f;
            lodManager.SetCameraPosition(cameraPosition);
            
            // Clear and populate spatial grid
            {
                PROFILE_PHYSICS_FUNCTION(profiler, "SpatialGrid");
                spatialGrid.Clear();
                for (auto& body : bodies) {
                    if (body.isActive) {
                        spatialGrid.AddBody(&body);
                        errorHandler.ValidatePhysicsBody(body, "SpatialGrid");
                    }
                }
            }
            
            // LOD-based physics update
            {
                PROFILE_PHYSICS_FUNCTION(profiler, "LODUpdate");
                lodManager.OptimizePhysicsUpdate(bodies, currentTime, deltaTime);
            }
            
            // Optimized collision detection
            {
                PROFILE_PHYSICS_FUNCTION(profiler, "CollisionDetection");
                auto collisionPairs = spatialGrid.GetPotentialCollisions();
                
                for (const auto& pair : collisionPairs) {
                    // Simple distance check
                    float distance = (pair.first->position - pair.second->position).Magnitude();
                    if (distance < 2.0f) { // Collision threshold
                        // Handle collision
                        PhysicsVector3D direction = (pair.second->position - pair.first->position).Normalized();
                        pair.first->velocity -= direction * 0.5f;
                        pair.second->velocity += direction * 0.5f;
                    }
                }
            }
            
            // Apply environmental forces
            {
                PROFILE_PHYSICS_FUNCTION(profiler, "EnvironmentalForces");
                PhysicsVector3D gravity(0.0f, -9.81f, 0.0f);
                PhysicsVector3D wind(sin(currentTime * 0.3f) * 2.0f, 0.0f, cos(currentTime * 0.5f) * 1.5f);
                
                for (auto& body : bodies) {
                    if (body.isActive) {
                        body.acceleration += gravity + wind * 0.1f;
                    }
                }
            }
            
            // Update debug visualization every 4 frames
            if (frame % 4 == 0) {
                PROFILE_PHYSICS_FUNCTION(profiler, "DebugVisualization");
                debugRenderer.UpdateDebugVisualization(bodies, spatialGrid);
            }
            
            // Memory management
            if (frame % 300 == 0) { // Every 5 seconds
                lodManager.CleanupUnusedEntries();
                
                size_t totalMem, usedMem, freeMem;
                memoryPool.GetMemoryStats(totalMem, usedMem, freeMem);
                
#if defined(_DEBUG_PHYSICS_)
                debug.logDebugMessage(LogLevel::LOG_INFO,
                    L"[Physics] Memory - Total: %zu KB, Used: %zu KB, Free: %zu KB",
                    totalMem / 1024, usedMem / 1024, freeMem / 1024);
#endif
            }
            
        } catch (const std::exception& e) {
            errorHandler.HandlePhysicsError("MainLoop", e.what());
        }
        
        // Performance reporting every second
        if (frame % 60 == 0 && frame > 0) {
            profiler.PrintReport();
            profiler.Reset();
            
            int errorCount, warningCount;
            errorHandler.GetErrorStatistics(errorCount, warningCount);
            
            if (errorCount > 0 || warningCount > 0) {
                errorHandler.PrintErrorReport();
            }
        }
    }
    
    // Final cleanup and reporting
    physics.Cleanup();
    
#if defined(_DEBUG_PHYSICS_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[Physics] Performance optimization test completed successfully");
#endif
}
```

---

### Best Practices for Performance Optimization

1. **Profile First**: Always measure before optimizing to identify real bottlenecks
2. **Memory Management**: Use object pools and minimize allocations in update loops
3. **Spatial Partitioning**: Implement spatial data structures for collision detection
4. **LOD Systems**: Reduce computation complexity based on importance/distance
5. **Error Handling**: Implement robust error detection and recovery systems

### Common Performance Issues and Solutions

- **Frame Rate Drops**: Too many active objects → Implement LOD and culling systems
- **Memory Fragmentation**: Frequent allocations → Use memory pools and object recycling
- **Collision Bottlenecks**: O(n²) collision checks → Implement spatial partitioning
- **Precision Errors**: Accumulating floating-point errors → Validate and clamp values regularly
- **Thread Safety**: Race conditions in multi-threaded code → Use proper synchronization

---

**Chapter 14 Complete**


## Chapter 15: Advanced Integration Techniques

### Overview
The Physics system provides advanced integration capabilities for seamless interaction with rendering engines, audio systems, AI frameworks, and networking architectures. This chapter covers multi-threaded physics, engine integration patterns, and cross-system communication.

### Key Features
- Multi-threaded physics simulation
- Rendering engine integration patterns
- Audio system synchronization
- AI pathfinding integration
- Network physics synchronization
- Cross-platform compatibility
- Optimized data exchange using base set files

---

### 15.1 Multi-Threading Integration

#### Thread-Safe Physics Update
```cpp
#include "Physics.h"
#include "Debug.h"
#include "ExceptionHandler.h"
#include "MathPrecalculation.h"

class ThreadedPhysicsSystem
{
private:
    Physics* m_physics;
    std::thread m_physicsThread;
    std::atomic<bool> m_isRunning;
    std::atomic<bool> m_isPaused;
    std::mutex m_dataExchangeMutex;
    std::condition_variable m_updateCondition;
    
    // Double buffered data for thread safety
    struct PhysicsState {
        std::vector<PhysicsVector3D> positions;
        std::vector<PhysicsVector3D> velocities;
        std::vector<bool> activeStates;
        float deltaTime;
        int frameNumber;
    };
    
    PhysicsState m_frontBuffer;
    PhysicsState m_backBuffer;
    std::atomic<bool> m_bufferSwapReady;
    
public:
    ThreadedPhysicsSystem() : m_physics(nullptr), m_isRunning(false), m_isPaused(false), 
                             m_bufferSwapReady(false) {}
    
    bool Initialize(int maxBodies = 1000) {
        m_physics = new Physics();
        
        if (!m_physics->Initialize()) {
#if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ThreadedPhysics] Failed to initialize physics system");
#endif
            return false;
        }
        
        // Initialize buffers
        m_frontBuffer.positions.reserve(maxBodies);
        m_frontBuffer.velocities.reserve(maxBodies);
        m_frontBuffer.activeStates.reserve(maxBodies);
        
        m_backBuffer.positions.reserve(maxBodies);
        m_backBuffer.velocities.reserve(maxBodies);
        m_backBuffer.activeStates.reserve(maxBodies);
        
        m_isRunning = true;
        m_physicsThread = std::thread(&ThreadedPhysicsSystem::PhysicsThreadFunction, this);
        
#if defined(_DEBUG_PHYSICS_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[ThreadedPhysics] Initialized with multi-threading support");
#endif
        
        return true;
    }
    
    void Shutdown() {
        m_isRunning = false;
        m_updateCondition.notify_all();
        
        if (m_physicsThread.joinable()) {
            m_physicsThread.join();
        }
        
        if (m_physics) {
            m_physics->Cleanup();
            delete m_physics;
            m_physics = nullptr;
        }
        
#if defined(_DEBUG_PHYSICS_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[ThreadedPhysics] Shutdown completed");
#endif
    }
    
    void UpdateMainThread(float deltaTime) {
        // Check if physics thread has new data ready
        if (m_bufferSwapReady.load()) {
            std::lock_guard<std::mutex> lock(m_dataExchangeMutex);
            
            // Swap buffers
            std::swap(m_frontBuffer, m_backBuffer);
            m_bufferSwapReady = false;
            
            // Notify physics thread that new data is available
            m_updateCondition.notify_one();
        }
        
        // Main thread can now safely read from front buffer
        // Use m_frontBuffer.positions, velocities, etc. for rendering
    }
    
    void SetPaused(bool paused) {
        m_isPaused = paused;
        if (!paused) {
            m_updateCondition.notify_one();
        }
    }
    
    // Thread-safe data access for main thread
    std::vector<PhysicsVector3D> GetPositions() const {
        std::lock_guard<std::mutex> lock(m_dataExchangeMutex);
        return m_frontBuffer.positions;
    }
    
    void AddForce(int bodyIndex, const PhysicsVector3D& force) {
        // Queue force application for physics thread
        // Implementation would use a thread-safe command queue
    }
    
private:
    void PhysicsThreadFunction() {
        const float targetDeltaTime = 1.0f / 60.0f; // 60 Hz physics
        
        while (m_isRunning) {
            try {
                if (m_isPaused) {
                    std::unique_lock<std::mutex> lock(m_dataExchangeMutex);
                    m_updateCondition.wait(lock, [this] { return !m_isPaused || !m_isRunning; });
                    continue;
                }
                
                auto startTime = std::chrono::high_resolution_clock::now();
                
                // Physics update
                m_physics->Update(targetDeltaTime);
                
                // Prepare back buffer with new data
                {
                    std::lock_guard<std::mutex> lock(m_dataExchangeMutex);
                    
                    // Copy physics state to back buffer
                    // m_backBuffer.positions = GetCurrentPositions();
                    // m_backBuffer.velocities = GetCurrentVelocities();
                    m_backBuffer.frameNumber++;
                    
                    m_bufferSwapReady = true;
                }
                
                // Maintain consistent timing
                auto endTime = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
                auto sleepTime = std::chrono::microseconds(static_cast<long>(targetDeltaTime * 1000000)) - duration;
                
                if (sleepTime > std::chrono::microseconds(0)) {
                    std::this_thread::sleep_for(sleepTime);
                }
                
            } catch (const std::exception& e) {
                ExceptionHandler::GetInstance().LogException(e, "ThreadedPhysics::PhysicsThreadFunction");
                
#if defined(_DEBUG_PHYSICS_)
                debug.logDebugMessage(LogLevel::LOG_ERROR, 
                    L"[ThreadedPhysics] Exception in physics thread: %s", 
                    std::wstring(e.what(), e.what() + strlen(e.what())).c_str());
#endif
            }
        }
    }
};
```

---

### 15.2 Rendering Engine Integration

#### Graphics System Bridge
```cpp
struct RenderablePhysicsObject
{
    PhysicsVector3D position;
    PhysicsVector3D rotation;
    PhysicsVector3D scale;
    int renderObjectID;
    bool isVisible;
    float transparency;
    
    RenderablePhysicsObject() : scale(1.0f, 1.0f, 1.0f), renderObjectID(-1), 
                               isVisible(true), transparency(1.0f) {}
};

class PhysicsRenderBridge
{
private:
    Physics* m_physics;
    std::vector<RenderablePhysicsObject> m_renderObjects;
    std::unordered_map<int, int> m_physicsToRenderMap; // Physics body index to render object index
    
public:
    PhysicsRenderBridge(Physics* physics) : m_physics(physics) {}
    
    int CreateRenderablePhysicsObject(const PhysicsBody& physicsBody, int renderObjectID) {
        RenderablePhysicsObject renderObj;
        renderObj.position = physicsBody.position;
        renderObj.renderObjectID = renderObjectID;
        renderObj.isVisible = physicsBody.isActive;
        
        int index = static_cast<int>(m_renderObjects.size());
        m_renderObjects.push_back(renderObj);
        
        // Map physics body to render object (simplified - would need proper ID management)
        m_physicsToRenderMap[renderObjectID] = index;
        
#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"[PhysicsRenderBridge] Created renderable object %d at position (%.2f, %.2f, %.2f)",
            index, renderObj.position.x, renderObj.position.y, renderObj.position.z);
#endif
        
        return index;
    }
    
    void UpdateRenderTransforms(const std::vector<PhysicsBody>& physicsBodies) {
        for (size_t i = 0; i < physicsBodies.size() && i < m_renderObjects.size(); ++i) {
            const PhysicsBody& physicsBody = physicsBodies[i];
            RenderablePhysicsObject& renderObj = m_renderObjects[i];
            
            if (physicsBody.isActive) {
                // Update position
                renderObj.position = physicsBody.position;
                
                // Calculate rotation from velocity (for objects like projectiles)
                if (physicsBody.velocity.Magnitude() > 0.1f) {
                    PhysicsVector3D forward = physicsBody.velocity.Normalized();
                    PhysicsVector3D up(0.0f, 1.0f, 0.0f);
                    PhysicsVector3D right = forward.Cross(up).Normalized();
                    up = right.Cross(forward);
                    
                    // Convert to Euler angles (simplified)
                    renderObj.rotation.y = atan2(forward.x, forward.z);
                    renderObj.rotation.x = asin(-forward.y);
                }
                
                renderObj.isVisible = true;
            } else {
                renderObj.isVisible = false;
            }
        }
    }
    
    void UpdateParticleEffects(const std::vector<PhysicsParticle>& particles) {
        // Convert physics particles to render data
        for (size_t i = 0; i < particles.size(); ++i) {
            const PhysicsParticle& particle = particles[i];
            
            if (particle.isActive && i < m_renderObjects.size()) {
                RenderablePhysicsObject& renderObj = m_renderObjects[i];
                
                renderObj.position = particle.position;
                renderObj.transparency = particle.life / 5.0f; // Fade based on lifetime
                renderObj.scale = PhysicsVector3D(0.1f, 0.1f, 0.1f) * (particle.life + 0.5f);
                renderObj.isVisible = true;
            }
        }
    }
    
    // Interface for rendering system
    const std::vector<RenderablePhysicsObject>& GetRenderableObjects() const {
        return m_renderObjects;
    }
    
    void SetObjectVisibility(int objectIndex, bool visible) {
        if (objectIndex >= 0 && objectIndex < static_cast<int>(m_renderObjects.size())) {
            m_renderObjects[objectIndex].isVisible = visible;
        }
    }
    
    void SetObjectTransparency(int objectIndex, float transparency) {
        if (objectIndex >= 0 && objectIndex < static_cast<int>(m_renderObjects.size())) {
            m_renderObjects[objectIndex].transparency = std::clamp(transparency, 0.0f, 1.0f);
        }
    }
};
```

---

### 15.3 Audio System Integration

#### 3D Audio Physics Bridge
```cpp
class PhysicsAudioBridge
{
private:
    Physics* m_physics;
    
    struct AudioEmitter {
        PhysicsVector3D position;
        PhysicsVector3D velocity;
        float baseVolume;
        float basePitch;
        float currentVolume;
        float currentPitch;
        bool isActive;
        int audioSourceID;
    };
    
    std::vector<AudioEmitter> m_audioEmitters;
    PhysicsVector3D m_listenerPosition;
    PhysicsVector3D m_listenerVelocity;
    
public:
    PhysicsAudioBridge(Physics* physics) : m_physics(physics), 
        m_listenerPosition(0.0f, 1.8f, 0.0f), m_listenerVelocity(0.0f, 0.0f, 0.0f) {}
    
    int CreateAudioEmitter(const PhysicsVector3D& position, int audioSourceID, 
                          float volume = 1.0f, float pitch = 1.0f) {
        AudioEmitter emitter;
        emitter.position = position;
        emitter.velocity = PhysicsVector3D(0.0f, 0.0f, 0.0f);
        emitter.baseVolume = volume;
        emitter.basePitch = pitch;
        emitter.currentVolume = volume;
        emitter.currentPitch = pitch;
        emitter.isActive = true;
        emitter.audioSourceID = audioSourceID;
        
        int index = static_cast<int>(m_audioEmitters.size());
        m_audioEmitters.push_back(emitter);
        
#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"[PhysicsAudioBridge] Created audio emitter %d at position (%.2f, %.2f, %.2f)",
            index, position.x, position.y, position.z);
#endif
        
        return index;
    }
    
    void UpdateListener(const PhysicsVector3D& position, const PhysicsVector3D& velocity) {
        m_listenerPosition = position;
        m_listenerVelocity = velocity;
    }
    
    void UpdateAudioPhysics(float deltaTime) {
        for (auto& emitter : m_audioEmitters) {
            if (!emitter.isActive) continue;
            
            try {
                // Calculate 3D audio properties
                AudioPhysicsData audioData = m_physics->CalculateAudioPhysics(
                    m_listenerPosition, emitter.position, emitter.velocity
                );
                
                // Apply distance attenuation
                emitter.currentVolume = emitter.baseVolume * audioData.volumeFalloff;
                
                // Apply Doppler effect
                emitter.currentPitch = emitter.basePitch * audioData.dopplerShift;
                
                // Apply occlusion (simplified - would need obstacle data)
                emitter.currentVolume *= (1.0f - audioData.occlusion);
                
                // Update audio system (pseudo-code interface)
                // AudioSystem::SetSourceVolume(emitter.audioSourceID, emitter.currentVolume);
                // AudioSystem::SetSourcePitch(emitter.audioSourceID, emitter.currentPitch);
                // AudioSystem::SetSource3DPosition(emitter.audioSourceID, emitter.position);
                
            } catch (const std::exception& e) {
                ExceptionHandler::GetInstance().LogException(e, "PhysicsAudioBridge::UpdateAudioPhysics");
                
#if defined(_DEBUG_PHYSICS_)
                debug.logDebugMessage(LogLevel::LOG_ERROR, 
                    L"[PhysicsAudioBridge] Error updating audio emitter: %s",
                    std::wstring(e.what(), e.what() + strlen(e.what())).c_str());
#endif
            }
        }
    }
    
    void AttachEmitterToPhysicsBody(int emitterIndex, const PhysicsBody& physicsBody) {
        if (emitterIndex >= 0 && emitterIndex < static_cast<int>(m_audioEmitters.size())) {
            AudioEmitter& emitter = m_audioEmitters[emitterIndex];
            emitter.position = physicsBody.position;
            emitter.velocity = physicsBody.velocity;
            emitter.isActive = physicsBody.isActive;
        }
    }
    
    void CreateExplosionAudio(const PhysicsVector3D& position, float intensity) {
        // Create temporary audio emitter for explosion
        int emitterID = CreateAudioEmitter(position, -1, intensity, 1.0f);
        
        // Schedule removal after explosion sound duration
        // ScheduleEmitterRemoval(emitterID, 3.0f);
        
#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"[PhysicsAudioBridge] Created explosion audio at (%.2f, %.2f, %.2f) with intensity %.2f",
            position.x, position.y, position.z, intensity);
#endif
    }
    
    void SetEmitterActive(int emitterIndex, bool active) {
        if (emitterIndex >= 0 && emitterIndex < static_cast<int>(m_audioEmitters.size())) {
            m_audioEmitters[emitterIndex].isActive = active;
        }
    }
};
```

---

### 15.4 AI System Integration

#### AI Pathfinding Physics Integration
```cpp
struct AIPhysicsAgent
{
    PhysicsBody physicsBody;
    PhysicsVector3D targetPosition;
    PhysicsVector3D pathDirection;
    float movementSpeed;
    float rotationSpeed;
    float avoidanceRadius;
    bool hasTarget;
    bool isPathfinding;
    
    AIPhysicsAgent() : movementSpeed(5.0f), rotationSpeed(2.0f), avoidanceRadius(2.0f),
                      hasTarget(false), isPathfinding(false) {}
};

class AIPhysicsIntegration
{
private:
    Physics* m_physics;
    std::vector<AIPhysicsAgent> m_agents;
    
public:
    AIPhysicsIntegration(Physics* physics) : m_physics(physics) {}
    
    int CreateAIAgent(const PhysicsVector3D& startPosition, float speed = 5.0f) {
        AIPhysicsAgent agent;
        agent.physicsBody.position = startPosition;
        agent.physicsBody.SetMass(70.0f); // Average human mass
        agent.physicsBody.drag = 8.0f;    // High drag for responsive movement
        agent.physicsBody.isActive = true;
        agent.movementSpeed = speed;
        
        int index = static_cast<int>(m_agents.size());
        m_agents.push_back(agent);
        
#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"[AIPhysicsIntegration] Created AI agent %d at position (%.2f, %.2f, %.2f)",
            index, startPosition.x, startPosition.y, startPosition.z);
#endif
        
        return index;
    }
    
    void SetAgentTarget(int agentIndex, const PhysicsVector3D& target) {
        if (agentIndex >= 0 && agentIndex < static_cast<int>(m_agents.size())) {
            AIPhysicsAgent& agent = m_agents[agentIndex];
            agent.targetPosition = target;
            agent.hasTarget = true;
            agent.isPathfinding = true;
            
            // Calculate initial direction
            PhysicsVector3D direction = target - agent.physicsBody.position;
            direction.y = 0.0f; // Keep movement on ground plane
            
            if (direction.Magnitude() > 0.1f) {
                agent.pathDirection = direction.Normalized();
            }
        }
    }
    
    void UpdateAI(float deltaTime) {
        for (size_t i = 0; i < m_agents.size(); ++i) {
            AIPhysicsAgent& agent = m_agents[i];
            
            if (!agent.physicsBody.isActive || !agent.hasTarget) continue;
            
            try {
                // Check if reached target
                PhysicsVector3D toTarget = agent.targetPosition - agent.physicsBody.position;
                toTarget.y = 0.0f; // Ignore height difference
                float distanceToTarget = toTarget.Magnitude();
                
                if (distanceToTarget < 1.0f) {
                    // Reached target
                    agent.hasTarget = false;
                    agent.isPathfinding = false;
                    agent.physicsBody.velocity = PhysicsVector3D(0.0f, agent.physicsBody.velocity.y, 0.0f);
                    continue;
                }
                
                // Update path direction
                agent.pathDirection = toTarget.Normalized();
                
                // Obstacle avoidance
                PhysicsVector3D avoidanceForce = CalculateAvoidanceForce(i);
                PhysicsVector3D finalDirection = agent.pathDirection + avoidanceForce * 2.0f;
                
                if (finalDirection.Magnitude() > 0.1f) {
                    finalDirection = finalDirection.Normalized();
                }
                
                // Apply movement force
                PhysicsVector3D movementForce = finalDirection * agent.movementSpeed * agent.physicsBody.mass;
                agent.physicsBody.ApplyForce(movementForce);
                
                // Apply physics constraints (ground collision, etc.)
                ApplyMovementConstraints(agent);
                
                // Update physics
                m_physics->ApplyNewtonianMotion(agent.physicsBody, PhysicsVector3D(0.0f, 0.0f, 0.0f), deltaTime);
                
            } catch (const std::exception& e) {
                ExceptionHandler::GetInstance().LogException(e, "AIPhysicsIntegration::UpdateAI");
            }
        }
    }
    
    std::vector<PhysicsVector3D> GetAgentPositions() const {
        std::vector<PhysicsVector3D> positions;
        positions.reserve(m_agents.size());
        
        for (const auto& agent : m_agents) {
            positions.push_back(agent.physicsBody.position);
        }
        
        return positions;
    }
    
    void SetAgentActive(int agentIndex, bool active) {
        if (agentIndex >= 0 && agentIndex < static_cast<int>(m_agents.size())) {
            m_agents[agentIndex].physicsBody.isActive = active;
        }
    }
    
private:
    PhysicsVector3D CalculateAvoidanceForce(int agentIndex) {
        const AIPhysicsAgent& currentAgent = m_agents[agentIndex];
        PhysicsVector3D avoidanceForce(0.0f, 0.0f, 0.0f);
        
        // Check against other agents
        for (size_t i = 0; i < m_agents.size(); ++i) {
            if (i == static_cast<size_t>(agentIndex)) continue;
            
            const AIPhysicsAgent& otherAgent = m_agents[i];
            if (!otherAgent.physicsBody.isActive) continue;
            
            PhysicsVector3D toOther = otherAgent.physicsBody.position - currentAgent.physicsBody.position;
            toOther.y = 0.0f; // Ground plane only
            
            float distance = toOther.Magnitude();
            if (distance < currentAgent.avoidanceRadius && distance > 0.1f) {
                // Apply repulsion force
                PhysicsVector3D repulsion = toOther.Normalized() * -1.0f;
                float forceStrength = (currentAgent.avoidanceRadius - distance) / currentAgent.avoidanceRadius;
                avoidanceForce += repulsion * forceStrength;
            }
        }
        
        return avoidanceForce;
    }
    
    void ApplyMovementConstraints(AIPhysicsAgent& agent) {
        // Ground constraint
        if (agent.physicsBody.position.y < 0.0f) {
            agent.physicsBody.position.y = 0.0f;
            agent.physicsBody.velocity.y = 0.0f;
        }
        
        // Speed limiting
        PhysicsVector3D horizontalVelocity = agent.physicsBody.velocity;
        horizontalVelocity.y = 0.0f;
        
        if (horizontalVelocity.Magnitude() > agent.movementSpeed) {
            horizontalVelocity = horizontalVelocity.Normalized() * agent.movementSpeed;
            agent.physicsBody.velocity = PhysicsVector3D(horizontalVelocity.x, agent.physicsBody.velocity.y, horizontalVelocity.z);
        }
    }
};
```

---

### 15.5 Network Physics Synchronization

#### Network Physics Bridge
```cpp
struct NetworkPhysicsPacket
{
    int objectID;
    PhysicsVector3D position;
    PhysicsVector3D velocity;
    float timestamp;
    int frameNumber;
    
    NetworkPhysicsPacket() : objectID(-1), timestamp(0.0f), frameNumber(0) {}
};

class NetworkPhysicsSync
{
private:
    Physics* m_physics;
    std::unordered_map<int, PhysicsBody> m_networkedBodies;
    std::queue<NetworkPhysicsPacket> m_incomingPackets;
    std::queue<NetworkPhysicsPacket> m_outgoingPackets;
    std::mutex m_networkMutex;
    
    float m_currentTime;
    int m_frameNumber;
    bool m_isServer;
    
public:
    NetworkPhysicsSync(Physics* physics, bool isServer = false) 
        : m_physics(physics), m_currentTime(0.0f), m_frameNumber(0), m_isServer(isServer) {}
    
    void RegisterNetworkedObject(int objectID, const PhysicsBody& initialState) {
        std::lock_guard<std::mutex> lock(m_networkMutex);
        
        m_networkedBodies[objectID] = initialState;
        
#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"[NetworkPhysicsSync] Registered networked object %d", objectID);
#endif
    }
    
    void UpdateNetworkPhysics(float deltaTime) {
        m_currentTime += deltaTime;
        m_frameNumber++;
        
        // Process incoming network updates
        ProcessIncomingPackets();
        
        if (m_isServer) {
            // Server: Update physics and broadcast state
            UpdateServerPhysics(deltaTime);
            BroadcastPhysicsState();
        } else {
            // Client: Apply network corrections and predict
            ApplyNetworkCorrections();
            PredictClientPhysics(deltaTime);
        }
    }
    
    void ReceiveNetworkPacket(const NetworkPhysicsPacket& packet) {
        std::lock_guard<std::mutex> lock(m_networkMutex);
        m_incomingPackets.push(packet);
    }
    
    std::vector<NetworkPhysicsPacket> GetOutgoingPackets() {
        std::lock_guard<std::mutex> lock(m_networkMutex);
        
        std::vector<NetworkPhysicsPacket> packets;
        while (!m_outgoingPackets.empty()) {
            packets.push_back(m_outgoingPackets.front());
            m_outgoingPackets.pop();
        }
        
        return packets;
    }
    
private:
    void ProcessIncomingPackets() {
        std::lock_guard<std::mutex> lock(m_networkMutex);
        
        while (!m_incomingPackets.empty()) {
            const NetworkPhysicsPacket& packet = m_incomingPackets.front();
            
            auto it = m_networkedBodies.find(packet.objectID);
            if (it != m_networkedBodies.end()) {
                // Apply lag compensation
                float latency = m_currentTime - packet.timestamp;
                
                // Extrapolate position based on latency
                PhysicsVector3D correctedPosition = packet.position + packet.velocity * latency;
                
                // Apply network correction with smoothing
                PhysicsVector3D positionError = correctedPosition - it->second.position;
                float errorMagnitude = positionError.Magnitude();
                
                if (errorMagnitude > 0.1f) { // Significant error
                    // Apply correction over time
                    it->second.position += positionError * 0.3f; // 30% correction per frame
                    it->second.velocity = packet.velocity;
                    
#if defined(_DEBUG_PHYSICS_)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                        L"[NetworkPhysicsSync] Applied correction to object %d, error: %.3f",
                        packet.objectID, errorMagnitude);
#endif
                }
            }
            
            m_incomingPackets.pop();
        }
    }
    
    void UpdateServerPhysics(float deltaTime) {
        // Server authoritative physics update
        for (auto& pair : m_networkedBodies) {
            PhysicsBody& body = pair.second;
            
            if (body.isActive) {
                // Apply server-side physics
                m_physics->ApplyNewtonianMotion(body, PhysicsVector3D(0.0f, -9.81f * body.mass, 0.0f), deltaTime);
            }
        }
    }
    
    void BroadcastPhysicsState() {
        if (m_frameNumber % 3 != 0) return; // Broadcast every 3rd frame (20 Hz)
        
        std::lock_guard<std::mutex> lock(m_networkMutex);
        
        for (const auto& pair : m_networkedBodies) {
            const PhysicsBody& body = pair.second;
            
            if (body.isActive) {
                NetworkPhysicsPacket packet;
                packet.objectID = pair.first;
                packet.position = body.position;
                packet.velocity = body.velocity;
                packet.timestamp = m_currentTime;
                packet.frameNumber = m_frameNumber;
                
                m_outgoingPackets.push(packet);
            }
        }
    }
    
    void ApplyNetworkCorrections() {
        // Client-side network correction logic
        // Already handled in ProcessIncomingPackets()
    }
    
    void PredictClientPhysics(float deltaTime) {
        // Client-side prediction for responsive feel
        for (auto& pair : m_networkedBodies) {
            PhysicsBody& body = pair.second;
            
            if (body.isActive) {
                // Simple prediction - apply basic physics
                body.position += body.velocity * deltaTime;
                body.velocity.y -= 9.81f * deltaTime; // Gravity
                body.velocity *= 0.99f; // Simple drag
            }
        }
    }
};
```

---

### 15.6 Cross-Platform Integration

#### Platform Abstraction Layer
```cpp
class PlatformPhysicsAdapter
{
private:
    Physics* m_physics;
    
    struct PlatformConfig {
        float performanceScale;     // Performance scaling factor
        int maxParticles;          // Platform-specific limits
        bool useOptimizedMath;     // Use platform-specific optimizations
        bool supportMultiThreading; // Threading support
        float memoryBudget;        // Memory budget in MB
    };
    
    PlatformConfig m_config;
    
public:
    PlatformPhysicsAdapter(Physics* physics) : m_physics(physics) {
        DetectPlatform();
    }
    
    void DetectPlatform() {
#ifdef _WIN32
        ConfigureForWindows();
#elif defined(__ANDROID__)
        ConfigureForAndroid();
#elif defined(__APPLE__)
        #if TARGET_OS_IOS
            ConfigureForIOS();
        #else
            ConfigureForMacOS();
        #endif
#elif defined(__linux__)
        ConfigureForLinux();
#else
        ConfigureDefault();
#endif
        
#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"[PlatformAdapter] Configured for platform - Performance: %.2f, Max Particles: %d, Memory: %.1fMB",
            m_config.performanceScale, m_config.maxParticles, m_config.memoryBudget);
#endif
    }
    
    void OptimizeForPlatform() {
        try {
            // Apply platform-specific optimizations
            if (m_config.useOptimizedMath) {
                // Enable optimized math functions
                MathPrecalculation::GetInstance().Initialize();
            }
            
            // Set performance scaling
            if (m_config.performanceScale < 1.0f) {
                // Reduce quality for lower-end platforms
                ReducePhysicsQuality();
            }
            
            // Configure memory usage
            ConfigureMemoryUsage();
            
        } catch (const std::exception& e) {
            ExceptionHandler::GetInstance().LogException(e, "PlatformPhysicsAdapter::OptimizeForPlatform");
        }
    }
    
    float GetPlatformDeltaTime(float requestedDeltaTime) const {
        // Adjust delta time based on platform performance
        return requestedDeltaTime * m_config.performanceScale;
    }
    
    int GetMaxAllowedParticles() const {
        return m_config.maxParticles;
    }
    
private:
    void ConfigureForWindows() {
        m_config.performanceScale = 1.0f;
        m_config.maxParticles = 10000;
        m_config.useOptimizedMath = true;
        m_config.supportMultiThreading = true;
        m_config.memoryBudget = 512.0f; // 512MB
    }
    
    void ConfigureForAndroid() {
        m_config.performanceScale = 0.7f; // Reduced performance
        m_config.maxParticles = 2000;
        m_config.useOptimizedMath = true;
        m_config.supportMultiThreading = false; // Avoid threading on mobile
        m_config.memoryBudget = 128.0f; // 128MB
    }
    
    void ConfigureForIOS() {
        m_config.performanceScale = 0.8f;
        m_config.maxParticles = 3000;
        m_config.useOptimizedMath = true;
        m_config.supportMultiThreading = false;
        m_config.memoryBudget = 256.0f; // 256MB
    }
    
    void ConfigureForMacOS() {
        m_config.performanceScale = 0.9f;
        m_config.maxParticles = 8000;
        m_config.useOptimizedMath = true;
        m_config.supportMultiThreading = true;
        m_config.memoryBudget = 512.0f; // 512MB
    }
    
    void ConfigureForLinux() {
        m_config.performanceScale = 1.0f;
        m_config.maxParticles = 10000;
        m_config.useOptimizedMath = true;
        m_config.supportMultiThreading = true;
        m_config.memoryBudget = 512.0f; // 512MB
    }
    
    void ConfigureDefault() {
        m_config.performanceScale = 0.5f; // Conservative default
        m_config.maxParticles = 1000;
        m_config.useOptimizedMath = false;
        m_config.supportMultiThreading = false;
        m_config.memoryBudget = 64.0f; // 64MB
    }
    
    void ReducePhysicsQuality() {
        // Platform-specific quality reduction
        // Reduce collision detection precision
        // Lower update frequency for distant objects
        // Simplify particle systems
        
#if defined(_DEBUG_PHYSICS_)
        debug.logLevelMessage(LogLevel::LOG_INFO, 
            L"[PlatformAdapter] Applied quality reduction for performance");
#endif
    }
    
    void ConfigureMemoryUsage() {
        // Configure memory pools based on platform budget
        size_t memoryBytes = static_cast<size_t>(m_config.memoryBudget * 1024 * 1024);
        
        // Memory configuration would be applied here
        // Example: SetPhysicsMemoryBudget(memoryBytes);
    }
};
```

---

### 15.7 Complete Integration Example

#### Unified System Integration
```cpp
class UnifiedPhysicsIntegration
{
private:
    // Core systems
    Physics m_physics;
    ThreadedPhysicsSystem m_threadedPhysics;
    
    // Integration bridges
    PhysicsRenderBridge m_renderBridge;
    PhysicsAudioBridge m_audioBridge;
    AIPhysicsIntegration m_aiIntegration;
    NetworkPhysicsSync m_networkSync;
    PlatformPhysicsAdapter m_platformAdapter;
    
    // Debug and profiling
    PhysicsDebugRenderer m_debugRenderer;
    
    bool m_isInitialized;
    bool m_useMultiThreading;
    
public:
    UnifiedPhysicsIntegration() : m_renderBridge(&m_physics), m_audioBridge(&m_physics),
                                 m_aiIntegration(&m_physics), m_networkSync(&m_physics),
                                 m_platformAdapter(&m_physics), m_debugRenderer(&m_physics),
                                 m_isInitialized(false), m_useMultiThreading(false) {}
    
    bool Initialize(bool enableMultiThreading = false) {
        try {
            // Initialize core physics
            if (!m_physics.Initialize()) {
#if defined(_DEBUG_PHYSICS_)
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[UnifiedIntegration] Failed to initialize core physics");
#endif
                return false;
            }
            
            // Platform optimization
            m_platformAdapter.OptimizeForPlatform();
            
            // Initialize threading if supported and requested
            m_useMultiThreading = enableMultiThreading;
            if (m_useMultiThreading) {
                if (!m_threadedPhysics.Initialize()) {
#if defined(_DEBUG_PHYSICS_)
                    debug.logLevelMessage(LogLevel::LOG_WARNING, 
                        L"[UnifiedIntegration] Failed to initialize threading, using single-threaded mode");
#endif
                    m_useMultiThreading = false;
                }
            }
            
            // Setup debug visualization
            m_debugRenderer.SetDebugFlags(true, true, false, true, false);
            
            m_isInitialized = true;
            
#if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"[UnifiedIntegration] Initialized successfully - Threading: %s", 
                m_useMultiThreading ? L"Enabled" : L"Disabled");
#endif
            
            return true;
            
        } catch (const std::exception& e) {
            ExceptionHandler::GetInstance().LogException(e, "UnifiedPhysicsIntegration::Initialize");
            return false;
        }
    }
    
    void Shutdown() {
        if (!m_isInitialized) return;
        
        try {
            if (m_useMultiThreading) {
                m_threadedPhysics.Shutdown();
            }
            
            m_physics.Cleanup();
            m_isInitialized = false;
            
#if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[UnifiedIntegration] Shutdown completed");
#endif
            
        } catch (const std::exception& e) {
            ExceptionHandler::GetInstance().LogException(e, "UnifiedPhysicsIntegration::Shutdown");
        }
    }
    
    void Update(float deltaTime) {
        if (!m_isInitialized) return;
        
        try {
            // Adjust delta time for platform
            float adjustedDeltaTime = m_platformAdapter.GetPlatformDeltaTime(deltaTime);
            
            // Update physics (threaded or single-threaded)
            if (m_useMultiThreading) {
                m_threadedPhysics.UpdateMainThread(adjustedDeltaTime);
            } else {
                m_physics.Update(adjustedDeltaTime);
            }
            
            // Update integration systems
            UpdateIntegrationSystems(adjustedDeltaTime);
            
        } catch (const std::exception& e) {
            ExceptionHandler::GetInstance().LogException(e, "UnifiedPhysicsIntegration::Update");
        }
    }
    
    // Interface methods for different systems
    int CreatePhysicsObject(const PhysicsVector3D& position, float mass = 1.0f, int renderID = -1, int audioID = -1) {
        PhysicsBody body;
        body.position = position;
        body.SetMass(mass);
        body.isActive = true;
        
        // Add to render bridge if render ID provided
        if (renderID >= 0) {
            m_renderBridge.CreateRenderablePhysicsObject(body, renderID);
        }
        
        // Add to audio bridge if audio ID provided
        if (audioID >= 0) {
            m_audioBridge.CreateAudioEmitter(position, audioID);
        }
        
        return 0; // Would return actual body ID in real implementation
    }
    
    int CreateAIAgent(const PhysicsVector3D& position, float speed = 5.0f) {
        return m_aiIntegration.CreateAIAgent(position, speed);
    }
    
    void SetAITarget(int agentID, const PhysicsVector3D& target) {
        m_aiIntegration.SetAgentTarget(agentID, target);
    }
    
    void UpdateListenerPosition(const PhysicsVector3D& position, const PhysicsVector3D& velocity) {
        m_audioBridge.UpdateListener(position, velocity);
    }
    
    void CreateExplosion(const PhysicsVector3D& position, float force, int particleCount = 100) {
        // Create physics explosion
        std::vector<PhysicsParticle> particles = m_physics.CreateExplosion(position, particleCount, force, 3.0f);
        
        // Create audio effect
        m_audioBridge.CreateExplosionAudio(position, force * 0.1f);
        
        // Update render system with particles
        m_renderBridge.UpdateParticleEffects(particles);
        
#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"[UnifiedIntegration] Created explosion at (%.2f, %.2f, %.2f) with %d particles",
            position.x, position.y, position.z, particleCount);
#endif
    }
    
    void EnableDebugVisualization(bool enable) {
        m_debugRenderer.SetDebugFlags(enable, enable, enable, enable, enable);
    }
    
    // Network interface
    void RegisterNetworkedObject(int objectID, const PhysicsBody& body) {
        m_networkSync.RegisterNetworkedObject(objectID, body);
    }
    
    void ProcessNetworkPacket(const NetworkPhysicsPacket& packet) {
        m_networkSync.ReceiveNetworkPacket(packet);
    }
    
    // Data access for external systems
    const std::vector<RenderablePhysicsObject>& GetRenderableObjects() const {
        return m_renderBridge.GetRenderableObjects();
    }
    
    std::vector<PhysicsVector3D> GetAIAgentPositions() const {
        return m_aiIntegration.GetAgentPositions();
    }
    
private:
    void UpdateIntegrationSystems(float deltaTime) {
        // Update AI system
        m_aiIntegration.UpdateAI(deltaTime);
        
        // Update audio physics
        m_audioBridge.UpdateAudioPhysics(deltaTime);
        
        // Update network synchronization
        m_networkSync.UpdateNetworkPhysics(deltaTime);
        
        // Update render transforms (get current physics bodies)
        std::vector<PhysicsBody> currentBodies; // Would get from physics system
        m_renderBridge.UpdateRenderTransforms(currentBodies);
        
        // Update debug visualization every few frames
        static int debugUpdateCounter = 0;
        if (++debugUpdateCounter % 4 == 0) {
            // m_debugRenderer.UpdateDebugVisualization(currentBodies, spatialGrid);
        }
    }
};
```

---

### 15.8 Usage Example

#### Complete Integration Demonstration
```cpp
void DemonstrateUnifiedIntegration()
{
    UnifiedPhysicsIntegration integration;
    
    // Initialize with platform detection and optimization
    if (!integration.Initialize(true)) { // Enable multi-threading
        return;
    }
    
    try {
        // Create physics objects with integrated systems
        int player = integration.CreatePhysicsObject(PhysicsVector3D(0.0f, 2.0f, 0.0f), 70.0f, 1, 1);
        int obstacle = integration.CreatePhysicsObject(PhysicsVector3D(10.0f, 1.0f, 0.0f), 100.0f, 2, -1);
        
        // Create AI agents
        int agent1 = integration.CreateAIAgent(PhysicsVector3D(-10.0f, 0.0f, 0.0f), 3.0f);
        int agent2 = integration.CreateAIAgent(PhysicsVector3D(5.0f, 0.0f, 10.0f), 4.0f);
        
        // Set AI targets
        integration.SetAITarget(agent1, PhysicsVector3D(15.0f, 0.0f, 5.0f));
        integration.SetAITarget(agent2, PhysicsVector3D(-5.0f, 0.0f, -8.0f));
        
        // Update listener for 3D audio
        integration.UpdateListenerPosition(PhysicsVector3D(0.0f, 1.8f, -5.0f), PhysicsVector3D(0.0f, 0.0f, 0.0f));
        
        // Enable debug visualization
        integration.EnableDebugVisualization(true);
        
        // Main simulation loop
        float deltaTime = 0.016f; // 60 FPS
        
        for (int frame = 0; frame < 600; ++frame) { // 10 seconds
            // Update unified system
            integration.Update(deltaTime);
            
            // Create explosion every 3 seconds
            if (frame % 180 == 60) {
                PhysicsVector3D explosionPos(
                    (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 20.0f,
                    2.0f,
                    (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 20.0f
                );
                integration.CreateExplosion(explosionPos, 25.0f, 150);
            }
            
            // Get data for rendering system
            const auto& renderObjects = integration.GetRenderableObjects();
            
            // Render objects (pseudo-code)
            // for (const auto& obj : renderObjects) {
            //     if (obj.isVisible) {
            //         RenderSystem::DrawObject(obj.renderObjectID, obj.position, obj.rotation, obj.scale);
            //     }
            // }
            
            // Get AI positions for game logic
            auto aiPositions = integration.GetAIAgentPositions();
            
            // Process AI positions (pseudo-code)
            // for (const auto& pos : aiPositions) {
            //     GameLogic::UpdateAIState(pos);
            // }
        }
        
#if defined(_DEBUG_PHYSICS_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[Integration] Demonstration completed successfully");
#endif
        
    } catch (const std::exception& e) {
        ExceptionHandler::GetInstance().LogException(e, "DemonstrateUnifiedIntegration");
    }
    
    // Cleanup
    integration.Shutdown();
}
```

---

### Best Practices for Advanced Integration

1. **Thread Safety**: Always use proper synchronization when sharing data between systems
2. **Platform Optimization**: Detect platform capabilities and optimize accordingly
3. **Error Handling**: Implement robust error handling at integration boundaries
4. **Data Exchange**: Use efficient data structures for cross-system communication
5. **Performance Monitoring**: Profile integration overhead and optimize hot paths

### Common Integration Issues and Solutions

- **Threading Deadlocks**: Inconsistent lock ordering → Use consistent locking hierarchy
- **Data Synchronization**: Race conditions → Implement proper double buffering
- **Performance Bottlenecks**: Excessive data copying → Use reference-based data sharing
- **Platform Incompatibility**: System-specific features → Implement platform abstraction layers
- **Memory Leaks**: Cross-system ownership issues → Use smart pointers and clear ownership rules

---

**Chapter 15 Complete**


## Chapter 16: Best Practices and Common Pitfalls

### Overview
This chapter covers essential best practices for using the Physics system effectively, common pitfalls to avoid, and proven solutions for typical physics implementation challenges. Learn from real-world scenarios and maintain robust, performant physics simulations.

### Key Areas Covered
- Code organization and architecture patterns
- Performance optimization strategies
- Common implementation mistakes
- Debugging and troubleshooting techniques
- Memory management best practices
- Thread safety considerations

---

### 16.1 Code Organization Best Practices

#### Proper Physics System Architecture
```cpp
#include "Physics.h"
#include "Debug.h"
#include "ExceptionHandler.h"
#include "MathPrecalculation.h"

// ✅ GOOD: Centralized physics manager with clear responsibilities
class PhysicsManager
{
private:
    Physics m_physics;
    std::vector<PhysicsBody> m_dynamicBodies;
    std::vector<PhysicsBody> m_staticBodies;
    std::unordered_map<int, size_t> m_bodyIDMap;
    
    int m_nextBodyID;
    bool m_isInitialized;
    
public:
    PhysicsManager() : m_nextBodyID(0), m_isInitialized(false) {}
    
    bool Initialize() {
        if (m_isInitialized) {
#if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[PhysicsManager] Already initialized");
#endif
            return true;
        }
        
        try {
            if (!m_physics.Initialize()) {
#if defined(_DEBUG_PHYSICS_)
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PhysicsManager] Failed to initialize physics system");
#endif
                return false;
            }
            
            // Reserve memory for better performance
            m_dynamicBodies.reserve(1000);
            m_staticBodies.reserve(500);
            
            m_isInitialized = true;
            
#if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[PhysicsManager] Initialized successfully");
#endif
            
            return true;
            
        } catch (const std::exception& e) {
            ExceptionHandler::GetInstance().LogException(e, "PhysicsManager::Initialize");
            return false;
        }
    }
    
    int CreateDynamicBody(const PhysicsVector3D& position, float mass = 1.0f) {
        if (!m_isInitialized) {
#if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PhysicsManager] Not initialized");
#endif
            return -1;
        }
        
        try {
            PhysicsBody body;
            body.position = position;
            body.SetMass(mass);
            body.isStatic = false;
            body.isActive = true;
            
            int bodyID = m_nextBodyID++;
            m_bodyIDMap[bodyID] = m_dynamicBodies.size();
            m_dynamicBodies.push_back(body);
            
#if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"[PhysicsManager] Created dynamic body %d at (%.2f, %.2f, %.2f)",
                bodyID, position.x, position.y, position.z);
#endif
            
            return bodyID;
            
        } catch (const std::exception& e) {
            ExceptionHandler::GetInstance().LogException(e, "PhysicsManager::CreateDynamicBody");
            return -1;
        }
    }
    
    void Update(float deltaTime) {
        if (!m_isInitialized) return;
        
        try {
            RECORD_FUNCTION_CALL(); // Using ExceptionHandler macro
            
            // Validate delta time
            if (deltaTime <= 0.0f || deltaTime > 0.1f) {
#if defined(_DEBUG_PHYSICS_)
                debug.logDebugMessage(LogLevel::LOG_WARNING, 
                    L"[PhysicsManager] Invalid delta time: %.6f", deltaTime);
#endif
                deltaTime = 0.016f; // Fallback to 60 FPS
            }
            
            m_physics.Update(deltaTime);
            
        } catch (const std::exception& e) {
            ExceptionHandler::GetInstance().LogException(e, "PhysicsManager::Update");
        }
    }
    
    PhysicsBody* GetBody(int bodyID) {
        auto it = m_bodyIDMap.find(bodyID);
        if (it != m_bodyIDMap.end()) {
            size_t index = it->second;
            if (index < m_dynamicBodies.size()) {
                return &m_dynamicBodies[index];
            }
        }
        return nullptr;
    }
    
    void Cleanup() {
        if (!m_isInitialized) return;
        
        m_physics.Cleanup();
        m_dynamicBodies.clear();
        m_staticBodies.clear();
        m_bodyIDMap.clear();
        m_isInitialized = false;
        
#if defined(_DEBUG_PHYSICS_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[PhysicsManager] Cleanup completed");
#endif
    }
};

// ❌ BAD: Scattered physics code without proper organization
void BadPhysicsExample() {
    Physics physics; // No error checking
    physics.Initialize(); // No error handling
    
    PhysicsBody body; // No validation
    body.position = PhysicsVector3D(0, 0, 0);
    
    physics.Update(0.016f); // Magic number, no validation
    // No cleanup, no error handling
}
```

#### Proper Error Handling Patterns
```cpp
// ✅ GOOD: Comprehensive error handling with fallbacks
class RobustPhysicsSystem
{
private:
    Physics m_physics;
    bool m_hasRecoverableError;
    int m_consecutiveErrors;
    static const int MAX_CONSECUTIVE_ERRORS = 5;
    
public:
    bool SafeUpdate(float deltaTime) {
        try {
            // Validate input
            if (deltaTime <= 0.0f || deltaTime > 1.0f) {
#if defined(_DEBUG_PHYSICS_)
                debug.logDebugMessage(LogLevel::LOG_WARNING, 
                    L"[RobustPhysics] Clamping invalid delta time: %.6f", deltaTime);
#endif
                deltaTime = std::clamp(deltaTime, 0.001f, 0.1f);
            }
            
            m_physics.Update(deltaTime);
            
            // Reset error counter on successful update
            m_consecutiveErrors = 0;
            m_hasRecoverableError = false;
            
            return true;
            
        } catch (const std::runtime_error& e) {
            // Handle recoverable errors
            m_consecutiveErrors++;
            m_hasRecoverableError = true;
            
            ExceptionHandler::GetInstance().LogException(e, "RobustPhysicsSystem::SafeUpdate");
            
#if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, 
                L"[RobustPhysics] Recoverable error #%d: %s", 
                m_consecutiveErrors, std::wstring(e.what(), e.what() + strlen(e.what())).c_str());
#endif
            
            // Attempt recovery
            if (m_consecutiveErrors < MAX_CONSECUTIVE_ERRORS) {
                AttemptRecovery();
                return false; // Skip this frame
            } else {
                // Too many errors - enter safe mode
                EnterSafeMode();
                return false;
            }
            
        } catch (const std::exception& e) {
            // Handle critical errors
            ExceptionHandler::GetInstance().LogException(e, "RobustPhysicsSystem::SafeUpdate");
            
#if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, 
                L"[RobustPhysics] Critical error - entering safe mode");
#endif
            
            EnterSafeMode();
            return false;
        }
    }
    
private:
    void AttemptRecovery() {
        try {
            // Reset any corrupted state
            // m_physics.ResetCorruptedState();
            
#if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[RobustPhysics] Attempted recovery");
#endif
            
        } catch (...) {
            EnterSafeMode();
        }
    }
    
    void EnterSafeMode() {
        // Disable complex physics, use simplified calculations
        m_hasRecoverableError = false;
        m_consecutiveErrors = 0;
        
#if defined(_DEBUG_PHYSICS_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[RobustPhysics] Entered safe mode");
#endif
    }
};

// ❌ BAD: No error handling
void BadErrorHandling() {
    Physics physics;
    physics.Initialize(); // What if this fails?
    
    physics.Update(-0.5f); // Invalid delta time, no validation
    // No try-catch, system could crash
}
```

---

### 16.2 Performance Best Practices

#### Efficient Memory Usage
```cpp
// ✅ GOOD: Object pooling for frequent allocations
class PhysicsObjectPool
{
private:
    std::vector<PhysicsParticle> m_particlePool;
    std::vector<bool> m_particleInUse;
    std::queue<size_t> m_availableParticles;
    
    std::vector<PhysicsBody> m_bodyPool;
    std::vector<bool> m_bodyInUse;
    std::queue<size_t> m_availableBodies;
    
public:
    bool Initialize(size_t maxParticles = 5000, size_t maxBodies = 1000) {
        try {
            // Pre-allocate pools
            m_particlePool.resize(maxParticles);
            m_particleInUse.resize(maxParticles, false);
            
            m_bodyPool.resize(maxBodies);
            m_bodyInUse.resize(maxBodies, false);
            
            // Fill available queues
            for (size_t i = 0; i < maxParticles; ++i) {
                m_availableParticles.push(i);
            }
            
            for (size_t i = 0; i < maxBodies; ++i) {
                m_availableBodies.push(i);
            }
            
#if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"[ObjectPool] Initialized with %zu particles, %zu bodies", maxParticles, maxBodies);
#endif
            
            return true;
            
        } catch (const std::exception& e) {
            ExceptionHandler::GetInstance().LogException(e, "PhysicsObjectPool::Initialize");
            return false;
        }
    }
    
    PhysicsParticle* AcquireParticle() {
        if (m_availableParticles.empty()) {
#if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[ObjectPool] Particle pool exhausted");
#endif
            return nullptr;
        }
        
        size_t index = m_availableParticles.front();
        m_availableParticles.pop();
        m_particleInUse[index] = true;
        
        // Reset particle to default state
        PhysicsParticle& particle = m_particlePool[index];
        particle = PhysicsParticle(); // Reset to default
        particle.isActive = true;
        
        return &particle;
    }
    
    void ReleaseParticle(PhysicsParticle* particle) {
        if (!particle) return;
        
        // Find particle index
        size_t index = particle - &m_particlePool[0];
        
        if (index < m_particlePool.size() && m_particleInUse[index]) {
            particle->isActive = false;
            m_particleInUse[index] = false;
            m_availableParticles.push(index);
        }
    }
    
    void UpdateAndCleanup() {
        // Automatically release dead particles
        for (size_t i = 0; i < m_particlePool.size(); ++i) {
            if (m_particleInUse[i] && !m_particlePool[i].isActive) {
                ReleaseParticle(&m_particlePool[i]);
            }
        }
    }
};

// ❌ BAD: Frequent allocations in update loop
void BadMemoryUsage() {
    for (int frame = 0; frame < 1000; ++frame) {
        // BAD: Creating new vectors every frame
        std::vector<PhysicsParticle> particles;
        particles.resize(100); // Expensive allocation
        
        // BAD: Creating temporary objects
        for (int i = 0; i < 100; ++i) {
            PhysicsParticle particle; // Stack allocation is OK, but unnecessary object creation
            // ... use particle
        } // Objects destroyed, memory fragmented
    }
}
```

#### Spatial Optimization Implementation
```cpp
// ✅ GOOD: Efficient spatial partitioning
class OptimizedSpatialGrid
{
private:
    struct GridCell {
        std::vector<PhysicsBody*> bodies;
        
        void Clear() { 
            bodies.clear(); 
            // Keep capacity to avoid reallocations
        }
        
        void Reserve(size_t count) {
            if (bodies.capacity() < count) {
                bodies.reserve(count);
            }
        }
    };
    
    std::vector<GridCell> m_cells;
    float m_cellSize;
    int m_gridWidth, m_gridHeight, m_gridDepth;
    PhysicsVector3D m_worldMin, m_worldMax;
    
    // Performance tracking
    mutable size_t m_totalBodies;
    mutable size_t m_totalCells;
    mutable size_t m_occupiedCells;
    
public:
    void Initialize(float cellSize, const PhysicsVector3D& worldMin, const PhysicsVector3D& worldMax) {
        m_cellSize = cellSize;
        m_worldMin = worldMin;
        m_worldMax = worldMax;
        
        PhysicsVector3D worldSize = worldMax - worldMin;
        m_gridWidth = static_cast<int>(worldSize.x / cellSize) + 1;
        m_gridHeight = static_cast<int>(worldSize.y / cellSize) + 1;
        m_gridDepth = static_cast<int>(worldSize.z / cellSize) + 1;
        
        m_totalCells = m_gridWidth * m_gridHeight * m_gridDepth;
        m_cells.resize(m_totalCells);
        
        // Pre-reserve cell capacity based on expected density
        size_t expectedBodiesPerCell = 4;
        for (auto& cell : m_cells) {
            cell.Reserve(expectedBodiesPerCell);
        }
        
#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"[SpatialGrid] Initialized %dx%dx%d grid, %zu total cells, cell size: %.2f",
            m_gridWidth, m_gridHeight, m_gridDepth, m_totalCells, cellSize);
#endif
    }
    
    void Update(const std::vector<PhysicsBody>& bodies) {
        RECORD_FUNCTION_CALL();
        
        // Clear all cells efficiently
        m_occupiedCells = 0;
        for (auto& cell : m_cells) {
            if (!cell.bodies.empty()) {
                cell.Clear();
                m_occupiedCells++;
            }
        }
        
        // Add bodies to appropriate cells
        m_totalBodies = 0;
        for (const auto& body : bodies) {
            if (!body.isActive) continue;
            
            int cellIndex = GetCellIndex(body.position);
            if (cellIndex >= 0 && cellIndex < static_cast<int>(m_cells.size())) {
                m_cells[cellIndex].bodies.push_back(const_cast<PhysicsBody*>(&body));
                m_totalBodies++;
            }
        }
        
#if defined(_DEBUG_PHYSICS_)
        static int debugCounter = 0;
        if (++debugCounter % 300 == 0) { // Log every 5 seconds at 60 FPS
            float occupancyRate = static_cast<float>(m_occupiedCells) / m_totalCells * 100.0f;
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"[SpatialGrid] Bodies: %zu, Occupied cells: %zu/%zu (%.1f%%)",
                m_totalBodies, m_occupiedCells, m_totalCells, occupancyRate);
        }
#endif
    }
    
private:
    int GetCellIndex(const PhysicsVector3D& position) const {
        // Inline for performance
        int x = static_cast<int>((position.x - m_worldMin.x) / m_cellSize);
        int y = static_cast<int>((position.y - m_worldMin.y) / m_cellSize);
        int z = static_cast<int>((position.z - m_worldMin.z) / m_cellSize);
        
        // Bounds checking
        if (x < 0 || x >= m_gridWidth || y < 0 || y >= m_gridHeight || z < 0 || z >= m_gridDepth) {
            return -1;
        }
        
        return x * m_gridHeight * m_gridDepth + y * m_gridDepth + z;
    }
};

// ❌ BAD: Inefficient collision detection
void BadCollisionDetection(const std::vector<PhysicsBody>& bodies) {
    // BAD: O(n²) collision detection without optimization
    for (size_t i = 0; i < bodies.size(); ++i) {
        for (size_t j = i + 1; j < bodies.size(); ++j) {
            // Check collision between every pair - very expensive!
            float distance = (bodies[i].position - bodies[j].position).Magnitude();
            // ... collision logic
        }
    }
}
```

---

### 16.3 Common Pitfalls and Solutions

#### Numerical Instability Issues
```cpp
// ✅ GOOD: Robust numerical handling
class NumericallyStablePhysics
{
private:
    static constexpr float MIN_DELTA_TIME = 0.001f;
    static constexpr float MAX_DELTA_TIME = 0.05f;
    static constexpr float MAX_VELOCITY = 1000.0f;
    static constexpr float MIN_MASS = 0.001f;
    static constexpr float MAX_POSITION = 10000.0f;
    
public:
    static bool ValidatePhysicsBody(PhysicsBody& body, const std::string& context) {
        bool isValid = true;
        
        // Check for NaN values
        if (std::isnan(body.position.x) || std::isnan(body.position.y) || std::isnan(body.position.z)) {
#if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, 
                L"[NumericalStability] NaN position detected in %s", 
                std::wstring(context.begin(), context.end()).c_str());
#endif
            body.position = PhysicsVector3D(0.0f, 0.0f, 0.0f);
            isValid = false;
        }
        
        if (std::isnan(body.velocity.x) || std::isnan(body.velocity.y) || std::isnan(body.velocity.z)) {
#if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, 
                L"[NumericalStability] NaN velocity detected in %s", 
                std::wstring(context.begin(), context.end()).c_str());
#endif
            body.velocity = PhysicsVector3D(0.0f, 0.0f, 0.0f);
            isValid = false;
        }
        
        // Check for infinite values
        if (std::isinf(body.position.Magnitude())) {
#if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[NumericalStability] Infinite position detected");
#endif
            body.position = PhysicsVector3D(0.0f, 0.0f, 0.0f);
            isValid = false;
        }
        
        // Clamp extreme values
        if (body.velocity.Magnitude() > MAX_VELOCITY) {
#if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, 
                L"[NumericalStability] Clamping excessive velocity: %.2f", body.velocity.Magnitude());
#endif
            body.velocity = body.velocity.Normalized() * MAX_VELOCITY;
        }
        
        if (body.position.Magnitude() > MAX_POSITION) {
#if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, 
                L"[NumericalStability] Clamping excessive position: %.2f", body.position.Magnitude());
#endif
            body.position = body.position.Normalized() * MAX_POSITION;
        }
        
        if (body.mass < MIN_MASS && !body.isStatic) {
#if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, 
                L"[NumericalStability] Clamping low mass: %.6f", body.mass);
#endif
            body.SetMass(MIN_MASS);
        }
        
        return isValid;
    }
    
    static float SafeDeltaTime(float requestedDeltaTime) {
        if (requestedDeltaTime <= 0.0f || std::isnan(requestedDeltaTime)) {
#if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, 
                L"[NumericalStability] Invalid delta time: %.6f, using fallback", requestedDeltaTime);
#endif
            return 0.016f; // 60 FPS fallback
        }
        
        return std::clamp(requestedDeltaTime, MIN_DELTA_TIME, MAX_DELTA_TIME);
    }
};

// ❌ BAD: No numerical validation
void BadNumericalHandling() {
    PhysicsBody body;
    body.velocity = PhysicsVector3D(1e20f, 1e20f, 1e20f); // Extreme values
    body.SetMass(0.0f); // Invalid mass
    
    float deltaTime = -0.1f; // Invalid delta time
    // No validation - will cause instability or crashes
}
```

#### Thread Safety Issues
```cpp
// ✅ GOOD: Thread-safe physics operations
class ThreadSafePhysicsWrapper
{
private:
    Physics m_physics;
    mutable std::shared_mutex m_physicsMutex;
    std::atomic<bool> m_isUpdating;
    
    // Thread-safe command queue
    struct PhysicsCommand {
        enum Type { ADD_FORCE, SET_POSITION, SET_VELOCITY };
        Type type;
        int bodyID;
        PhysicsVector3D vector;
    };
    
    std::queue<PhysicsCommand> m_commandQueue;
    std::mutex m_commandMutex;
    
public:
    ThreadSafePhysicsWrapper() : m_isUpdating(false) {}
    
    void Update(float deltaTime) {
        // Exclusive lock for physics update
        std::unique_lock<std::shared_mutex> lock(m_physicsMutex);
        m_isUpdating = true;
        
        try {
            // Process queued commands
            ProcessCommandQueue();
            
            // Update physics
            m_physics.Update(deltaTime);
            
        } catch (const std::exception& e) {
            ExceptionHandler::GetInstance().LogException(e, "ThreadSafePhysicsWrapper::Update");
        }
        
        m_isUpdating = false;
    }
    
    void AddForceThreadSafe(int bodyID, const PhysicsVector3D& force) {
        // Queue command for next update
        std::lock_guard<std::mutex> lock(m_commandMutex);
        
        PhysicsCommand cmd;
        cmd.type = PhysicsCommand::ADD_FORCE;
        cmd.bodyID = bodyID;
        cmd.vector = force;
        
        m_commandQueue.push(cmd);
    }
    
    PhysicsVector3D GetPositionThreadSafe(int bodyID) const {
        // Shared lock for reading
        std::shared_lock<std::shared_mutex> lock(m_physicsMutex);
        
        // Wait if currently updating
        while (m_isUpdating.load()) {
            std::this_thread::yield();
        }
        
        // Safe to read position
        // return m_physics.GetBodyPosition(bodyID);
        return PhysicsVector3D(); // Placeholder
    }
    
private:
    void ProcessCommandQueue() {
        std::lock_guard<std::mutex> lock(m_commandMutex);
        
        while (!m_commandQueue.empty()) {
            const PhysicsCommand& cmd = m_commandQueue.front();
            
            switch (cmd.type) {
                case PhysicsCommand::ADD_FORCE:
                    // Apply force to body
                    break;
                case PhysicsCommand::SET_POSITION:
                    // Set body position
                    break;
                case PhysicsCommand::SET_VELOCITY:
                    // Set body velocity
                    break;
            }
            
            m_commandQueue.pop();
        }
    }
};

// ❌ BAD: Race conditions and data corruption
class UnsafePhysicsExample
{
private:
    Physics m_physics;
    std::vector<PhysicsBody> m_bodies; // No synchronization
    
public:
    void UpdateFromMainThread() {
        m_physics.Update(0.016f);
        // Modifying m_bodies without locks
        for (auto& body : m_bodies) {
            body.position += body.velocity * 0.016f;
        }
    }
    
    void AddForceFromWorkerThread(int index, const PhysicsVector3D& force) {
        // BAD: Accessing m_bodies from different thread without locks
        if (index < m_bodies.size()) {
            m_bodies[index].ApplyForce(force); // Race condition!
        }
    }
};
```

---

### 16.4 Debugging and Troubleshooting

#### Comprehensive Debug System
```cpp
class PhysicsDebugSystem
{
private:
    Physics* m_physics;
    
    struct DebugSnapshot {
        float timestamp;
        std::vector<PhysicsVector3D> positions;
        std::vector<PhysicsVector3D> velocities;
        std::vector<bool> activeStates;
        int frameNumber;
    };
    
    std::deque<DebugSnapshot> m_snapshots;
    static const size_t MAX_SNAPSHOTS = 300; // 5 seconds at 60 FPS
    
    bool m_enableLogging;
    bool m_enableValidation;
    bool m_enablePerformanceTracking;
    
public:
    PhysicsDebugSystem(Physics* physics) : m_physics(physics), 
        m_enableLogging(true), m_enableValidation(true), m_enablePerformanceTracking(true) {}
    
    void CaptureSnapshot(const std::vector<PhysicsBody>& bodies, float timestamp, int frameNumber) {
        if (!m_enableLogging) return;
        
        try {
            DebugSnapshot snapshot;
            snapshot.timestamp = timestamp;
            snapshot.frameNumber = frameNumber;
            
            snapshot.positions.reserve(bodies.size());
            snapshot.velocities.reserve(bodies.size());
            snapshot.activeStates.reserve(bodies.size());
            
            for (const auto& body : bodies) {
                snapshot.positions.push_back(body.position);
                snapshot.velocities.push_back(body.velocity);
                snapshot.activeStates.push_back(body.isActive);
            }
            
            m_snapshots.push_back(snapshot);
            
            // Maintain snapshot limit
            if (m_snapshots.size() > MAX_SNAPSHOTS) {
                m_snapshots.pop_front();
            }
            
        } catch (const std::exception& e) {
            ExceptionHandler::GetInstance().LogException(e, "PhysicsDebugSystem::CaptureSnapshot");
        }
    }
    
    void ValidatePhysicsState(const std::vector<PhysicsBody>& bodies, int frameNumber) {
        if (!m_enableValidation) return;
        
        try {
            for (size_t i = 0; i < bodies.size(); ++i) {
                const PhysicsBody& body = bodies[i];
                
                // Check for common physics errors
                if (body.isActive) {
                    // Velocity explosion check
                    if (body.velocity.Magnitude() > 1000.0f) {
#if defined(_DEBUG_PHYSICS_)
                        debug.logDebugMessage(LogLevel::LOG_ERROR, 
                            L"[PhysicsDebug] Body %zu has explosive velocity: %.2f at frame %d",
                            i, body.velocity.Magnitude(), frameNumber);
#endif
                    }
                    
                    // Position drift check
                    if (body.position.Magnitude() > 5000.0f) {
#if defined(_DEBUG_PHYSICS_)
                        debug.logDebugMessage(LogLevel::LOG_WARNING, 
                            L"[PhysicsDebug] Body %zu drifted far from origin: %.2f at frame %d",
                            i, body.position.Magnitude(), frameNumber);
#endif
                    }
                    
                    // Energy conservation check (simplified)
                    float kineticEnergy = 0.5f * body.mass * body.velocity.MagnitudeSquared();
                    if (kineticEnergy > 100000.0f) { // Arbitrary threshold
#if defined(_DEBUG_PHYSICS_)
                        debug.logDebugMessage(LogLevel::LOG_WARNING, 
                            L"[PhysicsDebug] Body %zu has excessive kinetic energy: %.2f at frame %d",
                            i, kineticEnergy, frameNumber);
#endif
                    }
                }
            }
            
        } catch (const std::exception& e) {
            ExceptionHandler::GetInstance().LogException(e, "PhysicsDebugSystem::ValidatePhysicsState");
        }
    }
    
    void DetectAnomalies(int currentFrame) {
        if (m_snapshots.size() < 2) return;
        
        try {
            const DebugSnapshot& current = m_snapshots.back();
            const DebugSnapshot& previous = m_snapshots[m_snapshots.size() - 2];
            
            if (current.positions.size() != previous.positions.size()) return;
            
            // Detect sudden position changes (teleportation)
            for (size_t i = 0; i < current.positions.size(); ++i) {
                if (current.activeStates[i] && previous.activeStates[i]) {
                    float distance = (current.positions[i] - previous.positions[i]).Magnitude();
                    float expectedDistance = current.velocities[i].Magnitude() * 0.016f; // Expected at 60 FPS
                    
                    if (distance > expectedDistance * 5.0f) { // 5x threshold
#if defined(_DEBUG_PHYSICS_)
                        debug.logDebugMessage(LogLevel::LOG_ERROR, 
                            L"[PhysicsDebug] Possible teleportation detected for body %zu: moved %.2f units between frames %d-%d",
                            i, distance, previous.frameNumber, current.frameNumber);
#endif
                    }
                }
            }
            
        } catch (const std::exception& e) {
            ExceptionHandler::GetInstance().LogException(e, "PhysicsDebugSystem::DetectAnomalies");
        }
    }
    
    void PrintPerformanceReport() const {
        if (!m_enablePerformanceTracking) return;
        
#if defined(_DEBUG_PHYSICS_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[PhysicsDebug] Performance Report:");
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Snapshots captured: %zu", m_snapshots.size());
        
        if (!m_snapshots.empty()) {
            float totalTime = m_snapshots.back().timestamp - m_snapshots.front().timestamp;
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Time span: %.2f seconds", totalTime);
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Average FPS: %.1f", 
                m_snapshots.size() / totalTime);
        }
#endif
    }
    
    void SetDebugFlags(bool logging, bool validation, bool performance) {
        m_enableLogging = logging;
        m_enableValidation = validation;
        m_enablePerformanceTracking = performance;
        
#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"[PhysicsDebug] Debug flags set - Logging: %d, Validation: %d, Performance: %d",
            logging, validation, performance);
#endif
    }
};
```

---

### 16.5 Integration Anti-Patterns

#### Avoiding Common Integration Mistakes
```cpp
// ✅ GOOD: Proper system decoupling
class WellDesignedGameSystem
{
private:
    PhysicsManager m_physicsManager;
    
    struct GameObjectPhysics {
        int physicsBodyID;
        int gameObjectID;
        bool needsSync;
    };
    
    std::vector<GameObjectPhysics> m_physicsObjects;
    
public:
    void UpdateGameLogic(float deltaTime) {
        // Update physics separately
        m_physicsManager.Update(deltaTime);
        
        // Sync physics results to game objects
        SyncPhysicsToGameObjects();
        
        // Update game logic based on new positions
        UpdateGameObjectStates();
    }
    
private:
    void SyncPhysicsToGameObjects() {
        for (auto& obj : m_physicsObjects) {
            if (obj.needsSync) {
                PhysicsBody* body = m_physicsManager.GetBody(obj.physicsBodyID);
                if (body) {
                    // Update game object position from physics
                    // GameObject::SetPosition(obj.gameObjectID, body->position);
                    obj.needsSync = false;
                }
            }
        }
    }
    
    void UpdateGameObjectStates() {
        // Game logic updates based on physics results
        // Clean separation of concerns
    }
};

// ❌ BAD: Tightly coupled systems
class BadlyDesignedGameSystem
{
private:
    Physics m_physics; // Direct physics access from game logic
    
public:
    void UpdateEverything(float deltaTime) {
        // BAD: Mixing concerns in one function
        
        // Update physics
        m_physics.Update(deltaTime);
        
        // Directly access physics data from game logic
        // for (auto& gameObject : gameObjects) {
        //     PhysicsBody* body = GetPhysicsBody(gameObject.id); // Tight coupling
        //     gameObject.position = body->position; // Direct dependency
        //     
        //     // Game logic mixed with physics
        //     if (gameObject.health <= 0) {
        //         body->isActive = false; // Game logic affecting physics directly
        //     }
        // }
    }
};
```

#### Memory Management Best Practices
```cpp
// ✅ GOOD: RAII and smart pointer usage
class ResourceManagedPhysics
{
private:
    std::unique_ptr<Physics> m_physics;
    std::unique_ptr<PhysicsObjectPool> m_objectPool;
    
    struct ManagedPhysicsObject {
        std::shared_ptr<PhysicsBody> body;
        int referenceCount;
        
        ManagedPhysicsObject() : referenceCount(0) {}
    };
    
    std::vector<ManagedPhysicsObject> m_managedObjects;
    
public:
    bool Initialize() {
        try {
            m_physics = std::make_unique<Physics>();
            m_objectPool = std::make_unique<PhysicsObjectPool>();
            
            if (!m_physics->Initialize()) {
                return false;
            }
            
            if (!m_objectPool->Initialize()) {
                return false;
            }
            
#if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[ResourceManagedPhysics] Initialized with RAII");
#endif
            
            return true;
            
        } catch (const std::exception& e) {
            ExceptionHandler::GetInstance().LogException(e, "ResourceManagedPhysics::Initialize");
            return false;
        }
    }
    
    std::shared_ptr<PhysicsBody> CreateManagedBody(const PhysicsVector3D& position) {
        try {
            auto body = std::make_shared<PhysicsBody>();
            body->position = position;
            body->isActive = true;
            
            ManagedPhysicsObject managedObj;
            managedObj.body = body;
            managedObj.referenceCount = 1;
            
            m_managedObjects.push_back(managedObj);
            
            return body;
            
        } catch (const std::exception& e) {
            ExceptionHandler::GetInstance().LogException(e, "ResourceManagedPhysics::CreateManagedBody");
            return nullptr;
        }
    }
    
    // Automatic cleanup when unique_ptr goes out of scope
    ~ResourceManagedPhysics() = default; // RAII cleanup
};

// ❌ BAD: Manual memory management with leaks
class LeakyPhysicsSystem
{
private:
    Physics* m_physics; // Raw pointer - potential leak
    PhysicsBody* m_bodies; // Manual array - potential leak
    size_t m_bodyCount;
    
public:
    bool Initialize() {
        m_physics = new Physics(); // Manual allocation
        if (!m_physics->Initialize()) {
            return false; // LEAK: m_physics not deleted on failure
        }
        
        m_bodies = new PhysicsBody[1000]; // Manual array allocation
        m_bodyCount = 1000;
        
        return true;
    }
    
    void Cleanup() {
        // BAD: Manual cleanup - easy to forget or miss
        delete m_physics; // What if this throws?
        delete[] m_bodies; // What if caller forgets to call Cleanup()?
        
        m_physics = nullptr;
        m_bodies = nullptr;
    }
    
    // BAD: No destructor - guaranteed leak if Cleanup() not called
};
```

---

### 16.6 Performance Monitoring and Profiling

#### Built-in Performance Monitoring
```cpp
class PhysicsPerformanceMonitor
{
private:
    struct PerformanceMetrics {
        float averageUpdateTime;
        float maxUpdateTime;
        float minUpdateTime;
        int samplesCount;
        int droppedFrames;
        float totalTime;
        
        PerformanceMetrics() : averageUpdateTime(0.0f), maxUpdateTime(0.0f), 
                              minUpdateTime(FLT_MAX), samplesCount(0), 
                              droppedFrames(0), totalTime(0.0f) {}
    };
    
    PerformanceMetrics m_metrics;
    std::chrono::high_resolution_clock::time_point m_lastUpdateStart;
    bool m_isMonitoring;
    
    // Rolling average buffer
    static const int SAMPLE_BUFFER_SIZE = 60;
    std::array<float, SAMPLE_BUFFER_SIZE> m_sampleBuffer;
    int m_sampleIndex;
    
public:
    PhysicsPerformanceMonitor() : m_isMonitoring(false), m_sampleIndex(0) {
        m_sampleBuffer.fill(0.0f);
    }
    
    void StartFrame() {
        if (!m_isMonitoring) return;
        
        m_lastUpdateStart = std::chrono::high_resolution_clock::now();
    }
    
    void EndFrame() {
        if (!m_isMonitoring) return;
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - m_lastUpdateStart);
        float frameTimeMs = duration.count() / 1000.0f;
        
        UpdateMetrics(frameTimeMs);
    }
    
    void EnableMonitoring(bool enable) {
        m_isMonitoring = enable;
        
        if (enable) {
            ResetMetrics();
#if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[PerformanceMonitor] Monitoring enabled");
#endif
        }
    }
    
    void PrintReport() const {
        if (!m_isMonitoring || m_metrics.samplesCount == 0) return;
        
#if defined(_DEBUG_PHYSICS_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[PerformanceMonitor] Physics Performance Report:");
        debug.logDebugMessage(LogLevel::LOG_INFO, L"  Average update time: %.3f ms", m_metrics.averageUpdateTime);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"  Min update time: %.3f ms", m_metrics.minUpdateTime);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"  Max update time: %.3f ms", m_metrics.maxUpdateTime);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"  Samples collected: %d", m_metrics.samplesCount);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"  Dropped frames (>16.67ms): %d", m_metrics.droppedFrames);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"  Total time monitored: %.2f seconds", m_metrics.totalTime);
        
        float targetFrameTime = 16.67f; // 60 FPS
        float performanceRatio = m_metrics.averageUpdateTime / targetFrameTime;
        
        if (performanceRatio > 1.0f) {
            debug.logDebugMessage(LogLevel::LOG_WARNING, 
                L"  WARNING: Physics update time exceeds 60 FPS budget by %.1f%%", 
                (performanceRatio - 1.0f) * 100.0f);
        } else {
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"  Performance margin: %.1f%% under 60 FPS budget", 
                (1.0f - performanceRatio) * 100.0f);
        }
#endif
    }
    
    float GetAverageFrameTime() const { return m_metrics.averageUpdateTime; }
    int GetDroppedFrameCount() const { return m_metrics.droppedFrames; }
    
private:
    void UpdateMetrics(float frameTimeMs) {
        // Update rolling buffer
        m_sampleBuffer[m_sampleIndex] = frameTimeMs;
        m_sampleIndex = (m_sampleIndex + 1) % SAMPLE_BUFFER_SIZE;
        
        // Update metrics
        m_metrics.samplesCount++;
        m_metrics.totalTime += frameTimeMs / 1000.0f; // Convert to seconds
        
        if (frameTimeMs < m_metrics.minUpdateTime) {
            m_metrics.minUpdateTime = frameTimeMs;
        }
        
        if (frameTimeMs > m_metrics.maxUpdateTime) {
            m_metrics.maxUpdateTime = frameTimeMs;
        }
        
        if (frameTimeMs > 16.67f) { // Above 60 FPS threshold
            m_metrics.droppedFrames++;
        }
        
        // Calculate rolling average
        float sum = 0.0f;
        int validSamples = std::min(m_metrics.samplesCount, SAMPLE_BUFFER_SIZE);
        for (int i = 0; i < validSamples; ++i) {
            sum += m_sampleBuffer[i];
        }
        m_metrics.averageUpdateTime = sum / validSamples;
    }
    
    void ResetMetrics() {
        m_metrics = PerformanceMetrics();
        m_sampleBuffer.fill(0.0f);
        m_sampleIndex = 0;
    }
};
```

---

### 16.7 Configuration and Tuning Guidelines

#### Physics Configuration System
```cpp
struct PhysicsConfig
{
    // Performance settings
    float targetFrameRate = 60.0f;
    int maxPhysicsBodies = 1000;
    int maxParticles = 5000;
    bool enableMultiThreading = true;
    
    // Quality settings
    int collisionIterations = 4;
    int constraintIterations = 8;
    float positionCorrectionFactor = 0.8f;
    float velocityThreshold = 0.01f;
    
    // Spatial optimization
    float spatialGridCellSize = 5.0f;
    PhysicsVector3D worldBounds = PhysicsVector3D(1000.0f, 100.0f, 1000.0f);
    
    // Debug settings
    bool enableDebugVisualization = false;
    bool enablePerformanceMonitoring = true;
    bool enableValidation = true;
    
    // Platform-specific overrides
    void ApplyPlatformSettings() {
#ifdef _DEBUG
        enableDebugVisualization = true;
        enableValidation = true;
        collisionIterations = 8; // Higher quality in debug
#else
        enableDebugVisualization = false;
        enableValidation = false;
        collisionIterations = 4; // Performance optimized
#endif

#ifdef __ANDROID__
        maxPhysicsBodies = 500;
        maxParticles = 2000;
        enableMultiThreading = false;
        collisionIterations = 2;
#endif

#ifdef __IOS__
        maxPhysicsBodies = 750;
        maxParticles = 3000;
        enableMultiThreading = false;
        collisionIterations = 3;
#endif
    }
    
    bool Validate() const {
        if (targetFrameRate <= 0.0f || targetFrameRate > 240.0f) {
            return false;
        }
        
        if (maxPhysicsBodies <= 0 || maxPhysicsBodies > 100000) {
            return false;
        }
        
        if (collisionIterations < 1 || collisionIterations > 20) {
            return false;
        }
        
        if (spatialGridCellSize <= 0.1f || spatialGridCellSize > 100.0f) {
            return false;
        }
        
        return true;
    }
};

class ConfigurablePhysicsSystem
{
private:
    Physics m_physics;
    PhysicsConfig m_config;
    PhysicsPerformanceMonitor m_monitor;
    
public:
    bool Initialize(const PhysicsConfig& config = PhysicsConfig()) {
        m_config = config;
        m_config.ApplyPlatformSettings();
        
        if (!m_config.Validate()) {
#if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ConfigurablePhysics] Invalid configuration");
#endif
            return false;
        }
        
        try {
            if (!m_physics.Initialize()) {
                return false;
            }
            
            ApplyConfiguration();
            
            m_monitor.EnableMonitoring(m_config.enablePerformanceMonitoring);
            
#if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[ConfigurablePhysics] Initialized with custom configuration");
            LogConfiguration();
#endif
            
            return true;
            
        } catch (const std::exception& e) {
            ExceptionHandler::GetInstance().LogException(e, "ConfigurablePhysicsSystem::Initialize");
            return false;
        }
    }
    
    void Update(float deltaTime) {
        m_monitor.StartFrame();
        
        try {
            // Adaptive quality based on performance
            AdaptiveQualityControl();
            
            m_physics.Update(deltaTime);
            
        } catch (const std::exception& e) {
            ExceptionHandler::GetInstance().LogException(e, "ConfigurablePhysicsSystem::Update");
        }
        
        m_monitor.EndFrame();
    }
    
private:
    void ApplyConfiguration() {
        // Apply configuration settings to physics system
        // Implementation would set internal physics parameters
    }
    
    void AdaptiveQualityControl() {
        static int frameCounter = 0;
        
        if (++frameCounter % 60 == 0) { // Check every second
            float avgFrameTime = m_monitor.GetAverageFrameTime();
            float targetFrameTime = 1000.0f / m_config.targetFrameRate;
            
            if (avgFrameTime > targetFrameTime * 1.2f) {
                // Performance is suffering - reduce quality
                if (m_config.collisionIterations > 1) {
                    m_config.collisionIterations--;
#if defined(_DEBUG_PHYSICS_)
                    debug.logDebugMessage(LogLevel::LOG_INFO, 
                        L"[AdaptiveQuality] Reduced collision iterations to %d", 
                        m_config.collisionIterations);
#endif
                }
            } else if (avgFrameTime < targetFrameTime * 0.8f) {
                // Performance headroom - increase quality
                if (m_config.collisionIterations < 8) {
                    m_config.collisionIterations++;
#if defined(_DEBUG_PHYSICS_)
                    debug.logDebugMessage(LogLevel::LOG_INFO, 
                        L"[AdaptiveQuality] Increased collision iterations to %d", 
                        m_config.collisionIterations);
#endif
                }
            }
        }
    }
    
    void LogConfiguration() const {
#if defined(_DEBUG_PHYSICS_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[ConfigurablePhysics] Configuration:");
        debug.logDebugMessage(LogLevel::LOG_INFO, L"  Target FPS: %.1f", m_config.targetFrameRate);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"  Max bodies: %d", m_config.maxPhysicsBodies);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"  Max particles: %d", m_config.maxParticles);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"  Multi-threading: %s", 
            m_config.enableMultiThreading ? L"Enabled" : L"Disabled");
        debug.logDebugMessage(LogLevel::LOG_INFO, L"  Collision iterations: %d", m_config.collisionIterations);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"  Grid cell size: %.2f", m_config.spatialGridCellSize);
#endif
    }
};
```

---

### Summary of Best Practices

#### Essential Guidelines
1. **Always validate inputs** - Check for NaN, infinite values, and reasonable ranges
2. **Use RAII and smart pointers** - Prevent memory leaks and ensure proper cleanup
3. **Implement proper error handling** - Use ExceptionHandler and Debug logging
4. **Profile performance regularly** - Monitor frame times and memory usage
5. **Design for thread safety** - Use proper synchronization when needed
6. **Separate concerns** - Keep physics separate from game logic
7. **Use object pooling** - Avoid allocations in update loops
8. **Implement spatial optimization** - Use spatial data structures for collision detection
9. **Configure for platforms** - Adapt settings for different hardware capabilities
10. **Debug extensively** - Use comprehensive debugging and validation systems

#### Common Pitfalls to Avoid
- ❌ No input validation leading to NaN propagation
- ❌ Direct memory management causing leaks
- ❌ Missing error handling causing crashes
- ❌ Race conditions in multi-threaded code
- ❌ Tight coupling between systems
- ❌ Allocations in performance-critical loops
- ❌ O(n²) algorithms without spatial optimization
- ❌ Fixed settings across all platforms
- ❌ Insufficient debugging and monitoring
- ❌ Ignoring numerical stability issues

**Chapter 16 Complete**

---

# Troubleshooting Guide

## Quick Problem Resolution

### 1. Initialization Issues

**Problem**: Physics system fails to initialize
```cpp
Physics physics;
if (!physics.IsInitialized()) {
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Physics system failed to initialize");
    return false;
}
```

**Solution**: Ensure MathPrecalculation is initialized first
```cpp
// Correct initialization order
MathPrecalculation& mathPrecalc = MathPrecalculation::GetInstance();
if (!mathPrecalc.Initialize()) {
    debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"MathPrecalculation failed to initialize");
    return false;
}

Physics physics;
if (!physics.Initialize()) {
    debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Physics failed to initialize");
    return false;
}
```

### 2. Performance Problems

**Problem**: Physics update taking too long
```cpp
// Check update time
float updateTime = physics.GetLastUpdateTime();
if (updateTime > 16.0f) { // 60 FPS threshold
#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_WARNING, L"Physics update slow: %.2f ms", updateTime);
#endif
}
```

**Solution**: Reduce active body count or optimize simulation
```cpp
// Check physics statistics
int activeBodyCount, collisionCount, particleCount;
physics.GetPhysicsStatistics(activeBodyCount, collisionCount, particleCount);

// Deactivate distant bodies
for (auto& body : physicsBodies) {
    float distance = physics.FastDistance(body.position, cameraPosition);
    body.isActive = (distance < MAX_PHYSICS_DISTANCE);
}
```

### 3. Memory Issues

**Problem**: Excessive memory usage
```cpp
// Monitor physics statistics
int activeBodyCount, collisionCount, particleCount;
physics.GetPhysicsStatistics(activeBodyCount, collisionCount, particleCount);

#if defined(_DEBUG_PHYSICS_)
if (activeBodyCount > 1000) {
    debug.logDebugMessage(LogLevel::LOG_WARNING, L"High body count: %d", activeBodyCount);
}
#endif
```

**Solution**: Clean up inactive objects
```cpp
// Remove inactive particles
particles.erase(
    std::remove_if(particles.begin(), particles.end(),
        [](const PhysicsParticle& p) { return !p.isActive; }),
    particles.end()
);

// Update particle system
physics.UpdateParticleSystem(particles, deltaTime);
```

### 4. Collision Detection Issues

**Problem**: Objects passing through each other
```cpp
// Enable continuous collision detection
bool willCollide = physics.ContinuousCollisionDetection(bodyA, bodyB, deltaTime);
if (willCollide) {
#if defined(_DEBUG_PHYSICS_)
    debug.logLevelMessage(LogLevel::LOG_WARNING, L"Continuous collision detected");
#endif
    // Handle collision before it happens
}
```

**Solution**: Use proper collision detection methods
```cpp
// Check sphere collision
bool collision = physics.CheckSphereCollision(bodyA.position, radiusA, bodyB.position, radiusB);

// Generate collision manifold for detailed response
CollisionManifold manifold = physics.GenerateCollisionManifold(bodyA, bodyB);
if (!manifold.contacts.empty()) {
    physics.ResolveCollisionResponse(manifold);
}
```

### 5. Ragdoll Physics Problems

**Problem**: Ragdoll exploding or unstable
```cpp
// Create stable ragdoll
std::vector<PhysicsVector3D> jointPositions = {
    PhysicsVector3D(0.0f, 2.0f, 0.0f),  // Head
    PhysicsVector3D(0.0f, 1.5f, 0.0f),  // Neck
    PhysicsVector3D(0.0f, 1.0f, 0.0f)   // Torso
};

std::vector<std::pair<int, int>> connections = { {0, 1}, {1, 2} };
std::vector<PhysicsBody> ragdollBodies = physics.CreateRagdoll(jointPositions, connections);
```

**Solution**: Adjust joint parameters
```cpp
// Stable ragdoll joint settings
RagdollJoint joint;
joint.bodyA = &ragdollBodies[0];
joint.bodyB = &ragdollBodies[1];
joint.stiffness = 500.0f;   // Lower for more flexibility
joint.damping = 100.0f;     // Higher for more stability
joint.isActive = true;

physics.AddRagdollJoint(joint);
```

### 6. Particle System Issues

**Problem**: Particles not updating properly
```cpp
// Create explosion with proper parameters
std::vector<PhysicsParticle> particles = physics.CreateExplosion(
    PhysicsVector3D(0.0f, 0.0f, 0.0f),  // Center
    50,                                  // Particle count
    10.0f,                              // Explosion force
    3.0f                                // Lifetime
);

#if defined(_DEBUG_PHYSICS_)
if (particles.empty()) {
    debug.logLevelMessage(LogLevel::LOG_WARNING, L"No particles created in explosion");
}
#endif
```

**Solution**: Verify particle initialization and update
```cpp
// Proper particle update loop
physics.UpdateParticleSystem(particles, deltaTime);
physics.ApplyGravityToParticles(particles, DEFAULT_GRAVITY);

// Apply wind effects
PhysicsVector3D windForce(5.0f, 0.0f, 0.0f);
physics.ApplyWindForce(particles, windForce);
```

### 7. Audio Physics Problems

**Problem**: Incorrect audio positioning
```cpp
// Calculate proper audio physics
AudioPhysicsData audioData = physics.CalculateAudioPhysics(
    listenerPosition,
    sourcePosition,
    sourceVelocity
);

#if defined(_DEBUG_PHYSICS_)
if (audioData.dopplerShift < 0.5f || audioData.dopplerShift > 2.0f) {
    debug.logDebugMessage(LogLevel::LOG_WARNING, L"Extreme Doppler shift: %.3f", audioData.dopplerShift);
}
#endif
```

**Solution**: Use proper audio calculations
```cpp
// Calculate sound occlusion
std::vector<PhysicsVector3D> obstacles;
float occlusion = physics.CalculateSoundOcclusion(sourcePosition, listenerPosition, obstacles);

// Calculate reverb
float reverb = physics.CalculateReverb(listenerPosition, 100.0f, 0.3f);
```

### 8. Gravity Field Issues

**Problem**: Objects not affected by gravity
```cpp
// Add gravity field properly
GravityField earthGravity;
earthGravity.center = PhysicsVector3D(0.0f, -1000.0f, 0.0f);
earthGravity.mass = 5.972e24f;          // Earth mass
earthGravity.intensity = 1.0f;
earthGravity.radius = 1000.0f;
earthGravity.isBlackHole = false;

physics.AddGravityField(earthGravity);
```

**Solution**: Verify gravity calculations
```cpp
// Test gravity at position
PhysicsVector3D gravityForce = physics.CalculateGravityAtPosition(testPosition);

#if defined(_DEBUG_PHYSICS_)
if (gravityForce.Magnitude() < 0.1f) {
    debug.logDebugMessage(LogLevel::LOG_WARNING, L"Weak gravity force: %.3f", gravityForce.Magnitude());
}
#endif
```

### 9. Curved Path Issues

**Problem**: Path calculation errors
```cpp
// Create proper 2D curved path
std::vector<PhysicsVector2D> controlPoints = {
    PhysicsVector2D(0.0f, 0.0f),
    PhysicsVector2D(10.0f, 5.0f),
    PhysicsVector2D(20.0f, 0.0f)
};

CurvedPath2D path = physics.CreateCurvedPath2D(controlPoints, 100);

#if defined(_DEBUG_PHYSICS_)
if (path.coordinates.empty()) {
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to create curved path");
}
#endif
```

**Solution**: Use proper path parameters
```cpp
// Get point on curve safely
float t = std::clamp(timeParameter, 0.0f, 1.0f);
PhysicsVector2D point = physics.GetCurvePoint2D(path, t);
PhysicsVector2D tangent = physics.GetCurveTangent2D(path, t);
```

### 10. Reflection Calculations

**Problem**: Incorrect reflection physics
```cpp
// Calculate reflection properly
PhysicsVector3D incomingVelocity(10.0f, -5.0f, 0.0f);
PhysicsVector3D surfaceNormal(0.0f, 1.0f, 0.0f);

ReflectionData reflection = physics.CalculateReflection(
    incomingVelocity,
    surfaceNormal,
    0.8f,  // Restitution
    0.3f   // Friction
);

#if defined(_DEBUG_PHYSICS_)
debug.logDebugMessage(LogLevel::LOG_INFO, L"Energy loss: %.3f", reflection.energyLoss);
#endif
```

### 11. Integration Instability

**Problem**: Physics simulation becoming unstable
```cpp
// Clamp time step to prevent instability
float clampedDeltaTime = std::clamp(deltaTime, 0.001f, 0.033f); // Max 30 FPS
physics.Update(clampedDeltaTime);
```

**Solution**: Use proper physics body setup
```cpp
// Configure physics body properly
PhysicsBody body;
body.SetMass(1.0f);                     // Positive mass
body.restitution = 0.7f;                // Reasonable bounce
body.friction = 0.5f;                   // Reasonable friction
body.drag = 0.01f;                      // Air resistance
body.isStatic = false;                  // Dynamic body
body.isActive = true;                   // Participate in physics
```

### 12. Projectile Motion Issues

**Problem**: Incorrect trajectory calculations
```cpp
// Calculate projectile motion properly
std::vector<PhysicsVector3D> trajectory = physics.CalculateProjectileMotion(
    PhysicsVector3D(0.0f, 0.0f, 0.0f),  // Start position
    PhysicsVector3D(20.0f, 15.0f, 0.0f), // Initial velocity
    DEFAULT_GRAVITY,                     // Gravity
    0.01f,                              // Drag
    0.016f,                             // Time step
    10.0f                               // Max time
);

#if defined(_DEBUG_PHYSICS_)
debug.logDebugMessage(LogLevel::LOG_INFO, L"Trajectory points: %zu", trajectory.size());
#endif
```

## Common Error Messages and Solutions

| Error Message | Cause | Solution |
|---------------|--------|----------|
| "Physics not initialized" | Update called before Initialize() | Call physics.Initialize() first |
| "Invalid body mass" | Mass set to zero or negative | Use body.SetMass() with positive value |
| "Null physics body" | Body pointer is nullptr | Check body allocation before use |
| "Maximum contacts exceeded" | Too many collision contacts | Reduce collision complexity |
| "Path coordinates full" | Too many path points | Use fewer control points |

## Performance Monitoring

```cpp
// Add to main loop for monitoring
void MonitorPhysicsPerformance(Physics& physics) {
    static int frameCount = 0;
    static float totalTime = 0.0f;
    
    float updateTime = physics.GetLastUpdateTime();
    totalTime += updateTime;
    frameCount++;
    
    if (frameCount >= 60) {
        float avgTime = totalTime / frameCount;
        
#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Physics avg update time: %.2f ms", avgTime);
#endif
        
        frameCount = 0;
        totalTime = 0.0f;
    }
}
```

## Quick Fixes Checklist

- [ ] MathPrecalculation initialized before Physics
- [ ] Debug flags enabled in Debug.h (_DEBUG_PHYSICS_)
- [ ] Time step clamped to reasonable values
- [ ] Memory cleanup in destructors
- [ ] Valid mass values for all bodies (> 0)
- [ ] Reasonable restitution/friction coefficients (0-1)
- [ ] Active flags set correctly on objects
- [ ] Null pointer checks before use
- [ ] Performance monitoring enabled

## Emergency Recovery

```cpp
// If physics system becomes completely unstable
void EmergencyPhysicsReset(Physics& physics) {
#if defined(_DEBUG_PHYSICS_)
    debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Emergency physics reset initiated");
#endif
    
    // Clear all physics objects
    physics.ClearGravityFields();
    physics.ClearDebugLines();
    
    // Reset performance counters
    physics.ResetPerformanceCounters();
    
    // Reinitialize if needed
    if (!physics.IsInitialized()) {
        physics.Initialize();
    }
}
```

**Chapter 17 Complete**

---

# Complete Example Projects

## Project 1: Basic Bouncing Ball

```cpp
#include "Physics.h"
#include "Debug.h"

class BouncingBallExample {
private:
    Physics physics;
    PhysicsBody ball;
    
public:
    bool Initialize() {
        // Initialize physics system
        if (!physics.Initialize()) {
#if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to initialize physics");
#endif
            return false;
        }
        
        // Setup ball properties
        ball.position = PhysicsVector3D(0.0f, 10.0f, 0.0f);
        ball.SetMass(1.0f);
        ball.restitution = 0.8f;
        ball.friction = 0.2f;
        ball.drag = 0.01f;
        ball.isStatic = false;
        ball.isActive = true;
        
        // Add gravity field
        GravityField gravity;
        gravity.center = PhysicsVector3D(0.0f, -1000.0f, 0.0f);
        gravity.intensity = DEFAULT_GRAVITY;
        gravity.mass = 1.0f;
        physics.AddGravityField(gravity);
        
        return true;
    }
    
    void Update(float deltaTime) {
        // Apply gravity to ball
        PhysicsVector3D gravityForce = physics.CalculateGravityAtPosition(ball.position);
        ball.ApplyForce(gravityForce);
        
        // Check ground collision
        if (ball.position.y <= 0.0f && ball.velocity.y < 0.0f) {
            PhysicsVector3D surfaceNormal(0.0f, 1.0f, 0.0f);
            ReflectionData reflection = physics.CalculateReflection(ball.velocity, surfaceNormal, ball.restitution);
            ball.velocity = reflection.reflectedVelocity;
            ball.position.y = 0.0f;
            
#if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Ball bounced with energy loss: %.3f", reflection.energyLoss);
#endif
        }
        
        // Update physics
        physics.ApplyNewtonianMotion(ball, PhysicsVector3D(), deltaTime);
    }
};
```

## Project 2: Projectile Launcher

```cpp
class ProjectileLauncher {
private:
    Physics physics;
    std::vector<PhysicsBody> projectiles;
    PhysicsVector3D launcherPosition;
    
public:
    bool Initialize() {
        if (!physics.Initialize()) return false;
        
        launcherPosition = PhysicsVector3D(0.0f, 1.0f, 0.0f);
        projectiles.reserve(100);
        
        // Add gravity
        GravityField gravity;
        gravity.center = PhysicsVector3D(0.0f, -1000.0f, 0.0f);
        gravity.intensity = DEFAULT_GRAVITY;
        physics.AddGravityField(gravity);
        
        return true;
    }
    
    void LaunchProjectile(const PhysicsVector3D& target) {
        // Calculate launch velocity
        PhysicsVector3D launchVelocity = physics.CalculateTrajectoryToTarget(
            launcherPosition, target, DEFAULT_GRAVITY, 25.0f);
        
        // Create projectile
        PhysicsBody projectile;
        projectile.position = launcherPosition;
        projectile.velocity = launchVelocity;
        projectile.SetMass(0.5f);
        projectile.restitution = 0.3f;
        projectile.drag = 0.02f;
        projectile.isActive = true;
        
        projectiles.push_back(projectile);
        
#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Launched projectile with velocity: (%.2f, %.2f, %.2f)",
                             launchVelocity.x, launchVelocity.y, launchVelocity.z);
#endif
    }
    
    void Update(float deltaTime) {
        for (auto& projectile : projectiles) {
            if (!projectile.isActive) continue;
            
            // Apply gravity and physics
            PhysicsVector3D gravity = physics.CalculateGravityAtPosition(projectile.position);
            physics.ApplyNewtonianMotion(projectile, gravity, deltaTime);
            
            // Remove if hit ground
            if (projectile.position.y <= 0.0f) {
                projectile.isActive = false;
            }
        }
        
        // Clean up inactive projectiles
        projectiles.erase(
            std::remove_if(projectiles.begin(), projectiles.end(),
                [](const PhysicsBody& p) { return !p.isActive; }),
            projectiles.end()
        );
    }
};
```

## Project 3: Particle Explosion System

```cpp
class ExplosionSystem {
private:
    Physics physics;
    std::vector<PhysicsParticle> particles;
    
public:
    bool Initialize() {
        if (!physics.Initialize()) return false;
        
        // Add gravity for particles
        GravityField gravity;
        gravity.center = PhysicsVector3D(0.0f, -1000.0f, 0.0f);
        gravity.intensity = DEFAULT_GRAVITY;
        physics.AddGravityField(gravity);
        
        return true;
    }
    
    void CreateExplosion(const PhysicsVector3D& center, float force) {
        // Create explosion particles
        std::vector<PhysicsParticle> newParticles = physics.CreateExplosion(
            center, 100, force, 5.0f);
        
        // Add to existing particles
        particles.insert(particles.end(), newParticles.begin(), newParticles.end());
        
#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Created explosion with %zu particles at (%.2f, %.2f, %.2f)",
                             newParticles.size(), center.x, center.y, center.z);
#endif
    }
    
    void Update(float deltaTime) {
        // Update particle physics
        physics.UpdateParticleSystem(particles, deltaTime);
        physics.ApplyGravityToParticles(particles, DEFAULT_GRAVITY);
        
        // Apply wind effects
        PhysicsVector3D wind(2.0f, 0.0f, 1.0f);
        physics.ApplyWindForce(particles, wind);
        
        // Count active particles
        int activeCount = 0;
        for (const auto& particle : particles) {
            if (particle.isActive) activeCount++;
        }
        
#if defined(_DEBUG_PHYSICS_)
        static int frameCount = 0;
        if (++frameCount % 60 == 0) {
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Active particles: %d", activeCount);
        }
#endif
    }
};
```

## Project 4: Simple Ragdoll Character

```cpp
class RagdollCharacter {
private:
    Physics physics;
    std::vector<PhysicsBody> ragdollBodies;
    
public:
    bool Initialize() {
        if (!physics.Initialize()) return false;
        
        // Define character joint positions
        std::vector<PhysicsVector3D> jointPositions = {
            PhysicsVector3D(0.0f, 2.0f, 0.0f),   // Head
            PhysicsVector3D(0.0f, 1.7f, 0.0f),   // Neck
            PhysicsVector3D(0.0f, 1.3f, 0.0f),   // Upper torso
            PhysicsVector3D(0.0f, 0.9f, 0.0f),   // Lower torso
            PhysicsVector3D(-0.3f, 1.3f, 0.0f),  // Left shoulder
            PhysicsVector3D(0.3f, 1.3f, 0.0f),   // Right shoulder
            PhysicsVector3D(-0.3f, 0.9f, 0.0f),  // Left elbow
            PhysicsVector3D(0.3f, 0.9f, 0.0f),   // Right elbow
            PhysicsVector3D(-0.2f, 0.5f, 0.0f),  // Left hip
            PhysicsVector3D(0.2f, 0.5f, 0.0f),   // Right hip
            PhysicsVector3D(-0.2f, 0.0f, 0.0f),  // Left knee
            PhysicsVector3D(0.2f, 0.0f, 0.0f)    // Right knee
        };
        
        // Define joint connections
        std::vector<std::pair<int, int>> connections = {
            {0, 1}, {1, 2}, {2, 3},     // Spine
            {2, 4}, {2, 5},             // Shoulders
            {4, 6}, {5, 7},             // Arms
            {3, 8}, {3, 9},             // Hips
            {8, 10}, {9, 11}            // Legs
        };
        
        // Create ragdoll
        ragdollBodies = physics.CreateRagdoll(jointPositions, connections);
        
        // Add gravity
        GravityField gravity;
        gravity.center = PhysicsVector3D(0.0f, -1000.0f, 0.0f);
        gravity.intensity = DEFAULT_GRAVITY;
        physics.AddGravityField(gravity);
        
#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Created ragdoll with %zu bodies", ragdollBodies.size());
#endif
        
        return true;
    }
    
    void ApplyImpact(const PhysicsVector3D& force, int bodyIndex) {
        if (bodyIndex >= 0 && bodyIndex < static_cast<int>(ragdollBodies.size())) {
            ragdollBodies[bodyIndex].ApplyImpulse(force);
            
#if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Applied impact force (%.2f, %.2f, %.2f) to body %d",
                                 force.x, force.y, force.z, bodyIndex);
#endif
        }
    }
    
    void Update(float deltaTime) {
        // Update ragdoll physics
        physics.UpdateRagdoll(ragdollBodies, deltaTime);
        
        // Check for ground collisions
        for (auto& body : ragdollBodies) {
            if (body.position.y <= 0.0f && body.velocity.y < 0.0f) {
                PhysicsVector3D normal(0.0f, 1.0f, 0.0f);
                ReflectionData reflection = physics.CalculateReflection(body.velocity, normal, 0.3f, 0.8f);
                body.velocity = reflection.reflectedVelocity;
                body.position.y = 0.0f;
            }
        }
    }
};
```

## Project 5: 3D Audio Positioning System

```cpp
class Audio3DSystem {
private:
    Physics physics;
    PhysicsVector3D listenerPosition;
    PhysicsVector3D listenerVelocity;
    
    struct AudioSource {
        PhysicsVector3D position;
        PhysicsVector3D velocity;
        float volume;
        bool isActive;
    };
    
    std::vector<AudioSource> audioSources;
    
public:
    bool Initialize() {
        if (!physics.Initialize()) return false;
        
        listenerPosition = PhysicsVector3D(0.0f, 1.8f, 0.0f);
        listenerVelocity = PhysicsVector3D();
        
        return true;
    }
    
    void AddAudioSource(const PhysicsVector3D& position, float volume) {
        AudioSource source;
        source.position = position;
        source.velocity = PhysicsVector3D();
        source.volume = volume;
        source.isActive = true;
        
        audioSources.push_back(source);
        
#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Added audio source at (%.2f, %.2f, %.2f)",
                             position.x, position.y, position.z);
#endif
    }
    
    void UpdateListener(const PhysicsVector3D& position, const PhysicsVector3D& velocity) {
        listenerPosition = position;
        listenerVelocity = velocity;
    }
    
    void Update(float deltaTime) {
        for (auto& source : audioSources) {
            if (!source.isActive) continue;
            
            // Calculate audio physics
            AudioPhysicsData audioData = physics.CalculateAudioPhysics(
                listenerPosition, source.position, source.velocity);
            
            // Calculate occlusion (simplified - no obstacles)
            std::vector<PhysicsVector3D> obstacles;
            float occlusion = physics.CalculateSoundOcclusion(
                source.position, listenerPosition, obstacles);
            
            // Calculate reverb
            float reverb = physics.CalculateReverb(listenerPosition, 50.0f, 0.3f);
            
            // Apply audio effects
            float finalVolume = source.volume * audioData.volumeFalloff * (1.0f - occlusion);
            float finalPitch = audioData.dopplerShift;
            
#if defined(_DEBUG_PHYSICS_)
            static int debugCounter = 0;
            if (++debugCounter % 120 == 0) {
                debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                    L"Audio: Volume=%.3f, Pitch=%.3f, Distance=%.2f",
                    finalVolume, finalPitch, audioData.distance);
            }
#endif
        }
    }
};
```

## Project 6: Curved Path Following Vehicle

```cpp
class PathFollowingVehicle {
private:
    Physics physics;
    PhysicsBody vehicle;
    CurvedPath3D racingPath;
    float pathProgress;
    
public:
    bool Initialize() {
        if (!physics.Initialize()) return false;
        
        // Create racing track path
        std::vector<PhysicsVector3D> controlPoints = {
            PhysicsVector3D(0.0f, 0.0f, 0.0f),
            PhysicsVector3D(20.0f, 2.0f, 5.0f),
            PhysicsVector3D(40.0f, 0.0f, 20.0f),
            PhysicsVector3D(20.0f, -1.0f, 35.0f),
            PhysicsVector3D(0.0f, 0.0f, 40.0f),
            PhysicsVector3D(-20.0f, 1.0f, 35.0f),
            PhysicsVector3D(-40.0f, 0.0f, 20.0f),
            PhysicsVector3D(-20.0f, -1.0f, 5.0f)
        };
        
        racingPath = physics.CreateCurvedPath3D(controlPoints, 200);
        
        // Initialize vehicle
        vehicle.position = physics.GetCurvePoint3D(racingPath, 0.0f);
        vehicle.SetMass(1000.0f);
        vehicle.drag = 0.05f;
        vehicle.friction = 0.8f;
        vehicle.isActive = true;
        
        pathProgress = 0.0f;
        
#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Created racing path with %zu points",
                             racingPath.coordinates.size());
#endif
        
        return true;
    }
    
    void SetThrottle(float throttle) {
        // Get path tangent for forward direction
        PhysicsVector3D forward = physics.GetCurveTangent3D(racingPath, pathProgress);
        
        // Apply throttle force
        PhysicsVector3D force = forward * (throttle * 5000.0f);
        vehicle.ApplyForce(force);
    }
    
    void Update(float deltaTime) {
        // Update vehicle physics
        physics.ApplyNewtonianMotion(vehicle, PhysicsVector3D(), deltaTime);
        
        // Calculate new path progress based on vehicle position
        // (Simplified - find closest point on path)
        float minDistance = FLT_MAX;
        float newProgress = pathProgress;
        
        for (int i = 0; i < 100; ++i) {
            float t = static_cast<float>(i) / 99.0f;
            PhysicsVector3D pathPoint = physics.GetCurvePoint3D(racingPath, t);
            float distance = physics.FastDistance(vehicle.position, pathPoint);
            
            if (distance < minDistance) {
                minDistance = distance;
                newProgress = t;
            }
        }
        
        pathProgress = newProgress;
        
        // Add debug visualization
        PhysicsVector3D targetPoint = physics.GetCurvePoint3D(racingPath, pathProgress);
        physics.AddDebugLine(vehicle.position, targetPoint);
        
#if defined(_DEBUG_PHYSICS_)
        static int frameCount = 0;
        if (++frameCount % 60 == 0) {
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Vehicle progress: %.3f, Distance to path: %.2f",
                                 pathProgress, minDistance);
        }
#endif
    }
};
```

## Project Integration Template

```cpp
class CompletePhysicsExample {
private:
    Physics physics;
    BouncingBallExample ball;
    ProjectileLauncher launcher;
    ExplosionSystem explosions;
    RagdollCharacter ragdoll;
    Audio3DSystem audio;
    PathFollowingVehicle vehicle;
    
public:
    bool Initialize() {
        // Initialize main physics system
        if (!physics.Initialize()) {
#if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to initialize main physics system");
#endif
            return false;
        }
        
        // Initialize all subsystems
        bool success = true;
        success &= ball.Initialize();
        success &= launcher.Initialize();
        success &= explosions.Initialize();
        success &= ragdoll.Initialize();
        success &= audio.Initialize();
        success &= vehicle.Initialize();
        
        if (!success) {
#if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to initialize physics subsystems");
#endif
            return false;
        }
        
#if defined(_DEBUG_PHYSICS_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"All physics examples initialized successfully");
#endif
        
        return true;
    }
    
    void Update(float deltaTime) {
        // Update all physics examples
        ball.Update(deltaTime);
        launcher.Update(deltaTime);
        explosions.Update(deltaTime);
        ragdoll.Update(deltaTime);
        audio.Update(deltaTime);
        vehicle.Update(deltaTime);
        
        // Monitor performance
        int activeBodyCount, collisionCount, particleCount;
        physics.GetPhysicsStatistics(activeBodyCount, collisionCount, particleCount);
        
#if defined(_DEBUG_PHYSICS_)
        static int perfCounter = 0;
        if (++perfCounter % 300 == 0) {
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Physics Stats - Bodies: %d, Collisions: %d, Particles: %d, Update Time: %.2f ms",
                activeBodyCount, collisionCount, particleCount, physics.GetLastUpdateTime());
        }
#endif
    }
    
    void Cleanup() {
        physics.Cleanup();
        
#if defined(_DEBUG_PHYSICS_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Physics examples cleaned up");
#endif
    }
};
```

## Usage Notes

1. **Initialization Order**: Always initialize MathPrecalculation before Physics
2. **Debug Output**: Enable `_DEBUG_PHYSICS_` in Debug.h for detailed logging
3. **Performance**: Monitor update times and active object counts
4. **Memory Management**: Clean up physics objects when no longer needed
5. **Thread Safety**: Use appropriate locking if accessing from multiple threads

**Chapter 18 Complete**

---
