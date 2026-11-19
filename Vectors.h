// ================================================================================================
//  Vectors.h  -  CPGE vector system (Vector2/3/4, MyVector, MyQuaternion, MyMath, MyGeom, effects)
//
//  PORTABLE / MERGE-READY: this header + Vectors.cpp form an independent engine-level unit meant to be
//  merged into any CPGE project.  It needs the global "Includes.h" (platform and renderer definitions)
//  and, for its sine / cosine / arccosine lookups, MathPrecalculation.h/.cpp - whose additions
//  (FastSinCosDeg, FastACosAccurate, GetUnitCircle) are part of the same merge.  The renderers and
//  game code are not needed.  Everything from the original Vectors.h (Vector2/3/4 with their
//  VECTOR3/4_H_INCLUDED guards) is kept unchanged at the top, so this file replaces the base
//  project's Vectors.h as a strict superset.
//  It is engine-level code, so there is deliberately no PROJECT_ONLY_CODE guard.
//
//  Supported platforms: Windows, Linux, Android, macOS and iOS (whatever Includes.h defines as
//  PLATFORM_*).  Nothing in here is platform specific; the platform handling is the macros below.
//
//  INCLUDE ORDER (important): Includes.h itself includes this header on builds that have no
//  DirectXMath, because it aliases XMFLOAT2/3/4 to Vector2/3/4.  So Vector2/3/4 are defined FIRST
//  (they need only the C++ standard library), Includes.h is pulled in right after them, and the rest
//  of the header follows.  That makes either file safe to include first, in any project.
//  NOTE: never define PLATFORM_* here before including Includes.h - it wraps its platform
//  includes in "#ifndef PLATFORM_xxx" and would skip them.
// ================================================================================================
#pragma once

// C++17 is the engine standard (std::filesystem, inline constexpr statics).  MSVC reports 199711L in
// __cplusplus unless /Zc:__cplusplus is set, so check _MSVC_LANG there.
#if !(__cplusplus >= 201703L || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L))
    #error "Vectors.h requires C++17 or newer."
#endif

// Keep <windows.h> from defining min/max macros (they break std::min/std::max and the
// numeric_limits calls below).  Includes.h sets the same thing; the guard avoids a redefinition.
#if defined(_WIN32) || defined(_WIN64)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
#endif

#include <cmath>
#include <string>
#include <sstream>

class Vector2 {
public:
    float x, y;

    // Constructors
    Vector2() : x(0), y(0) {}
    Vector2(float x, float y) : x(x), y(y) {}

    // Basic Operations
    Vector2 operator+(const Vector2& other) const { return Vector2(x + other.x, y + other.y); }
    Vector2 operator-(const Vector2& other) const { return Vector2(x - other.x, y - other.y); }
    Vector2 operator*(float scalar) const { return Vector2(x * scalar, y * scalar); }
    Vector2 operator/(float scalar) const { return (scalar != 0) ? Vector2(x / scalar, y / scalar) : Vector2(0, 0); }
    // Define the += operator
    Vector2& operator+=(const Vector2& other) { x += other.x; y += other.y; return *this; }

    // Magnitude and Normalization
    float Magnitude() const { return std::sqrt(x * x + y * y); }
    Vector2 Normalized() const {
        float mag = Magnitude();
        return (mag > 0) ? Vector2(x / mag, y / mag) : Vector2(0, 0);
    }

    // String Conversion
    std::string ToString() const {
        std::ostringstream oss;
        oss << "Vector2(" << x << ", " << y << ")";
        return oss.str();
    }

    // Serialization
    std::string Serialize() const {
        std::ostringstream oss;
        oss << x << "," << y;
        return oss.str();
    }

    static Vector2 Deserialize(const std::string& str) {
        std::istringstream iss(str);
        float x, y;
        char comma;
        if (iss >> x >> comma >> y && comma == ',') {
            return Vector2(x, y);
        }
        return Vector2(); // Return default if invalid
    }
};

#ifndef VECTOR4_H_INCLUDED
#define VECTOR4_H_INCLUDED

// Vector4 class for 4D vector operations (RGBA colors, homogeneous coordinates, etc.)
// Used primarily for OpenGL color operations and 4D transformations
class Vector4 {
public:
    float x, y, z, w;                                                           // Four-dimensional vector components

    // Constructors
    Vector4() : x(0), y(0), z(0), w(0) {}                                       // Default constructor - initializes to zero vector
    Vector4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}     // Parameterized constructor with all components
    Vector4(float value) : x(value), y(value), z(value), w(value) {}            // Single value constructor - sets all components to same value

    // Conversion constructor from Vector2 (extends to 4D with z=0, w=1)
    explicit Vector4(const Vector2& v2) : x(v2.x), y(v2.y), z(0.0f), w(1.0f) {} // Convert Vector2 to Vector4 with default z,w values

    // Basic arithmetic operations for vector mathematics
    Vector4 operator+(const Vector4& other) const {
        return Vector4(x + other.x, y + other.y, z + other.z, w + other.w);     // Component-wise addition
    }

    Vector4 operator-(const Vector4& other) const {
        return Vector4(x - other.x, y - other.y, z - other.z, w - other.w);     // Component-wise subtraction
    }

    Vector4 operator*(float scalar) const {
        return Vector4(x * scalar, y * scalar, z * scalar, w * scalar);          // Scalar multiplication
    }

    Vector4 operator/(float scalar) const {
        return (scalar != 0) ? Vector4(x / scalar, y / scalar, z / scalar, w / scalar) : Vector4(0, 0, 0, 0); // Scalar division with zero check
    }

    // Compound assignment operators for efficient in-place operations
    Vector4& operator+=(const Vector4& other) {
        x += other.x; y += other.y; z += other.z; w += other.w;
        return *this;                                                           // Component-wise addition assignment
    }

    Vector4& operator-=(const Vector4& other) {
        x -= other.x; y -= other.y; z -= other.z; w -= other.w;
        return *this;                                                           // Component-wise subtraction assignment
    }

    Vector4& operator*=(float scalar) {
        x *= scalar; y *= scalar; z *= scalar; w *= scalar;
        return *this;                                                           // Scalar multiplication assignment
    }

    // Vector magnitude and normalization operations
    float Magnitude() const {
        return std::sqrt(x * x + y * y + z * z + w * w);                        // Calculate 4D vector magnitude using Euclidean norm
    }

    Vector4 Normalized() const {
        float mag = Magnitude();                                                // Get vector magnitude
        return (mag > 0) ? Vector4(x / mag, y / mag, z / mag, w / mag) : Vector4(0, 0, 0, 0); // Return normalized vector or zero if magnitude is zero
    }

    // Dot product for vector angle calculations and projections
    float Dot(const Vector4& other) const {
        return x * other.x + y * other.y + z * other.z + w * other.w;           // Calculate dot product of two 4D vectors
    }

    // Component access methods for array-style indexing
    float& operator[](int index) {
        switch (index) {                                                    // Provide array-style access to components
        case 0: return x;                                                   // Index 0 returns x component
        case 1: return y;                                                   // Index 1 returns y component
        case 2: return z;                                                   // Index 2 returns z component
        case 3: return w;                                                   // Index 3 returns w component
        default: return x;                                                  // Default to x component for invalid indices
        }
    }

    const float& operator[](int index) const {
        switch (index) {                                                    // Provide const array-style access to components
        case 0: return x;                                                   // Index 0 returns x component
        case 1: return y;                                                   // Index 1 returns y component
        case 2: return z;                                                   // Index 2 returns z component
        case 3: return w;                                                   // Index 3 returns w component
        default: return x;                                                  // Default to x component for invalid indices
        }
    }

    // String conversion for debugging and logging purposes
    std::string ToString() const {
        std::ostringstream oss;                                                 // Create string stream for formatting
        oss << "Vector4(" << x << ", " << y << ", " << z << ", " << w << ")";   // Format as readable string
        return oss.str();                                                       // Return formatted string
    }

    // Serialization for data persistence and network transmission
    std::string Serialize() const {
        std::ostringstream oss;                                                 // Create string stream for serialization
        oss << x << "," << y << "," << z << "," << w;                           // Format as comma-separated values
        return oss.str();                                                       // Return serialized string
    }

    // Deserialization from string data
    static Vector4 Deserialize(const std::string& str) {
        std::istringstream iss(str);                                            // Create input string stream
        float x, y, z, w;                                                       // Temporary storage for components
        char comma1, comma2, comma3;                                            // Separator characters

        if (iss >> x >> comma1 >> y >> comma2 >> z >> comma3 >> w &&           // Parse components and separators
            comma1 == ',' && comma2 == ',' && comma3 == ',') {                  // Verify correct format
            return Vector4(x, y, z, w);                                         // Return parsed vector
        }
        return Vector4();                                                       // Return default vector if parsing fails
    }

    // Common predefined vectors for convenience
    static Vector4 Zero() { return Vector4(0.0f, 0.0f, 0.0f, 0.0f); }          // Zero vector
    static Vector4 One() { return Vector4(1.0f, 1.0f, 1.0f, 1.0f); }           // Unit vector (all components = 1)
    static Vector4 UnitX() { return Vector4(1.0f, 0.0f, 0.0f, 0.0f); }         // Unit vector along X axis
    static Vector4 UnitY() { return Vector4(0.0f, 1.0f, 0.0f, 0.0f); }         // Unit vector along Y axis
    static Vector4 UnitZ() { return Vector4(0.0f, 0.0f, 1.0f, 0.0f); }         // Unit vector along Z axis
    static Vector4 UnitW() { return Vector4(0.0f, 0.0f, 0.0f, 1.0f); }         // Unit vector along W axis
};

