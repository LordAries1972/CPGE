//-------------------------------------------------------------------------------------------------
// Physics.cpp - Comprehensive Physics Simulation Class Implementation
//
// This implementation provides high-performance physics calculations optimized for gaming
// platforms with extensive use of MathPrecalculation for real-time performance.
//-------------------------------------------------------------------------------------------------

#include "Includes.h"
#include "Physics.h"
#include "Debug.h"
#include "ExceptionHandler.h"
#include "MathPrecalculation.h"

// External references
extern Debug debug;
extern ExceptionHandler exceptionHandler;

// Global physics instance
Physics* g_pPhysics = nullptr;

//==============================================================================
// PhysicsVector2D Implementation
//==============================================================================
float PhysicsVector2D::Magnitude() const
{
    // Use fast square root from MathPrecalculation for optimal performance
    return FAST_SQRT(x * x + y * y);
}

PhysicsVector2D PhysicsVector2D::Normalized() const
{
    // Calculate magnitude using fast math operations
    float mag = Magnitude();
    
    // Handle zero-length vector to prevent division by zero
    if (mag < MIN_VELOCITY_THRESHOLD)
    {
        return PhysicsVector2D(0.0f, 0.0f);
    }
    
    // Normalize by dividing by magnitude
    float invMag = 1.0f / mag;
    return PhysicsVector2D(x * invMag, y * invMag);
}

//==============================================================================
// PhysicsVector3D Implementation
//==============================================================================
float PhysicsVector3D::Magnitude() const
{
    // Use fast square root from MathPrecalculation for optimal performance
    return FAST_SQRT(x * x + y * y + z * z);
}

PhysicsVector3D PhysicsVector3D::Normalized() const
{
    // Calculate magnitude using fast math operations
    float mag = Magnitude();
    
    // Handle zero-length vector to prevent division by zero
    if (mag < MIN_VELOCITY_THRESHOLD)
    {
        return PhysicsVector3D(0.0f, 0.0f, 0.0f);
    }
    
    // Normalize by dividing by magnitude
    float invMag = 1.0f / mag;
    return PhysicsVector3D(x * invMag, y * invMag, z * invMag);
}

PhysicsVector3D PhysicsVector3D::Cross(const PhysicsVector3D& other) const
{
    // Calculate cross product using standard formula
    return PhysicsVector3D(
        y * other.z - z * other.y,
        z * other.x - x * other.z,
        x * other.y - y * other.x
    );
}

//==============================================================================
// CurvedPath2D Implementation
//==============================================================================
void CurvedPath2D::AddPoint(const PhysicsVector2D& point)
{
    // Ensure we don't exceed maximum path coordinates
    if (coordinates.size() >= MAX_PATH_COORDINATES)
    {
#if defined(_DEBUG_PHYSICS_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[Physics] Maximum path coordinates reached, ignoring additional points");
#endif
        return;
    }
    
    // Add point to the path
    coordinates.push_back(point);
    
    // Recalculate path properties if we have enough points
    if (coordinates.size() > 1)
    {
        CalculateTangentsAndCurvatures();
    }
}

void CurvedPath2D::CalculateTangentsAndCurvatures()
{
    // Clear existing data
    tangents.clear();
    curvatures.clear();
    totalLength = 0.0f;
    
    // Need at least 2 points for tangent calculation
    if (coordinates.size() < 2)
    {
        return;
    }
    
    // Reserve memory for efficiency
    tangents.reserve(coordinates.size());
    curvatures.reserve(coordinates.size());
    
    // Calculate tangents and curvatures for each point
    for (size_t i = 0; i < coordinates.size(); ++i)
    {
        PhysicsVector2D tangent;
        float curvature = 0.0f;
        
        if (i == 0)
        {
            // First point: use forward difference
            tangent = coordinates[i + 1] - coordinates[i];
        }
        else if (i == coordinates.size() - 1)
        {
            // Last point: use backward difference
            tangent = coordinates[i] - coordinates[i - 1];
        }
        else
        {
            // Middle points: use central difference for better accuracy
            tangent = coordinates[i + 1] - coordinates[i - 1];
            tangent = tangent * 0.5f; // Scale for central difference
            
            // Calculate curvature using three points
            PhysicsVector2D p1 = coordinates[i - 1];
            PhysicsVector2D p2 = coordinates[i];
            PhysicsVector2D p3 = coordinates[i + 1];
            
            // Curvature formula: |v1 x v2| / |v1|^3 where v1 = p2-p1, v2 = p3-p2
            PhysicsVector2D v1 = p2 - p1;
            PhysicsVector2D v2 = p3 - p2;
            float crossProduct = v1.Cross(v2);
            float v1Magnitude = v1.Magnitude();
            
            if (v1Magnitude > MIN_VELOCITY_THRESHOLD)
            {
                curvature = std::abs(crossProduct) / (v1Magnitude * v1Magnitude * v1Magnitude);
            }
        }
        
        // Normalize tangent and store angle
        PhysicsVector2D normalizedTangent = tangent.Normalized();
        float angle = FAST_ATAN2(normalizedTangent.y, normalizedTangent.x);
        
        tangents.push_back(angle);
        curvatures.push_back(curvature);
        
        // Add to total length (except for first point)
        if (i > 0)
        {
            PhysicsVector2D segment = coordinates[i] - coordinates[i - 1];
            totalLength += segment.Magnitude();
        }
    }
    
#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[Physics] Calculated path with %zu points, total length: %.2f", 
                         coordinates.size(), totalLength);
#endif
}

PhysicsVector2D CurvedPath2D::GetPointAtDistance(float distance) const
{
    // Handle edge cases
    if (coordinates.empty())
    {
        return PhysicsVector2D();
    }
    
    if (coordinates.size() == 1)
    {
        return coordinates[0];
    }
    
    // Clamp distance to valid range
    distance = std::clamp(distance, 0.0f, totalLength);
    
    // Find the segment that contains this distance
    float currentDistance = 0.0f;
    for (size_t i = 1; i < coordinates.size(); ++i)
    {
        PhysicsVector2D segment = coordinates[i] - coordinates[i - 1];
        float segmentLength = segment.Magnitude();
        
        if (currentDistance + segmentLength >= distance)
        {
            // Interpolate within this segment
            float t = (distance - currentDistance) / segmentLength;
            return coordinates[i - 1] + segment * t;
        }
        
        currentDistance += segmentLength;
    }
    
    // Return last point if we somehow didn't find the segment
    return coordinates.back();
}

PhysicsVector2D CurvedPath2D::GetTangentAtDistance(float distance) const
{
    // Handle edge cases
    if (tangents.empty())
    {
        return PhysicsVector2D(1.0f, 0.0f); // Default to right direction
    }
    
    // Find corresponding point index for this distance
    float currentDistance = 0.0f;
    size_t pointIndex = 0;
    
    for (size_t i = 1; i < coordinates.size(); ++i)
    {
        PhysicsVector2D segment = coordinates[i] - coordinates[i - 1];
        float segmentLength = segment.Magnitude();
        
        if (currentDistance + segmentLength >= distance)
        {
            pointIndex = i - 1;
            break;
        }
        
        currentDistance += segmentLength;
    }
    
    // Get tangent angle and convert to vector
    float angle = tangents[pointIndex];
    return PhysicsVector2D(FAST_COS(angle), FAST_SIN(angle));
}

void CurvedPath2D::Clear()
{
    // Clear all path data
    coordinates.clear();
    tangents.clear();
    curvatures.clear();
    totalLength = 0.0f;
    isLooped = false;
    
#if defined(_DEBUG_PHYSICS_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[Physics] Cleared 2D curved path");
#endif
}

//==============================================================================
// CurvedPath3D Implementation
//==============================================================================
void CurvedPath3D::AddPoint(const PhysicsVector3D& point)
{
    // Ensure we don't exceed maximum path coordinates
    if (coordinates.size() >= MAX_PATH_COORDINATES)
    {
#if defined(_DEBUG_PHYSICS_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[Physics] Maximum path coordinates reached, ignoring additional points");
#endif
        return;
    }
    
    // Add point to the path
    coordinates.push_back(point);
    
    // Recalculate path properties if we have enough points
    if (coordinates.size() > 1)
    {
        CalculateTangentsAndCurvatures();
    }
}

void CurvedPath3D::CalculateTangentsAndCurvatures()
{
    // Clear existing data
    tangents.clear();
    curvatures.clear();
    totalLength = 0.0f;
    
    // Need at least 2 points for tangent calculation
    if (coordinates.size() < 2)
    {
        return;
    }
    
    // Reserve memory for efficiency
    tangents.reserve(coordinates.size());
    curvatures.reserve(coordinates.size());
    
    // Calculate tangents and curvatures for each point
    for (size_t i = 0; i < coordinates.size(); ++i)
    {
        PhysicsVector3D tangent;
        float curvature = 0.0f;
        
        if (i == 0)
        {
            // First point: use forward difference
            tangent = coordinates[i + 1] - coordinates[i];
        }
        else if (i == coordinates.size() - 1)
        {
            // Last point: use backward difference
            tangent = coordinates[i] - coordinates[i - 1];
        }
        else
        {
            // Middle points: use central difference for better accuracy
            tangent = coordinates[i + 1] - coordinates[i - 1];
            tangent = tangent * 0.5f; // Scale for central difference
            
            // Calculate curvature using three points
            PhysicsVector3D p1 = coordinates[i - 1];
            PhysicsVector3D p2 = coordinates[i];
            PhysicsVector3D p3 = coordinates[i + 1];
            
            // Curvature formula for 3D: |v1 x v2| / |v1|^3 where v1 = p2-p1, v2 = p3-p2
            PhysicsVector3D v1 = p2 - p1;
            PhysicsVector3D v2 = p3 - p2;
            PhysicsVector3D crossProduct = v1.Cross(v2);
            float crossMagnitude = crossProduct.Magnitude();
            float v1Magnitude = v1.Magnitude();
            
            if (v1Magnitude > MIN_VELOCITY_THRESHOLD)
            {
                curvature = crossMagnitude / (v1Magnitude * v1Magnitude * v1Magnitude);
            }
        }
        
        // Normalize tangent and store
        PhysicsVector3D normalizedTangent = tangent.Normalized();
        tangents.push_back(normalizedTangent);
        curvatures.push_back(curvature);
        
        // Add to total length (except for first point)
        if (i > 0)
        {
            PhysicsVector3D segment = coordinates[i] - coordinates[i - 1];
            totalLength += segment.Magnitude();
        }
    }
    
#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[Physics] Calculated 3D path with %zu points, total length: %.2f", 
                         coordinates.size(), totalLength);
#endif
}

PhysicsVector3D CurvedPath3D::GetPointAtDistance(float distance) const
{
    // Handle edge cases
    if (coordinates.empty())
    {
        return PhysicsVector3D();
    }
    
    if (coordinates.size() == 1)
    {
        return coordinates[0];
    }
    
    // Clamp distance to valid range
    distance = std::clamp(distance, 0.0f, totalLength);
    
    // Find the segment that contains this distance
    float currentDistance = 0.0f;
    for (size_t i = 1; i < coordinates.size(); ++i)
    {
        PhysicsVector3D segment = coordinates[i] - coordinates[i - 1];
        float segmentLength = segment.Magnitude();
        
        if (currentDistance + segmentLength >= distance)
        {
            // Interpolate within this segment
            float t = (distance - currentDistance) / segmentLength;
            return coordinates[i - 1] + segment * t;
        }
        
        currentDistance += segmentLength;
    }
    
    // Return last point if we somehow didn't find the segment
    return coordinates.back();
}

PhysicsVector3D CurvedPath3D::GetTangentAtDistance(float distance) const
{
    // Handle edge cases
    if (tangents.empty())
    {
        return PhysicsVector3D(1.0f, 0.0f, 0.0f); // Default to forward direction
    }
    
    // Find corresponding point index for this distance
    float currentDistance = 0.0f;
    size_t pointIndex = 0;
    
    for (size_t i = 1; i < coordinates.size(); ++i)
    {
        PhysicsVector3D segment = coordinates[i] - coordinates[i - 1];
        float segmentLength = segment.Magnitude();
        
        if (currentDistance + segmentLength >= distance)
        {
            pointIndex = i - 1;
            break;
        }
        
        currentDistance += segmentLength;
    }
    
    // Return tangent vector at this point
    return tangents[pointIndex];
}

void CurvedPath3D::Clear()
{
    // Clear all path data
    coordinates.clear();
    tangents.clear();
    curvatures.clear();
    totalLength = 0.0f;
    isLooped = false;
    
#if defined(_DEBUG_PHYSICS_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[Physics] Cleared 3D curved path");
#endif
}

//==============================================================================
// GravityField Implementation
//==============================================================================
float GravityField::CalculateGravityForce(float distance) const
{
    // Prevent division by zero for very close distances
    if (distance < 0.1f)
    {
        distance = 0.1f;
    }
    
    // Newton's law of universal gravitation: F = G * m1 * m2 / r^2
    // Simplified for gaming: F = intensity * mass / r^2
    float force = intensity * mass / (distance * distance);
    
    // Special handling for black holes (exponential increase in gravity)
    if (isBlackHole && distance < radius)
    {
        float factor = 1.0f - (distance / radius);
        force *= std::exp(factor * 5.0f); // Exponential increase as we approach black hole
    }
    
    return force;
}

PhysicsVector3D GravityField::CalculateGravityVector(const PhysicsVector3D& position) const
{
    // Calculate direction vector from position to gravity center
    PhysicsVector3D direction = center - position;
    float distance = direction.Magnitude();
    
    // Calculate gravity force magnitude
    float force = CalculateGravityForce(distance);
    
    // Normalize direction and apply force
    PhysicsVector3D normalizedDirection = direction.Normalized();
    return normalizedDirection * force;
}

