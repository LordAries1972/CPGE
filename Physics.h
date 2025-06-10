#pragma once

//-------------------------------------------------------------------------------------------------
// Physics.h - Comprehensive Physics Simulation Class
// 
// Purpose: Provides high-performance physics calculations for gaming platforms including
//          curved paths, reflections, gravity, collisions, ragdoll physics, and more.
//          Optimized for real-time performance with MathPrecalculation integration.
//
// Features:
// - 2D and 3D curved path calculations with up to 1024 coordinates
// - Reflection path calculations based on speed, force, and angle
// - Variable gravity intensity with distance-based calculations
// - Bouncing mechanics with inertia and directional forces
// - Advanced collision detection and response systems
// - Ragdoll physics with joint constraints
// - Newtonian motion simulation
// - Audio physics for sound propagation
// - Particle system physics
// - Physics-based animation blending
//-------------------------------------------------------------------------------------------------

#include "Includes.h"
#include "Debug.h"
#include "ExceptionHandler.h"
#include "MathPrecalculation.h"
#include "ThreadManager.h"
#include "DirectXMath.h"

#include <vector>
#include <array>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <mutex>

using namespace DirectX;

#pragma warning(push)
#pragma warning(disable: 4101)

//==============================================================================
// Physics Constants and Configuration
//==============================================================================
const int MAX_PATH_COORDINATES = 1024;                                         // Maximum number of coordinates in a curved path
const int MAX_COLLISION_CONTACTS = 32;                                         // Maximum collision contact points per object
const int MAX_RAGDOLL_JOINTS = 64;                                             // Maximum number of joints in ragdoll system
const int MAX_PARTICLE_COUNT = 10000;                                          // Maximum particles in a system
const int AUDIO_PROPAGATION_SAMPLES = 256;                                     // Audio propagation calculation samples

const float DEFAULT_GRAVITY = 9.81f;                                           // Earth's gravity in m/s²
const float MIN_VELOCITY_THRESHOLD = 0.001f;                                   // Minimum velocity before object stops
const float DEFAULT_AIR_RESISTANCE = 0.01f;                                    // Default air resistance coefficient
const float DEFAULT_RESTITUTION = 0.8f;                                        // Default bounce coefficient
const float DEFAULT_FRICTION = 0.3f;                                           // Default friction coefficient

//==============================================================================
// Physics Data Structures
//==============================================================================

// 2D Vector structure for physics calculations
struct PhysicsVector2D {
    float x, y;

    // Constructor with default values
    PhysicsVector2D(float x = 0.0f, float y = 0.0f) : x(x), y(y) {}

    // Vector operations
    PhysicsVector2D operator+(const PhysicsVector2D& other) const { return PhysicsVector2D(x + other.x, y + other.y); }
    PhysicsVector2D operator-(const PhysicsVector2D& other) const { return PhysicsVector2D(x - other.x, y - other.y); }
    PhysicsVector2D operator*(float scalar) const { return PhysicsVector2D(x * scalar, y * scalar); }
    PhysicsVector2D& operator+=(const PhysicsVector2D& other) { x += other.x; y += other.y; return *this; }
    PhysicsVector2D& operator-=(const PhysicsVector2D& other) { x -= other.x; y -= other.y; return *this; }
    PhysicsVector2D& operator*=(float scalar) { x *= scalar; y *= scalar; return *this; }

    // Utility functions
    float Magnitude() const;
    float MagnitudeSquared() const { return x * x + y * y; }
    PhysicsVector2D Normalized() const;
    float Dot(const PhysicsVector2D& other) const { return x * other.x + y * other.y; }
    float Cross(const PhysicsVector2D& other) const { return x * other.y - y * other.x; }
};

// 3D Vector structure for physics calculations
struct PhysicsVector3D {
    float x, y, z;

    // Constructor with default values
    PhysicsVector3D(float x = 0.0f, float y = 0.0f, float z = 0.0f) : x(x), y(y), z(z) {}

    // Conversion from XMFLOAT3
    PhysicsVector3D(const XMFLOAT3& xmFloat) : x(xmFloat.x), y(xmFloat.y), z(xmFloat.z) {}

