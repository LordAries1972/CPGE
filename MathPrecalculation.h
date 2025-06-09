#pragma once

//-------------------------------------------------------------------------------------------------
// MathPrecalculation.h - High-Performance Mathematical Precalculation Class
// 
// Purpose: Provides optimized lookup tables and precalculated values for complex mathematical
//          operations to avoid expensive calculations during real-time loops and rendering.
//          Designed for gaming platforms where timing is critical.
//
// Features:
// - Trigonometric lookup tables (sin, cos, tan) with configurable precision
// - Color space conversion matrices and lookup tables
// - Interpolation coefficient tables for smooth animations
// - Matrix transformation precalculations
// - YUV to RGB conversion lookup tables for video processing
// - Particle physics precalculations
// - Camera projection and view matrix optimizations
// - Thread-safe singleton pattern for global access
//-------------------------------------------------------------------------------------------------

#include "Includes.h"
#include "Debug.h"
#include "ThreadManager.h"
#include "Vectors.h"
#include "DirectXMath.h"

#include <vector>
#include <array>
#include <unordered_map>
#include <mutex>
#include <atomic>

using namespace DirectX;

//==============================================================================
// Constants and Configuration
//==============================================================================
const int TRIG_TABLE_SIZE = 65536;                                             // 2^16 for high precision trigonometric lookups
const int SQRT_TABLE_SIZE = 32768;                                             // Square root lookup table size

const int COLOR_CONVERSION_TABLE_SIZE = 256;                                   // Color component lookup table size (0-255)
const int INTERPOLATION_TABLE_SIZE = 1024;                                     // Interpolation coefficient table size
const int PARTICLE_ANGLE_DIVISIONS = 360;                                      // Precalculated particle angles
const int YUV_LOOKUP_SIZE = 256;                                               // YUV to RGB conversion lookup size

// Precision factors for lookup table indexing
const float TRIG_PRECISION_FACTOR = static_cast<float>(TRIG_TABLE_SIZE) / (2.0f * XM_PI);
const float SQRT_PRECISION_FACTOR = static_cast<float>(SQRT_TABLE_SIZE) / 1000.0f;
const float INTERP_PRECISION_FACTOR = static_cast<float>(INTERPOLATION_TABLE_SIZE);

// Inverse trigonometric lookup table sizes and precision factors
const int INVERSE_TRIG_TABLE_SIZE = 32768;                                     // Inverse trig lookup table size for domain [-1, 1]
const float INVERSE_TRIG_PRECISION_FACTOR = static_cast<float>(INVERSE_TRIG_TABLE_SIZE - 1) / 2.0f;  // Maps [-1, 1] to [0, table_size-1]

//===================================================
// Physics Constants
//===================================================
const int GRAVITY_LOOKUP_TABLE_SIZE = 1024;                                    // Size of gravity intensity lookup table
const int REFLECTION_ANGLE_TABLE_SIZE = 360;                                   // Size of reflection angle lookup table
const int INERTIA_COEFFICIENT_TABLE_SIZE = 256;                                // Size of inertia coefficient lookup table
const int COLLISION_RESPONSE_TABLE_SIZE = 512;                                 // Size of collision response lookup table
const int AUDIO_PROPAGATION_TABLE_SIZE = 256;                                  // Size of audio propagation lookup table
const int GRAVITY_INTENSITY_TABLE_SIZE = 2048;                                 // Gravity intensity lookup table for distance-based calculations
const int AUDIO_ATTENUATION_TABLE_SIZE = 1024;                                 // Audio attenuation lookup table for distance-based sound
const int ORBITAL_MECHANICS_TABLE_SIZE = 512;                                  // Orbital mechanics lookup table for velocity calculations