//==============================================================================
// PhysicsBody Implementation
//==============================================================================
void PhysicsBody::SetMass(float newMass)
{
    // Ensure mass is positive
    if (newMass <= 0.0f)
    {
#if defined(_DEBUG_PHYSICS_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[Physics] Attempting to set non-positive mass, using default value");
#endif
        newMass = 1.0f;
    }
    
    mass = newMass;
    
    // Calculate inverse mass for optimized physics calculations
    // Static bodies have infinite mass (inverse mass = 0)
    inverseMass = isStatic ? 0.0f : (1.0f / mass);
}

void PhysicsBody::ApplyForce(const PhysicsVector3D& force)
{
    // Only apply force to dynamic bodies
    if (!isStatic && isActive)
    {
        // F = ma, therefore a = F/m = F * inverseMass
        acceleration += force * inverseMass;
    }
}

void PhysicsBody::ApplyImpulse(const PhysicsVector3D& impulse)
{
    // Only apply impulse to dynamic bodies
    if (!isStatic && isActive)
    {
        // Impulse directly changes velocity: Δv = J/m = J * inverseMass
        velocity += impulse * inverseMass;
    }
}

void PhysicsBody::IntegrateVelocity(float deltaTime)
{
    // Only integrate for dynamic bodies
    if (!isStatic && isActive)
    {
        // Update velocity: v = v + a * dt
        velocity += acceleration * deltaTime;
        
        // Apply drag/air resistance: v = v * (1 - drag * dt)
        float dragFactor = 1.0f - (drag * deltaTime);
        dragFactor = std::max(0.0f, dragFactor); // Prevent negative drag
        velocity *= dragFactor;
        
        // Reset acceleration for next frame
        acceleration = PhysicsVector3D();
    }
}

void PhysicsBody::IntegratePosition(float deltaTime)
{
    // Only integrate for dynamic bodies
    if (!isStatic && isActive)
    {
        // Update position: p = p + v * dt
        position += velocity * deltaTime;
        
        // Apply angular velocity to rotation (if needed for 3D rotations)
        // This would require quaternion or matrix rotations in a full implementation
    }
}

//==============================================================================
// ContactPoint and CollisionManifold Implementation
//==============================================================================
void CollisionManifold::AddContact(const ContactPoint& contact)
{
    // Ensure we don't exceed maximum contact points
    if (contacts.size() >= MAX_COLLISION_CONTACTS)
    {
#if defined(_DEBUG_PHYSICS_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[Physics] Maximum collision contacts reached");
#endif
        return;
    }
    
    contacts.push_back(contact);
}

void CollisionManifold::ResolveCollision()
{
    // Ensure we have valid bodies
    if (!bodyA || !bodyB || contacts.empty())
    {
        return;
    }
    
    // Calculate relative velocity
    PhysicsVector3D relativeVelocity = bodyB->velocity - bodyA->velocity;
    separatingVelocity = relativeVelocity.Dot(normal);
    
    // Don't resolve if velocities are separating
    if (separatingVelocity > 0.0f)
    {
        return;
    }
    
    // Calculate restitution coefficient (bounce)
    float restitution = std::min(bodyA->restitution, bodyB->restitution);
    
    // Calculate impulse magnitude
    float impulseMagnitude = -(1.0f + restitution) * separatingVelocity;
    impulseMagnitude /= (bodyA->inverseMass + bodyB->inverseMass);
    
    // Apply impulse to both bodies
    PhysicsVector3D impulse = normal * impulseMagnitude;
    bodyA->ApplyImpulse(impulse * -1.0f);
    bodyB->ApplyImpulse(impulse);
    
#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[Physics] Resolved collision with impulse magnitude: %.3f", impulseMagnitude);
#endif
}

//==============================================================================
// RagdollJoint Implementation
//==============================================================================
void RagdollJoint::ApplyConstraints()
{
    // Ensure we have valid bodies
    if (!bodyA || !bodyB || !isActive)
    {
        return;
    }
    
    // Calculate current relative position
    PhysicsVector3D relativePosition = bodyB->position - bodyA->position;
    PhysicsVector3D targetPosition = anchorB - anchorA;
    
    // Calculate constraint violation
    PhysicsVector3D constraint = relativePosition - targetPosition;
    float constraintMagnitude = constraint.Magnitude();
    
    // Apply position correction if constraint is violated
    if (constraintMagnitude > MIN_VELOCITY_THRESHOLD)
    {
        PhysicsVector3D correction = constraint.Normalized() * (constraintMagnitude * 0.5f);
        
        // Apply correction based on inverse mass ratio
        float totalInverseMass = bodyA->inverseMass + bodyB->inverseMass;
        if (totalInverseMass > 0.0f)
        {
            float massRatioA = bodyA->inverseMass / totalInverseMass;
            float massRatioB = bodyB->inverseMass / totalInverseMass;
            
            bodyA->position += correction * massRatioA;
            bodyB->position -= correction * massRatioB;
        }
    }
    
    // Apply angular constraints (simplified)
    PhysicsVector3D relativeVelocity = bodyB->velocity - bodyA->velocity;
    PhysicsVector3D dampingForce = relativeVelocity * (-damping);
    
    bodyA->ApplyForce(dampingForce * -1.0f);
    bodyB->ApplyForce(dampingForce);
}

//==============================================================================
// AudioPhysicsData Implementation
//==============================================================================
void AudioPhysicsData::CalculateAudioProperties(float speedOfSound)
{
    // Calculate distance between source and listener
    PhysicsVector3D distanceVector = sourcePosition - listenerPosition;
    distance = distanceVector.Magnitude();
    
    // Calculate volume falloff based on inverse square law
    if (distance > 0.1f)
    {
        volumeFalloff = 1.0f / (1.0f + distance * distance * 0.01f);
    }
    else
    {
        volumeFalloff = 1.0f;
    }
    
    // Calculate Doppler shift
    if (distance > MIN_VELOCITY_THRESHOLD)
    {
        PhysicsVector3D direction = distanceVector.Normalized();
        float relativeVelocity = sourceVelocity.Dot(direction);
        dopplerShift = speedOfSound / (speedOfSound - relativeVelocity);
        
        // Clamp Doppler shift to reasonable range
        dopplerShift = std::clamp(dopplerShift, 0.5f, 2.0f);
    }
    else
    {
        dopplerShift = 1.0f;
    }
    
    // Calculate basic reverb based on distance
    reverb = std::min(0.8f, distance * 0.01f);
    
#if defined(_DEBUG_PHYSICS_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[Physics] Audio properties - Distance: %.2f, Doppler: %.3f, Volume: %.3f", 
                         distance, dopplerShift, volumeFalloff);
#endif
}

//==============================================================================
// PhysicsParticle Implementation
//==============================================================================
void PhysicsParticle::Update(float deltaTime)
{
    // Only update active particles
    if (!isActive)
    {
        return;
    }
    
    // Update velocity with acceleration
    velocity += acceleration * deltaTime;
    
    // Apply drag
    float dragFactor = 1.0f - (drag * deltaTime);
    dragFactor = std::max(0.0f, dragFactor);
    velocity *= dragFactor;
    
    // Update position
    position += velocity * deltaTime;
    
    // Update lifetime
    life -= deltaTime;
    
    // Deactivate particle if lifetime expired
    if (life <= 0.0f)
    {
        isActive = false;
    }
    
    // Reset acceleration for next frame
    acceleration = PhysicsVector3D();
}

//==============================================================================
// Physics Class Constructor and Destructor
//==============================================================================
Physics::Physics() :
    m_bIsInitialized(false),
    m_bHasCleanedUp(false),
    m_lastUpdateTime(0.0f),
    m_activeBodyCount(0),
    m_collisionCount(0),
    m_particleCount(0),
    m_mathPrecalc(MathPrecalculation::GetInstance()),
m_exceptionHandler(ExceptionHandler::GetInstance())
{
   PHYSICS_RECORD_FUNCTION();
   
   // Reserve memory for physics collections to avoid frequent reallocations
   m_physicsBodies.reserve(1000);
   m_gravityFields.reserve(10);
   m_ragdollJoints.reserve(MAX_RAGDOLL_JOINTS);
   m_collisionManifolds.reserve(100);
   m_debugLines.reserve(1000);
   
#if defined(_DEBUG_PHYSICS_)
   debug.logLevelMessage(LogLevel::LOG_INFO, L"[Physics] Constructor called - Memory reserved for physics systems");
#endif
}

Physics::~Physics()
{
   PHYSICS_RECORD_FUNCTION();
   
   // Ensure cleanup is performed
   if (!m_bHasCleanedUp.load())
   {
       Cleanup();
   }
   
#if defined(_DEBUG_PHYSICS_)
   debug.logLevelMessage(LogLevel::LOG_INFO, L"[Physics] Destructor called - All resources cleaned up");
#endif
}

//==============================================================================
// Initialization and Cleanup Methods
//==============================================================================
bool Physics::Initialize()
{
   PHYSICS_RECORD_FUNCTION();
   
   // Check if already initialized to prevent double initialization
   if (m_bIsInitialized.load())
   {
#if defined(_DEBUG_PHYSICS_)
       debug.logLevelMessage(LogLevel::LOG_WARNING, L"[Physics] Already initialized - skipping");
#endif
       return true;
   }
   
   // Thread-safe initialization using mutex
   std::lock_guard<std::mutex> lock(m_physicsMutex);
   
   // Double-check pattern to ensure thread safety
   if (m_bIsInitialized.load())
   {
       return true;
   }
   
#if defined(_DEBUG_PHYSICS_)
   debug.logLevelMessage(LogLevel::LOG_INFO, L"[Physics] Starting initialization of physics systems");
#endif
   
   try
   {
       // Initialize MathPrecalculation if not already done
       if (!m_mathPrecalc.IsInitialized())
       {
           if (!m_mathPrecalc.Initialize())
           {
               throw std::runtime_error("Failed to initialize MathPrecalculation system");
           }
       }
       
       // Initialize exception handler if not already done
       if (!m_exceptionHandler.Initialize())
       {
#if defined(_DEBUG_PHYSICS_)
           debug.logLevelMessage(LogLevel::LOG_WARNING, L"[Physics] ExceptionHandler initialization failed, continuing without it");
#endif
       }
       
       // Initialize physics-specific precalculations
       InitializePhysicsPrecalculations();
       
       // Allocate physics memory pools
       AllocatePhysicsMemory();
       
       // Reset performance counters
       ResetPerformanceCounters();
       
       // Set global physics instance
       g_pPhysics = this;
       
       // Mark as successfully initialized
       m_bIsInitialized.store(true);
       
#if defined(_DEBUG_PHYSICS_)
       debug.logDebugMessage(LogLevel::LOG_INFO, L"[Physics] Initialization completed successfully - Memory usage: %zu bytes", 
                            GetPhysicsMemoryUsage());
#endif
       
       return true;
   }
   catch (const std::exception& e)
   {
       // Convert exception message to wide string for logging
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       
       debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[Physics] Initialization failed with exception: " + wErrorMsg);
       
       // Clean up any partially initialized resources
       Cleanup();
       return false;
   }
   catch (...)
   {
       debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[Physics] Initialization failed with unknown exception");
       
       // Clean up any partially initialized resources
       Cleanup();
       return false;
   }
}

void Physics::Cleanup()
{
   PHYSICS_RECORD_FUNCTION();
   
   // Check if already cleaned up
   if (m_bHasCleanedUp.load())
   {
       return;
   }
   
   // Thread-safe cleanup using mutex
   std::lock_guard<std::mutex> lock(m_physicsMutex);
   
   // Double-check pattern to ensure thread safety
   if (m_bHasCleanedUp.load())
   {
       return;
   }
   
#if defined(_DEBUG_PHYSICS_)
   debug.logLevelMessage(LogLevel::LOG_INFO, L"[Physics] Starting cleanup of physics systems");
#endif
   
   // Deallocate physics memory
   DeallocatePhysicsMemory();
   
   // Clear all physics collections
   m_physicsBodies.clear();
   m_gravityFields.clear();
   m_ragdollJoints.clear();
   m_collisionManifolds.clear();
   m_debugLines.clear();
   
   // Shrink collections to free memory
   m_physicsBodies.shrink_to_fit();
   m_gravityFields.shrink_to_fit();
   m_ragdollJoints.shrink_to_fit();
   m_collisionManifolds.shrink_to_fit();
   m_debugLines.shrink_to_fit();
   
   // Reset counters
   m_activeBodyCount.store(0);
   m_collisionCount.store(0);
   m_particleCount.store(0);
   m_lastUpdateTime = 0.0f;
   
   // Clear global physics instance
   if (g_pPhysics == this)
   {
       g_pPhysics = nullptr;
   }
   
   // Reset state flags
   m_bIsInitialized.store(false);
   m_bHasCleanedUp.store(true);
   
#if defined(_DEBUG_PHYSICS_)
   debug.logLevelMessage(LogLevel::LOG_INFO, L"[Physics] Cleanup completed successfully");
#endif
}