    // Vector operations
    PhysicsVector3D operator+(const PhysicsVector3D& other) const { return PhysicsVector3D(x + other.x, y + other.y, z + other.z); }
    PhysicsVector3D operator-(const PhysicsVector3D& other) const { return PhysicsVector3D(x - other.x, y - other.y, z - other.z); }
    PhysicsVector3D operator*(float scalar) const { return PhysicsVector3D(x * scalar, y * scalar, z * scalar); }
    PhysicsVector3D& operator+=(const PhysicsVector3D& other) { x += other.x; y += other.y; z += other.z; return *this; }
    PhysicsVector3D& operator-=(const PhysicsVector3D& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
    PhysicsVector3D& operator*=(float scalar) { x *= scalar; y *= scalar; z *= scalar; return *this; }

    // Utility functions
    float Magnitude() const;
    float MagnitudeSquared() const { return x * x + y * y + z * z; }
    PhysicsVector3D Normalized() const;
    float Dot(const PhysicsVector3D& other) const { return x * other.x + y * other.y + z * other.z; }
    PhysicsVector3D Cross(const PhysicsVector3D& other) const;

    // Conversion to XMFLOAT3
    XMFLOAT3 ToXMFLOAT3() const { return XMFLOAT3(x, y, z); }
};

// Curved path structure for 2D coordinates
struct CurvedPath2D {
    std::vector<PhysicsVector2D> coordinates;                                   // Path coordinate points
    std::vector<float> tangents;                                                // Tangent angles at each point
    std::vector<float> curvatures;                                              // Curvature values at each point
    float totalLength;                                                          // Total path length
    bool isLooped;                                                              // Whether path forms a closed loop

    // Constructor
    CurvedPath2D() : totalLength(0.0f), isLooped(false) { coordinates.reserve(MAX_PATH_COORDINATES); }

    // Path manipulation methods
    void AddPoint(const PhysicsVector2D& point);
    void CalculateTangentsAndCurvatures();
    PhysicsVector2D GetPointAtDistance(float distance) const;
    PhysicsVector2D GetTangentAtDistance(float distance) const;
    void Clear();
};

// Curved path structure for 3D coordinates
struct CurvedPath3D {
    std::vector<PhysicsVector3D> coordinates;                                   // Path coordinate points
    std::vector<PhysicsVector3D> tangents;                                      // Tangent vectors at each point
    std::vector<float> curvatures;                                              // Curvature values at each point
    float totalLength;                                                          // Total path length
    bool isLooped;                                                              // Whether path forms a closed loop

    // Constructor
    CurvedPath3D() : totalLength(0.0f), isLooped(false) { coordinates.reserve(MAX_PATH_COORDINATES); }

    // Path manipulation methods
    void AddPoint(const PhysicsVector3D& point);
    void CalculateTangentsAndCurvatures();
    PhysicsVector3D GetPointAtDistance(float distance) const;
    PhysicsVector3D GetTangentAtDistance(float distance) const;
    void Clear();
};

// Reflection data structure
struct ReflectionData {
    PhysicsVector3D incomingVelocity;                                           // Velocity before reflection
    PhysicsVector3D surfaceNormal;                                              // Surface normal at reflection point
    PhysicsVector3D reflectedVelocity;                                          // Velocity after reflection
    float restitution;                                                          // Bounce coefficient (0 = no bounce, 1 = perfect bounce)
    float friction;                                                             // Surface friction coefficient
    float energyLoss;                                                           // Energy lost in collision

    // Constructor
    ReflectionData() : restitution(DEFAULT_RESTITUTION), friction(DEFAULT_FRICTION), energyLoss(0.0f) {}
};

// Gravity field structure for variable intensity
struct GravityField {
    PhysicsVector3D center;                                                     // Center of gravity source
    float mass;                                                                 // Mass of gravity source
    float radius;                                                               // Effective radius of gravity field
    float intensity;                                                            // Gravity intensity multiplier
    bool isBlackHole;                                                           // Special handling for black holes

    // Constructor
    GravityField() : mass(1.0f), radius(1000.0f), intensity(1.0f), isBlackHole(false) {}