// Physics precision factors for lookup table indexing
const float GRAVITY_DISTANCE_PRECISION = static_cast<float>(GRAVITY_INTENSITY_TABLE_SIZE) / 1000.0f;  // Maps distance [0, 1000] to table
const float REFLECTION_ANGLE_PRECISION = static_cast<float>(REFLECTION_ANGLE_TABLE_SIZE) / (2.0f * XM_PI);  // Maps angle [0, 2π] to table
const float INERTIA_MASS_PRECISION = static_cast<float>(INERTIA_COEFFICIENT_TABLE_SIZE) / 100.0f;     // Maps mass [0, 100] to table
const float COLLISION_RESPONSE_PRECISION = static_cast<float>(COLLISION_RESPONSE_TABLE_SIZE) / 1.0f;  // Maps coefficients [0, 1] to table
const float AUDIO_DISTANCE_PRECISION = static_cast<float>(AUDIO_ATTENUATION_TABLE_SIZE) / 500.0f;     // Maps distance [0, 500] to table
const float ORBITAL_DISTANCE_PRECISION = static_cast<float>(ORBITAL_MECHANICS_TABLE_SIZE) / 10000.0f; // Maps distance [0, 10000] to table

//==============================================================================
// Precalculated Data Structures
//==============================================================================
// Structure for precalculated trigonometric values
struct TrigonometricData {
    float sine;                                                                 // Sine value
    float cosine;                                                               // Cosine value
    float tangent;                                                              // Tangent value
    float cotangent;                                                            // Cotangent value
};

// Structure for precalculated inverse trigonometric values
struct InverseTrigonometricData {
    float arcSine;                                                              // Arcsine value in radians
    float arcCosine;                                                            // Arccosine value in radians  
    float arcTangent;                                                           // Arctangent value in radians
    float inputValue;                                                           // Original input value for validation
};

// Structure for color space conversion coefficients
struct ColorConversionData {
    XMFLOAT3 yuvToRgbR;                                                         // YUV to RGB conversion coefficients for Red
    XMFLOAT3 yuvToRgbG;                                                         // YUV to RGB conversion coefficients for Green
    XMFLOAT3 yuvToRgbB;                                                         // YUV to RGB conversion coefficients for Blue
    uint8_t clampedValues[COLOR_CONVERSION_TABLE_SIZE];                         // Preclamped color values (0-255)
};

// Structure for particle system precalculations
struct ParticleData {
    XMFLOAT2 direction;                                                         // Normalized direction vector
    float angleRadians;                                                         // Angle in radians
    float angleDegrees;                                                         // Angle in degrees
    XMFLOAT2 velocity;                                                          // Velocity vector for unit speed
};

// Structure for interpolation coefficients
struct InterpolationData {
    float linear;                                                               // Linear interpolation coefficient
    float smoothStep;                                                           // Smooth step interpolation coefficient
    float smootherStep;                                                         // Smoother step interpolation coefficient
    float easeIn;                                                               // Ease-in interpolation coefficient
    float easeOut;                                                              // Ease-out interpolation coefficient
    float easeInOut;                                                            // Ease-in-out interpolation coefficient
};

// Structure for matrix transformation precalculations
struct MatrixTransformData {
    XMMATRIX scaleMatrix;                                                       // Precalculated scale matrix
    XMMATRIX rotationMatrix;                                                    // Precalculated rotation matrix
    XMMATRIX translationMatrix;                                                 // Precalculated translation matrix
    XMMATRIX combinedMatrix;                                                    // Combined transformation matrix
};

// Physics-specific data structures for precalculated values
struct GravityIntensityData {
    float force;                                                                // Gravity force magnitude
    float acceleration;                                                         // Gravity acceleration
    float potential;                                                            // Gravitational potential energy coefficient
    float tidalForce;                                                           // Tidal force coefficient for advanced effects
};

struct ReflectionAngleData {
    XMFLOAT3 reflectionVector;                                                  // Precalculated reflection vector
    float energyLoss;                                                           // Energy loss coefficient
    float tangentialFriction;                                                   // Tangential friction component
    float normalRestitution;                                                    // Normal restitution component
};