//==============================================================================
// Main Physics Update Method
//==============================================================================
void Physics::Update(float deltaTime)
{
   PHYSICS_RECORD_FUNCTION();
   
   // Ensure physics system is initialized
   if (!m_bIsInitialized.load())
   {
#if defined(_DEBUG_PHYSICS_)
       debug.logLevelMessage(LogLevel::LOG_WARNING, L"[Physics] Update called before initialization");
#endif
       return;
   }
   
   // Measure update time for performance profiling
   auto startTime = std::chrono::high_resolution_clock::now();
   
   try
   {
       // Thread-safe update using mutex
       std::lock_guard<std::mutex> lock(m_physicsMutex);
       
       // Update all physics bodies
       int activeBodies = 0;
       for (auto& body : m_physicsBodies)
       {
           if (body.isActive)
           {
               // Apply gravity forces
               PhysicsVector3D gravityForce = CalculateGravityAtPosition(body.position);
               body.ApplyForce(gravityForce);
               
               // Integrate physics using Verlet integration for stability
               VerletIntegration(body, deltaTime);
               
               activeBodies++;
           }
       }
       
       // Update active body count
       m_activeBodyCount.store(activeBodies);
       
       // Perform collision detection and response
       BroadPhaseCollisionDetection();
       NarrowPhaseCollisionDetection();
       
       // Resolve collision manifolds
       int collisionCount = 0;
       for (auto& manifold : m_collisionManifolds)
       {
           manifold.ResolveCollision();
           collisionCount++;
       }
       m_collisionCount.store(collisionCount);
       
       // Update ragdoll joint constraints
       for (auto& joint : m_ragdollJoints)
       {
           if (joint.isActive)
           {
               joint.ApplyConstraints();
           }
       }
       
       // Solve position and velocity constraints
       SolvePositionConstraints();
       SolveVelocityConstraints();
       
       // Clear collision manifolds for next frame
       m_collisionManifolds.clear();
       
#if defined(_DEBUG_PHYSICS_)
       if (activeBodies > 0 || collisionCount > 0)
       {
           debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[Physics] Updated %d active bodies, %d collisions", 
                                activeBodies, collisionCount);
       }
#endif
   }
   catch (const std::exception& e)
   {
       // Log exception and continue (don't crash the game)
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Exception during update: " + wErrorMsg);
       
       // Record exception for debugging
       m_exceptionHandler.LogException(e, "Physics::Update");
   }
   catch (...)
   {
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Unknown exception during update");
       
       // Record unknown exception
       m_exceptionHandler.LogCustomException("Unknown exception in Physics::Update", "Physics::Update");
   }
   
   // Calculate update time
   auto endTime = std::chrono::high_resolution_clock::now();
   auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
   m_lastUpdateTime = duration.count() / 1000.0f; // Convert to milliseconds
}

//==============================================================================
// Curved Path Calculation Methods
//==============================================================================
CurvedPath2D Physics::CreateCurvedPath2D(const std::vector<PhysicsVector2D>& controlPoints, int resolution)
{
   PHYSICS_RECORD_FUNCTION();
   
   CurvedPath2D path;
   
   // Validate input parameters
   if (controlPoints.size() < 2)
   {
#if defined(_DEBUG_PHYSICS_)
       debug.logLevelMessage(LogLevel::LOG_WARNING, L"[Physics] Need at least 2 control points for curved path");
#endif
       return path;
   }
   
   if (resolution <= 0)
   {
       resolution = 100; // Default resolution
   }
   
   // Ensure we don't exceed maximum path coordinates
   resolution = std::min(resolution, MAX_PATH_COORDINATES - static_cast<int>(controlPoints.size()));
   
#if defined(_DEBUG_PHYSICS_)
   debug.logDebugMessage(LogLevel::LOG_INFO, L"[Physics] Creating 2D curved path with %zu control points, resolution %d", 
                        controlPoints.size(), resolution);
#endif
   
   try
   {
       // Generate smooth curve using Catmull-Rom spline interpolation
       for (int i = 0; i < resolution; ++i)
       {
           float t = static_cast<float>(i) / static_cast<float>(resolution - 1);
           
           // Find the appropriate segment for this t value
           float segmentT = t * static_cast<float>(controlPoints.size() - 1);
           int segmentIndex = static_cast<int>(segmentT);
           float localT = segmentT - static_cast<float>(segmentIndex);
           
           // Ensure we stay within bounds
           segmentIndex = std::clamp(segmentIndex, 0, static_cast<int>(controlPoints.size()) - 2);
           
           // Get control points for Catmull-Rom interpolation
           PhysicsVector2D p0 = (segmentIndex > 0) ? controlPoints[segmentIndex - 1] : controlPoints[segmentIndex];
           PhysicsVector2D p1 = controlPoints[segmentIndex];
           PhysicsVector2D p2 = controlPoints[segmentIndex + 1];
           PhysicsVector2D p3 = (segmentIndex + 2 < static_cast<int>(controlPoints.size())) ? 
                                 controlPoints[segmentIndex + 2] : controlPoints[segmentIndex + 1];
           
           // Calculate interpolated point using Catmull-Rom spline
           PhysicsVector2D interpolatedPoint = CatmullRomInterpolation2D(p0, p1, p2, p3, localT);
           path.AddPoint(interpolatedPoint);
       }
       
       // Add original control points to ensure path passes through them
       for (const auto& controlPoint : controlPoints)
       {
           path.AddPoint(controlPoint);
       }
       
#if defined(_DEBUG_PHYSICS_)
       debug.logDebugMessage(LogLevel::LOG_INFO, L"[Physics] Created 2D curved path with %zu total points", 
                            path.coordinates.size());
#endif
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error creating 2D curved path: " + wErrorMsg);
       
       // Return empty path on error
       path.Clear();
   }
   
   return path;
}

CurvedPath3D Physics::CreateCurvedPath3D(const std::vector<PhysicsVector3D>& controlPoints, int resolution)
{
   PHYSICS_RECORD_FUNCTION();
   
   CurvedPath3D path;
   
   // Validate input parameters
   if (controlPoints.size() < 2)
   {
#if defined(_DEBUG_PHYSICS_)
       debug.logLevelMessage(LogLevel::LOG_WARNING, L"[Physics] Need at least 2 control points for curved path");
#endif
       return path;
   }
   
   if (resolution <= 0)
   {
       resolution = 100; // Default resolution
   }
   
   // Ensure we don't exceed maximum path coordinates
   resolution = std::min(resolution, MAX_PATH_COORDINATES - static_cast<int>(controlPoints.size()));
   
#if defined(_DEBUG_PHYSICS_)
   debug.logDebugMessage(LogLevel::LOG_INFO, L"[Physics] Creating 3D curved path with %zu control points, resolution %d", 
                        controlPoints.size(), resolution);
#endif
   
   try
   {
       // Generate smooth curve using Catmull-Rom spline interpolation
       for (int i = 0; i < resolution; ++i)
       {
           float t = static_cast<float>(i) / static_cast<float>(resolution - 1);
           
           // Find the appropriate segment for this t value
           float segmentT = t * static_cast<float>(controlPoints.size() - 1);
           int segmentIndex = static_cast<int>(segmentT);
           float localT = segmentT - static_cast<float>(segmentIndex);
           
           // Ensure we stay within bounds
           segmentIndex = std::clamp(segmentIndex, 0, static_cast<int>(controlPoints.size()) - 2);
           
           // Get control points for Catmull-Rom interpolation
           PhysicsVector3D p0 = (segmentIndex > 0) ? controlPoints[segmentIndex - 1] : controlPoints[segmentIndex];
           PhysicsVector3D p1 = controlPoints[segmentIndex];
           PhysicsVector3D p2 = controlPoints[segmentIndex + 1];
           PhysicsVector3D p3 = (segmentIndex + 2 < static_cast<int>(controlPoints.size())) ? 
                                 controlPoints[segmentIndex + 2] : controlPoints[segmentIndex + 1];
           
           // Calculate interpolated point using Catmull-Rom spline
           PhysicsVector3D interpolatedPoint = CatmullRomInterpolation3D(p0, p1, p2, p3, localT);
           path.AddPoint(interpolatedPoint);
       }
       
       // Add original control points to ensure path passes through them
       for (const auto& controlPoint : controlPoints)
       {
           path.AddPoint(controlPoint);
       }
       
#if defined(_DEBUG_PHYSICS_)
       debug.logDebugMessage(LogLevel::LOG_INFO, L"[Physics] Created 3D curved path with %zu total points", 
                            path.coordinates.size());
#endif
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error creating 3D curved path: " + wErrorMsg);
       
       // Return empty path on error
       path.Clear();
   }
   
   return path;
}

PhysicsVector2D Physics::GetCurvePoint2D(const CurvedPath2D& path, float t) const
{
   PHYSICS_RECORD_FUNCTION();
   
   // Clamp t to valid range [0, 1]
   t = std::clamp(t, 0.0f, 1.0f);
   
   // Handle edge cases
   if (path.coordinates.empty())
   {
       return PhysicsVector2D();
   }
   
   if (path.coordinates.size() == 1)
   {
       return path.coordinates[0];
   }
   
   // Calculate distance along path
   float targetDistance = t * path.totalLength;
   return path.GetPointAtDistance(targetDistance);
}

PhysicsVector3D Physics::GetCurvePoint3D(const CurvedPath3D& path, float t) const
{
   PHYSICS_RECORD_FUNCTION();
   
   // Clamp t to valid range [0, 1]
   t = std::clamp(t, 0.0f, 1.0f);
   
   // Handle edge cases
   if (path.coordinates.empty())
   {
       return PhysicsVector3D();
   }
   
   if (path.coordinates.size() == 1)
   {
       return path.coordinates[0];
   }
   
   // Calculate distance along path
   float targetDistance = t * path.totalLength;
   return path.GetPointAtDistance(targetDistance);
}

PhysicsVector2D Physics::GetCurveTangent2D(const CurvedPath2D& path, float t) const
{
   PHYSICS_RECORD_FUNCTION();
   
   // Clamp t to valid range [0, 1]
   t = std::clamp(t, 0.0f, 1.0f);
   
   // Handle edge cases
   if (path.tangents.empty())
   {
       return PhysicsVector2D(1.0f, 0.0f); // Default to right direction
   }
   
   // Calculate distance along path
   float targetDistance = t * path.totalLength;
   return path.GetTangentAtDistance(targetDistance);
}

PhysicsVector3D Physics::GetCurveTangent3D(const CurvedPath3D& path, float t) const
{
   PHYSICS_RECORD_FUNCTION();
   
   // Clamp t to valid range [0, 1]
   t = std::clamp(t, 0.0f, 1.0f);
   
   // Handle edge cases
   if (path.tangents.empty())
   {
       return PhysicsVector3D(1.0f, 0.0f, 0.0f); // Default to forward direction
   }
   
   // Calculate distance along path
   float targetDistance = t * path.totalLength;
   return path.GetTangentAtDistance(targetDistance);
}

float Physics::CalculateCurveLength2D(const CurvedPath2D& path, float startT, float endT) const
{
   PHYSICS_RECORD_FUNCTION();
   
   // Ensure valid t range
   startT = std::clamp(startT, 0.0f, 1.0f);
   endT = std::clamp(endT, 0.0f, 1.0f);
   
   if (startT > endT)
   {
       std::swap(startT, endT);
   }
   
   // Calculate distances along path
   float startDistance = startT * path.totalLength;
   float endDistance = endT * path.totalLength;
   
   return endDistance - startDistance;
}

float Physics::CalculateCurveLength3D(const CurvedPath3D& path, float startT, float endT) const
{
   PHYSICS_RECORD_FUNCTION();
   
   // Ensure valid t range
   startT = std::clamp(startT, 0.0f, 1.0f);
   endT = std::clamp(endT, 0.0f, 1.0f);
   
   if (startT > endT)
   {
       std::swap(startT, endT);
   }
   
   // Calculate distances along path
   float startDistance = startT * path.totalLength;
   float endDistance = endT * path.totalLength;
   
   return endDistance - startDistance;
}

//==============================================================================
// Reflection Path Calculation Methods
//==============================================================================
ReflectionData Physics::CalculateReflection(const PhysicsVector3D& incomingVelocity, const PhysicsVector3D& surfaceNormal, 
                                          float restitution, float friction)
{
   PHYSICS_RECORD_FUNCTION();
   
   ReflectionData reflectionData;
   reflectionData.incomingVelocity = incomingVelocity;
   reflectionData.surfaceNormal = surfaceNormal.Normalized();
   reflectionData.restitution = std::clamp(restitution, 0.0f, 1.0f);
   reflectionData.friction = std::clamp(friction, 0.0f, 1.0f);
   
   try
   {
       // Calculate velocity components relative to surface normal
       float normalVelocity = FastDotProduct(incomingVelocity, reflectionData.surfaceNormal);
       PhysicsVector3D normalComponent = reflectionData.surfaceNormal * normalVelocity;
       PhysicsVector3D tangentialComponent = incomingVelocity - normalComponent;
       
       // Apply reflection to normal component with restitution
       PhysicsVector3D reflectedNormal = normalComponent * (-reflectionData.restitution);
       
       // Apply friction to tangential component
       PhysicsVector3D reflectedTangential = tangentialComponent * (1.0f - reflectionData.friction);
       
       // Combine components for final reflected velocity
       reflectionData.reflectedVelocity = reflectedNormal + reflectedTangential;
       
       // Calculate energy loss
       float incomingEnergy = incomingVelocity.MagnitudeSquared();
       float reflectedEnergy = reflectionData.reflectedVelocity.MagnitudeSquared();
       reflectionData.energyLoss = (incomingEnergy - reflectedEnergy) / incomingEnergy;
       
#if defined(_DEBUG_PHYSICS_)
       debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[Physics] Calculated reflection - Energy loss: %.3f, Restitution: %.3f", 
                            reflectionData.energyLoss, reflectionData.restitution);
#endif
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error calculating reflection: " + wErrorMsg);
       
       // Return safe default values
       reflectionData.reflectedVelocity = PhysicsVector3D();
       reflectionData.energyLoss = 1.0f;
   }
   
   return reflectionData;
}

PhysicsVector2D Physics::CalculateReflection2D(const PhysicsVector2D& incomingVelocity, const PhysicsVector2D& surfaceNormal, 
                                             float restitution)
{
   PHYSICS_RECORD_FUNCTION();
   
   try
   {
       // Normalize surface normal
       PhysicsVector2D normal = surfaceNormal.Normalized();
       
       // Calculate reflection using formula: R = V - 2(V·N)N
       float dotProduct = incomingVelocity.Dot(normal);
       PhysicsVector2D reflection = incomingVelocity - normal * (2.0f * dotProduct);
       
       // Apply restitution coefficient
       reflection *= std::clamp(restitution, 0.0f, 1.0f);
       
#if defined(_DEBUG_PHYSICS_)
       debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[Physics] Calculated 2D reflection with restitution: %.3f", restitution);
#endif
       
       return reflection;
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error calculating 2D reflection: " + wErrorMsg);
       
       return PhysicsVector2D();
   }
}