    // Calculate gravity force at distance
    float CalculateGravityForce(float distance) const;
    PhysicsVector3D CalculateGravityVector(const PhysicsVector3D& position) const;
};

// Physics body structure for collision and movement
struct PhysicsBody {
    PhysicsVector3D position;                                                   // Current position
    PhysicsVector3D velocity;                                                   // Current velocity
    PhysicsVector3D acceleration;                                               // Current acceleration
    PhysicsVector3D angularVelocity;                                            // Rotational velocity
    float mass;                                                                 // Body mass
    float inverseMass;                                                          // Precomputed inverse mass (1/mass)
    float restitution;                                                          // Bounce coefficient
    float friction;                                                             // Friction coefficient
    float drag;                                                                 // Air resistance coefficient
    bool isStatic;                                                              // Whether body is immovable
    bool isActive;                                                              // Whether body participates in physics

    // Constructor
    PhysicsBody() : mass(1.0f), inverseMass(1.0f), restitution(DEFAULT_RESTITUTION),
        friction(DEFAULT_FRICTION), drag(DEFAULT_AIR_RESISTANCE),
        isStatic(false), isActive(true) {
    }

    // Set mass and automatically calculate inverse mass
    void SetMass(float newMass);

    // Apply force to the body
    void ApplyForce(const PhysicsVector3D& force);
    void ApplyImpulse(const PhysicsVector3D& impulse);

    // Integration methods
    void IntegrateVelocity(float deltaTime);
    void IntegratePosition(float deltaTime);
};

// Collision contact point structure
struct ContactPoint {
    PhysicsVector3D position;                                                   // Contact position in world space
    PhysicsVector3D normal;                                                     // Contact normal (pointing from A to B)
    float penetrationDepth;                                                     // How deep objects are penetrating
    float restitution;                                                          // Combined restitution coefficient
    float friction;                                                             // Combined friction coefficient

    // Constructor
    ContactPoint() : penetrationDepth(0.0f), restitution(DEFAULT_RESTITUTION), friction(DEFAULT_FRICTION) {}
};

// Collision manifold structure
struct CollisionManifold {
    PhysicsBody* bodyA;                                                         // First colliding body
    PhysicsBody* bodyB;                                                         // Second colliding body
    std::vector<ContactPoint> contacts;                                         // Contact points
    PhysicsVector3D normal;                                                     // Collision normal
    float separatingVelocity;                                                   // Relative velocity along normal

    // Constructor
    CollisionManifold() : bodyA(nullptr), bodyB(nullptr), separatingVelocity(0.0f) { contacts.reserve(MAX_COLLISION_CONTACTS); }

    // Add contact point to manifold
    void AddContact(const ContactPoint& contact);

    // Resolve collision
    void ResolveCollision();
};

// Ragdoll joint structure
struct RagdollJoint {
    PhysicsBody* bodyA;                                                         // First connected body
    PhysicsBody* bodyB;                                                         // Second connected body
    PhysicsVector3D anchorA;                                                    // Anchor point on body A
    PhysicsVector3D anchorB;                                                    // Anchor point on body B
    float minAngle;                                                             // Minimum joint angle (radians)
    float maxAngle;                                                             // Maximum joint angle (radians)
    float stiffness;                                                            // Joint stiffness
    float damping;                                                              // Joint damping
    bool isActive;                                                              // Whether joint is active

    // Constructor
    RagdollJoint() : bodyA(nullptr), bodyB(nullptr), minAngle(-XM_PI), maxAngle(XM_PI),
        stiffness(1000.0f), damping(50.0f), isActive(true) {
    }

    // Apply joint constraints
    void ApplyConstraints();
};

// Audio physics data structure
struct AudioPhysicsData {
    PhysicsVector3D listenerPosition;                                           // Listener position
    PhysicsVector3D sourcePosition;                                             // Sound source position
    PhysicsVector3D sourceVelocity;                                             // Sound source velocity
    float distance;                                                             // Distance between source and listener
    float dopplerShift;                                                         // Doppler effect frequency shift
    float occlusion;                                                            // Occlusion factor (0 = clear, 1 = fully blocked)
    float reverb;                                                               // Reverb intensity
    float volumeFalloff;                                                        // Volume falloff based on distance