#endif // VECTOR4_H_INCLUDED

#ifndef VECTOR3_H_INCLUDED
#define VECTOR3_H_INCLUDED
// Vector3 class for 3D vector operations (positions, normals, directions, etc.)
// Used extensively for OpenGL 3D graphics operations and transformations
class Vector3 {
public:
    float x, y, z;                                                              // Three-dimensional vector components

    // Constructors
    Vector3() : x(0), y(0), z(0) {}                                            // Default constructor - initializes to zero vector
    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}                   // Parameterized constructor with all components
    Vector3(float value) : x(value), y(value), z(value) {}                     // Single value constructor - sets all components to same value

    // Conversion constructor from Vector2 (extends to 3D with z=0)
    explicit Vector3(const Vector2& v2) : x(v2.x), y(v2.y), z(0.0f) {}         // Convert Vector2 to Vector3 with default z value

    // Conversion constructor from Vector4 (projects to 3D by ignoring w)
    explicit Vector3(const Vector4& v4) : x(v4.x), y(v4.y), z(v4.z) {}         // Convert Vector4 to Vector3 by dropping w component

    // Basic arithmetic operations for vector mathematics
    Vector3 operator+(const Vector3& other) const {
        return Vector3(x + other.x, y + other.y, z + other.z);                  // Component-wise addition
    }

    Vector3 operator-(const Vector3& other) const {
        return Vector3(x - other.x, y - other.y, z - other.z);                  // Component-wise subtraction
    }

    Vector3 operator*(float scalar) const {
        return Vector3(x * scalar, y * scalar, z * scalar);                     // Scalar multiplication
    }

    Vector3 operator/(float scalar) const {
        return (scalar != 0) ? Vector3(x / scalar, y / scalar, z / scalar) : Vector3(0, 0, 0); // Scalar division with zero check
    }

    // Compound assignment operators for efficient in-place operations
    Vector3& operator+=(const Vector3& other) {
        x += other.x; y += other.y; z += other.z;
        return *this;                                                           // Component-wise addition assignment
    }

    Vector3& operator-=(const Vector3& other) {
        x -= other.x; y -= other.y; z -= other.z;
        return *this;                                                           // Component-wise subtraction assignment
    }

    Vector3& operator*=(float scalar) {
        x *= scalar; y *= scalar; z *= scalar;
        return *this;                                                           // Scalar multiplication assignment
    }

    // Vector magnitude and normalization operations
    float Magnitude() const {
        return std::sqrt(x * x + y * y + z * z);                                // Calculate 3D vector magnitude using Euclidean norm
    }

    Vector3 Normalized() const {
        float mag = Magnitude();                                                // Get vector magnitude
        return (mag > 0) ? Vector3(x / mag, y / mag, z / mag) : Vector3(0, 0, 0); // Return normalized vector or zero if magnitude is zero
    }

    // Dot product for vector angle calculations and projections
    float Dot(const Vector3& other) const {
        return x * other.x + y * other.y + z * other.z;                         // Calculate dot product of two 3D vectors
    }

    // Cross product for surface normals and perpendicular vectors
    Vector3 Cross(const Vector3& other) const {
        return Vector3(
            y * other.z - z * other.y,                                          // Cross product X component
            z * other.x - x * other.z,                                          // Cross product Y component
            x * other.y - y * other.x                                           // Cross product Z component
        );
    }

    // Component access methods for array-style indexing
    float& operator[](int index) {
        switch (index) {                                                         // Provide array-style access to components
        case 0: return x;                                                   // Index 0 returns x component
        case 1: return y;                                                   // Index 1 returns y component
        case 2: return z;                                                   // Index 2 returns z component
        default: return x;                                                  // Default to x component for invalid indices
        }
    }

    const float& operator[](int index) const {
        switch (index) {                                                         // Provide const array-style access to components
        case 0: return x;                                                   // Index 0 returns x component
        case 1: return y;                                                   // Index 1 returns y component
        case 2: return z;                                                   // Index 2 returns z component
        default: return x;                                                  // Default to x component for invalid indices
        }
    }

    // String conversion for debugging and logging purposes
    std::string ToString() const {
        std::ostringstream oss;                                                 // Create string stream for formatting
        oss << "Vector3(" << x << ", " << y << ", " << z << ")";                // Format as readable string
        return oss.str();                                                       // Return formatted string
    }

    // Serialization for data persistence and network transmission
    std::string Serialize() const {
        std::ostringstream oss;                                                 // Create string stream for serialization
        oss << x << "," << y << "," << z;                                       // Format as comma-separated values
        return oss.str();                                                       // Return serialized string
    }

    // Deserialization from string data
    static Vector3 Deserialize(const std::string& str) {
        std::istringstream iss(str);                                            // Create input string stream
        float x, y, z;                                                          // Temporary storage for components
        char comma1, comma2;                                                    // Separator characters

        if (iss >> x >> comma1 >> y >> comma2 >> z &&                          // Parse components and separators
            comma1 == ',' && comma2 == ',') {                                   // Verify correct format
            return Vector3(x, y, z);                                            // Return parsed vector
        }
        return Vector3();                                                       // Return default vector if parsing fails
    }

    // Common predefined vectors for convenience
    static Vector3 Zero() { return Vector3(0.0f, 0.0f, 0.0f); }                // Zero vector
    static Vector3 One() { return Vector3(1.0f, 1.0f, 1.0f); }                 // Unit vector (all components = 1)
    static Vector3 UnitX() { return Vector3(1.0f, 0.0f, 0.0f); }               // Unit vector along X axis
    static Vector3 UnitY() { return Vector3(0.0f, 1.0f, 0.0f); }               // Unit vector along Y axis
    static Vector3 UnitZ() { return Vector3(0.0f, 0.0f, 1.0f); }               // Unit vector along Z axis
    static Vector3 Forward() { return Vector3(0.0f, 0.0f, 1.0f); }             // Forward direction vector
    static Vector3 Back() { return Vector3(0.0f, 0.0f, -1.0f); }               // Backward direction vector
    static Vector3 Up() { return Vector3(0.0f, 1.0f, 0.0f); }                  // Up direction vector
    static Vector3 Down() { return Vector3(0.0f, -1.0f, 0.0f); }               // Down direction vector
    static Vector3 Right() { return Vector3(1.0f, 0.0f, 0.0f); }               // Right direction vector
    static Vector3 Left() { return Vector3(-1.0f, 0.0f, 0.0f); }               // Left direction vector
};
#endif // VECTOR3_H_INCLUDED

// Global platform / renderer definitions (PLATFORM_WINDOWS / _LINUX / _ANDROID / _APPLE / _IOS,
// __USE_*__ renderer macros, NOMINMAX ...).  Placed here, after Vector2/3/4, because Includes.h
// includes this header on non-DirectXMath builds and needs those three classes already defined.
// If a project's Includes.h did not define any platform macro, derive one from the compiler macros
// so code below that tests PLATFORM_* still behaves.
#include "Includes.h"

#if !defined(PLATFORM_WINDOWS) && !defined(PLATFORM_LINUX) && !defined(PLATFORM_ANDROID) && \
    !defined(PLATFORM_APPLE)   && !defined(PLATFORM_IOS)
    #if defined(_WIN32) || defined(_WIN64)
        #define PLATFORM_WINDOWS
    #elif defined(__ANDROID__)
        #define PLATFORM_ANDROID
    #elif defined(__linux__)
        #define PLATFORM_LINUX
    #elif defined(__APPLE__)
        #define PLATFORM_APPLE
    #endif
#endif