std::vector<PhysicsVector3D> Physics::CalculateMultipleBounces(const PhysicsVector3D& startPosition, 
                                                             const PhysicsVector3D& initialVelocity, 
                                                             const std::vector<PhysicsVector3D>& surfaceNormals, 
                                                             int maxBounces)
{
   PHYSICS_RECORD_FUNCTION();
   
   std::vector<PhysicsVector3D> bouncePath;
   bouncePath.reserve(maxBounces + 1);
   
   try
   {
       PhysicsVector3D currentPosition = startPosition;
       PhysicsVector3D currentVelocity = initialVelocity;
       
       // Add starting position
       bouncePath.push_back(currentPosition);
       
       // Calculate each bounce
       for (int bounce = 0; bounce < maxBounces && bounce < static_cast<int>(surfaceNormals.size()); ++bounce)
       {
           // Simulate movement until collision (simplified)
           currentPosition += currentVelocity * 0.1f; // Simplified time step
           
           // Calculate reflection
           ReflectionData reflection = CalculateReflection(currentVelocity, surfaceNormals[bounce]);
           currentVelocity = reflection.reflectedVelocity;
           
           // Add bounce position
           bouncePath.push_back(currentPosition);
           
           // Stop if velocity becomes too small
           if (currentVelocity.Magnitude() < MIN_VELOCITY_THRESHOLD)
           {
               break;
           }
       }
       
#if defined(_DEBUG_PHYSICS_)
       debug.logDebugMessage(LogLevel::LOG_INFO, L"[Physics] Calculated %zu bounce positions with %d max bounces", 
                            bouncePath.size(), maxBounces);
#endif
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error calculating multiple bounces: " + wErrorMsg);
   }
   
   return bouncePath;
}

//==============================================================================
// Internal Helper Methods Implementation
//==============================================================================
PhysicsVector2D Physics::CatmullRomInterpolation2D(const PhysicsVector2D& p0, const PhysicsVector2D& p1, 
                                                  const PhysicsVector2D& p2, const PhysicsVector2D& p3, float t) const
{
   // Catmull-Rom spline interpolation formula
   float t2 = t * t;
   float t3 = t2 * t;
   
   // Calculate interpolation coefficients
   float c0 = -0.5f * t3 + t2 - 0.5f * t;
   float c1 = 1.5f * t3 - 2.5f * t2 + 1.0f;
   float c2 = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
   float c3 = 0.5f * t3 - 0.5f * t2;
   
   // Apply coefficients to control points
   return p0 * c0 + p1 * c1 + p2 * c2 + p3 * c3;
}

PhysicsVector3D Physics::CatmullRomInterpolation3D(const PhysicsVector3D& p0, const PhysicsVector3D& p1, 
                                                  const PhysicsVector3D& p2, const PhysicsVector3D& p3, float t) const
{
   // Catmull-Rom spline interpolation formula
   float t2 = t * t;
   float t3 = t2 * t;
   
   // Calculate interpolation coefficients
   float c0 = -0.5f * t3 + t2 - 0.5f * t;
   float c1 = 1.5f * t3 - 2.5f * t2 + 1.0f;
   float c2 = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
   float c3 = 0.5f * t3 - 0.5f * t2;
   
   // Apply coefficients to control points
   return p0 * c0 + p1 * c1 + p2 * c2 + p3 * c3;
}

void Physics::VerletIntegration(PhysicsBody& body, float deltaTime)
{
    PHYSICS_RECORD_FUNCTION();

    try
    {
        // Verlet integration for better stability
        // x(t+dt) = 2*x(t) - x(t-dt) + a(t)*dt^2

        // Only integrate for dynamic bodies
        if (!body.isStatic && body.isActive)
        {
            // Store previous position for next frame
            static std::unordered_map<PhysicsBody*, PhysicsVector3D> previousPositions;

            PhysicsVector3D currentPosition = body.position;
            PhysicsVector3D acceleration = body.acceleration;

            // Get previous position or initialize with current position for first frame
            auto it = previousPositions.find(&body);
            PhysicsVector3D previousPosition = (it != previousPositions.end()) ? it->second : currentPosition;

            // Calculate new position using Verlet integration formula
            PhysicsVector3D newPosition = currentPosition * 2.0f - previousPosition + acceleration * (deltaTime * deltaTime);

            // Calculate velocity using CURRENT and PREVIOUS positions with proper vector scalar division
            // This is the correct Verlet velocity calculation for PhysicsVector3D
            if (deltaTime > MIN_VELOCITY_THRESHOLD)
            {
                float invDeltaTime = 1.0f / deltaTime;
                body.velocity = (currentPosition - previousPosition) * invDeltaTime;
            }
            else
            {
                // Prevent division by zero for very small time steps
                body.velocity = PhysicsVector3D(0.0f, 0.0f, 0.0f);
            }

            // Apply drag to velocity
            float dragFactor = 1.0f - (body.drag * deltaTime);
            dragFactor = std::max(0.0f, dragFactor);
            body.velocity *= dragFactor;

            // Store current position as previous for next frame
            previousPositions[&body] = currentPosition;

            // Update body position to new calculated position
            body.position = newPosition;

            // Reset acceleration for next frame
            body.acceleration = PhysicsVector3D();
        }
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error in Verlet integration: " + wErrorMsg);
    }
}

float Physics::FastMagnitude(const PhysicsVector3D& vector) const
{
   // Use MathPrecalculation for fast square root
   return FAST_SQRT(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
}

PhysicsVector3D Physics::FastNormalize(const PhysicsVector3D& vector) const
{
   // Calculate magnitude using fast math
   float magnitude = FastMagnitude(vector);
   
   // Handle zero-length vector
if (magnitude < MIN_VELOCITY_THRESHOLD)
   {
       return PhysicsVector3D(0.0f, 0.0f, 0.0f);
   }
   
   // Normalize using fast inverse
   float invMagnitude = 1.0f / magnitude;
   return PhysicsVector3D(vector.x * invMagnitude, vector.y * invMagnitude, vector.z * invMagnitude);
}

float Physics::FastDotProduct(const PhysicsVector3D& a, const PhysicsVector3D& b) const
{
   // Standard dot product calculation
   return a.x * b.x + a.y * b.y + a.z * b.z;
}

PhysicsVector3D Physics::FastCrossProduct(const PhysicsVector3D& a, const PhysicsVector3D& b) const
{
   // Standard cross product calculation
   return PhysicsVector3D(
       a.y * b.z - a.z * b.y,
       a.z * b.x - a.x * b.z,
       a.x * b.y - a.y * b.x
   );
}

float Physics::FastDistance(const PhysicsVector3D& a, const PhysicsVector3D& b) const
{
   // Calculate distance using fast magnitude
   PhysicsVector3D diff = b - a;
   return FastMagnitude(diff);
}

//==============================================================================
// Gravity Field Methods Implementation
//==============================================================================
void Physics::AddGravityField(const GravityField& gravityField)
{
   PHYSICS_RECORD_FUNCTION();
   
   try
   {
       std::lock_guard<std::mutex> lock(m_physicsMutex);
       m_gravityFields.push_back(gravityField);
       
#if defined(_DEBUG_PHYSICS_)
       debug.logDebugMessage(LogLevel::LOG_INFO, L"[Physics] Added gravity field at position (%.2f, %.2f, %.2f) with mass %.2f", 
                            gravityField.center.x, gravityField.center.y, gravityField.center.z, gravityField.mass);
#endif
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error adding gravity field: " + wErrorMsg);
   }
}

void Physics::RemoveGravityField(int index)
{
   PHYSICS_RECORD_FUNCTION();
   
   try
   {
       std::lock_guard<std::mutex> lock(m_physicsMutex);
       
       if (index >= 0 && index < static_cast<int>(m_gravityFields.size()))
       {
           m_gravityFields.erase(m_gravityFields.begin() + index);
           
#if defined(_DEBUG_PHYSICS_)
           debug.logDebugMessage(LogLevel::LOG_INFO, L"[Physics] Removed gravity field at index %d", index);
#endif
       }
       else
       {
#if defined(_DEBUG_PHYSICS_)
           debug.logDebugMessage(LogLevel::LOG_WARNING, L"[Physics] Invalid gravity field index: %d", index);
#endif
       }
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error removing gravity field: " + wErrorMsg);
   }
}

void Physics::ClearGravityFields()
{
   PHYSICS_RECORD_FUNCTION();
   
   std::lock_guard<std::mutex> lock(m_physicsMutex);
   m_gravityFields.clear();
   
#if defined(_DEBUG_PHYSICS_)
   debug.logLevelMessage(LogLevel::LOG_INFO, L"[Physics] Cleared all gravity fields");
#endif
}

PhysicsVector3D Physics::CalculateGravityAtPosition(const PhysicsVector3D& position) const
{
   PHYSICS_RECORD_FUNCTION();
   
   PhysicsVector3D totalGravity(0.0f, -DEFAULT_GRAVITY, 0.0f); // Default downward gravity
   
   try
   {
       // Add gravitational forces from all gravity fields
       for (const auto& gravityField : m_gravityFields)
       {
           PhysicsVector3D fieldGravity = gravityField.CalculateGravityVector(position);
           totalGravity += fieldGravity;
       }
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error calculating gravity at position: " + wErrorMsg);
   }
   
   return totalGravity;
}

float Physics::CalculateOrbitalVelocity(const PhysicsVector3D& position, const GravityField& gravityField) const
{
   PHYSICS_RECORD_FUNCTION();
   
   try
   {
       // Calculate distance from gravity center
       float distance = FastDistance(position, gravityField.center);
       
       // Prevent division by zero
       if (distance < 0.1f)
       {
           return 0.0f;
       }
       
       // Orbital velocity formula: v = sqrt(GM/r)
       // Simplified for gaming: v = sqrt(intensity * mass / distance)
       float orbitalVelocity = FAST_SQRT((gravityField.intensity * gravityField.mass) / distance);
       
#if defined(_DEBUG_PHYSICS_)
       debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[Physics] Calculated orbital velocity: %.3f at distance %.2f", 
                            orbitalVelocity, distance);
#endif
       
       return orbitalVelocity;
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error calculating orbital velocity: " + wErrorMsg);
       return 0.0f;
   }
}

float Physics::CalculateEscapeVelocity(const PhysicsVector3D& position, const GravityField& gravityField) const
{
   PHYSICS_RECORD_FUNCTION();
   
   try
   {
       // Calculate distance from gravity center
       float distance = FastDistance(position, gravityField.center);
       
       // Prevent division by zero
       if (distance < 0.1f)
       {
           return 0.0f;
       }
       
       // Escape velocity formula: v = sqrt(2*GM/r)
       // Simplified for gaming: v = sqrt(2 * intensity * mass / distance)
       float escapeVelocity = FAST_SQRT((2.0f * gravityField.intensity * gravityField.mass) / distance);
       
#if defined(_DEBUG_PHYSICS_)
       debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[Physics] Calculated escape velocity: %.3f at distance %.2f", 
                            escapeVelocity, distance);
#endif
       
       return escapeVelocity;
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error calculating escape velocity: " + wErrorMsg);
       return 0.0f;
   }
}

//==============================================================================
// Bouncing and Trajectory Methods Implementation
//==============================================================================
std::vector<PhysicsVector3D> Physics::CalculateBouncingTrajectory(const PhysicsVector3D& startPosition, 
                                                                 const PhysicsVector3D& initialVelocity, 
                                                                 float groundHeight, float restitution, 
                                                                 float drag, int maxBounces)
{
   PHYSICS_RECORD_FUNCTION();
   
   std::vector<PhysicsVector3D> trajectory;
   trajectory.reserve(maxBounces * 10); // Reserve space for trajectory points
   
   try
   {
       PhysicsVector3D currentPosition = startPosition;
       PhysicsVector3D currentVelocity = initialVelocity;
       
       float timeStep = 0.016f; // 60 FPS time step
       float currentTime = 0.0f;
       int bounceCount = 0;
       
       // Add starting position
       trajectory.push_back(currentPosition);
       
       // Simulate trajectory with bouncing
       while (bounceCount < maxBounces && currentTime < 30.0f) // Maximum simulation time
       {
           // Apply gravity
           PhysicsVector3D gravity = CalculateGravityAtPosition(currentPosition);
           currentVelocity += gravity * timeStep;
           
           // Apply drag
           float dragFactor = 1.0f - (drag * timeStep);
           dragFactor = std::max(0.0f, dragFactor);
           currentVelocity *= dragFactor;
           
           // Update position
           PhysicsVector3D nextPosition = currentPosition + currentVelocity * timeStep;
           
           // Check for ground collision
           if (nextPosition.y <= groundHeight && currentVelocity.y < 0.0f)
           {
               // Calculate exact collision point
               float collisionTime = (groundHeight - currentPosition.y) / currentVelocity.y;
               PhysicsVector3D collisionPoint = currentPosition + currentVelocity * collisionTime;
               collisionPoint.y = groundHeight;
               
               // Add collision point to trajectory
               trajectory.push_back(collisionPoint);
               
               // Calculate reflection
               PhysicsVector3D surfaceNormal(0.0f, 1.0f, 0.0f); // Ground normal points up
               ReflectionData reflection = CalculateReflection(currentVelocity, surfaceNormal, restitution, 0.1f);
               currentVelocity = reflection.reflectedVelocity;
               currentPosition = collisionPoint;
               
               bounceCount++;
               
               // Stop if velocity becomes too small
               if (currentVelocity.Magnitude() < MIN_VELOCITY_THRESHOLD)
               {
                   break;
               }
           }
           else
           {
               currentPosition = nextPosition;
               trajectory.push_back(currentPosition);
           }
           
           currentTime += timeStep;
       }
       
#if defined(_DEBUG_PHYSICS_)
       debug.logDebugMessage(LogLevel::LOG_INFO, L"[Physics] Calculated bouncing trajectory with %zu points and %d bounces", 
                            trajectory.size(), bounceCount);
#endif
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error calculating bouncing trajectory: " + wErrorMsg);
   }
   
   return trajectory;
}