struct InertiaData {
    float momentOfInertia;                                                      // Moment of inertia for rotation
    float angularMass;                                                          // Angular mass coefficient
    float gyroscopicEffect;                                                     // Gyroscopic effect coefficient
    float torqueResponse;                                                       // Torque response coefficient
};

struct CollisionResponseData {
    XMFLOAT3 velocityChange;                                                    // Velocity change vector
    float impulseStrength;                                                      // Impulse magnitude
    float separationDistance;                                                   // Required separation distance
    float contactTime;                                                          // Contact duration estimate
};

struct AudioAttenuationData {
    float volumeAttenuation;                                                    // Volume attenuation factor
    float frequencyShift;                                                       // Frequency shift for distance effects
    float reverbCoefficient;                                                    // Reverb coefficient
    float occlusionFactor;                                                      // Occlusion factor
};

struct OrbitalMechanicsData {
    float orbitalVelocity;                                                      // Circular orbital velocity
    float escapeVelocity;                                                       // Escape velocity
    float periodTime;                                                           // Orbital period
    float eccentricity;                                                         // Orbital eccentricity coefficient
};

//==============================================================================
// MathPrecalculation Class Declaration
//==============================================================================
class MathPrecalculation {
public:
    // Singleton pattern implementation for global access
    static MathPrecalculation& GetInstance();

    // Initialization and cleanup methods
    bool Initialize();
    void Cleanup();
    bool IsInitialized() const { return m_bIsInitialized.load(); }

    //==========================================================================
    // Trigonometric Lookup Methods
    //==========================================================================
    // Fast sine lookup using precalculated table with linear interpolation
    float FastSin(float angle) const;

    // Fast cosine lookup using precalculated table with linear interpolation
    float FastCos(float angle) const;

    // Fast tangent lookup using precalculated table with linear interpolation
    float FastTan(float angle) const;

    // Fast cotangent lookup using precalculated table
    float FastCot(float angle) const;

    // Get both sine and cosine values simultaneously for efficiency
    void FastSinCos(float angle, float& outSin, float& outCos) const;

    // Fast square root lookup for values 0-1000 with interpolation
    float FastSqrt(float value) const;

    //==========================================================================
    // Inverse Trigonometric Lookup Methods
    //==========================================================================
    // Fast arcsine lookup using precalculated table with linear interpolation
    // Input domain: [-1, 1], Output range: [-π/2, π/2]
    float FastASin(float value) const;

    // Fast arccosine lookup using precalculated table with linear interpolation  
    // Input domain: [-1, 1], Output range: [0, π]
    float FastACos(float value) const;

    // Fast arctangent lookup using precalculated table with linear interpolation
    // Input domain: [-∞, ∞], Output range: [-π/2, π/2]
    float FastATan(float value) const;

    // Fast two-argument arctangent for angle calculation from y/x coordinates
    // Returns angle in range [-π, π] for proper quadrant determination
    float FastATan2(float y, float x) const;

    //==========================================================================
    // Color Conversion Methods
    //==========================================================================
    // Fast YUV to RGB conversion using lookup tables (optimized for video processing)
    void FastYuvToRgb(uint8_t y, uint8_t u, uint8_t v, uint8_t& outR, uint8_t& outG, uint8_t& outB) const;

    // Fast YUV to RGB conversion for XMFLOAT4 color values
    XMFLOAT4 FastYuvToRgbFloat(float y, float u, float v) const;

    // Fast RGB to YUV conversion using precalculated coefficients
    void FastRgbToYuv(uint8_t r, uint8_t g, uint8_t b, uint8_t& outY, uint8_t& outU, uint8_t& outV) const;

    // Gamma correction lookup for color values
    uint8_t FastGammaCorrect(uint8_t input, float gamma = 2.2f) const;

    // Fast color clamping using lookup table
    uint8_t FastClamp(int value) const;