    // Constructor
    AudioPhysicsData() : distance(0.0f), dopplerShift(1.0f), occlusion(0.0f), reverb(0.0f), volumeFalloff(1.0f) {}

    // Calculate audio properties
    void CalculateAudioProperties(float speedOfSound = 343.0f);
};

// Particle physics structure
struct PhysicsParticle {
    PhysicsVector3D position;                                                   // Current position
    PhysicsVector3D velocity;                                                   // Current velocity
    PhysicsVector3D acceleration;                                               // Current acceleration
    float life;                                                                 // Particle lifetime
    float mass;                                                                 // Particle mass
    float drag;                                                                 // Air resistance
    bool isActive;                                                              // Whether particle is active

    // Constructor
    PhysicsParticle() : life(1.0f), mass(0.1f), drag(DEFAULT_AIR_RESISTANCE), isActive(false) {}

    // Update particle physics
    void Update(float deltaTime);
};

//==============================================================================
// Physics Class Declaration
//==============================================================================
class Physics {
public:
    // Constructor and destructor
    Physics();
    ~Physics();

    // Initialization and cleanup
    bool Initialize();
    void Cleanup();
    bool IsInitialized() const { return m_bIsInitialized.load(); }

    // Update physics simulation
    void Update(float deltaTime);

    //==========================================================================
    // Curved Path Calculations (2D and 3D)
    //==========================================================================
    // Create curved path from array of points using Catmull-Rom splines
    CurvedPath2D CreateCurvedPath2D(const std::vector<PhysicsVector2D>& controlPoints, int resolution = 100);
    CurvedPath3D CreateCurvedPath3D(const std::vector<PhysicsVector3D>& controlPoints, int resolution = 100);

    // Calculate point on curve at specified time parameter (0.0 to 1.0)
    PhysicsVector2D GetCurvePoint2D(const CurvedPath2D& path, float t) const;
    PhysicsVector3D GetCurvePoint3D(const CurvedPath3D& path, float t) const;

    // Calculate tangent vector at specified point on curve
    PhysicsVector2D GetCurveTangent2D(const CurvedPath2D& path, float t) const;
    PhysicsVector3D GetCurveTangent3D(const CurvedPath3D& path, float t) const;

    // Calculate curve length between two points
    float CalculateCurveLength2D(const CurvedPath2D& path, float startT = 0.0f, float endT = 1.0f) const;
    float CalculateCurveLength3D(const CurvedPath3D& path, float startT = 0.0f, float endT = 1.0f) const;

    //==========================================================================
    // Reflection Path Calculations
    //==========================================================================
    // Calculate reflection based on incoming velocity and surface normal
    ReflectionData CalculateReflection(const PhysicsVector3D& incomingVelocity, const PhysicsVector3D& surfaceNormal,
        float restitution = DEFAULT_RESTITUTION, float friction = DEFAULT_FRICTION);

    // Calculate reflection for 2D scenarios
    PhysicsVector2D CalculateReflection2D(const PhysicsVector2D& incomingVelocity, const PhysicsVector2D& surfaceNormal,
        float restitution = DEFAULT_RESTITUTION);

    // Calculate multiple bounces along a path
    std::vector<PhysicsVector3D> CalculateMultipleBounces(const PhysicsVector3D& startPosition, const PhysicsVector3D& initialVelocity,
        const std::vector<PhysicsVector3D>& surfaceNormals, int maxBounces = 5);

    //==========================================================================
    // Gravity Calculations
    //==========================================================================
    // Add gravity field to simulation
    void AddGravityField(const GravityField& gravityField);
    void RemoveGravityField(int index);
    void ClearGravityFields();

    // Calculate gravity force at position
    PhysicsVector3D CalculateGravityAtPosition(const PhysicsVector3D& position) const;

    // Calculate orbital velocity for circular orbit
    float CalculateOrbitalVelocity(const PhysicsVector3D& position, const GravityField& gravityField) const;

    // Calculate escape velocity from gravity field
    float CalculateEscapeVelocity(const PhysicsVector3D& position, const GravityField& gravityField) const;