PhysicsVector3D Physics::CalculateRestingPosition(const PhysicsVector3D& startPosition, 
                                                 const PhysicsVector3D& initialVelocity, 
                                                 float groundHeight, float restitution, float drag)
{
   PHYSICS_RECORD_FUNCTION();
   
   try
   {
       // Calculate final trajectory
       std::vector<PhysicsVector3D> trajectory = CalculateBouncingTrajectory(startPosition, initialVelocity, 
                                                                             groundHeight, restitution, drag, 20);
       
       // Return last position in trajectory as resting position
       if (!trajectory.empty())
       {
           PhysicsVector3D restingPosition = trajectory.back();
           restingPosition.y = groundHeight; // Ensure it's on the ground
           
#if defined(_DEBUG_PHYSICS_)
           debug.logDebugMessage(LogLevel::LOG_INFO, L"[Physics] Calculated resting position: (%.2f, %.2f, %.2f)", 
                                restingPosition.x, restingPosition.y, restingPosition.z);
#endif
           
           return restingPosition;
       }
       else
       {
           // Fallback to start position on ground
           PhysicsVector3D fallbackPosition = startPosition;
           fallbackPosition.y = groundHeight;
           return fallbackPosition;
       }
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error calculating resting position: " + wErrorMsg);
       
       // Return safe fallback position
       PhysicsVector3D fallbackPosition = startPosition;
       fallbackPosition.y = groundHeight;
       return fallbackPosition;
   }
}

//==============================================================================
// Collision Detection Methods Implementation
//==============================================================================
bool Physics::CheckAABBCollision(const PhysicsVector3D& minA, const PhysicsVector3D& maxA, 
                                const PhysicsVector3D& minB, const PhysicsVector3D& maxB) const
{
   PHYSICS_RECORD_FUNCTION();
   
   // AABB collision detection using separating axis theorem
   return (minA.x <= maxB.x && maxA.x >= minB.x &&
           minA.y <= maxB.y && maxA.y >= minB.y &&
           minA.z <= maxB.z && maxA.z >= minB.z);
}

bool Physics::CheckSphereCollision(const PhysicsVector3D& centerA, float radiusA, 
                                  const PhysicsVector3D& centerB, float radiusB) const
{
   PHYSICS_RECORD_FUNCTION();
   
   // Sphere collision detection using distance comparison
   float distance = FastDistance(centerA, centerB);
   float combinedRadius = radiusA + radiusB;
   
   return distance <= combinedRadius;
}

bool Physics::RaySphereIntersection(const PhysicsVector3D& rayOrigin, const PhysicsVector3D& rayDirection, 
                                   const PhysicsVector3D& sphereCenter, float sphereRadius, float& hitDistance) const
{
   PHYSICS_RECORD_FUNCTION();
   
   try
   {
       // Ray-sphere intersection using quadratic formula
       PhysicsVector3D toSphere = rayOrigin - sphereCenter;
       
       float a = FastDotProduct(rayDirection, rayDirection);
       float b = 2.0f * FastDotProduct(toSphere, rayDirection);
       float c = FastDotProduct(toSphere, toSphere) - (sphereRadius * sphereRadius);
       
       float discriminant = b * b - 4.0f * a * c;
       
       if (discriminant < 0.0f)
       {
           return false; // No intersection
       }
       
       // Calculate hit distance
       float sqrtDiscriminant = FAST_SQRT(discriminant);
       float t1 = (-b - sqrtDiscriminant) / (2.0f * a);
       float t2 = (-b + sqrtDiscriminant) / (2.0f * a);
       
       // Return closest positive intersection
       if (t1 > 0.0f)
       {
           hitDistance = t1;
           return true;
       }
       else if (t2 > 0.0f)
       {
           hitDistance = t2;
           return true;
       }
       
       return false; // Intersection behind ray origin
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error in ray-sphere intersection: " + wErrorMsg);
       return false;
   }
}

bool Physics::ContinuousCollisionDetection(const PhysicsBody& bodyA, const PhysicsBody& bodyB, float deltaTime) const
{
   PHYSICS_RECORD_FUNCTION();
   
   try
   {
       // Simple CCD using swept sphere test
       PhysicsVector3D relativeVelocity = bodyA.velocity - bodyB.velocity;
       PhysicsVector3D relativePosition = bodyA.position - bodyB.position;
       
       // Assume spherical bodies with radius based on mass (simplified)
       float radiusA = FAST_SQRT(bodyA.mass) * 0.5f;
       float radiusB = FAST_SQRT(bodyB.mass) * 0.5f;
       float combinedRadius = radiusA + radiusB;
       
       // Check if objects are moving toward each other
       if (FastDotProduct(relativeVelocity, relativePosition) >= 0.0f)
       {
           return false; // Moving away from each other
       }
       
       // Calculate time to collision
       float relativeSpeed = relativeVelocity.Magnitude();
       float currentDistance = relativePosition.Magnitude();
       
       if (relativeSpeed > MIN_VELOCITY_THRESHOLD)
       {
           float timeToCollision = (currentDistance - combinedRadius) / relativeSpeed;
           return timeToCollision <= deltaTime && timeToCollision >= 0.0f;
       }
       
       return false;
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error in continuous collision detection: " + wErrorMsg);
       return false;
   }
}

//==============================================================================
// Audio Physics Methods Implementation
//==============================================================================
AudioPhysicsData Physics::CalculateAudioPhysics(const PhysicsVector3D& listenerPosition, 
                                               const PhysicsVector3D& sourcePosition, 
                                               const PhysicsVector3D& sourceVelocity)
{
   PHYSICS_RECORD_FUNCTION();
   
   AudioPhysicsData audioData;
   audioData.listenerPosition = listenerPosition;
   audioData.sourcePosition = sourcePosition;
   audioData.sourceVelocity = sourceVelocity;
   
   try
   {
       // Calculate audio properties
       audioData.CalculateAudioProperties();
       
#if defined(_DEBUG_PHYSICS_)
       debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[Physics] Calculated audio physics - Distance: %.2f, Doppler: %.3f", 
                            audioData.distance, audioData.dopplerShift);
#endif
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error calculating audio physics: " + wErrorMsg);
   }
   
   return audioData;
}

float Physics::CalculateDopplerShift(const PhysicsVector3D& sourceVelocity, const PhysicsVector3D& listenerVelocity, 
                                    const PhysicsVector3D& direction, float speedOfSound) const
{
   PHYSICS_RECORD_FUNCTION();
   
   try
   {
       // Calculate relative velocities along the line of sight
       float sourceVelAlongLOS = FastDotProduct(sourceVelocity, direction);
       float listenerVelAlongLOS = FastDotProduct(listenerVelocity, direction);
       
       // Doppler shift formula: f' = f * (v + vr) / (v + vs)
       // where v = speed of sound, vr = receiver velocity, vs = source velocity
       float dopplerFactor = (speedOfSound + listenerVelAlongLOS) / (speedOfSound + sourceVelAlongLOS);
       
       // Clamp to reasonable range
       return std::clamp(dopplerFactor, 0.5f, 2.0f);
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error calculating Doppler shift: " + wErrorMsg);
       return 1.0f; // No shift on error
   }
}

float Physics::CalculateSoundOcclusion(const PhysicsVector3D& sourcePosition, const PhysicsVector3D& listenerPosition, 
                                      const std::vector<PhysicsVector3D>& obstacles) const
{
   PHYSICS_RECORD_FUNCTION();
   
   try
   {
       // Calculate occlusion based on obstacles between source and listener
       PhysicsVector3D direction = listenerPosition - sourcePosition;
       float distance = direction.Magnitude();
       
       if (distance < MIN_VELOCITY_THRESHOLD)
       {
           return 0.0f; // No occlusion if source and listener are at same position
       }
       
       direction = direction.Normalized();
       float occlusion = 0.0f;
       
       // Check intersection with each obstacle (simplified as spheres)
       for (const auto& obstacle : obstacles)
       {
           float hitDistance;
           if (RaySphereIntersection(sourcePosition, direction, obstacle, 1.0f, hitDistance))
           {
               if (hitDistance < distance)
               {
                   occlusion += 0.3f; // Each obstacle adds 30% occlusion
               }
           }
       }
       
       // Clamp occlusion to maximum of 90%
       return std::clamp(occlusion, 0.0f, 0.9f);
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error calculating sound occlusion: " + wErrorMsg);
       return 0.0f;
   }
}

float Physics::CalculateReverb(const PhysicsVector3D& position, float roomSize, float absorptionCoefficient) const
{
   PHYSICS_RECORD_FUNCTION();
   
   try
   {
       // Simple reverb calculation based on room size and absorption
       float baseReverb = roomSize * 0.01f; // Larger rooms have more reverb
       float absorption = std::clamp(absorptionCoefficient, 0.0f, 1.0f);
       
       // Apply absorption coefficient
       float reverb = baseReverb * (1.0f - absorption);
       
       // Clamp to reasonable range
       return std::clamp(reverb, 0.0f, 1.0f);
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error calculating reverb: " + wErrorMsg);
       return 0.0f;
   }
}

