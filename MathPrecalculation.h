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

#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
    #include <DirectXMath.h>
    using namespace DirectX;
#endif

#include <vector>
#include <array>
#include <unordered_map>
#include <mutex>
#include <atomic>

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

// Vector-math tables (added for MyVector / MyQuaternion in Vectors.h)
// Accurate arccosine: acos(|v|) = 2 * asin(sqrt((1 - |v|) / 2)).  The asin argument stays inside
// [0, 1/sqrt(2)] where asin is gently sloped, so a modest table + interpolation is accurate
// everywhere - unlike a table indexed directly by v, whose step explodes near +-1.
const int   HALF_ANGLE_TABLE_SIZE = 8192;                                      // Entries covering asin(s), s in [0, 1/sqrt(2)]
const float HALF_ANGLE_MAX_INPUT = 0.70710678f;                                // 1/sqrt(2): largest s ever produced by (1-|v|)/2
const float HALF_ANGLE_PRECISION_FACTOR = static_cast<float>(HALF_ANGLE_TABLE_SIZE - 1) / HALF_ANGLE_MAX_INPUT;
// Degree-domain trig index factor: lets rotation code that works in degrees skip the radian conversion.
const float TRIG_DEGREE_PRECISION_FACTOR = static_cast<float>(TRIG_TABLE_SIZE) / 360.0f;

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
// Reciprocal-of-integer table (compile-time)
//
// 1/n for n = 0..64 (entry 0 is 0).  Per-face averages ("zSum / vertexCount"), per-segment steps and
// similar integer divisions become one table load and a multiply.  Built by the compiler, so it needs
// no Initialize() call and no memory is touched at start-up.
//==============================================================================
struct InverseIntTable {
    static constexpr int kCount = 65;
    float v[kCount];
    constexpr InverseIntTable() : v() {
        v[0] = 0.0f;
        for (int i = 1; i < kCount; ++i) v[i] = 1.0f / static_cast<float>(i);
    }
};
inline constexpr InverseIntTable kInverseInt{};