    //==========================================================================
    // Bouncing with Inertia and Forces
    //==========================================================================
    // Calculate bouncing trajectory with inertia
    std::vector<PhysicsVector3D> CalculateBouncingTrajectory(const PhysicsVector3D& startPosition, const PhysicsVector3D& initialVelocity,
        float groundHeight, float restitution = DEFAULT_RESTITUTION,
        float drag = DEFAULT_AIR_RESISTANCE, int maxBounces = 10);

    // Calculate final resting position after bouncing
    PhysicsVector3D CalculateRestingPosition(const PhysicsVector3D& startPosition, const PhysicsVector3D& initialVelocity,
        float groundHeight, float restitution = DEFAULT_RESTITUTION,
        float drag = DEFAULT_AIR_RESISTANCE);

    //==========================================================================
    // Collision Detection and Response
    //==========================================================================
    // AABB collision detection
    bool CheckAABBCollision(const PhysicsVector3D& minA, const PhysicsVector3D& maxA,
        const PhysicsVector3D& minB, const PhysicsVector3D& maxB) const;

    // Sphere collision detection
    bool CheckSphereCollision(const PhysicsVector3D& centerA, float radiusA,
        const PhysicsVector3D& centerB, float radiusB) const;

    // Ray-sphere intersection
    bool RaySphereIntersection(const PhysicsVector3D& rayOrigin, const PhysicsVector3D& rayDirection,
        const PhysicsVector3D& sphereCenter, float sphereRadius, float& hitDistance) const;

    // Continuous collision detection (prevents tunneling)
    bool ContinuousCollisionDetection(const PhysicsBody& bodyA, const PhysicsBody& bodyB, float deltaTime) const;

    // Generate collision manifold
    CollisionManifold GenerateCollisionManifold(PhysicsBody& bodyA, PhysicsBody& bodyB) const;

    // Resolve collision response
    void ResolveCollisionResponse(CollisionManifold& manifold);

    //==========================================================================
    // Ragdoll Physics
    //==========================================================================
    // Create ragdoll system
    std::vector<PhysicsBody> CreateRagdoll(const std::vector<PhysicsVector3D>& jointPositions,
        const std::vector<std::pair<int, int>>& connections);

    // Add joint constraint to ragdoll
    void AddRagdollJoint(const RagdollJoint& joint);
    void RemoveRagdollJoint(int index);

    // Update ragdoll physics
    void UpdateRagdoll(std::vector<PhysicsBody>& ragdollBodies, float deltaTime);

    // Apply animation blending to ragdoll
    void BlendRagdollWithAnimation(std::vector<PhysicsBody>& ragdollBodies,
        const std::vector<PhysicsVector3D>& animationPositions, float blendFactor);

    //==========================================================================
    // Newtonian Motion
    //==========================================================================
    // Apply Newton's laws of motion
    void ApplyNewtonianMotion(PhysicsBody& body, const PhysicsVector3D& force, float deltaTime);

    // Calculate projectile motion
    std::vector<PhysicsVector3D> CalculateProjectileMotion(const PhysicsVector3D& startPosition, const PhysicsVector3D& initialVelocity,
        float gravity = DEFAULT_GRAVITY, float drag = DEFAULT_AIR_RESISTANCE,
        float timeStep = 0.016f, float maxTime = 10.0f);

    // Calculate trajectory for given target
    PhysicsVector3D CalculateTrajectoryToTarget(const PhysicsVector3D& startPosition, const PhysicsVector3D& targetPosition,
        float gravity = DEFAULT_GRAVITY, float launchSpeed = 20.0f);

    //==========================================================================
    // Audio Physics
    //==========================================================================
    // Calculate audio properties for 3D sound
    AudioPhysicsData CalculateAudioPhysics(const PhysicsVector3D& listenerPosition, const PhysicsVector3D& sourcePosition,
        const PhysicsVector3D& sourceVelocity = PhysicsVector3D());

    // Calculate Doppler effect
    float CalculateDopplerShift(const PhysicsVector3D& sourceVelocity, const PhysicsVector3D& listenerVelocity,
        const PhysicsVector3D& direction, float speedOfSound = 343.0f) const;