//==============================================================================
// Particle System Methods Implementation
//==============================================================================
std::vector<PhysicsParticle> Physics::CreateExplosion(const PhysicsVector3D& center, int particleCount, 
                                                     float explosionForce, float particleLifetime)
{
   PHYSICS_RECORD_FUNCTION();
   
   std::vector<PhysicsParticle> particles;
   particles.reserve(particleCount);
   
   try
   {
       // Clamp particle count to reasonable limits
       particleCount = std::clamp(particleCount, 1, MAX_PARTICLE_COUNT);
       
       for (int i = 0; i < particleCount; ++i)
       {
           PhysicsParticle particle;
           
           // Set particle position at explosion center with small random offset
           particle.position = center + PhysicsVector3D(
               (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 0.2f,
               (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 0.2f,
               (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 0.2f
           );
           
           // Calculate random direction for explosion
           float theta = static_cast<float>(rand()) / RAND_MAX * 2.0f * XM_PI; // Azimuth angle
           float phi = static_cast<float>(rand()) / RAND_MAX * XM_PI; // Elevation angle
           
           PhysicsVector3D direction(
               FAST_SIN(phi) * FAST_COS(theta),
               FAST_COS(phi),
               FAST_SIN(phi) * FAST_SIN(theta)
           );
           
           // Set particle velocity based on explosion force and direction
           float speed = explosionForce * (0.5f + static_cast<float>(rand()) / RAND_MAX * 0.5f); // Random speed variation
           particle.velocity = direction * speed;
           
           // Set particle properties
           particle.life = particleLifetime * (0.8f + static_cast<float>(rand()) / RAND_MAX * 0.4f); // Random lifetime variation
           particle.mass = 0.1f + static_cast<float>(rand()) / RAND_MAX * 0.1f; // Random mass variation
           particle.drag = DEFAULT_AIR_RESISTANCE;
           particle.isActive = true;
           
           particles.push_back(particle);
       }
       
       // Update particle count
       m_particleCount.store(static_cast<int>(particles.size()));
       
#if defined(_DEBUG_PHYSICS_)
       debug.logDebugMessage(LogLevel::LOG_INFO, L"[Physics] Created explosion with %d particles at position (%.2f, %.2f, %.2f)", 
                            particleCount, center.x, center.y, center.z);
#endif
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error creating explosion: " + wErrorMsg);
   }
   
   return particles;
}

void Physics::UpdateParticleSystem(std::vector<PhysicsParticle>& particles, float deltaTime)
{
   PHYSICS_RECORD_FUNCTION();
   
   try
   {
       int activeParticles = 0;
       
       for (auto& particle : particles)
       {
           if (particle.isActive)
           {
               particle.Update(deltaTime);
               if (particle.isActive)
               {
                   activeParticles++;
               }
           }
       }
       
       // Update particle count
       m_particleCount.store(activeParticles);
       
#if defined(_DEBUG_PHYSICS_)
       static int debugCounter = 0;
       if (++debugCounter % 60 == 0) // Log every 60 frames to avoid spam
       {
           debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[Physics] Updated particle system - %d active particles", activeParticles);
       }
#endif
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error updating particle system: " + wErrorMsg);
   }
}

void Physics::ApplyWindForce(std::vector<PhysicsParticle>& particles, const PhysicsVector3D& windVelocity)
{
   PHYSICS_RECORD_FUNCTION();
   
   try
   {
       for (auto& particle : particles)
       {
           if (particle.isActive)
           {
               // Apply wind force based on particle mass and drag
               PhysicsVector3D windForce = windVelocity * (particle.drag * particle.mass);
               particle.acceleration += windForce;
           }
       }
       
#if defined(_DEBUG_PHYSICS_)
       debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[Physics] Applied wind force (%.2f, %.2f, %.2f) to particle system", 
                            windVelocity.x, windVelocity.y, windVelocity.z);
#endif
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error applying wind force: " + wErrorMsg);
   }
}

void Physics::ApplyGravityToParticles(std::vector<PhysicsParticle>& particles, float gravity)
{
   PHYSICS_RECORD_FUNCTION();
   
   try
   {
       PhysicsVector3D gravityForce(0.0f, -gravity, 0.0f);
       
       for (auto& particle : particles)
       {
           if (particle.isActive)
           {
               // Apply gravity force based on particle mass
               particle.acceleration += gravityForce * particle.mass;
           }
       }
       
#if defined(_DEBUG_PHYSICS_)
       static int debugCounter = 0;
       if (++debugCounter % 120 == 0) // Log every 120 frames to avoid spam
       {
           debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[Physics] Applied gravity %.2f to particle system", gravity);
       }
#endif
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error applying gravity to particles: " + wErrorMsg);
   }
}

//==============================================================================
// Internal Helper Methods Implementation (continued)
//==============================================================================
void Physics::InitializePhysicsPrecalculations()
{
   PHYSICS_RECORD_FUNCTION();
   
   try
   {
        #if defined(_DEBUG_PHYSICS_)
               debug.logLevelMessage(LogLevel::LOG_INFO, L"[Physics] Initializing physics-specific precalculations");
        #endif
       
       // Initialize gravity lookup tables
       UpdateGravityLookupTables();
       
       // Initialize reflection angle tables
       UpdateReflectionTables();
       
       // Initialize inertia coefficient tables
       UpdateInertiaCoefficients();
       
        #if defined(_DEBUG_PHYSICS_)
               debug.logLevelMessage(LogLevel::LOG_INFO, L"[Physics] Physics precalculations initialized successfully");
        #endif
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error initializing physics precalculations: " + wErrorMsg);
       throw;
   }
}

void Physics::UpdateGravityLookupTables()
{
    // This method would populate gravity intensity lookup tables
    // for fast distance-based gravity calculations
    PHYSICS_RECORD_FUNCTION();

    try
    {
        // Note: These lookup tables would be integrated into MathPrecalculation class
        // for optimal performance. For now, we document the intention.

#if defined(_DEBUG_PHYSICS_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[Physics] Updated gravity lookup tables");
#endif
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error updating gravity lookup tables: " + wErrorMsg);
    }
}

void Physics::UpdateReflectionTables()
{
    // This method would populate reflection angle lookup tables
    // for fast collision response calculations
    PHYSICS_RECORD_FUNCTION();

    try
    {
        // Note: These lookup tables would be integrated into MathPrecalculation class
        // for optimal performance. For now, we document the intention.

#if defined(_DEBUG_PHYSICS_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[Physics] Updated reflection lookup tables");
#endif
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error updating reflection tables: " + wErrorMsg);
    }
}

void Physics::UpdateInertiaCoefficients()
{
    // This method would populate inertia coefficient lookup tables
    // for fast rotational physics calculations
    PHYSICS_RECORD_FUNCTION();

    try
    {
        // Note: These lookup tables would be integrated into MathPrecalculation class
        // for optimal performance. For now, we document the intention.

#if defined(_DEBUG_PHYSICS_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[Physics] Updated inertia coefficient tables");
#endif
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error updating inertia coefficients: " + wErrorMsg);
    }
}

void Physics::AllocatePhysicsMemory()
{
    PHYSICS_RECORD_FUNCTION();

    try
    {
        // Memory pools would be allocated here for optimal performance
        // This is a placeholder for future memory management optimization

#if defined(_DEBUG_PHYSICS_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[Physics] Allocated physics memory pools");
#endif
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error allocating physics memory: " + wErrorMsg);
        throw;
    }
}

void Physics::DeallocatePhysicsMemory()
{
    PHYSICS_RECORD_FUNCTION();

    try
    {
        // Memory pools would be deallocated here
        // This is a placeholder for future memory management optimization

#if defined(_DEBUG_PHYSICS_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[Physics] Deallocated physics memory pools");
#endif
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error deallocating physics memory: " + wErrorMsg);
    }
}

size_t Physics::GetPhysicsMemoryUsage() const
{
    PHYSICS_RECORD_FUNCTION();

    size_t totalMemory = 0;

    try
    {
        // Calculate memory usage for physics collections
        totalMemory += m_physicsBodies.size() * sizeof(PhysicsBody);
        totalMemory += m_gravityFields.size() * sizeof(GravityField);
        totalMemory += m_ragdollJoints.size() * sizeof(RagdollJoint);
        totalMemory += m_collisionManifolds.size() * sizeof(CollisionManifold);
        totalMemory += m_debugLines.size() * sizeof(PhysicsVector3D);

        // Add estimated memory for internal structures
        totalMemory += sizeof(Physics);

        return totalMemory;
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error calculating memory usage: " + wErrorMsg);
        return 0;
    }
}

void Physics::BroadPhaseCollisionDetection()
{
    PHYSICS_RECORD_FUNCTION();

    try
    {
        // Simple broad-phase collision detection using spatial hashing
        // This is a simplified implementation for demonstration

        // Clear previous manifolds
        m_collisionManifolds.clear();

        // Check all pairs of active bodies (O(n²) - would be optimized with spatial partitioning)
        for (size_t i = 0; i < m_physicsBodies.size(); ++i)
        {
            for (size_t j = i + 1; j < m_physicsBodies.size(); ++j)
            {
                PhysicsBody& bodyA = m_physicsBodies[i];
                PhysicsBody& bodyB = m_physicsBodies[j];

                // Skip inactive or static-static pairs
                if (!bodyA.isActive || !bodyB.isActive || (bodyA.isStatic && bodyB.isStatic))
                {
                    continue;
                }

                // Simple distance check for broad phase
                float distance = FastDistance(bodyA.position, bodyB.position);
                float combinedRadius = FAST_SQRT(bodyA.mass) + FAST_SQRT(bodyB.mass); // Approximate radius from mass

                if (distance <= combinedRadius * 1.5f) // Broad phase threshold
                {
                    // Potential collision - add to narrow phase
                    CollisionManifold manifold;
                    manifold.bodyA = &bodyA;
                    manifold.bodyB = &bodyB;
                    m_collisionManifolds.push_back(manifold);
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error in broad phase collision detection: " + wErrorMsg);
    }
}

void Physics::NarrowPhaseCollisionDetection()
{
    PHYSICS_RECORD_FUNCTION();

    try
    {
        // Narrow phase collision detection for potential collisions
        for (auto& manifold : m_collisionManifolds)
        {
            if (!manifold.bodyA || !manifold.bodyB)
            {
                continue;
            }

            // Generate detailed collision information
            manifold = GenerateCollisionManifold(*manifold.bodyA, *manifold.bodyB);
        }

        // Remove manifolds with no valid contacts
        m_collisionManifolds.erase(
            std::remove_if(m_collisionManifolds.begin(), m_collisionManifolds.end(),
                [](const CollisionManifold& manifold) { return manifold.contacts.empty(); }),
            m_collisionManifolds.end()
        );

#if defined(_DEBUG_PHYSICS_)
        if (!m_collisionManifolds.empty())
        {
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[Physics] Narrow phase detected %zu valid collisions",
                m_collisionManifolds.size());
        }
#endif
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error in narrow phase collision detection: " + wErrorMsg);
    }
}

CollisionManifold Physics::GenerateCollisionManifold(PhysicsBody& bodyA, PhysicsBody& bodyB) const
{
    PHYSICS_RECORD_FUNCTION();

    CollisionManifold manifold;
    manifold.bodyA = &bodyA;
    manifold.bodyB = &bodyB;

    try
    {
        // Simplified sphere-sphere collision detection
        float radiusA = FAST_SQRT(bodyA.mass) * 0.5f;
        float radiusB = FAST_SQRT(bodyB.mass) * 0.5f;

        PhysicsVector3D direction = bodyB.position - bodyA.position;
        float distance = direction.Magnitude();
        float combinedRadius = radiusA + radiusB;

        if (distance < combinedRadius && distance > MIN_VELOCITY_THRESHOLD)
        {
            // Collision detected - generate contact point
            ContactPoint contact;
            contact.normal = direction.Normalized();
            contact.penetrationDepth = combinedRadius - distance;
            contact.position = bodyA.position + direction * 0.5f; // Midpoint
            contact.restitution = std::min(bodyA.restitution, bodyB.restitution);
            contact.friction = std::sqrt(bodyA.friction * bodyB.friction); // Combined friction

            manifold.AddContact(contact);
            manifold.normal = contact.normal;

            // Calculate separating velocity
            PhysicsVector3D relativeVelocity = bodyB.velocity - bodyA.velocity;
            manifold.separatingVelocity = FastDotProduct(relativeVelocity, manifold.normal);
        }
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error generating collision manifold: " + wErrorMsg);
    }

    return manifold;
}

void Physics::SolvePositionConstraints()
{
    PHYSICS_RECORD_FUNCTION();

    try
    {
        // Solve position constraints to prevent penetration
        const int maxIterations = 4; // Limit iterations for performance

        for (int iteration = 0; iteration < maxIterations; ++iteration)
        {
            for (auto& manifold : m_collisionManifolds)
            {
                for (const auto& contact : manifold.contacts)
                {
                    if (contact.penetrationDepth > MIN_VELOCITY_THRESHOLD)
                    {
                        // Calculate position correction
                        float totalInverseMass = manifold.bodyA->inverseMass + manifold.bodyB->inverseMass;
                        if (totalInverseMass > 0.0f)
                        {
                            float correctionMagnitude = contact.penetrationDepth / totalInverseMass * 0.8f; // Baumgarte stabilization
                            PhysicsVector3D correction = contact.normal * correctionMagnitude;

                            // Apply position corrections
                            manifold.bodyA->position -= correction * manifold.bodyA->inverseMass;
                            manifold.bodyB->position += correction * manifold.bodyB->inverseMass;
                        }
                    }
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error solving position constraints: " + wErrorMsg);
    }
}

void Physics::SolveVelocityConstraints()
{
    PHYSICS_RECORD_FUNCTION();

    try
    {
        // Solve velocity constraints for collision response
        for (auto& manifold : m_collisionManifolds)
        {
            manifold.ResolveCollision();
        }
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error solving velocity constraints: " + wErrorMsg);
    }
}

//==============================================================================
// Ragdoll Physics Methods Implementation
//==============================================================================
std::vector<PhysicsBody> Physics::CreateRagdoll(const std::vector<PhysicsVector3D>& jointPositions,
    const std::vector<std::pair<int, int>>& connections)
{
    PHYSICS_RECORD_FUNCTION();

    std::vector<PhysicsBody> ragdollBodies;

    try
    {
        // Validate input
        if (jointPositions.empty() || connections.empty())
        {
#if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[Physics] Invalid ragdoll parameters - empty positions or connections");
#endif
            return ragdollBodies;
        }

        // Create physics bodies for each joint
        ragdollBodies.reserve(jointPositions.size());

        for (size_t i = 0; i < jointPositions.size(); ++i)
        {
            PhysicsBody body;
            body.position = jointPositions[i];
            body.SetMass(1.0f); // Default mass
            body.restitution = 0.1f; // Low bounce for realistic ragdoll
            body.friction = 0.8f; // High friction for stability
            body.drag = 0.1f; // Some air resistance
            body.isStatic = false;
            body.isActive = true;

            ragdollBodies.push_back(body);
        }

        // Create joint constraints
        for (const auto& connection : connections)
        {
            if (connection.first < static_cast<int>(ragdollBodies.size()) &&
                connection.second < static_cast<int>(ragdollBodies.size()) &&
                connection.first != connection.second)
            {
                RagdollJoint joint;
                joint.bodyA = &ragdollBodies[connection.first];
                joint.bodyB = &ragdollBodies[connection.second];
                joint.anchorA = PhysicsVector3D(); // Local anchor points (simplified)
                joint.anchorB = PhysicsVector3D();
                joint.stiffness = 1000.0f;
                joint.damping = 50.0f;
                joint.isActive = true;

                AddRagdollJoint(joint);
            }
        }

#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[Physics] Created ragdoll with %zu bodies and %zu connections",
            ragdollBodies.size(), connections.size());
#endif
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error creating ragdoll: " + wErrorMsg);
    }

    return ragdollBodies;
}

void Physics::AddRagdollJoint(const RagdollJoint& joint)
{
    PHYSICS_RECORD_FUNCTION();

    try
    {
        std::lock_guard<std::mutex> lock(m_physicsMutex);

        if (m_ragdollJoints.size() < MAX_RAGDOLL_JOINTS)
        {
            m_ragdollJoints.push_back(joint);

#if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[Physics] Added ragdoll joint - Total joints: %zu", m_ragdollJoints.size());
#endif
        }
        else
        {
#if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[Physics] Maximum ragdoll joints reached, ignoring additional joint");
#endif
        }
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error adding ragdoll joint: " + wErrorMsg);
    }
}

void Physics::RemoveRagdollJoint(int index)
{
    PHYSICS_RECORD_FUNCTION();

    try
    {
        std::lock_guard<std::mutex> lock(m_physicsMutex);

        if (index >= 0 && index < static_cast<int>(m_ragdollJoints.size()))
        {
            m_ragdollJoints.erase(m_ragdollJoints.begin() + index);

#if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[Physics] Removed ragdoll joint at index %d", index);
#endif
        }
        else
        {
#if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"[Physics] Invalid ragdoll joint index: %d", index);
#endif
        }
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error removing ragdoll joint: " + wErrorMsg);
    }
}

void Physics::UpdateRagdoll(std::vector<PhysicsBody>& ragdollBodies, float deltaTime)
{
    PHYSICS_RECORD_FUNCTION();

    try
    {
        // Update each ragdoll body
        for (auto& body : ragdollBodies)
        {
            if (body.isActive)
            {
                // Apply gravity
                PhysicsVector3D gravity = CalculateGravityAtPosition(body.position);
                body.ApplyForce(gravity);

                // Integrate physics
                VerletIntegration(body, deltaTime);
            }
        }

        // Apply joint constraints
        for (auto& joint : m_ragdollJoints)
        {
            if (joint.isActive)
            {
                joint.ApplyConstraints();
            }
        }

#if defined(_DEBUG_PHYSICS_)
        static int debugCounter = 0;
        if (++debugCounter % 60 == 0) // Log every 60 frames
        {
            int activeBodies = 0;
            for (const auto& body : ragdollBodies)
            {
                if (body.isActive) activeBodies++;
            }
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[Physics] Updated ragdoll - %d active bodies", activeBodies);
        }
#endif
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error updating ragdoll: " + wErrorMsg);
    }
}

void Physics::BlendRagdollWithAnimation(std::vector<PhysicsBody>& ragdollBodies,
    const std::vector<PhysicsVector3D>& animationPositions, float blendFactor)
{
    PHYSICS_RECORD_FUNCTION();

    try
    {
        // Ensure arrays are same size
        size_t bodyCount = std::min(ragdollBodies.size(), animationPositions.size());
        blendFactor = std::clamp(blendFactor, 0.0f, 1.0f);

        for (size_t i = 0; i < bodyCount; ++i)
        {
            if (ragdollBodies[i].isActive)
            {
                // Blend physics position with animation position
                PhysicsVector3D blendedPosition = BlendPhysicsWithAnimation(
                    ragdollBodies[i].position,
                    animationPositions[i],
                    blendFactor
                );

                ragdollBodies[i].position = blendedPosition;
            }
        }

#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[Physics] Blended ragdoll with animation - blend factor: %.3f", blendFactor);
#endif
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error blending ragdoll with animation: " + wErrorMsg);
    }
}

//==============================================================================
// Newtonian Motion Methods Implementation
//==============================================================================
void Physics::ApplyNewtonianMotion(PhysicsBody& body, const PhysicsVector3D& force, float deltaTime)
{
    PHYSICS_RECORD_FUNCTION();

    try
    {
        // Apply Newton's second law: F = ma, therefore a = F/m
        body.ApplyForce(force);

        // Integrate using Verlet integration for stability
        VerletIntegration(body, deltaTime);

#if defined(_DEBUG_PHYSICS_)
        static int debugCounter = 0;
        if (++debugCounter % 120 == 0) // Log every 120 frames to avoid spam
        {
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[Physics] Applied Newtonian motion - Force: (%.2f, %.2f, %.2f)",
                force.x, force.y, force.z);
        }
#endif
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error applying Newtonian motion: " + wErrorMsg);
    }
}

std::vector<PhysicsVector3D> Physics::CalculateProjectileMotion(const PhysicsVector3D& startPosition,
    const PhysicsVector3D& initialVelocity,
    float gravity, float drag,
    float timeStep, float maxTime)
{
    PHYSICS_RECORD_FUNCTION();

    std::vector<PhysicsVector3D> trajectory;

    try
    {
        PhysicsVector3D currentPosition = startPosition;
        PhysicsVector3D currentVelocity = initialVelocity;
        float currentTime = 0.0f;

        // Reserve space for trajectory points
        int maxPoints = static_cast<int>(maxTime / timeStep);
        trajectory.reserve(maxPoints);

        // Add starting position
        trajectory.push_back(currentPosition);

        // Simulate projectile motion
        while (currentTime < maxTime)
        {
            // Apply gravity
            PhysicsVector3D gravityForce(0.0f, -gravity, 0.0f);
            currentVelocity += gravityForce * timeStep;

            // Apply drag
            float dragFactor = 1.0f - (drag * timeStep);
            dragFactor = std::max(0.0f, dragFactor);
            currentVelocity *= dragFactor;

            // Update position
            currentPosition += currentVelocity * timeStep;
            trajectory.push_back(currentPosition);

            currentTime += timeStep;

            // Stop if projectile hits ground (y = 0)
            if (currentPosition.y <= 0.0f && currentVelocity.y < 0.0f)
            {
                break;
            }
        }

#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[Physics] Calculated projectile motion with %zu trajectory points",
            trajectory.size());
#endif
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error calculating projectile motion: " + wErrorMsg);
    }

    return trajectory;
}

PhysicsVector3D Physics::CalculateTrajectoryToTarget(const PhysicsVector3D& startPosition,
    const PhysicsVector3D& targetPosition,
    float gravity, float launchSpeed)
{
    PHYSICS_RECORD_FUNCTION();

    try
    {
        // Calculate displacement vector
        PhysicsVector3D displacement = targetPosition - startPosition;
        float horizontalDistance = FAST_SQRT(displacement.x * displacement.x + displacement.z * displacement.z);
        float verticalDistance = displacement.y;

        // Calculate launch angle for projectile motion
        // Using simplified ballistic trajectory formula
        float g = gravity;
        float v = launchSpeed;

        // Calculate optimal launch angle
        float discriminant = (v * v * v * v) - g * (g * horizontalDistance * horizontalDistance + 2.0f * verticalDistance * v * v);

        if (discriminant < 0.0f)
        {
            // Target unreachable with given speed, return high-angle trajectory
#if defined(_DEBUG_PHYSICS_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[Physics] Target unreachable with given launch speed");
#endif

            // Calculate direction and use 45-degree angle
            PhysicsVector3D direction = displacement.Normalized();
            float angle = XM_PI * 0.25f; // 45 degrees

            return PhysicsVector3D(
                direction.x * FAST_COS(angle) * launchSpeed,
                FAST_SIN(angle) * launchSpeed,
                direction.z * FAST_COS(angle) * launchSpeed
            );
        }

        float angle = FAST_ATAN((v * v + FAST_SQRT(discriminant)) / (g * horizontalDistance));

        // Calculate launch velocity components
        PhysicsVector3D horizontalDirection(displacement.x, 0.0f, displacement.z);
        horizontalDirection = horizontalDirection.Normalized();

        float horizontalSpeed = launchSpeed * FAST_COS(angle);
        float verticalSpeed = launchSpeed * FAST_SIN(angle);

        PhysicsVector3D launchVelocity = horizontalDirection * horizontalSpeed;
        launchVelocity.y = verticalSpeed;

#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[Physics] Calculated trajectory to target - Launch angle: %.2f degrees",
            angle * 180.0f / XM_PI);
#endif

        return launchVelocity;
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error calculating trajectory to target: " + wErrorMsg);

        // Return safe fallback velocity
        return PhysicsVector3D(0.0f, launchSpeed * 0.7f, 0.0f);
    }
}

//==============================================================================
// Physics-based Animation Methods Implementation
//==============================================================================
PhysicsVector3D Physics::BlendPhysicsWithAnimation(const PhysicsVector3D& physicsPosition,
    const PhysicsVector3D& animationPosition,
    float blendFactor) const
{
    PHYSICS_RECORD_FUNCTION();

    try
    {
        // Clamp blend factor to valid range
        blendFactor = std::clamp(blendFactor, 0.0f, 1.0f);

        // Linear interpolation between physics and animation
        // blendFactor = 0: pure physics, blendFactor = 1: pure animation
        PhysicsVector3D blendedPosition = physicsPosition * (1.0f - blendFactor) + animationPosition * blendFactor;

        return blendedPosition;
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error blending physics with animation: " + wErrorMsg);

        // Return safe fallback (physics position)
        return physicsPosition;
    }
}

PhysicsVector3D Physics::CalculateRecoilAnimation(const PhysicsVector3D& force, float mass, float dampening) const
{
    PHYSICS_RECORD_FUNCTION();

    try
    {
        // Calculate recoil displacement based on impulse and mass
        // Impulse = Force * time (assume unit time for simplicity)
        PhysicsVector3D impulse = force;

        // Calculate recoil velocity: v = impulse / mass
        PhysicsVector3D recoilVelocity = impulse * (1.0f / mass);

        // Apply dampening to recoil
        dampening = std::clamp(dampening, 0.0f, 1.0f);
        recoilVelocity *= dampening;

        // Convert velocity to displacement (assume unit time)
        PhysicsVector3D recoilDisplacement = recoilVelocity;

#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[Physics] Calculated recoil animation - Displacement: (%.3f, %.3f, %.3f)",
            recoilDisplacement.x, recoilDisplacement.y, recoilDisplacement.z);
#endif

        return recoilDisplacement;
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error calculating recoil animation: " + wErrorMsg);

        return PhysicsVector3D();
    }
}

PhysicsVector3D Physics::ApplySecondaryMotion(const PhysicsVector3D& primaryMotion,
    const PhysicsVector3D& previousSecondary,
    float stiffness, float damping) const
{
    PHYSICS_RECORD_FUNCTION();
try
   {
       // Clamp parameters to valid ranges
       stiffness = std::clamp(stiffness, 0.0f, 1.0f);
       damping = std::clamp(damping, 0.0f, 1.0f);
       
       // Calculate secondary motion using spring-damper system
       // Force = -stiffness * (current - target) - damping * velocity
       PhysicsVector3D displacement = previousSecondary - primaryMotion;
       PhysicsVector3D velocity = displacement; // Simplified velocity calculation
       
       PhysicsVector3D springForce = displacement * (-stiffness);
       PhysicsVector3D dampingForce = velocity * (-damping);
       PhysicsVector3D totalForce = springForce + dampingForce;
       
       // Apply force to get new secondary motion
       PhysicsVector3D newSecondaryMotion = previousSecondary + totalForce;
       
#if defined(_DEBUG_PHYSICS_)
       debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[Physics] Applied secondary motion - Stiffness: %.3f, Damping: %.3f", 
                            stiffness, damping);
#endif
       
       return newSecondaryMotion;
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error applying secondary motion: " + wErrorMsg);
       
       // Return safe fallback (primary motion)
       return primaryMotion;
   }
}

//==============================================================================
// Utility and Debug Methods Implementation
//==============================================================================
void Physics::GetPhysicsStatistics(int& activeBodyCount, int& collisionCount, int& particleCount) const
{
   PHYSICS_RECORD_FUNCTION();
   
   try
   {
       activeBodyCount = m_activeBodyCount.load();
       collisionCount = m_collisionCount.load();
       particleCount = m_particleCount.load();
       
#if defined(_DEBUG_PHYSICS_)
       static int debugCounter = 0;
       if (++debugCounter % 300 == 0) // Log every 300 frames to avoid spam
       {
           debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[Physics] Statistics - Bodies: %d, Collisions: %d, Particles: %d", 
                                activeBodyCount, collisionCount, particleCount);
       }
#endif
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error getting physics statistics: " + wErrorMsg);
       
       // Return safe default values
       activeBodyCount = 0;
       collisionCount = 0;
       particleCount = 0;
   }
}