// LOBBED SHOTS (grenades).  A lobbed shot is aimed by choosing its FLIGHT TIME from the horizontal distance R (a long throw takes longer, so it flies higher and arrives more gently): then
//     horizontal speed  hs = R / t,     vertical speed  vy = dy / t + g t / 2     (dy = target height - launch height, g = gravity)
// which lands exactly on the target, with  apex = vy^2 / 2g  above the launch point and launch angle = atan(vy / hs).  Two arc profiles are tabled at startup (MathPrecalculation::
// InitializeLobbedShotTables): 0 = the hand grenade (t = R / 14 limited to 1.2 .. 2.2 s), 1 = the grenade launcher (t = R / 16 limited to 1.3 .. 2.6 s), for R = 0 .. 64 in steps of 0.25.
// A table entry holds t and 1 / t, so a throw is two table reads, a lerp and a few multiplies - no division, no clamp, no trig on the frame.
inline constexpr float kLobbedShotStep = 0.25f;
inline constexpr int   kLobbedShotCount = 257;                                  // R = 0 .. 64
struct LobbedShotData { float flightTime; float invFlightTime; };
struct LobbedShotSolution { float flightTime, hSpeed, vSpeed, apexHeight, launchAngleDeg, landDistance; bool reachable; };       // reachable: the distance was inside the table (else it was clamped to 64)

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
    // Vector-Math Lookup Methods (used by MyVector / MyQuaternion)
    //==========================================================================
    // Accurate arccosine for dot-product angles. Input [-1, 1] (clamped), output [0, PI] radians.
    // Linear interpolation over a well-conditioned half-angle table: error ~1e-7 rad everywhere,
    // including the nearly-parallel / nearly-opposite cases where FastACos is ~0.01 rad off.
    float FastACosAccurate(float value) const;

    // Sine and cosine of an angle given in DEGREES (any magnitude, negative allowed).
    // Interpolates between table entries (error ~1e-9) and wraps in O(1) - no radian conversion,
    // no normalisation loops - so it is the call of choice for per-frame rotation work.
    void FastSinCosDeg(float degrees, float& outSin, float& outCos) const;

    // Precalculated unit-circle points (cos(2*PI*k/n), sin(2*PI*k/n)), k = 0..n-1, for the common
    // segment counts 3,4,5,6,8,10,12,16,20,24,32,48,64.  Used to build prisms, cones, discs and
    // spheres without any trig at mesh-build time. Read-only after Initialize() => lock-free.
    // Returns an empty vector when segments is not one of the precomputed counts.
    const std::vector<XMFLOAT2>& GetUnitCircle(int segments) const;
    // exp(-age * rate), interpolated over [0,8]; zero beyond the visible tail.
    // Prepared at engine startup for scanner returns and response envelopes.
    float FastDecay(float age, float rate) const;
    float FastSurfaceReflection(float cosine, float roughness) const;

    // Precalculated unit directions on the sphere: kUnitSphereCount points spread evenly by a Fibonacci
    // spiral (no clumping at the poles).  A random burst direction is one table load: no sin / cos, no
    // sqrt and no rejection-sampling loop.  `rnd` is any random number; bits 10..19 pick the entry, so a
    // plain LCG (whose low bits are weak) works.  Read-only after Initialize() => lock-free.
    static constexpr int kUnitSphereCount = 1024;
    Vector3 GetUnitSphereDirection(uint32_t rnd) const;

    // Power curves t^exponent over t in [0,1] as interpolated 1024-step tables (easing in per-particle,
    // per-frame code instead of std::pow, which costs tens of nanoseconds a call).
    //   RegisterPowerCurve : call ONCE per exponent (e.g. into a function-local static); it builds the
    //                        table and returns an id.  The same exponent returns the same id.  Up to
    //                        kMaxPowerCurves distinct exponents; when full, the last curve's id is returned.
    //   FastPowCurve       : t is clamped to [0,1].  Error below ~1e-6 for exponents >= 1.
    // Tables are never discarded (not even by Cleanup()), so an id stays valid for the program's life.
    typedef int PowerCurveId;
    static constexpr int kMaxPowerCurves  = 8;
    static constexpr int kPowerCurveSteps = 1024;
    PowerCurveId RegisterPowerCurve(float exponent);
    float        FastPowCurve(PowerCurveId id, float t) const;

    // 1 / n for n = 1..64 from a compile-time table (see kInverseInt); falls back to a real divide
    // outside that range and returns 0 for n <= 0.  Inline: no singleton access.
    static float FastInvInt(int n) {
        return (n >= 0 && n < InverseIntTable::kCount) ? kInverseInt.v[n] : (n > 0 ? 1.0f / static_cast<float>(n) : 0.0f);
    }

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

    // Lobbed-shot solver (see LobbedShotData): the speeds that drop a thrown / launched shot on a point `horizDist` away and `targetY - launchY` higher, using the precalculated arc profile
    // (0 hand grenade, 1 grenade launcher).  False before initialisation or for an unknown profile; `out` is then untouched.
    bool SolveLobbedShot(int profile, float horizDist, float launchY, float targetY, float gravity, LobbedShotSolution& out) const;

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

    // Initialize the vector-math tables (accurate acos half-angle table, unit-circle tables)
    void InitializeVectorMathTables();

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
    void InitializeLobbedShotTables();                                                       // the grenade arc profiles (see LobbedShotData)

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

    // Vector-math tables
    std::vector<float> m_halfAngleAsinTable;                                    // asin(s) for s in [0, 1/sqrt(2)] (accurate acos)
    std::unordered_map<int, std::vector<XMFLOAT2>> m_unitCircleTables;          // segment count -> unit-circle points
    std::vector<Vector3> m_unitSphereDirections;                                // Fibonacci-spiral unit directions (GetUnitSphereDirection)
    std::array<float, 257> m_decayTable{};
    std::array<std::array<float, 257>, 2> m_surfaceReflection{};

    // Power-curve tables (RegisterPowerCurve / FastPowCurve).  Slots are filled under m_powerCurveMutex and then
    // published by raising m_powerCurveCount (release); readers only look at slots below the count they load (acquire).
    std::array<std::array<float, kPowerCurveSteps + 1>, kMaxPowerCurves> m_powerCurveTables;
    std::array<float, kMaxPowerCurves> m_powerCurveExponent;
    std::atomic<int> m_powerCurveCount{ 0 };
    std::mutex m_powerCurveMutex;

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
    std::vector<LobbedShotData> m_lobbedShotTable[2];                           // lobbed-shot flight times: 0 hand grenade, 1 grenade launcher
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
#define FAST_ACOS_ACCURATE(value) FAST_MATH.FastACosAccurate(value)
#define FAST_SINCOS_DEG(degrees, outSin, outCos) FAST_MATH.FastSinCosDeg(degrees, outSin, outCos)
#define FAST_UNIT_CIRCLE(segments) FAST_MATH.GetUnitCircle(segments)
#define FAST_UNIT_SPHERE(rnd) FAST_MATH.GetUnitSphereDirection(rnd)
#define FAST_INV_INT(n) MathPrecalculation::FastInvInt(n)

// Physics optimization macros
#define FAST_GRAVITY_INTENSITY(distance, mass, intensity) FAST_MATH.FastGravityIntensity(distance, mass, intensity)
#define FAST_REFLECTION_VECTOR(incoming, normal, restitution) FAST_MATH.FastReflectionVector(incoming, normal, restitution)
#define FAST_INERTIA_COEFFICIENT(mass, radius) FAST_MATH.FastInertiaCoefficient(mass, radius)
#define FAST_COLLISION_RESPONSE(velocity, normal, restitution, friction) FAST_MATH.FastCollisionResponse(velocity, normal, restitution, friction)
#define FAST_AUDIO_PROPAGATION(distance, occlusion) FAST_MATH.FastAudioPropagation(distance, occlusion)
#define FAST_PROJECTILE_TRAJECTORY(start, target, speed, gravity) FAST_MATH.FastProjectileTrajectory(start, target, speed, gravity)
#define FAST_LOBBED_SHOT(profile, dist, launchY, targetY, gravity, out) FAST_MATH.SolveLobbedShot(profile, dist, launchY, targetY, gravity, out)
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