    //==========================================================================
    // Interpolation Methods
    //==========================================================================
    // Fast linear interpolation using precalculated coefficients
    float FastLerp(float start, float end, float t) const;

    // Fast smooth step interpolation (3t^2 - 2t^3)
    float FastSmoothStep(float start, float end, float t) const;

    // Fast smoother step interpolation (6t^5 - 15t^4 + 10t^3)
    float FastSmootherStep(float start, float end, float t) const;

    // Fast ease-in interpolation
    float FastEaseIn(float start, float end, float t) const;

    // Fast ease-out interpolation
    float FastEaseOut(float start, float end, float t) const;

    // Fast ease-in-out interpolation
    float FastEaseInOut(float start, float end, float t) const;

    //==========================================================================
    // Particle System Helper Methods
    //==========================================================================
    // Get precalculated particle direction for explosion effects
    XMFLOAT2 GetParticleDirection(int particleIndex, int totalParticles) const;

    // Get precalculated velocity vector for particle movement
    XMFLOAT2 GetParticleVelocity(float angle, float speed) const;

    // Fast distance calculation using optimized square root
    float FastDistance(const XMFLOAT2& point1, const XMFLOAT2& point2) const;

    // Fast normalize vector using precalculated reciprocal square root
    XMFLOAT2 FastNormalize(const XMFLOAT2& vector) const;

    //==========================================================================
    // Matrix Transformation Methods
    //==========================================================================
    // Get precalculated transformation matrix for common scales
    XMMATRIX GetScaleMatrix(float scaleX, float scaleY, float scaleZ) const;

    // Get precalculated rotation matrix for common angles
    XMMATRIX GetRotationMatrix(float angleX, float angleY, float angleZ) const;

    // Fast matrix multiplication using precalculated components
    XMMATRIX FastMatrixMultiply(const XMMATRIX& matrix1, const XMMATRIX& matrix2) const;

    // Get precalculated view matrix for camera positions
    XMMATRIX GetViewMatrix(const XMFLOAT3& position, const XMFLOAT3& target, const XMFLOAT3& up) const;

    //==========================================================================
    // Text Rendering Optimization Methods
    //==========================================================================
    // Fast character width calculation using lookup table
    float GetCharacterWidthFast(wchar_t character, float fontSize) const;

    // Fast text transparency calculation for consistent scroller
    float GetTextTransparencyFast(float position, float regionStart, float regionEnd, float fadeDistance) const;

    //==========================================================================
    // Utility Methods
    //==========================================================================
    // Get memory usage statistics for debugging
    size_t GetMemoryUsage() const;

    // Validate all lookup tables for debugging
    bool ValidateTables() const;

    // Dump table statistics to debug output
    void DumpTableStatistics() const;

    //==========================================================================
    // Compression and Checksum Optimization Methods
    //==========================================================================
    // Bit manipulation methods for compression optimization
    uint8_t FastCountLeadingZeros(uint32_t value) const;
    uint8_t FastCountTrailingZeros(uint32_t value) const;
    bool FastIsPowerOfTwo(uint32_t value) const;
    uint32_t FastNextPowerOfTwo(uint32_t value) const;

    // Fast bit manipulation operations for compression algorithms
    uint32_t FastRotateLeft(uint32_t value, int positions) const;
    uint32_t FastRotateRight(uint32_t value, int positions) const;

    // Optimized hash calculations for compression
    uint32_t FastFNV1aHash(const void* data, size_t size) const;
    uint64_t FastFNV1aHash64(const void* data, size_t size) const;

    // Fast modular arithmetic for encryption
    uint32_t FastModPow(uint32_t base, uint32_t exponent, uint32_t modulus) const;

    // Optimized byte operations for compression
    void FastByteSwap(uint8_t* data, size_t size) const;
    uint32_t FastCountSetBits(uint32_t value) const;
    uint32_t FastReverseBits(uint32_t value) const;

    //==========================================================================
    // Physics Optimization Methods
    //==========================================================================