void Physics::AddDebugLine(const PhysicsVector3D& start, const PhysicsVector3D& end)
{
   PHYSICS_RECORD_FUNCTION();
   
   try
   {
       std::lock_guard<std::mutex> lock(m_physicsMutex);
       
       // Add both start and end points for line rendering
       m_debugLines.push_back(start);
       m_debugLines.push_back(end);
       
       // Limit debug lines to prevent memory issues
       const size_t maxDebugLines = 2000; // 1000 lines max
       if (m_debugLines.size() > maxDebugLines)
       {
           // Remove oldest lines (FIFO)
           m_debugLines.erase(m_debugLines.begin(), m_debugLines.begin() + 200); // Remove 100 lines
       }
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error adding debug line: " + wErrorMsg);
   }
}

void Physics::ResetPerformanceCounters()
{
   PHYSICS_RECORD_FUNCTION();
   
   try
   {
       m_lastUpdateTime = 0.0f;
       m_activeBodyCount.store(0);
       m_collisionCount.store(0);
       m_particleCount.store(0);
       
#if defined(_DEBUG_PHYSICS_)
       debug.logLevelMessage(LogLevel::LOG_INFO, L"[Physics] Reset performance counters");
#endif
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error resetting performance counters: " + wErrorMsg);
   }
}

//==============================================================================
// Additional Helper Methods Implementation
//==============================================================================
void Physics::UpdateSpatialHash()
{
   PHYSICS_RECORD_FUNCTION();
   
   try
   {
       // Spatial hash implementation for broad-phase collision optimization
       // This is a placeholder for future optimization
       
#if defined(_DEBUG_PHYSICS_)
       static int debugCounter = 0;
       if (++debugCounter % 600 == 0) // Log every 600 frames to avoid spam
       {
           debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[Physics] Updated spatial hash");
       }
#endif
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error updating spatial hash: " + wErrorMsg);
   }
}

bool Physics::AABBvsAABB(const PhysicsVector3D& minA, const PhysicsVector3D& maxA, 
                       const PhysicsVector3D& minB, const PhysicsVector3D& maxB) const
{
   PHYSICS_RECORD_FUNCTION();
   
   // AABB vs AABB collision detection using separating axis theorem
   return (minA.x <= maxB.x && maxA.x >= minB.x &&
           minA.y <= maxB.y && maxA.y >= minB.y &&
           minA.z <= maxB.z && maxA.z >= minB.z);
}

bool Physics::SphereVsSphere(const PhysicsVector3D& centerA, float radiusA, 
                           const PhysicsVector3D& centerB, float radiusB, ContactPoint& contact) const
{
   PHYSICS_RECORD_FUNCTION();
   
   try
   {
       PhysicsVector3D direction = centerB - centerA;
       float distance = direction.Magnitude();
       float combinedRadius = radiusA + radiusB;
       
       if (distance <= combinedRadius && distance > MIN_VELOCITY_THRESHOLD)
       {
           // Collision detected - fill contact information
           contact.normal = direction.Normalized();
           contact.penetrationDepth = combinedRadius - distance;
           contact.position = centerA + direction * 0.5f;
           
           return true;
       }
       
       return false;
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error in sphere vs sphere collision: " + wErrorMsg);
       return false;
   }
}

bool Physics::AABBvsSphere(const PhysicsVector3D& aabbMin, const PhysicsVector3D& aabbMax, 
                         const PhysicsVector3D& sphereCenter, float sphereRadius, ContactPoint& contact) const
{
   PHYSICS_RECORD_FUNCTION();
   
   try
   {
       // Find closest point on AABB to sphere center
       PhysicsVector3D closestPoint;
       closestPoint.x = std::clamp(sphereCenter.x, aabbMin.x, aabbMax.x);
       closestPoint.y = std::clamp(sphereCenter.y, aabbMin.y, aabbMax.y);
       closestPoint.z = std::clamp(sphereCenter.z, aabbMin.z, aabbMax.z);
       
       // Calculate distance from sphere center to closest point
       PhysicsVector3D direction = sphereCenter - closestPoint;
       float distance = direction.Magnitude();
       
       if (distance <= sphereRadius)
       {
           // Collision detected - fill contact information
           if (distance > MIN_VELOCITY_THRESHOLD)
           {
               contact.normal = direction.Normalized();
           }
           else
           {
               // Sphere center is inside AABB, use arbitrary normal
               contact.normal = PhysicsVector3D(0.0f, 1.0f, 0.0f);
           }
           
           contact.penetrationDepth = sphereRadius - distance;
           contact.position = closestPoint;
           
           return true;
       }
       
       return false;
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error in AABB vs sphere collision: " + wErrorMsg);
       return false;
   }
}