    // Calculate sound occlusion based on obstacles
    float CalculateSoundOcclusion(const PhysicsVector3D& sourcePosition, const PhysicsVector3D& listenerPosition,
        const std::vector<PhysicsVector3D>& obstacles) const;

    // Calculate reverb based on environment
    float CalculateReverb(const PhysicsVector3D& position, float roomSize, float absorptionCoefficient = 0.3f) const;

    //==========================================================================
    // Particle Systems
    //==========================================================================
    // Create particle explosion effect
    std::vector<PhysicsParticle> CreateExplosion(const PhysicsVector3D& center, int particleCount,
        float explosionForce, float particleLifetime = 3.0f);

    // Update particle system physics
    void UpdateParticleSystem(std::vector<PhysicsParticle>& particles, float deltaTime);

    // Apply wind force to particles
    void ApplyWindForce(std::vector<PhysicsParticle>& particles, const PhysicsVector3D& windVelocity);

    // Apply gravity to particles
    void ApplyGravityToParticles(std::vector<PhysicsParticle>& particles, float gravity = DEFAULT_GRAVITY);

    //==========================================================================
    // Physics-based Animation
    //==========================================================================
    // Blend physics simulation with keyframe animation
    PhysicsVector3D BlendPhysicsWithAnimation(const PhysicsVector3D& physicsPosition, const PhysicsVector3D& animationPosition,
        float blendFactor) const;

    // Calculate recoil animation based on force
    PhysicsVector3D CalculateRecoilAnimation(const PhysicsVector3D& force, float mass, float dampening = 0.9f) const;

    // Apply secondary motion (follow-through) to animation
    PhysicsVector3D ApplySecondaryMotion(const PhysicsVector3D& primaryMotion, const PhysicsVector3D& previousSecondary,
        float stiffness = 0.1f, float damping = 0.8f) const;

    //==========================================================================
    // Utility and Debug Methods
    //==========================================================================
    // Get physics statistics
    void GetPhysicsStatistics(int& activeBodyCount, int& collisionCount, int& particleCount) const;

    // Debug visualization data
    std::vector<PhysicsVector3D> GetDebugLines() const { return m_debugLines; }
    void ClearDebugLines() { m_debugLines.clear(); }
    void AddDebugLine(const PhysicsVector3D& start, const PhysicsVector3D& end);

    // Performance profiling
    float GetLastUpdateTime() const { return m_lastUpdateTime; }
    void ResetPerformanceCounters();

    // Collision response helper methods
    void ApplyFrictionImpulse(PhysicsBody& bodyA, PhysicsBody& bodyB, const ContactPoint& contact,
        float combinedFriction, float normalImpulseMagnitude);

    void ApplyPositionCorrection(PhysicsBody& bodyA, PhysicsBody& bodyB, const ContactPoint& contact,
        const PhysicsVector3D& normal);

private:
    // Internal state
    std::atomic<bool> m_bIsInitialized;                                         // Initialization state
    std::atomic<bool> m_bHasCleanedUp;                                          // Cleanup state
    mutable std::mutex m_physicsMutex;                                          // Thread safety mutex

    // Physics simulation data
    std::vector<PhysicsBody> m_physicsBodies;                                   // All physics bodies in simulation
    std::vector<GravityField> m_gravityFields;                                  // Gravity fields affecting simulation
    std::vector<RagdollJoint> m_ragdollJoints;                                  // Ragdoll joint constraints
    std::vector<CollisionManifold> m_collisionManifolds;                       // Current collision manifolds

    // Debug and visualization
    std::vector<PhysicsVector3D> m_debugLines;                                  // Debug line visualization data

    // Performance tracking
    float m_lastUpdateTime;                                                     // Time taken for last update
    std::atomic<int> m_activeBodyCount;                                         // Number of active physics bodies
    std::atomic<int> m_collisionCount;
    std::atomic<int> m_particleCount;                                           // Number of active particles

    // References to required systems
    MathPrecalculation& m_mathPrecalc;                                          // Reference to math precalculation system
    ExceptionHandler& m_exceptionHandler;                                       // Reference to exception handler