    // Forward declaration for physics vector type
    struct PhysicsVector3D;

    // Fast gravity intensity calculation for physics simulations
    float FastGravityIntensity(float distance, float mass, float intensity = 1.0f) const;

    // Fast reflection angle calculation for collision response
    XMFLOAT3 FastReflectionVector(const XMFLOAT3& incoming, const XMFLOAT3& normal, float restitution = 0.8f) const;

    // Fast inertia coefficient lookup for rotational physics
    float FastInertiaCoefficient(float mass, float radius) const;

    // Fast collision response calculation
    XMFLOAT3 FastCollisionResponse(const XMFLOAT3& velocity, const XMFLOAT3& normal,
        float restitution, float friction) const;

    // Fast audio propagation coefficient
    float FastAudioPropagation(float distance, float occlusionFactor = 0.0f) const;

    // Fast projectile trajectory calculation
    XMFLOAT3 FastProjectileTrajectory(const XMFLOAT3& startPos, const XMFLOAT3& targetPos,
        float launchSpeed, float gravity = 9.81f) const;

    // Fast orbital velocity calculation
    float FastOrbitalVelocity(float distance, float mass) const;

    // Fast escape velocity calculation
    float FastEscapeVelocity(float distance, float mass) const;

private:
    // Private constructor for singleton pattern
    MathPrecalculation();
    ~MathPrecalculation();

    // Prevent copying and assignment
    MathPrecalculation(const MathPrecalculation&) = delete;
    MathPrecalculation& operator=(const MathPrecalculation&) = delete;

    //==========================================================================
    // Initialization Methods
    //==========================================================================
    // Initialize trigonometric lookup tables
    void InitializeTrigonometricTables();

    // Initialize inverse trigonometric lookup tables
    void InitializeInverseTrigonometricTables();

    // Initialize color conversion lookup tables
    void InitializeColorConversionTables();

    // Initialize interpolation coefficient tables
    void InitializeInterpolationTables();

    // Initialize particle system precalculations
    void InitializeParticleData();

    // Initialize matrix transformation caches
    void InitializeMatrixCaches();

    // Initialize text rendering optimizations
    void InitializeTextOptimizations();

    // Physics-specific initialization methods
    void InitializeGravityIntensityTables();
    void InitializeReflectionAngleTables();
    void InitializeInertiaTables();
    void InitializeCollisionResponseTables();
    void InitializeAudioAttenuationTables();
    void InitializeOrbitalMechanicsTables();
    void InitializePhysicsPrecalculations();

    //==========================================================================
    // Helper Methods
    //==========================================================================
    // Normalize angle to range [0, 2π]
    float NormalizeAngle(float angle) const;

    // Convert angle to lookup table index with bounds checking
    int AngleToIndex(float angle) const;

    // Linear interpolation between two table values
    float InterpolateTableValue(float value, const std::vector<float>& table, float maxValue) const;

    // Clamp value to valid range
    template<typename T>
    T ClampValue(T value, T minVal, T maxVal) const;

private:
    // Initialization state
    std::atomic<bool> m_bIsInitialized;
    std::atomic<bool> m_bHasCleanedUp;

    // Thread safety
    mutable std::mutex m_tablesMutex;

    // Trigonometric lookup tables
    std::vector<TrigonometricData> m_trigonometricTable;
    std::vector<float> m_sqrtTable;

    // Inverse trigonometric lookup tables
    std::vector<InverseTrigonometricData> m_inverseTrigonometricTable;

    // Color conversion lookup tables
    std::vector<ColorConversionData> m_colorConversionTable;
    std::vector<uint8_t> m_yuvToRgbLookup;                                      // Dynamic YUV to RGB lookup table
    std::array<uint8_t, 512> m_clampTable;                                      // Extended range for clamping

    // Interpolation coefficient tables
    std::vector<InterpolationData> m_interpolationTable;