// =====================================================================================
//  MyVector / MyQuaternion / MyMath
//  -------------------------------------------------------------------------------------
//  Full 3D vector toolkit for the CPGE engine.  It is renderer-agnostic (pure math) so the
//  exact same code drives DirectX 11, DirectX 12, OpenGL and Vulkan on every platform.
//
//  PERFORMANCE: trivial arithmetic (add, dot, cross, lerp ...) is inline in this header.
//  Everything that needs trigonometry (Angle, Slerp, Rotate*, quaternion maths) lives in
//  Vectors.cpp and is routed through the MathPrecalculation lookup tables (FAST_SIN,
//  FAST_COS, FAST_ATAN2 ...) instead of the C runtime.  Square roots use the hardware
//  sqrtss instruction: it is exact and faster than a table lookup + atomic statistics bump.
// =====================================================================================
#ifndef MYVECTOR_H_INCLUDED
#define MYVECTOR_H_INCLUDED

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <functional>
#include <limits>
#include <vector>

// -------------------------------------------------------------------------------------
//  MyMath - scalar helpers (the Unity "Mathf" subset the vector code depends on)
//  min/max are written out by hand so these headers never collide with the Windows
//  min/max macros, whatever include order a translation unit uses.
// -------------------------------------------------------------------------------------
namespace MyMath
{
    constexpr float kPi          = 3.14159265358979323846f;
    constexpr float kTwoPi       = 6.28318530717958647692f;
    constexpr float kHalfPi      = 1.57079632679489661923f;
    constexpr float kDeg2Rad     = kPi / 180.0f;                                    // degrees -> radians
    constexpr float kRad2Deg     = 180.0f / kPi;                                    // radians -> degrees
    constexpr float kEpsilon     = 1e-5f;                                           // Vector3.kEpsilon
    constexpr float kEpsilonNormalSqrt = 1e-15f;                                    // Vector3.kEpsilonNormalSqrt
    constexpr float kFloatMin    = 1.17549435e-38f;                                 // Mathf.Epsilon equivalent
    constexpr float kInfinity    = std::numeric_limits<float>::infinity();

    // ---- trivial inline helpers --------------------------------------------------------
    inline float Min(float a, float b)                  { return (a < b) ? a : b; }
    inline float Max(float a, float b)                  { return (a > b) ? a : b; }
    inline float Abs(float a)                           { return (a < 0.0f) ? -a : a; }
    inline float Sign(float f)                          { return (f >= 0.0f) ? 1.0f : -1.0f; }   // Mathf.Sign (0 -> +1)
    inline float Sqrt(float v)                          { return std::sqrt(v); }                  // hardware sqrtss
    inline float Clamp(float v, float lo, float hi)     { return (v < lo) ? lo : ((v > hi) ? hi : v); }
    inline float Clamp01(float v)                       { return (v < 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v); }
    // Shared radial blast response: no damage at/outside the radius; finite input only.
    inline float BlastFalloff(float distance, float radius)
    { return radius > 0.0f && std::isfinite(radius) && std::isfinite(distance) ? Clamp01(1.0f - (std::max)(0.0f, distance) / radius) : 0.0f; }
    // Vector score panels have nine digits. Keep long campaigns inside that domain.
    inline int AddScore(int score, int delta)
    { return static_cast<int>((std::clamp)(static_cast<int64_t>(score) + delta, int64_t(0), int64_t(999999999))); }
    inline float Lerp(float a, float b, float t)        { return a + (b - a) * Clamp01(t); }
    inline float LerpUnclamped(float a, float b, float t){ return a + (b - a) * t; }
    inline float InverseLerp(float a, float b, float v) { return (a != b) ? Clamp01((v - a) / (b - a)) : 0.0f; }
    inline float Repeat(float t, float length)          { return Clamp(t - std::floor(t / length) * length, 0.0f, length); }
    inline float PingPong(float t, float length)        { t = Repeat(t, length * 2.0f); return length - Abs(t - length); }
    inline float DeltaAngle(float cur, float target)    // shortest signed difference in degrees
    {
        float d = Repeat(target - cur, 360.0f);
        if (d > 180.0f) d -= 360.0f;
        return d;
    }
    inline float LerpAngle(float a, float b, float t)   // Mathf.LerpAngle / MathUtil.LerpAngle
    {
        float d = Repeat(b - a, 360.0f);
        if (d > 180.0f) d -= 360.0f;
        return a + d * Clamp01(t);
    }
    inline float MoveTowards(float cur, float target, float maxDelta)
    {
        if (Abs(target - cur) <= maxDelta) return target;
        return cur + Sign(target - cur) * maxDelta;
    }
    inline float MoveTowardsAngle(float cur, float target, float maxDelta)
    {
        float d = DeltaAngle(cur, target);
        if (-maxDelta < d && d < maxDelta) return target;
        return MoveTowards(cur, cur + d, maxDelta);
    }
    inline float SmoothStep(float from, float to, float t)
    {
        t = Clamp01(t);
        t = -2.0f * t * t * t + 3.0f * t * t;
        return to * t + from * (1.0f - t);
    }
    inline bool  Approximately(float a, float b)        // Mathf.Approximately
    {
        return Abs(b - a) < Max(1e-6f * Max(Abs(a), Abs(b)), kFloatMin * 8.0f);
    }
    inline float ToRad(float deg)                       { return deg * kDeg2Rad; }
    inline float ToDeg(float rad)                       { return rad * kRad2Deg; }               // NOTE: the game's own MathUtil.ToDeg multiplies by PI/180 (bug); this is the correct 180/PI.

    // Critically damped spring (Mathf.SmoothDamp)
    float SmoothDamp(float current, float target, float& currentVelocity, float smoothTime,
                     float maxSpeed = kInfinity, float deltaTime = 1.0f / 60.0f);

    // ---- table-driven trigonometry (defined in Vectors.cpp -> MathPrecalculation) ------
    float Sin(float radians);
    float Cos(float radians);
    void  SinCos(float radians, float& outSin, float& outCos);                                   // one lookup, both values
    void  SinCosDeg(float degrees, float& outSin, float& outCos);                                // degrees in, interpolated, O(1) wrap
    float Asin(float v);
    float Acos(float v);                                                                         // accurate everywhere (MathPrecalculation::FastACosAccurate)
    float Atan2(float y, float x);

    // Scales a particle count by a quality density (0..1) with stochastic rounding: the fractional part of count * density is the chance of one extra
    // particle, so a single-particle call thins out on average, and a call for two or more never drops to none.  r01 = a random number in [0,1).
    // Use it in any particle spawner so the vector quality setting reaches it:  n = MyMath::ScaleCount(n, density, Rand01());
    int ScaleCount(int count, float density, float r01);
}

class MyQuaternion;

// -------------------------------------------------------------------------------------
//  MyVector - 3D vector (position, direction, velocity ...).  Layout is exactly three
//  contiguous floats (12 bytes) so arrays of MyVector can be uploaded straight into a
//  vertex / constant buffer for any of the four renderers.
// -------------------------------------------------------------------------------------
class MyVector
{
public:
    float x, y, z;