    //==========================================================================
    // Internal Helper Methods
    //==========================================================================
    // Catmull-Rom spline interpolation
    PhysicsVector2D CatmullRomInterpolation2D(const PhysicsVector2D& p0, const PhysicsVector2D& p1,
        const PhysicsVector2D& p2, const PhysicsVector2D& p3, float t) const;
    PhysicsVector3D CatmullRomInterpolation3D(const PhysicsVector3D& p0, const PhysicsVector3D& p1,
        const PhysicsVector3D& p2, const PhysicsVector3D& p3, float t) const;

    // Bezier curve interpolation
    PhysicsVector2D BezierInterpolation2D(const PhysicsVector2D& p0, const PhysicsVector2D& p1,
        const PhysicsVector2D& p2, const PhysicsVector2D& p3, float t) const;
    PhysicsVector3D BezierInterpolation3D(const PhysicsVector3D& p0, const PhysicsVector3D& p1,
        const PhysicsVector3D& p2, const PhysicsVector3D& p3, float t) const;

    // Integration methods
    void EulerIntegration(PhysicsBody& body, float deltaTime);
    void VerletIntegration(PhysicsBody& body, float deltaTime);
    void RK4Integration(PhysicsBody& body, float deltaTime);

    // Collision detection helpers
    bool AABBvsAABB(const PhysicsVector3D& minA, const PhysicsVector3D& maxA,
        const PhysicsVector3D& minB, const PhysicsVector3D& maxB) const;
    bool SphereVsSphere(const PhysicsVector3D& centerA, float radiusA,
        const PhysicsVector3D& centerB, float radiusB, ContactPoint& contact) const;
    bool AABBvsSphere(const PhysicsVector3D& aabbMin, const PhysicsVector3D& aabbMax,
        const PhysicsVector3D& sphereCenter, float sphereRadius, ContactPoint& contact) const;

    // Constraint solving
    void SolvePositionConstraints();
    void SolveVelocityConstraints();
    void ApplyImpulseConstraints(CollisionManifold& manifold);

    // Optimization helpers
    void BroadPhaseCollisionDetection();
    void NarrowPhaseCollisionDetection();
    void UpdateSpatialHash();

    // Math utility functions using MathPrecalculation
    float FastMagnitude(const PhysicsVector3D& vector) const;
    PhysicsVector3D FastNormalize(const PhysicsVector3D& vector) const;
    float FastDotProduct(const PhysicsVector3D& a, const PhysicsVector3D& b) const;
    PhysicsVector3D FastCrossProduct(const PhysicsVector3D& a, const PhysicsVector3D& b) const;
    float FastDistance(const PhysicsVector3D& a, const PhysicsVector3D& b) const;

    // Precalculation integration methods
    void InitializePhysicsPrecalculations();
    void UpdateGravityLookupTables();
    void UpdateReflectionTables();
    void UpdateInertiaCoefficients();

    // Memory management
    void AllocatePhysicsMemory();
    void DeallocatePhysicsMemory();
    size_t GetPhysicsMemoryUsage() const;
};

//==============================================================================
// Global Physics Instance Access
//==============================================================================
extern Physics* g_pPhysics;

//==============================================================================
// Physics Utility Macros
//==============================================================================
#define PHYSICS_INSTANCE() (*g_pPhysics)
#define PHYSICS_SAFE_CALL(call) if(g_pPhysics && g_pPhysics->IsInitialized()) { call; }

//==============================================================================
// Debug Preprocessor Directives
//==============================================================================
#if defined(_DEBUG_PHYSICS_)
#define PHYSICS_DEBUG_LOG(level, message) debug.logLevelMessage(level, message)
#define PHYSICS_DEBUG_MSG(level, format, ...) debug.logDebugMessage(level, format, __VA_ARGS__)
#else
#define PHYSICS_DEBUG_LOG(level, message) ((void)0)
#define PHYSICS_DEBUG_MSG(level, format, ...) ((void)0)
#endif

// Record function calls for exception handling
#define PHYSICS_RECORD_FUNCTION() RECORD_FUNCTION_CALL()

// External references
extern Debug debug;
extern ExceptionHandler exceptionHandler;

#pragma warning(pop)