    // Particle system precalculations
    std::vector<ParticleData> m_particleDirections;
    std::unordered_map<int, std::vector<XMFLOAT2>> m_explosionPatterns;

    // Matrix transformation caches
    std::unordered_map<int, XMMATRIX> m_scaleMatrixCache;
    std::unordered_map<int, XMMATRIX> m_rotationMatrixCache;

    // Text rendering optimizations
    std::unordered_map<wchar_t, float> m_characterWidthCache;
    std::vector<float> m_transparencyLookup;

    // Debug and statistics
    mutable std::atomic<size_t> m_totalMemoryUsage;
    mutable std::atomic<size_t> m_lookupCount;

    // Physics-specific lookup tables
    std::vector<GravityIntensityData> m_gravityIntensityTable;                  // Gravity intensity lookup table
    std::vector<ReflectionAngleData> m_reflectionAngleTable;                    // Reflection angle lookup table
    std::vector<InertiaData> m_inertiaTable;                                    // Inertia coefficient lookup table
    std::vector<CollisionResponseData> m_collisionResponseTable;                // Collision response lookup table
    std::vector<AudioAttenuationData> m_audioAttenuationTable;                  // Audio attenuation lookup table
    std::vector<OrbitalMechanicsData> m_orbitalMechanicsTable;                  // Orbital mechanics lookup table
};

//==============================================================================
// Global Access Macros for Convenience
//==============================================================================
#define FAST_MATH MathPrecalculation::GetInstance()
#define FAST_SIN(angle) FAST_MATH.FastSin(angle)
#define FAST_COS(angle) FAST_MATH.FastCos(angle)
#define FAST_TAN(angle) FAST_MATH.FastTan(angle)
#define FAST_SQRT(value) FAST_MATH.FastSqrt(value)
#define FAST_LERP(start, end, t) FAST_MATH.FastLerp(start, end, t)

#define FAST_ASIN(value) FAST_MATH.FastASin(value)
#define FAST_ACOS(value) FAST_MATH.FastACos(value)
#define FAST_ATAN(value) FAST_MATH.FastATan(value)
#define FAST_ATAN2(y, x) FAST_MATH.FastATan2(y, x)

// Physics optimization macros
#define FAST_GRAVITY_INTENSITY(distance, mass, intensity) FAST_MATH.FastGravityIntensity(distance, mass, intensity)
#define FAST_REFLECTION_VECTOR(incoming, normal, restitution) FAST_MATH.FastReflectionVector(incoming, normal, restitution)
#define FAST_INERTIA_COEFFICIENT(mass, radius) FAST_MATH.FastInertiaCoefficient(mass, radius)
#define FAST_COLLISION_RESPONSE(velocity, normal, restitution, friction) FAST_MATH.FastCollisionResponse(velocity, normal, restitution, friction)
#define FAST_AUDIO_PROPAGATION(distance, occlusion) FAST_MATH.FastAudioPropagation(distance, occlusion)
#define FAST_PROJECTILE_TRAJECTORY(start, target, speed, gravity) FAST_MATH.FastProjectileTrajectory(start, target, speed, gravity)
#define FAST_ORBITAL_VELOCITY(distance, mass) FAST_MATH.FastOrbitalVelocity(distance, mass)
#define FAST_ESCAPE_VELOCITY(distance, mass) FAST_MATH.FastEscapeVelocity(distance, mass)

//==============================================================================
// Debug Preprocessor Directives
//==============================================================================
#if defined(_DEBUG_MATHPRECALC_)
#define MATHPRECALC_DEBUG_LOG(level, message) debug.logLevelMessage(level, message)
#define MATHPRECALC_DEBUG_MSG(level, format, ...) debug.logDebugMessage(level, format, __VA_ARGS__)
#else
#define MATHPRECALC_DEBUG_LOG(level, message) ((void)0)
#define MATHPRECALC_DEBUG_MSG(level, format, ...) ((void)0)
#endif

// External reference for global debug instance
extern Debug debug;