    // ---- construction ------------------------------------------------------------------
    constexpr MyVector() : x(0.0f), y(0.0f), z(0.0f) {}
    constexpr MyVector(float x_, float y_) : x(x_), y(y_), z(0.0f) {}
    constexpr MyVector(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    explicit constexpr MyVector(float v) : x(v), y(v), z(v) {}
    MyVector(const Vector3& v) : x(v.x), y(v.y), z(v.z) {}                              // engine Vector3 -> MyVector
    explicit MyVector(const Vector2& v) : x(v.x), y(v.y), z(0.0f) {}
    explicit MyVector(const Vector4& v) : x(v.x), y(v.y), z(v.z) {}
    operator Vector3() const { return Vector3(x, y, z); }                               // MyVector -> engine Vector3

    // Generic bridge for any {x,y,z} type (DirectX::XMFLOAT3, glm::vec3, VkXYZ ...).
    template <class V> static MyVector FromXYZ(const V& v) { return MyVector(v.x, v.y, v.z); }
    template <class V> V ToXYZ() const { V v; v.x = x; v.y = y; v.z = z; return v; }
    Vector4 ToVector4(float w = 0.0f) const { return Vector4(x, y, z, w); }

    // ---- constants (Vector3.zero / one / up ...) ---------------------------------------
    static constexpr MyVector Zero()              { return MyVector(0.0f, 0.0f, 0.0f); }
    static constexpr MyVector One()               { return MyVector(1.0f, 1.0f, 1.0f); }
    static constexpr MyVector Up()                { return MyVector(0.0f, 1.0f, 0.0f); }
    static constexpr MyVector Down()              { return MyVector(0.0f, -1.0f, 0.0f); }
    static constexpr MyVector Left()              { return MyVector(-1.0f, 0.0f, 0.0f); }
    static constexpr MyVector Right()             { return MyVector(1.0f, 0.0f, 0.0f); }
    static constexpr MyVector Forward()           { return MyVector(0.0f, 0.0f, 1.0f); }
    static constexpr MyVector Back()              { return MyVector(0.0f, 0.0f, -1.0f); }
    static constexpr MyVector PositiveInfinity()  { return MyVector(MyMath::kInfinity, MyMath::kInfinity, MyMath::kInfinity); }
    static constexpr MyVector NegativeInfinity()  { return MyVector(-MyMath::kInfinity, -MyMath::kInfinity, -MyMath::kInfinity); }

    // ---- element access ----------------------------------------------------------------
    float& operator[](int i)                      { return (i == 0) ? x : ((i == 1) ? y : z); }   // out-of-range -> z (never UB)
    float  operator[](int i) const                { return (i == 0) ? x : ((i == 1) ? y : z); }
    void   Set(float nx, float ny, float nz)      { x = nx; y = ny; z = nz; }
    const float* Data() const                     { return &x; }                                  // 3 contiguous floats

    // ---- arithmetic operators ----------------------------------------------------------
    MyVector operator+(const MyVector& o) const   { return MyVector(x + o.x, y + o.y, z + o.z); }
    MyVector operator-(const MyVector& o) const   { return MyVector(x - o.x, y - o.y, z - o.z); }
    MyVector operator-() const                    { return MyVector(-x, -y, -z); }
    MyVector operator*(float s) const             { return MyVector(x * s, y * s, z * s); }
    MyVector operator/(float s) const             { const float inv = 1.0f / s; return MyVector(x * inv, y * inv, z * inv); }   // one divide, three multiplies
    friend MyVector operator*(float s, const MyVector& v) { return v * s; }
    MyVector& operator+=(const MyVector& o)       { x += o.x; y += o.y; z += o.z; return *this; }
    MyVector& operator-=(const MyVector& o)       { x -= o.x; y -= o.y; z -= o.z; return *this; }
    MyVector& operator*=(float s)                 { x *= s; y *= s; z *= s; return *this; }
    MyVector& operator/=(float s)                 { const float inv = 1.0f / s; x *= inv; y *= inv; z *= inv; return *this; }
    // Unity semantics: approximately equal when the squared distance is below 1e-10.
    bool operator==(const MyVector& o) const      { return (*this - o).SqrMagnitude() < 9.99999944e-11f; }
    bool operator!=(const MyVector& o) const      { return !(*this == o); }
    bool Equals(const MyVector& o) const          { return x == o.x && y == o.y && z == o.z; }     // exact, bitwise-style compare

    // ---- length ------------------------------------------------------------------------
    float Magnitude() const                       { return std::sqrt(x * x + y * y + z * z); }
    float SqrMagnitude() const                    { return x * x + y * y + z * z; }
    static float Magnitude(const MyVector& v)     { return v.Magnitude(); }
    static float SqrMagnitude(const MyVector& v)  { return v.SqrMagnitude(); }

    // ---- normalisation -----------------------------------------------------------------
    // Vector3.Normalize: vectors shorter than kEpsilon collapse to zero instead of producing NaN.
    void Normalize()
    {
        const float m = Magnitude();
        if (m > MyMath::kEpsilon) { const float inv = 1.0f / m; x *= inv; y *= inv; z *= inv; }
        else                      { x = 0.0f; y = 0.0f; z = 0.0f; }
    }
    MyVector Normalized() const                   { MyVector r(*this); r.Normalize(); return r; }
    static MyVector Normalize(const MyVector& v)  { return v.Normalized(); }

    // ---- products / distances ----------------------------------------------------------
    float Dot(const MyVector& o) const            { return x * o.x + y * o.y + z * o.z; }
    static float Dot(const MyVector& a, const MyVector& b) { return a.Dot(b); }
    MyVector Cross(const MyVector& o) const       { return MyVector(y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x); }
    static MyVector Cross(const MyVector& a, const MyVector& b) { return a.Cross(b); }
    static float Distance(const MyVector& a, const MyVector& b)    { return (a - b).Magnitude(); }
    static float SqrDistance(const MyVector& a, const MyVector& b) { return (a - b).SqrMagnitude(); }
    MyVector Scale(const MyVector& o) const       { return MyVector(x * o.x, y * o.y, z * o.z); }      // component-wise multiply
    static MyVector Scale(const MyVector& a, const MyVector& b)   { return a.Scale(b); }
    static MyVector Min(const MyVector& a, const MyVector& b)     { return MyVector(MyMath::Min(a.x, b.x), MyMath::Min(a.y, b.y), MyMath::Min(a.z, b.z)); }
    static MyVector Max(const MyVector& a, const MyVector& b)     { return MyVector(MyMath::Max(a.x, b.x), MyMath::Max(a.y, b.y), MyMath::Max(a.z, b.z)); }

    // ---- projection / reflection -------------------------------------------------------
    static MyVector Reflect(const MyVector& inDirection, const MyVector& inNormal)   // inNormal must be unit length
    {
        const float f = -2.0f * inNormal.Dot(inDirection);
        return MyVector(f * inNormal.x + inDirection.x, f * inNormal.y + inDirection.y, f * inNormal.z + inDirection.z);
    }
    static MyVector Project(const MyVector& v, const MyVector& onNormal)             // component of v along onNormal
    {
        const float sq = onNormal.Dot(onNormal);
        if (sq < MyMath::kFloatMin) return MyVector();
        return onNormal * (v.Dot(onNormal) / sq);
    }
    static MyVector ProjectOnPlane(const MyVector& v, const MyVector& planeNormal)   // v with its plane-normal component removed
    {
        const float sq = planeNormal.Dot(planeNormal);
        if (sq < MyMath::kFloatMin) return v;
        return v - planeNormal * (v.Dot(planeNormal) / sq);
    }
    static MyVector Exclude(const MyVector& excludeThis, const MyVector& fromThat)   // Vector3.Exclude (internal in Unity)
    {
        return fromThat - Project(fromThat, excludeThis);
    }
    // Game helper FORGE3D.F3DMath.ProjectVectorOnPlane (assumes a UNIT plane normal; argument order matches the game).
    static MyVector ProjectVectorOnPlane(const MyVector& planeNormal, const MyVector& v) { return v - planeNormal * v.Dot(planeNormal); }
    static MyVector ClampMagnitude(const MyVector& v, float maxLength)
    {
        const float sq = v.SqrMagnitude();
        if (sq > maxLength * maxLength) return v * (maxLength / std::sqrt(sq));
        return v;
    }

    // ---- interpolation / movement ------------------------------------------------------
    static MyVector Lerp(const MyVector& a, const MyVector& b, float t)
    {
        t = MyMath::Clamp01(t);
        return MyVector(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
    }
    static MyVector LerpUnclamped(const MyVector& a, const MyVector& b, float t)
    {
        return MyVector(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
    }
    static MyVector MoveTowards(const MyVector& current, const MyVector& target, float maxDistanceDelta)
    {
        const MyVector to = target - current;
        const float sq = to.SqrMagnitude();
        if (sq == 0.0f || (maxDistanceDelta >= 0.0f && sq <= maxDistanceDelta * maxDistanceDelta)) return target;
        return current + to * (maxDistanceDelta / std::sqrt(sq));
    }
    // Critically damped spring toward target (Vector3.SmoothDamp).  currentVelocity is caller state.
    static MyVector SmoothDamp(const MyVector& current, const MyVector& target, MyVector& currentVelocity,
                               float smoothTime, float maxSpeed = MyMath::kInfinity, float deltaTime = 1.0f / 60.0f);

    // ---- trigonometry based (table driven, see Vectors.cpp) ---------------------------
    static float Angle(const MyVector& from, const MyVector& to);                      // unsigned degrees [0,180]
    static float SignedAngle(const MyVector& from, const MyVector& to, const MyVector& axis);   // signed degrees (-180,180]
    static float AngleBetween(const MyVector& from, const MyVector& to);               // unsigned RADIANS (Vector3.AngleBetween)
    static float SignedVectorAngle(const MyVector& reference, const MyVector& other, const MyVector& normal); // F3DMath.SignedVectorAngle
    static MyVector Slerp(const MyVector& a, const MyVector& b, float t);              // spherical, t clamped
    static MyVector SlerpUnclamped(const MyVector& a, const MyVector& b, float t);
    static MyVector RotateTowards(const MyVector& current, const MyVector& target, float maxRadiansDelta, float maxMagnitudeDelta);
    static void OrthoNormalize(MyVector& normal, MyVector& tangent);                   // Gram-Schmidt, 2 vectors
    static void OrthoNormalize(MyVector& normal, MyVector& tangent, MyVector& binormal);

    // ---- rotation of THIS vector (degrees, right-handed formula about the stated axis) -
    MyVector RotatedX(float degrees) const;
    MyVector RotatedY(float degrees) const;
    MyVector RotatedZ(float degrees) const;
    MyVector RotatedAround(const MyVector& axis, float degrees) const;                 // Rodrigues' formula
    MyVector Rotated(const MyQuaternion& q) const;                                     // same as q * v

    // Rotate N vectors about one axis with ONE table lookup for the whole batch.  This is the
    // hot-path version for meshes / particle clouds: per-vertex cost is a handful of multiplies.
    static void RotateAroundBatch(MyVector* v, size_t count, const MyVector& axis, float degrees);

    // ---- game helpers recovered from Assembly-CSharp.dll (MathUtil) ---------------------
    static float DirectionTo2dDeg(float x, float y);                                   // heading in [0,360), 0 = +Y, counter-clockwise
    static MyVector ReflectionDirectionFromAngle(float angleDeg);                      // snaps to one of the 4 axis normals
    static MyVector LineDirectionFromAngle(float angleDeg);                            // same with wider up/down cones
    static MyVector NormalFromVelocity(const MyVector& direction);                     // MathUtil.GetNormalFromVel
    static MyVector Direction(const MyVector& from, const MyVector& to) { return to - from; }

    // ---- text / persistence ------------------------------------------------------------
    std::string ToString() const;                                                      // "MyVector(x, y, z)"
    std::string Serialize() const;                                                     // "x,y,z"
    static MyVector Deserialize(const std::string& s);                                 // zero vector when malformed
    size_t GetHashCode() const;
};

// -------------------------------------------------------------------------------------
//  MyQuaternion - rotation quaternion (Unity Quaternion subset).  Euler order Z, X, Y.
// -------------------------------------------------------------------------------------
class MyQuaternion
{
public:
    float x, y, z, w;

    constexpr MyQuaternion() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}                   // identity
    constexpr MyQuaternion(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    static constexpr MyQuaternion Identity() { return MyQuaternion(0.0f, 0.0f, 0.0f, 1.0f); }

    // Hamilton product: (a * b) applies b first, then a.
    MyQuaternion operator*(const MyQuaternion& o) const
    {
        return MyQuaternion(w * o.x + x * o.w + y * o.z - z * o.y,
                            w * o.y + y * o.w + z * o.x - x * o.z,
                            w * o.z + z * o.w + x * o.y - y * o.x,
                            w * o.w - x * o.x - y * o.y - z * o.z);
    }
    // Rotate a vector: v' = v + 2w(q x v) + 2 q x (q x v)  - no matrix needed, 18 multiplies.
    MyVector operator*(const MyVector& v) const
    {
        const MyVector q(x, y, z);
        const MyVector t = q.Cross(v) * 2.0f;
        return v + t * w + q.Cross(t);
    }
    bool operator==(const MyQuaternion& o) const { return Dot(*this, o) > 0.999999f; }
    bool operator!=(const MyQuaternion& o) const { return !(*this == o); }

    void Set(float nx, float ny, float nz, float nw) { x = nx; y = ny; z = nz; w = nw; }
    static float Dot(const MyQuaternion& a, const MyQuaternion& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
    MyQuaternion Conjugate() const { return MyQuaternion(-x, -y, -z, w); }
    static MyQuaternion Inverse(const MyQuaternion& q);                                // conjugate / |q|^2
    void Normalize();
    MyQuaternion Normalized() const { MyQuaternion r(*this); r.Normalize(); return r; }

    // ---- factories (angles in degrees) -------------------------------------------------
    static MyQuaternion AngleAxis(float degrees, const MyVector& axis);
    static MyQuaternion Euler(float xDeg, float yDeg, float zDeg);
    static MyQuaternion Euler(const MyVector& eulerDeg) { return Euler(eulerDeg.x, eulerDeg.y, eulerDeg.z); }
    static MyQuaternion LookRotation(const MyVector& forward, const MyVector& up = MyVector::Up());
    static MyQuaternion FromToRotation(const MyVector& from, const MyVector& to);      // trig-free half-vector method

    // ---- queries -----------------------------------------------------------------------
    MyVector EulerAngles() const;                                                      // degrees, [0,360)
    void ToAngleAxis(float& outDegrees, MyVector& outAxis) const;
    static float Angle(const MyQuaternion& a, const MyQuaternion& b);                  // degrees between two rotations

    // ---- interpolation -----------------------------------------------------------------
    static MyQuaternion Lerp(const MyQuaternion& a, const MyQuaternion& b, float t);   // normalised lerp, t clamped
    static MyQuaternion LerpUnclamped(const MyQuaternion& a, const MyQuaternion& b, float t);
    static MyQuaternion Slerp(const MyQuaternion& a, const MyQuaternion& b, float t);  // shortest-arc, t clamped
    static MyQuaternion SlerpUnclamped(const MyQuaternion& a, const MyQuaternion& b, float t);
    static MyQuaternion RotateTowards(const MyQuaternion& from, const MyQuaternion& to, float maxDegreesDelta);

    std::string ToString() const;
};

// -------------------------------------------------------------------------------------
//  MyGeom - small geometry helpers built on MyVector / MyQuaternion.  They were global helper
//  functions in main.cpp's vector demo; they hold no state, so they live with the vector system.
//  Names are kept as they were so callers only need "using namespace MyGeom;".
//  Convention for the 2-D helpers: a game position stores u in x and v in z (the ground plane).
// -------------------------------------------------------------------------------------
namespace MyGeom
{
    // Axis-aligned rectangle: x0..x1 along the first axis, y0..y1 along the second.
    struct Rect2 { float x0, x1, y0, y1; };

    // 3x3 rotation matrix (row-major) built from a quaternion.  Everything is inline and `m` is
    // deliberately left uninitialised by the default constructor: Mul() runs once per vertex.
    struct Mat3
    {
        float m[9];

        static Mat3 FromQuat(const MyQuaternion& q)
        {
            const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
            const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
            const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
            Mat3 r;
            r.m[0] = 1.0f - 2.0f * (yy + zz); r.m[1] = 2.0f * (xy - wz);        r.m[2] = 2.0f * (xz + wy);
            r.m[3] = 2.0f * (xy + wz);        r.m[4] = 1.0f - 2.0f * (xx + zz); r.m[5] = 2.0f * (yz - wx);
            r.m[6] = 2.0f * (xz - wy);        r.m[7] = 2.0f * (yz + wx);        r.m[8] = 1.0f - 2.0f * (xx + yy);
            return r;
        }
        MyVector Mul(const MyVector& v) const
        {
            return MyVector(m[0] * v.x + m[1] * v.y + m[2] * v.z,
                            m[3] * v.x + m[4] * v.y + m[5] * v.z,
                            m[6] * v.x + m[7] * v.y + m[8] * v.z);
        }
        // This matrix times diag(s): folds a per-axis scale into the matrix, so Scaled(s).Mul(v) == Mul(v.Scale(s)).
        // Build it once per mesh instance and each vertex then costs ONE matrix multiply instead of a scale plus a multiply.
        Mat3 Scaled(const MyVector& s) const
        {
            Mat3 r;
            for (int row = 0; row < 3; ++row)
            {
                r.m[row * 3 + 0] = m[row * 3 + 0] * s.x;
                r.m[row * 3 + 1] = m[row * 3 + 1] * s.y;
                r.m[row * 3 + 2] = m[row * 3 + 2] * s.z;
            }
            return r;
        }
        Mat3 operator*(const Mat3& o) const
        {
            Mat3 r;
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 3; ++col)
                    r.m[row * 3 + col] = m[row * 3 + 0] * o.m[0 + col] + m[row * 3 + 1] * o.m[3 + col] + m[row * 3 + 2] * o.m[6 + col];
            return r;
        }
    };

    // Unit vector in the ground plane: 0 degrees = +Z (up the screen), clockwise.  One table lookup.
    inline MyVector HeadingDir(float deg)
    {
        float s, c;
        MyMath::SinCosDeg(deg, s, c);
        return MyVector(s, 0.0f, c);
    }

    // Any unit vector tangent to the unit sphere at n (n must be a unit vector).
    inline MyVector TangentRef(const MyVector& n)
    {
        const MyVector a = (MyMath::Abs(n.y) < 0.9f) ? MyVector::Up() : MyVector::Right();
        return a.Cross(n).Normalized();
    }

    // Rotates the 2-D vector (du, dv) by `deg` degrees into (ou, ov).  One table lookup.
    inline void Rotate2D(float du, float dv, float deg, float& ou, float& ov)
    {
        float s, c;
        MyMath::SinCosDeg(deg, s, c);
        ou = du * c - dv * s;
        ov = du * s + dv * c;
    }

    // Toroidal playfield: a position that left one side of `f` re-enters on the opposite side.
    inline void WrapInto(MyVector& p, const Rect2& f)
    {
        const float w = f.x1 - f.x0, h = f.y1 - f.y0;
        if (p.x < f.x0) p.x += w; else if (p.x > f.x1) p.x -= w;
        if (p.z < f.y0) p.z += h; else if (p.z > f.y1) p.z -= h;
    }

    // Orientation that points a +X-nosed mesh along `dir` (horizontal), keeping it level.
    // Identity for a zero vector.
    MyQuaternion FaceAlongX(const MyVector& dir);

    // Pushes a disc (radius r) out of one solid block through the side with the smallest penetration,
    // reflecting the velocity about that side's normal (scaled by restitution).  True when it bounced.
    bool BounceBlock(MyVector& pos, MyVector& vel, float r, const Rect2& inner, float restitution);

    // Bounces a disc (radius r) inside `outer` and around the solid block `inner`.  Every contact
    // reflects the velocity about the wall normal (MyVector::Reflect) and scales it by `restitution`.
    bool BounceArena(MyVector& pos, MyVector& vel, float r, const Rect2& outer, const Rect2& inner, float restitution);
}

// -------------------------------------------------------------------------------------
//  MyCubeExplosion - cube-particle explosion effect (pooled, allocation free in play).
//  Spawn() throws a burst of small cubes outward in uniformly random directions; every
//  cube tumbles on all three axes while it flies, slows under drag, optionally falls under
//  gravity and shrinks / fades as its life runs out.  The class is pure math (no renderer
//  dependency): the caller draws each live particle as a cube, so the same effect works on
//  DirectX 11, DirectX 12, OpenGL and Vulkan.
//
//      static MyCubeExplosion boom(512);                       // once: reserves the pool
//      boom.Spawn(target.pos, 24, 6.0f, 0.22f, 1.1f, 255, 160, 40);   // on a kill
//      boom.Update(dt);                                        // every frame
//      for (size_t i = 0; i < boom.Capacity(); ++i) {          // draw
//          const MyCubeParticle& p = boom.At(i);
//          if (p.life > 0.0f) DrawCube(p.pos, p.orient, p.size * MyCubeExplosion::Fade(p), p.r, p.g, p.b);
//      }
// -------------------------------------------------------------------------------------
struct MyCubeParticle
{
    MyVector     pos, vel;            // world position / velocity (units per second)
    MyQuaternion orient;              // current orientation of the cube
    MyVector     spinAxis;            // unit tumble axis
    float        spinRate = 0.0f;     // tumble speed in degrees per second
    float        life = 0.0f;         // seconds remaining; <= 0 means the pool slot is free
    float        maxLife = 0.0f;      // lifetime at birth (for the fade)
    float        size = 0.0f;         // cube edge length at birth
    uint8_t      r = 255, g = 255, b = 255;
};

class MyCubeExplosion
{
public:
    explicit MyCubeExplosion(size_t capacity = 512, uint32_t seed = 0xC0BE5EEDu);

    // Throws `count` cubes from origin.  speed = fastest outward speed, size = typical edge length,
    // lifetime = typical seconds of life, colour jittered per cube.  inheritVelocity lets the debris
    // keep the momentum of whatever exploded.  When the pool is full the oldest slots are recycled.
    void Spawn(const MyVector& origin, int count, float speed, float size, float lifetime,
               uint8_t r, uint8_t g, uint8_t b, const MyVector& inheritVelocity = MyVector());

    // Advances every live particle.  drag is the fraction of velocity lost per second.
    void Update(float dt, const MyVector& gravity = MyVector(), float drag = 0.8f);

    void   Clear();                                                  // kills every particle
    size_t AliveCount() const { return m_alive; }
    size_t Capacity() const   { return m_pool.size(); }
    const MyCubeParticle& At(size_t i) const { return m_pool[i]; }   // check life > 0 before drawing
    // Moves every live particle by `d` (no allocation).  For a game whose world WRAPS (Defender): when the camera wraps round the seam, the debris is shifted by the same amount so it stays where the player sees it.
    void Shift(const MyVector& d) { for (MyCubeParticle& p : m_pool) if (p.life > 0.0f) p.pos += d; }
    // Rotates every live particle rigidly about `centre` (positions, velocities and cube orientations): debris that belongs to a turning world (a rolling planet) turns with it.
    void Rotate(const MyQuaternion& q, const MyVector& centre) { for (MyCubeParticle& p : m_pool) if (p.life > 0.0f) { p.pos = centre + q * (p.pos - centre); p.vel = q * p.vel; p.spinAxis = q * p.spinAxis; p.orient = (q * p.orient).Normalized(); } }

    // Quality control, changeable at any time (no restart, no allocation): Spawn() creates count * density cubes, rounded stochastically
    // (the fraction is the chance of one extra cube), so a call for a single cube - a smoke trail - thins out on average, while a call for
    // two or more always makes at least one.  1 = full effect; the vector-quality settings lower it so big blasts cost less on slow machines.
    void   SetDensity(float density) { m_density = (density < 0.05f) ? 0.05f : ((density > 1.0f) ? 1.0f : density); }
    float  Density() const { return m_density; }

    // 1 at birth, 0 at death - multiply the size (or brightness) by it for a shrink / fade.
    static float Fade(const MyCubeParticle& p) { return (p.maxLife > 0.0f) ? MyMath::Clamp01(p.life / p.maxLife) : 0.0f; }

private:
    std::vector<MyCubeParticle> m_pool;
    size_t   m_next  = 0;                                            // next slot to (re)use
    size_t   m_alive = 0;
    float    m_density = 1.0f;                                       // see SetDensity()
    uint32_t m_rng;
    float    Rand01();
    float    Rand(float lo, float hi) { return lo + (hi - lo) * Rand01(); }
    MyVector RandomUnit();
};

// ================================================================================================
//  MyExplosionFX - a family of explosion effects that go beyond the cube-debris burst (MyCubeExplosion).  Pure math, no renderer
//  dependency: the caller draws each live particle according to its type (circles for shock-waves, balls for fireballs, streaks for
//  sparks ...), so the same effects work on DirectX 11 / 12, OpenGL and Vulkan.
//
//    Shockwave  a thin ring that RIPPLES OUTWARD from the blast, getting thinner and fading as it stretches (optionally with a second,
//               fainter ripple trailing behind it).  `normal` is the plane the ring lies in: the ground plane for top-down games, the
//               direction the camera looks in for side-scrollers, so it always reads as a circle.
//    Fireball   a glowing sphere that swells quickly and cools from white-hot through yellow and orange to dark red as it fades.
//    Sparks     fast bright streaks that fly outward and fade, lengthened along their direction of travel.
//    Smoke      slow grey puffs that drift, swell and fade.
//    Implosion  a ring that collapses INTO a point (charge-up, energy being drawn in, an enemy being sucked away).
//    Flak       a quick black puff with a red core (a hit on armour, an anti-aircraft burst).
//    Blast / Nova   composites: a fireball + shock-wave + sparks (+ smoke); Nova is the big one with two shock-waves and a lot more of
//               everything, for bosses and the player's ship.
//
//      static MyExplosionFX fx(160);                                   // once: reserves the pool
//      fx.Nova(pos, normal, 3.0f, 255, 160, 60, shipVelocity);         // a big kill
//      fx.Shockwave(pos, normal, 0.5f, 12.0f, 0.9f, 0.25f, 120, 220, 255);   // a smart-bomb ring
//      fx.Update(dt);                                                  // every frame
//      for (size_t i = 0; i < fx.Capacity(); ++i) {                    // draw
//          const MyExpParticle& p = fx.At(i);
//          if (p.life > 0.0f) Draw(p, MyExplosionFX::Radius(p), MyExplosionFX::Alpha(p));
//      }
// ================================================================================================
enum class MyExpType : uint8_t { Shockwave = 0, Fireball, Sparks, Smoke, Implosion, Flak };

struct MyExpParticle
{
    MyExpType type = MyExpType::Sparks;
    MyVector  pos, vel, normal;        // normal: the plane of a ring (Shockwave / Implosion)
    float     life = 0.0f;             // seconds remaining; <= 0 means the pool slot is free
    float     maxLife = 0.0f;
    float     r0 = 0.0f, r1 = 0.0f;    // radius at birth / death (rings, balls, puffs)
    float     thickness = 0.1f;        // half-width of a ring at birth
    float     size = 0.1f;             // spark streak length
    float     drag = 0.0f;             // fraction of velocity lost per second
    int       ripples = 1;             // rings: 1 or 2 (the second trails behind, fainter)
    uint8_t   r = 255, g = 255, b = 255;
};

class MyExplosionFX
{
public:
    explicit MyExplosionFX(size_t capacity = 160, uint32_t seed = 0x5EEDF00Du);

    // ---- single effects --------------------------------------------------------------------------
    void Shockwave(const MyVector& origin, const MyVector& normal, float startRadius, float endRadius, float life, float thickness,
                   uint8_t r, uint8_t g, uint8_t b, int ripples = 2);
    void Implosion(const MyVector& origin, const MyVector& normal, float startRadius, float life, uint8_t r, uint8_t g, uint8_t b);
    void Fireball(const MyVector& origin, float radius, float life, uint8_t r, uint8_t g, uint8_t b);
    void Sparks(const MyVector& origin, int count, float speed, float length, float life, uint8_t r, uint8_t g, uint8_t b,
                const MyVector& inheritVelocity = MyVector());
    void Smoke(const MyVector& origin, int count, float speed, float size, float life, uint8_t shade = 90);
    void Flak(const MyVector& origin, float size, float life);

    // ---- composites ------------------------------------------------------------------------------
    void Blast(const MyVector& origin, const MyVector& normal, float size, uint8_t r, uint8_t g, uint8_t b, const MyVector& inheritVelocity = MyVector());
    void Nova(const MyVector& origin, const MyVector& normal, float size, uint8_t r, uint8_t g, uint8_t b, const MyVector& inheritVelocity = MyVector());

    void Update(float dt, const MyVector& gravity = MyVector());
    void   Clear();
    size_t AliveCount() const { return m_alive; }
    size_t Capacity() const   { return m_pool.size(); }
    const MyExpParticle& At(size_t i) const { return m_pool[i]; }
    void Shift(const MyVector& d) { for (MyExpParticle& p : m_pool) if (p.life > 0.0f) p.pos += d; }
    void Rotate(const MyQuaternion& q, const MyVector& centre) { for (MyExpParticle& p : m_pool) if (p.life > 0.0f) { p.pos = centre + q * (p.pos - centre); p.vel = q * p.vel; p.normal = q * p.normal; } }      // see MyCubeExplosion::Rotate      // see MyCubeExplosion::Shift (wrapping worlds)

    // Quality control, changeable at any time: Sparks() and Smoke() (and so Blast / Nova) create count * density particles, rounded
    // stochastically (a call for one spark at density 0.4 makes one 40% of the time; a call for two or more always makes at least one).
    // Rings, fireballs and flak puffs are single dedicated particles and are not affected.
    void   SetDensity(float density) { m_density = (density < 0.05f) ? 0.05f : ((density > 1.0f) ? 1.0f : density); }
    float  Density() const { return m_density; }

    // ---- per-particle values for the renderer ----------------------------------------------------
    static float Progress(const MyExpParticle& p) { return (p.maxLife > 0.0f) ? MyMath::Clamp01(1.0f - p.life / p.maxLife) : 1.0f; }   // 0 at birth, 1 at death
    static float Radius(const MyExpParticle& p);      // current radius: rings and balls ease out (fast then slow); an implosion eases in
    static float Alpha(const MyExpParticle& p);       // 0..1 opacity / brightness: a shock-wave fades as it stretches out

private:
    std::vector<MyExpParticle> m_pool;
    size_t   m_next  = 0;
    size_t   m_alive = 0;
    float    m_density = 1.0f;                                       // see SetDensity()
    uint32_t m_rng;
    MyExpParticle& Slot();
    float    Rand01();
    float    Rand(float lo, float hi) { return lo + (hi - lo) * Rand01(); }
    MyVector RandomUnit();
};

// ================================================================================================
//  MyBlastStyles - twenty-three self-contained "signature" kill explosions with their OWN pool (independent of MyCubeExplosion / MyExplosionFX, so
//  they never recycle or disturb those particles).  Pure math; the caller draws each live particle by type (see SubmitBlastStyles in main.cpp).
//
//    Firework   a spherical burst of embers that arc down under gravity, followed by a late crackle of second-stage pops
//    Starburst  a flash with long glinting rays that shoot out in the view plane and a thin halo ring
//    Vortex     embers flung out along two spiral arms around a core
//    Shatter    the target breaks into flat tumbling plates (like glass) plus a few hot sparks
//    Bloom      three glowing shells swell out one after another (a plasma / nebula bloom) with drifting spores
//    Collapse   the target is sucked INTO a point (embers race inward, a ring shrinks) and then detonates outward
//    Confetti   a pop of fluttering coloured paper
//    Lightning  jagged bolts crawl outward from the blast, flickering
//    Gyro       three orthogonal rings tumble outward like a gyroscope
//    Pixelate   axis-aligned cubes that snap to a coarse grid and blink out (digital / retro disintegration)
//    Supernova  a huge white flash, two tilted shock rings, long white rays and a far-flung shower of hot embers
//    Meteors    streaks that fall away in a downward fan, each with a trail of embers, staggered like a meteor shower
//    Fountain   a flat base ring and a stream of embers jetting UP in a narrow cone and arcing back down under gravity
//    Mushroom   a rising stem of embers, a billowing cap (two swelling orbs and a flat ring) and a ground shock ring
//    Snowflake  six symmetric ice arms with two levels of side branches in the view plane, and drifting ice sparkles
//    Galaxy     three slow spiral arms of stars curling out round a bright core
//    Petals     eight curling petals of embers opening round the centre like a plasma flower
//    Pulse      five thin shock rings sent out one after another, like a sonar ping
//    Fireflies  a cloud of slow glowing motes that wander and fade, no gravity
//    Flame      a directed jet of fire along the normal: white-hot at the mouth, yellow, then red, rising
//    NovaCross  a lens-flare cross: four long rays and four short diagonal ones with a ring and sparks along the axes
//    CubeCascade heavy tumbling cubes that fall like blocks, with a quick flash
//    Helix      two strands of embers spiralling out of the blast like a double helix
//
//      static MyBlastStyles bs(220);
//      bs.Spawn(bs.Pick(allowedMask), pos, normal, 3.0f, 255, 160, 60, vel);   // a kill
//      bs.Update(dt, gravity);                                                 // every frame
// ================================================================================================
enum class MyBlastStyle : uint8_t { Firework = 0, Starburst, Vortex, Shatter, Bloom, Collapse, Confetti, Lightning, Gyro, Pixelate,
                                    Supernova, Meteors, Fountain, Mushroom, Snowflake, Galaxy, Petals, Pulse, Fireflies, Flame, NovaCross, CubeCascade, Helix, Count };
enum class MyBlastType  : uint8_t { Ember = 0, Shard, Bolt, Ring, Orb };

struct MyBlastParticle
{
    MyBlastType type = MyBlastType::Ember;
    MyVector    pos, vel;
    MyVector    dir;                    // Bolt: its direction; Ring: its plane normal; Collapse ember: the outward direction
    MyVector    spinAxis;               // spin is about this axis: an ember's velocity curls, a ring's plane tumbles, a shard tumbles
    MyQuaternion orient;                // shards
    float       life = 0.0f;            // seconds remaining; <= 0 means the pool slot is free
    float       maxLife = 0.0f;
    float       delay = 0.0f;           // > 0: simulated but not yet visible and not ageing (staggered stages)
    float       size = 0.1f;            // ember / shard edge, bolt length
    float       thick = 0.0f;           // ring half-width; pixel-grid step for Pixelate shards
    float       flat = 1.0f;            // shard thickness relative to its size (small = a plate)
    float       drag = 0.0f;            // fraction of velocity lost per second
    float       grav = 1.0f;            // multiplier on the gravity passed to Update()
    float       spin = 0.0f;            // radians per second about spinAxis
    float       flip = 0.0f;            // Collapse embers: when life drops to this, switch from inward to outward
    float       r0 = 0.0f, r1 = 0.0f;   // ring / orb radius at birth and death (Collapse embers: outward speed in r1)
    uint8_t     r = 255, g = 255, b = 255;
};

class MyBlastStyles
{
public:
    explicit MyBlastStyles(size_t capacity = 220, uint32_t seed = 0xB1A57E5u);

    // Picks one style out of the bits set in `allowedMask` (bit = 1 << style), never the same one twice in a row when there is a choice.
    MyBlastStyle Pick(uint32_t allowedMask);
    // size ~ 1..8 (blast radius in world units), normal = plane of rings / vortex (see MyExplosionFX), colour = the target's colour.
    void Spawn(MyBlastStyle style, const MyVector& origin, const MyVector& normal, float size, uint8_t r, uint8_t g, uint8_t b,
               const MyVector& inheritVelocity = MyVector());
    float confettiDim = 1.0f;                // brightness multiplier (0..1) for the NEXT Confetti burst's paper and pop flash: the Racing game sets it low at night (the burst otherwise ignores the colour passed in)
    void Update(float dt, const MyVector& gravity = MyVector());
    void   Clear();
    size_t Capacity() const { return m_pool.size(); }
    size_t AliveCount() const { return m_alive; }
    const MyBlastParticle& At(size_t i) const { return m_pool[i]; }      // draw only when life > 0 and delay <= 0
    void Shift(const MyVector& d) { for (MyBlastParticle& p : m_pool) if (p.life > 0.0f) p.pos += d; }
    void Rotate(const MyQuaternion& q, const MyVector& centre) { for (MyBlastParticle& p : m_pool) if (p.life > 0.0f) { p.pos = centre + q * (p.pos - centre); p.vel = q * p.vel; p.dir = q * p.dir; p.spinAxis = q * p.spinAxis; p.orient = (q * p.orient).Normalized(); } }      // see MyCubeExplosion::Rotate   // see MyCubeExplosion::Shift (wrapping worlds)
    void   SetDensity(float density) { m_density = (density < 0.05f) ? 0.05f : ((density > 1.0f) ? 1.0f : density); }

    static float Progress(const MyBlastParticle& p) { return (p.maxLife > 0.0f) ? MyMath::Clamp01(1.0f - p.life / p.maxLife) : 1.0f; }   // 0 birth .. 1 death
    static float Radius(const MyBlastParticle& p);     // rings / orbs: eased between r0 and r1
    static float Alpha(const MyBlastParticle& p);      // 0..1 brightness

private:
    std::vector<MyBlastParticle> m_pool;
    size_t   m_next = 0;
    size_t   m_alive = 0;
    float    m_density = 1.0f;
    int      m_last = -1;
    uint32_t m_rng;
    MyBlastParticle& Slot();
    int      Count(int n) { return MyMath::ScaleCount(n, m_density, Rand01()); }   // quality scaling (stochastic rounding)
    float    Rand01();
    float    Rand(float lo, float hi) { return lo + (hi - lo) * Rand01(); }
    MyVector RandomUnit();
    MyBlastParticle& Ember(const MyVector& pos, const MyVector& vel, float size, float life, float drag, float grav, uint8_t r, uint8_t g, uint8_t b);
    MyBlastParticle& Shard(const MyVector& pos, const MyVector& vel, float size, float flat, float life, float spin, uint8_t r, uint8_t g, uint8_t b);
    MyBlastParticle& Ring(const MyVector& pos, const MyVector& normal, float r0, float r1, float life, float thick, float delay, uint8_t r, uint8_t g, uint8_t b);
    MyBlastParticle& Orb(const MyVector& pos, float r0, float r1, float life, float delay, uint8_t r, uint8_t g, uint8_t b);
};

// ================================================================================================
//  MyHighScores - per-game TOP 5 (name, score and wave reached) plus the best wave ever reached, persisted to a small
//  text file.
//  Usage: SetFile() once, SetActiveGame(i) when a game is entered, ReportWave(n) whenever a new wave starts and
//  Update(score, gameOver, dt) once per frame.  When a run ends in game over with a score good enough for the table,
//  NeedsName() becomes true: the caller collects a name and passes it to SubmitName() (the games are paused meanwhile).
//  A run that is abandoned (restart / switching game) is entered under the last name used.  The file is only written
//  when something changed, and then only at those moments, every few seconds, or on Flush() - never every frame.
//  File format, one record per line:   "S <game> <score> <wave> <name>"   "W <game> <bestWave>"
// ================================================================================================
class MyHighScores
{
public:
    static constexpr int kMaxGames = 38;                  // covers the arcade scene IDs, including Soldier On at 36
    static constexpr int kTopCount = 5;
    static constexpr int kNameLen  = 10;                     // characters in a name (the buffer holds one more for the terminator)
    struct Entry {
        int score = 0, wave = 1; char name[kNameLen + 1] = { 0 }; bool demo = false;
        double seconds = 0.0, raceSeconds = 0.0;
        uint64_t replay = 0; int raceContext = -1;
    };

    void SetFile(const std::filesystem::path& file);        // also loads the saved table (first call only)
    void SetActiveGame(int game, bool autonomous = false);   // enters the old run in the table, flushes, starts a fresh run
    // Player play-back of a recorded run re-creates its old game state: while muted the table ignores every report, so a replay can never change a score, a wave or a retained recording.
    void SetMuted(bool muted) { m_muted = muted; }
    void ReportWave(int wave);                              // the player reached `wave` in the current run
    void Update(int score, bool gameOver, float dt);        // per frame: track the run, detect its end, autosave when due
    void Flush();                                           // write now if anything changed
    void SubmitName(const char* name);                      // finish a pending entry (an empty name becomes "PLAYER")
    // Autonomous results rank normally and preserve every earlier qualifying run.
    bool RecordDemoScore(int game, int score, int wave, double seconds = 0.0, uint64_t replay = 0);
    Entry DemoBest(int game) const { return m_demoBest[Clamp(game)]; }
    void SetRunRecording(uint64_t id) { m_runReplay = id; }
    void ReportRaceTime(double seconds, int context);
    bool RecordingReferenced(uint64_t id) const;
    std::filesystem::path RecordingPath(uint64_t id) const;
    void DiscardRecording(uint64_t id) const;
    void PruneRecordings() const;
    void ClearRecording(uint64_t id);
    int TimeContextCount(int game) const { return static_cast<int>(m_times[Clamp(game)].size()); }
    int TimeContext(int game, int index) const;
    const Entry& TopTime(int game, int context, int rank) const;
    bool PendingTimeOnly() const { return m_pending && !Qualifies(m_pendingScore); }

    bool HasFile() const { return !m_file.empty(); }
    bool NeedsName() const { return m_pending; }            // a qualifying run has ended and is waiting for a name
    int  PendingScore() const { return m_pendingScore; }
    int  RunWave() const { return m_runWave; }
    int  ActiveGame() const { return m_active; }
    // Best score of a game together with the best wave ever reached in it.
    Entry Best(int game) const { Entry e = m_top[Clamp(game)][0]; e.wave = m_bestWave[Clamp(game)]; return e; }
    Entry ActiveBest() const { return Best(m_active); }
    const Entry& Top(int game, int rank) const { return m_top[Clamp(game)][(rank >= 0 && rank < kTopCount) ? rank : 0]; }
    int  LastRank() const { return m_lastRank; }            // 0-based rank the most recent finished run took in the top 5, or -1
    bool IsNewBest(int score) const { return score > 0 && score >= Best(m_active).score; }
    bool Qualifies(int score) const { return score > 0 && score > m_top[Clamp(m_active)][kTopCount - 1].score; }

private:
    static int Clamp(int game) { return (game >= 0 && game < kMaxGames) ? game : 0; }
    int Insert(int game, int score, int wave, const char* name, bool demo = false,
        double seconds = 0.0, uint64_t replay = 0, double raceSeconds = 0.0, int raceContext = -1);
    void CommitRun();
    void Load();
    bool QualifiesTime(int game, int context, double seconds) const;
    bool RunQualifiesTime() const;
    void InsertTime(int game, int context, const Entry& entry);
    void CommitTimes(const char* name, bool autonomous, double seconds, uint64_t replay);

    std::filesystem::path m_file;
    Entry m_top[kMaxGames][kTopCount];
    Entry m_demoBest[kMaxGames];
    int   m_bestWave[kMaxGames] = {};
    char  m_lastName[kNameLen + 1] = { 'P', 'L', 'A', 'Y', 'E', 'R', 0 };
    int   m_active   = -1;
    int   m_runWave  = 1;
    int   m_lastScore = 0;
    int   m_lastRank = -1;
    int   m_pendingScore = 0, m_pendingWave = 1;
    bool  m_muted    = false;                                       // see SetMuted
    bool  m_pending  = false;
    bool  m_wasOver  = false;
    bool  m_committed = true;
    bool  m_dirty    = false;
    float m_saveClock = 0.0f;
    double m_runSeconds = 0.0, m_pendingSeconds = 0.0, m_raceSeconds = 0.0, m_pendingRaceSeconds = 0.0;
    uint64_t m_runReplay = 0, m_pendingReplay = 0;
    int m_raceContext = -1, m_pendingRaceContext = -1;
    struct TimeTable { int context = -1; Entry entries[kTopCount]; };
    struct RunTime { int context = -1; double seconds = 0.0; };
    static constexpr size_t kMaxTimeContexts = 4096; // Allocated only for played combinations, never per frame.
    std::vector<TimeTable> m_times[kMaxGames];
    std::vector<RunTime> m_runTimes;
    bool m_autonomous = false;
    Entry m_empty;
};

// Hash support so MyVector can be a key in unordered containers (spatial hashing, caches).
namespace std
{
    template <> struct hash<MyVector>
    {
        size_t operator()(const MyVector& v) const noexcept { return v.GetHashCode(); }
    };
}

#endif // MYVECTOR_H_INCLUDED