void Physics::EulerIntegration(PhysicsBody& body, float deltaTime)
{
   PHYSICS_RECORD_FUNCTION();
   
   try
   {
       // Simple Euler integration (less stable but faster)
       if (!body.isStatic && body.isActive)
       {
           // Update velocity: v = v + a * dt
           body.velocity += body.acceleration * deltaTime;
           
           // Apply drag
           float dragFactor = 1.0f - (body.drag * deltaTime);
           dragFactor = std::max(0.0f, dragFactor);
           body.velocity *= dragFactor;
           
           // Update position: p = p + v * dt
           body.position += body.velocity * deltaTime;
           
           // Reset acceleration
           body.acceleration = PhysicsVector3D();
       }
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error in Euler integration: " + wErrorMsg);
   }
}

void Physics::RK4Integration(PhysicsBody& body, float deltaTime)
{
   PHYSICS_RECORD_FUNCTION();
   
   try
   {
       // Runge-Kutta 4th order integration (more accurate but slower)
       if (!body.isStatic && body.isActive)
       {
           // This is a simplified RK4 implementation
           // In a full implementation, this would involve calculating derivatives at multiple points
           
           PhysicsVector3D k1_v = body.acceleration * deltaTime;
           PhysicsVector3D k1_p = body.velocity * deltaTime;
           
           PhysicsVector3D k2_v = body.acceleration * deltaTime; // Simplified
           PhysicsVector3D k2_p = (body.velocity + k1_v * 0.5f) * deltaTime;
           
           PhysicsVector3D k3_v = body.acceleration * deltaTime; // Simplified
           PhysicsVector3D k3_p = (body.velocity + k2_v * 0.5f) * deltaTime;
           
           PhysicsVector3D k4_v = body.acceleration * deltaTime; // Simplified
           PhysicsVector3D k4_p = (body.velocity + k3_v) * deltaTime;
           
           // Update velocity and position using weighted average
           body.velocity += (k1_v + k2_v * 2.0f + k3_v * 2.0f + k4_v) * (1.0f / 6.0f);
           body.position += (k1_p + k2_p * 2.0f + k3_p * 2.0f + k4_p) * (1.0f / 6.0f);
           
           // Apply drag
           float dragFactor = 1.0f - (body.drag * deltaTime);
           dragFactor = std::max(0.0f, dragFactor);
           body.velocity *= dragFactor;
           
           // Reset acceleration
           body.acceleration = PhysicsVector3D();
       }
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error in RK4 integration: " + wErrorMsg);
   }
}

PhysicsVector2D Physics::BezierInterpolation2D(const PhysicsVector2D& p0, const PhysicsVector2D& p1, 
                                              const PhysicsVector2D& p2, const PhysicsVector2D& p3, float t) const
{
   PHYSICS_RECORD_FUNCTION();
   
   try
   {
       // Cubic Bezier curve interpolation
       float u = 1.0f - t;
       float tt = t * t;
       float uu = u * u;
       float uuu = uu * u;
       float ttt = tt * t;
       
       // Calculate Bezier point
       PhysicsVector2D point = p0 * uuu + p1 * (3.0f * uu * t) + p2 * (3.0f * u * tt) + p3 * ttt;
       
       return point;
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error in 2D Bezier interpolation: " + wErrorMsg);
       
       // Return linear interpolation as fallback
       return p0 * (1.0f - t) + p3 * t;
   }
}

PhysicsVector3D Physics::BezierInterpolation3D(const PhysicsVector3D& p0, const PhysicsVector3D& p1, 
                                              const PhysicsVector3D& p2, const PhysicsVector3D& p3, float t) const
{
   PHYSICS_RECORD_FUNCTION();
   
   try
   {
       // Cubic Bezier curve interpolation
       float u = 1.0f - t;
       float tt = t * t;
       float uu = u * u;
       float uuu = uu * u;
       float ttt = tt * t;
       
       // Calculate Bezier point
       PhysicsVector3D point = p0 * uuu + p1 * (3.0f * uu * t) + p2 * (3.0f * u * tt) + p3 * ttt;
       
       return point;
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error in 3D Bezier interpolation: " + wErrorMsg);
       
       // Return linear interpolation as fallback
       return p0 * (1.0f - t) + p3 * t;
   }
}

void Physics::ApplyImpulseConstraints(CollisionManifold& manifold)
{
   PHYSICS_RECORD_FUNCTION();
   
   try
   {
       // Apply impulse-based collision response
       if (!manifold.bodyA || !manifold.bodyB || manifold.contacts.empty())
       {
           return;
       }
       
       for (const auto& contact : manifold.contacts)
       {
           // Calculate relative velocity at contact point
           PhysicsVector3D relativeVelocity = manifold.bodyB->velocity - manifold.bodyA->velocity;
           float normalVelocity = FastDotProduct(relativeVelocity, contact.normal);
           
           // Don't resolve if velocities are separating
           if (normalVelocity > 0.0f)
           {
               continue;
           }
           
           // Calculate impulse magnitude
           float impulseMagnitude = -(1.0f + contact.restitution) * normalVelocity;
           impulseMagnitude /= (manifold.bodyA->inverseMass + manifold.bodyB->inverseMass);
           
           // Apply impulse
           PhysicsVector3D impulse = contact.normal * impulseMagnitude;
           manifold.bodyA->ApplyImpulse(impulse * -1.0f);
           manifold.bodyB->ApplyImpulse(impulse);
           
           // Apply friction impulse
           PhysicsVector3D tangent = relativeVelocity - contact.normal * normalVelocity;
           float tangentMagnitude = tangent.Magnitude();
           
           if (tangentMagnitude > MIN_VELOCITY_THRESHOLD)
           {
               tangent = tangent.Normalized();
               
               float frictionImpulse = -FastDotProduct(relativeVelocity, tangent);
               frictionImpulse /= (manifold.bodyA->inverseMass + manifold.bodyB->inverseMass);
               
               // Clamp friction impulse to Coulomb friction limit
               float maxFriction = contact.friction * impulseMagnitude;
               frictionImpulse = std::clamp(frictionImpulse, -maxFriction, maxFriction);
               
               PhysicsVector3D frictionVector = tangent * frictionImpulse;
               manifold.bodyA->ApplyImpulse(frictionVector * -1.0f);
               manifold.bodyB->ApplyImpulse(frictionVector);
           }
       }
   }
   catch (const std::exception& e)
   {
       std::string errorMsg = e.what();
       std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
       debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error applying impulse constraints: " + wErrorMsg);
   }
}

//==============================================================================
// Collision Response Resolution Implementation
//==============================================================================
void Physics::ResolveCollisionResponse(CollisionManifold& manifold)
{
    PHYSICS_RECORD_FUNCTION();

    // Ensure we have valid bodies and contacts
    if (!manifold.bodyA || !manifold.bodyB || manifold.contacts.empty())
    {
#if defined(_DEBUG_PHYSICS_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[Physics] ResolveCollisionResponse called with invalid manifold");
#endif
        return;
    }

    try
    {
        // Skip resolution if both bodies are static
        if (manifold.bodyA->isStatic && manifold.bodyB->isStatic)
        {
            return;
        }

        // Skip resolution if bodies are inactive
        if (!manifold.bodyA->isActive || !manifold.bodyB->isActive)
        {
            return;
        }

        // Calculate relative velocity between the two bodies
        PhysicsVector3D relativeVelocity = manifold.bodyB->velocity - manifold.bodyA->velocity;

        // Calculate separating velocity along the collision normal
        float separatingVelocity = FastDotProduct(relativeVelocity, manifold.normal);
        manifold.separatingVelocity = separatingVelocity;

        // Don't resolve if velocities are already separating
        if (separatingVelocity > 0.0f)
        {
#if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[Physics] Bodies already separating - separating velocity: %.3f",
                separatingVelocity);
#endif
            return;
        }

        // Process each contact point in the manifold
        for (const auto& contact : manifold.contacts)
        {
            // Calculate combined restitution (coefficient of restitution)
            float combinedRestitution = std::min(manifold.bodyA->restitution, manifold.bodyB->restitution);

            // Calculate combined friction coefficient
            float combinedFriction = std::sqrt(manifold.bodyA->friction * manifold.bodyB->friction);

            // Calculate impulse magnitude using conservation of momentum
            // Impulse = -(1 + e) * v_sep / (1/m1 + 1/m2)
            float impulseMagnitude = -(1.0f + combinedRestitution) * separatingVelocity;
            impulseMagnitude /= (manifold.bodyA->inverseMass + manifold.bodyB->inverseMass);

            // Ensure impulse magnitude is positive (away from collision)
            if (impulseMagnitude < 0.0f)
            {
                continue;
            }

            // Calculate impulse vector along the collision normal
            PhysicsVector3D impulseVector = manifold.normal * impulseMagnitude;

            // Apply normal impulse to both bodies
            manifold.bodyA->ApplyImpulse(impulseVector * -1.0f);
            manifold.bodyB->ApplyImpulse(impulseVector);

            // Apply friction impulse for tangential motion
            ApplyFrictionImpulse(*manifold.bodyA, *manifold.bodyB, contact, combinedFriction, impulseMagnitude);

            // Apply position correction to prevent sinking
            ApplyPositionCorrection(*manifold.bodyA, *manifold.bodyB, contact, manifold.normal);

#if defined(_DEBUG_PHYSICS_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"[Physics] Applied collision impulse - Magnitude: %.6f, Restitution: %.3f, Friction: %.3f",
                impulseMagnitude, combinedRestitution, combinedFriction);
#endif
        }

        // Update collision count for statistics
        m_collisionCount.fetch_add(1);

        // Add debug lines for visualization if enabled
        AddDebugLine(manifold.bodyA->position, manifold.bodyB->position);

#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"[Physics] Resolved collision response - Contacts processed: %zu", manifold.contacts.size());
#endif
    }
    catch (const std::exception& e)
    {
        // Convert exception message to wide string for logging
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());

        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error resolving collision response: " + wErrorMsg);

        // Record exception for debugging
        m_exceptionHandler.LogException(e, "Physics::ResolveCollisionResponse");
    }
    catch (...)
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Unknown error resolving collision response");

        // Record unknown exception
        m_exceptionHandler.LogCustomException("Unknown exception in Physics::ResolveCollisionResponse",
            "Physics::ResolveCollisionResponse");
    }
}

//==============================================================================
// Collision Response Helper Methods Implementation
//==============================================================================
void Physics::ApplyFrictionImpulse(PhysicsBody& bodyA, PhysicsBody& bodyB, const ContactPoint& contact,
    float combinedFriction, float normalImpulseMagnitude)
{
    PHYSICS_RECORD_FUNCTION();

    try
    {
        // Calculate relative velocity at contact point
        PhysicsVector3D relativeVelocity = bodyB.velocity - bodyA.velocity;

        // Remove normal component to get tangential velocity
        float normalVelocity = FastDotProduct(relativeVelocity, contact.normal);
        PhysicsVector3D tangentialVelocity = relativeVelocity - contact.normal * normalVelocity;

        // Check if there's tangential motion to apply friction to
        float tangentialSpeed = tangentialVelocity.Magnitude();
        if (tangentialSpeed < MIN_VELOCITY_THRESHOLD)
        {
            return; // No tangential motion, no friction needed
        }

        // Calculate tangential direction (normalize tangential velocity)
        PhysicsVector3D tangentialDirection = tangentialVelocity.Normalized();

        // Calculate friction impulse magnitude
        float frictionImpulseMagnitude = -FastDotProduct(relativeVelocity, tangentialDirection);
        frictionImpulseMagnitude /= (bodyA.inverseMass + bodyB.inverseMass);

        // Apply Coulomb friction law: friction impulse <= μ * normal impulse
        float maxFrictionImpulse = combinedFriction * normalImpulseMagnitude;

        // Clamp friction impulse to Coulomb friction limit
        if (std::abs(frictionImpulseMagnitude) > maxFrictionImpulse)
        {
            // Kinetic friction (sliding)
            frictionImpulseMagnitude = (frictionImpulseMagnitude > 0.0f) ? maxFrictionImpulse : -maxFrictionImpulse;
        }
        // else: Static friction (no sliding)

        // Calculate friction impulse vector
        PhysicsVector3D frictionImpulse = tangentialDirection * frictionImpulseMagnitude;

        // Apply friction impulse to both bodies
        bodyA.ApplyImpulse(frictionImpulse * -1.0f);
        bodyB.ApplyImpulse(frictionImpulse);

#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"[Physics] Applied friction impulse - Magnitude: %.6f, Max allowed: %.6f",
            std::abs(frictionImpulseMagnitude), maxFrictionImpulse);
#endif
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error applying friction impulse: " + wErrorMsg);
    }
}

void Physics::ApplyPositionCorrection(PhysicsBody& bodyA, PhysicsBody& bodyB, const ContactPoint& contact,
    const PhysicsVector3D& normal)
{
    PHYSICS_RECORD_FUNCTION();

    try
    {
        // Only apply position correction if there's significant penetration
        if (contact.penetrationDepth <= MIN_VELOCITY_THRESHOLD)
        {
            return;
        }

        // Calculate total inverse mass
        float totalInverseMass = bodyA.inverseMass + bodyB.inverseMass;

        // Skip correction if both bodies are static (infinite mass)
        if (totalInverseMass <= 0.0f)
        {
            return;
        }

        // Calculate position correction using Baumgarte stabilization
        // This prevents objects from sinking into each other over multiple frames
        const float correctionPercentage = 0.8f;  // How much of the penetration to correct (80%)
        const float slop = 0.01f;                 // Penetration allowance to reduce jitter

        float correctionMagnitude = std::max(contact.penetrationDepth - slop, 0.0f) / totalInverseMass;
        correctionMagnitude *= correctionPercentage;

        // Calculate correction vector
        PhysicsVector3D correction = normal * correctionMagnitude;

        // Apply position corrections based on inverse mass ratios
        // Heavier objects move less, lighter objects move more
        bodyA.position -= correction * bodyA.inverseMass;
        bodyB.position += correction * bodyB.inverseMass;

#if defined(_DEBUG_PHYSICS_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"[Physics] Applied position correction - Penetration: %.6f, Correction: %.6f",
            contact.penetrationDepth, correctionMagnitude);
#endif
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Physics] Error applying position correction: " + wErrorMsg);
    }
}

