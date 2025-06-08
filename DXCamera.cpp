#include "Includes.h"

#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)

#include "MathPrecalculation.h"
#include "Configuration.h"
#include "DXCamera.h"
#include "Debug.h"

class Debug;
class Configuration;

extern Debug debug;
extern Configuration config;

using namespace DirectX;

Camera::Camera() {
    position = XMFLOAT3(0.0f, 0.0f, -5.0f);
    target = XMFLOAT3(0.0f, 0.0f, 0.0f);
    up = XMFLOAT3(0.0f, 1.0f, 0.0f);
    
    // Initialize jump animation variables
    m_isJumping = false;
    m_focusOnTarget = false;                    // Initialize focus flag
    m_isJumpingBackInHistory = false;           // Initialize history jumping flag
    m_historyJumpStepsRemaining = 0;            // Initialize history steps counter
    m_jumpStartPosition = XMFLOAT3(0.0f, 0.0f, 0.0f);
    m_jumpTargetPosition = XMFLOAT3(0.0f, 0.0f, 0.0f);
    m_originalTarget = XMFLOAT3(0.0f, 0.0f, 0.0f);     // Initialize original target storage
    m_jumpSpeed = 1;
    m_currentPathIndex = 0;
    m_jumpAnimationTimer = 0.0f;
    m_totalJumpTime = 0.0f;
    
    // Initialize continuous rotation variables
    m_isRotatingAroundTarget = false;           // Initialize rotation flag
    m_continuousRotation = false;               // Initialize continuous rotation flag
    m_rotateAroundX = false;                    // Initialize X-axis rotation flag
    m_rotateAroundY = false;                    // Initialize Y-axis rotation flag
    m_rotateAroundZ = false;                    // Initialize Z-axis rotation flag
    m_rotationSpeedX = 60.0f;                   // Default 60 degrees per second for X-axis
    m_rotationSpeedY = 60.0f;                   // Default 60 degrees per second for Y-axis
    m_rotationSpeedZ = 60.0f;                   // Default 60 degrees per second for Z-axis
    m_currentRotationX = 0.0f;                  // Initialize current X rotation
    m_currentRotationY = 0.0f;                  // Initialize current Y rotation
    m_currentRotationZ = 0.0f;                  // Initialize current Z rotation
    m_targetRotationX = 360.0f;                 // Target 360 degrees for full X rotation
    m_targetRotationY = 360.0f;                 // Target 360 degrees for full Y rotation
    m_targetRotationZ = 360.0f;                 // Target 360 degrees for full Z rotation
    m_rotationStartPosition = XMFLOAT3(0.0f, 0.0f, 0.0f);  // Initialize rotation start position
    m_rotationTarget = XMFLOAT3(0.0f, 0.0f, 0.0f);         // Initialize rotation target
    m_rotationDistance = 0.0f;                  // Initialize rotation distance
    
    // Reserve memory for jump history to avoid reallocations
    m_jumpHistory.reserve(MAX_JUMP_HISTORY);
    
    SetupDefaultCamera(800.0f, 600.0f); // Default window size
    UpdateViewMatrix();

    #if defined(_DEBUG_CAMERA_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"DX Camera created successfully with enhanced jump animation, history support, and continuous rotation");
    #endif
}

Camera::~Camera() {
    // Stop any active rotations before cleanup
    if (m_isRotatingAroundTarget)
    {
        StopRotating();
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[Camera] Stopped rotation during destructor cleanup");
        #endif
    }
    
    // Clean up jump animation data
    m_currentTravelPath.clear();
    m_jumpHistory.clear();
    
    // Reset all rotation variables
    m_isRotatingAroundTarget = false;
    m_continuousRotation = false;
    m_rotateAroundX = false;
    m_rotateAroundY = false;
    m_rotateAroundZ = false;
    
    #if defined(_DEBUG_CAMERA_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"DX Camera destroyed with complete jump animation, history, and rotation cleanup!");
    #endif
}

// === CAMERA JUMP FUNCTIONALITY ===

void Camera::JumpTo(float new_x, float new_y, float new_z, int speed, bool FocusOnTarget)
{
    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"[Camera] JumpTo called: target(%.2f, %.2f, %.2f), speed=%d, focusOnTarget=%s",
            new_x, new_y, new_z, speed, FocusOnTarget ? L"true" : L"false");
    #endif

    // Validate speed parameter (must be positive)
    if (speed <= 0)
    {
        speed = 1; // Default to fastest speed
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_WARNING,
                L"[Camera] Invalid speed parameter, defaulting to speed=1");
        #endif
    }

    // Store current position as starting point
    m_jumpStartPosition = position;

    // Set target position
    m_jumpTargetPosition = XMFLOAT3(new_x, new_y, new_z);

    // Store focus behavior flag
    m_focusOnTarget = FocusOnTarget;

    // If FocusOnTarget is true, store the original target to maintain focus
    if (m_focusOnTarget)
    {
        m_originalTarget = target;
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"[Camera] Maintaining focus on target: (%.2f, %.2f, %.2f)",
                m_originalTarget.x, m_originalTarget.y, m_originalTarget.z);
        #endif
    }

    // Check if we're already at the target position (within small tolerance)
    float distance = FAST_MATH.FastDistance(XMFLOAT2(position.x, position.z),
        XMFLOAT2(new_x, new_z)) +
        abs(position.y - new_y);

    if (distance < 0.01f)
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"[Camera] Already at target position, jump completed immediately");
        #endif
        return; // Already at target, no need to jump
    }

    // Calculate number of path points based on distance and speed
    // Increased base points and reduced speed divisor for faster movement
    int pathPoints = static_cast<int>((distance * 15.0f) / (speed * 2.0f)) + 15;
    pathPoints = std::clamp(pathPoints, 15, 120); // Reduced maximum for faster animation

    // Step 1: Calculate smooth travel path using MathPrecalculation Class
    m_currentTravelPath = CalculateSmoothTravelPath(m_jumpStartPosition, m_jumpTargetPosition, pathPoints);

    // Step 2: Set status flag that jumping is occurring
    m_isJumping = true;
    m_jumpSpeed = speed;
    m_currentPathIndex = 0;
    m_jumpAnimationTimer = 0.0f;

    // Calculate total time for this jump - significantly faster timing
    // Base time is now much faster, with speed 1 being very fast
    float baseTime = 0.8f;                                                      // Adjust for much faster or slower (higer value) base speed
    float speedMultiplier = 1.0f / (static_cast<float>(speed) * 1.5f);          // Increased speed effect
    m_totalJumpTime = baseTime * speedMultiplier * (1.0f + distance * 0.1f);    // Distance factor
    m_totalJumpTime = std::clamp(m_totalJumpTime, 0.2f, 3.0f);                  // Much faster bounds

    // Step 2: Add to jump history (record history for last 10 locations)
    AddToJumpHistory(m_jumpStartPosition, m_jumpTargetPosition, m_currentTravelPath, speed, FocusOnTarget);

    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"[Camera] Jump initiated: distance=%.2f, pathPoints=%d, totalTime=%.2f, focus=%s",
            distance, pathPoints, m_totalJumpTime, FocusOnTarget ? L"maintained" : L"free");
    #endif
}

void Camera::UpdateJumpAnimation()
{
    // CRITICAL: Update continuous rotation FIRST before jump processing
    UpdateContinuousRotation();

    // Only process jump animation if we're currently jumping
    if (!m_isJumping)
    {
        return;
    }

    // Increment animation timer based on frame time - increased speed
    // Using faster time step for more responsive animation
    float deltaTime = 1.0f / 60.0f; // Assuming 60 FPS
    float speedBoost = 1.8f; // Increased from 1.0f for overall faster animation
    m_jumpAnimationTimer += deltaTime * speedBoost;

    // Calculate current progress (0.0 to 1.0)
    float progress = m_jumpAnimationTimer / m_totalJumpTime;
    progress = std::clamp(progress, 0.0f, 1.0f);

    // Step 3: Animate movement using calculated travel path with speed variation
    if (progress >= 1.0f)
    {
        // Step 4: Jump completed - set final position and clear jumping state
        position = m_jumpTargetPosition;
        m_isJumping = false;
        m_currentPathIndex = 0;
        m_jumpAnimationTimer = 0.0f;
        
        // Check if this was a history jump that just completed
        if (m_isJumpingBackInHistory)
        {
            // Calculate how many entries to remove from history
            int entriesToRemove = m_historyJumpStepsRemaining;
            int currentHistorySize = static_cast<int>(m_jumpHistory.size());
            
            // Remove forward history entries (entries after our target position)
            if (entriesToRemove > 0 && entriesToRemove <= currentHistorySize)
            {
                RemoveForwardHistoryEntries(currentHistorySize - entriesToRemove);
                
                #if defined(_DEBUG_CAMERA_)
                    debug.logDebugMessage(LogLevel::LOG_INFO, 
                        L"[Camera] History jump completed. Removed %d forward entries, %d entries remain", 
                        entriesToRemove, static_cast<int>(m_jumpHistory.size()));
                #endif
            }
            
            // Clear history jump state
            m_isJumpingBackInHistory = false;
            m_historyJumpStepsRemaining = 0;
        }
        
        // Handle completion based on focus behavior
        if (m_focusOnTarget)
        {
            // Calculate the new forward direction from final position to original target
            XMVECTOR finalPos = XMLoadFloat3(&position);
            XMVECTOR originalTarget = XMLoadFloat3(&m_originalTarget);
            XMVECTOR newForwardDirection = XMVector3Normalize(originalTarget - finalPos);
            
            // Store the new forward direction
            XMStoreFloat3(&forward, newForwardDirection);
            
            // Update target to match the maintained focus
            XMStoreFloat3(&target, originalTarget);
            
            // Calculate new yaw and pitch based on the new forward direction
            // Extract yaw from the forward vector (rotation around Y-axis)
            float newYaw = FAST_ATAN2(XMVectorGetX(newForwardDirection), XMVectorGetZ(newForwardDirection));
            
            // Extract pitch from the forward vector (rotation around X-axis)
            float forwardLength = FAST_SQRT(XMVectorGetX(newForwardDirection) * XMVectorGetX(newForwardDirection) + 
                                          XMVectorGetZ(newForwardDirection) * XMVectorGetZ(newForwardDirection));
            float newPitch = FAST_ATAN2(XMVectorGetY(newForwardDirection), forwardLength);
            
            // Update the camera's yaw and pitch to match the new view direction
            m_yaw = newYaw;
            m_pitch = newPitch;
            
            // Ensure up vector is correct
            up = XMFLOAT3(0.0f, 1.0f, 0.0f);
            
            // Create the final view matrix with all properly calculated values
            XMVECTOR upVec = XMLoadFloat3(&up);
            viewMatrix = XMMatrixLookAtLH(finalPos, originalTarget, upVec);
            
            #if defined(_DEBUG_CAMERA_)
                debug.logDebugMessage(LogLevel::LOG_INFO, 
                    L"[Camera] Jump completed with focus maintained: pos(%.2f, %.2f, %.2f), target(%.2f, %.2f, %.2f)", 
                    position.x, position.y, position.z, target.x, target.y, target.z);
                debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                    L"[Camera] Updated orientation: yaw=%.3f, pitch=%.3f, forward(%.3f, %.3f, %.3f)", 
                    m_yaw, m_pitch, forward.x, forward.y, forward.z);
            #endif
        }
        else
        {
            // For free-look behavior, update view matrix normally without changing target
            UpdateViewMatrix();
            
            #if defined(_DEBUG_CAMERA_)
                debug.logDebugMessage(LogLevel::LOG_INFO, 
                    L"[Camera] Jump completed with free-look: final position(%.2f, %.2f, %.2f)", 
                    position.x, position.y, position.z);
            #endif
        }

        return;
    }

    // Calculate current position along the travel path using enhanced smooth interpolation
    float pathProgress = CalculateJumpAnimationSpeed(progress, m_jumpSpeed);
    
    // Find the appropriate point in the travel path
    int totalPathPoints = static_cast<int>(m_currentTravelPath.size());
    if (totalPathPoints > 1)
    {
        float exactIndex = pathProgress * (totalPathPoints - 1);
        int currentIndex = static_cast<int>(exactIndex);
        int nextIndex = std::min(currentIndex + 1, totalPathPoints - 1);
        float interpolationFactor = exactIndex - currentIndex;

        // Interpolate between current and next path points using MathPrecalculation
        const XMFLOAT3& currentPoint = m_currentTravelPath[currentIndex];
        const XMFLOAT3& nextPoint = m_currentTravelPath[nextIndex];

        position.x = FAST_MATH.FastLerp(currentPoint.x, nextPoint.x, interpolationFactor);
        position.y = FAST_MATH.FastLerp(currentPoint.y, nextPoint.y, interpolationFactor);
        position.z = FAST_MATH.FastLerp(currentPoint.z, nextPoint.z, interpolationFactor);

        // Handle focus behavior during animation
        if (m_focusOnTarget)
        {
            // Maintain focus on original target during movement
            XMVECTOR currentPos = XMLoadFloat3(&position);
            XMVECTOR originalTarget = XMLoadFloat3(&m_originalTarget);
            XMVECTOR upVec = XMLoadFloat3(&up);
            
            // Keep looking at the original target throughout the jump
            viewMatrix = XMMatrixLookAtLH(currentPos, originalTarget, upVec);
            
            // Update forward vector to match the maintained focus during animation
            XMVECTOR focusDirection = XMVector3Normalize(originalTarget - currentPos);
            XMStoreFloat3(&forward, focusDirection);
            XMStoreFloat3(&target, originalTarget);
            
            // Update yaw and pitch during animation to keep them synchronized
            float animYaw = FAST_ATAN2(XMVectorGetX(focusDirection), XMVectorGetZ(focusDirection));
            float forwardLength = FAST_SQRT(XMVectorGetX(focusDirection) * XMVectorGetX(focusDirection) + 
                                          XMVectorGetZ(focusDirection) * XMVectorGetZ(focusDirection));
            float animPitch = FAST_ATAN2(XMVectorGetY(focusDirection), forwardLength);
            
            // Smoothly interpolate yaw and pitch during animation for consistent behavior
            m_yaw = animYaw;
            m_pitch = animPitch;
        }
        else
        {
            // Free-look behavior - update view matrix normally
            UpdateViewMatrix();
        }

        #if defined(_DEBUG_CAMERA_)
            if (currentIndex != m_currentPathIndex) // Only log when we move to a new path point
            {
                const wchar_t* jumpType = m_isJumpingBackInHistory ? L"history" : L"normal";
                debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                    L"[Camera] %ls jump progress: %.1f%%, position(%.2f, %.2f, %.2f), focus=%s, yaw=%.2f, pitch=%.2f", 
                    jumpType, progress * 100.0f, position.x, position.y, position.z,
                    m_focusOnTarget ? L"maintained" : L"free", m_yaw, m_pitch);
                m_currentPathIndex = currentIndex;
            }
        #endif
    }
}

// === ROTATION FUNCTIONS ===

void Camera::RotateX(float degrees, int speed, bool FocusOnTarget)
{
    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"[Camera] RotateX called: degrees=%.2f, speed=%d, focusOnTarget=%s", 
            degrees, speed, FocusOnTarget ? L"true" : L"false");
    #endif

    // Validate speed parameter
    if (speed <= 0)
    {
        speed = 2; // Default to medium speed for rotations
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, 
                L"[Camera] Invalid speed parameter for RotateX, defaulting to speed=2");
        #endif
    }

    // Check if rotation angle is effectively zero
    if (abs(degrees) < 0.001f)
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"[Camera] RotateX: Angle is zero, no rotation needed");
        #endif
        return;
    }

    // Get current camera position and target for rotation calculations
    XMVECTOR currentPos = XMLoadFloat3(&position);
    XMVECTOR currentTarget = XMLoadFloat3(&target);
    
    // Determine pivot point and vector to rotate based on focus behavior
    XMVECTOR pivotPoint;
    XMVECTOR vectorToRotate;
    XMFLOAT3 newPosition;
    
    if (FocusOnTarget)
    {
        // When focusing on target, rotate camera around the current target position
        pivotPoint = currentTarget;
        vectorToRotate = currentPos - pivotPoint;
        
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"[Camera] RotateX: Focusing on target, rotating camera around target (%.2f, %.2f, %.2f)", 
                target.x, target.y, target.z);
        #endif
    }
    else
    {
        // When not focusing, rotate target around camera position
        pivotPoint = currentPos;
        vectorToRotate = currentTarget - pivotPoint;
        newPosition = position; // Camera stays in place for free-look
        
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"[Camera] RotateX: Free-look mode, rotating target around camera (%.2f, %.2f, %.2f)", 
                position.x, position.y, position.z);
        #endif
    }
    
    // Calculate the distance for rotation consistency
    float rotationDistance = XMVectorGetX(XMVector3Length(vectorToRotate));
    
    // If distance is too small, use a default distance to prevent issues
    if (rotationDistance < 0.1f)
    {
        rotationDistance = 5.0f; // Default rotation distance
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"[Camera] Using default rotation distance for RotateX: %.2f", rotationDistance);
        #endif
    }

    // Convert degrees to radians for rotation calculation
    float angleRadians = XMConvertToRadians(degrees);
    
    // Create rotation matrix for X-axis rotation
    XMMATRIX rotationMatrix = XMMatrixRotationX(angleRadians);
    
    // Apply rotation to the vector
    XMVECTOR rotatedVector = XMVector3Transform(vectorToRotate, rotationMatrix);
    rotatedVector = XMVector3Normalize(rotatedVector) * rotationDistance;
    
    if (FocusOnTarget)
    {
        // Calculate new camera position after rotation around target
        XMVECTOR newCameraPos = pivotPoint + rotatedVector;
        XMStoreFloat3(&newPosition, newCameraPos);
    }
    else
    {
        // Update target position after rotation around camera
        XMVECTOR newTargetPos = pivotPoint + rotatedVector;
        XMStoreFloat3(&target, newTargetPos);
        
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"[Camera] RotateX (free-look): Updated target to (%.2f, %.2f, %.2f)", 
                target.x, target.y, target.z);
        #endif
    }
    
    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, 
            L"[Camera] RotateX: old pos(%.2f, %.2f, %.2f) -> new pos(%.2f, %.2f, %.2f), focus=%s, angle=%.2f°", 
            position.x, position.y, position.z, newPosition.x, newPosition.y, newPosition.z,
            FocusOnTarget ? L"maintained" : L"free", degrees);
    #endif
    
    // Use JumpTo() to smoothly move to the new position with specified focus behavior
    JumpTo(newPosition.x, newPosition.y, newPosition.z, speed, FocusOnTarget);
}

void Camera::RotateY(float degrees, int speed, bool FocusOnTarget)
{
    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"[Camera] RotateY called: degrees=%.2f, speed=%d, focusOnTarget=%s", 
            degrees, speed, FocusOnTarget ? L"true" : L"false");
    #endif

    // Validate speed parameter
    if (speed <= 0)
    {
        speed = 2; // Default to medium speed for rotations
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, 
                L"[Camera] Invalid speed parameter for RotateY, defaulting to speed=2");
        #endif
    }

    // Check if rotation angle is effectively zero
    if (abs(degrees) < 0.001f)
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"[Camera] RotateY: Angle is zero, no rotation needed");
        #endif
        return;
    }

    // Get current camera position and target for rotation calculations
    XMVECTOR currentPos = XMLoadFloat3(&position);
    XMVECTOR currentTarget = XMLoadFloat3(&target);
    
    // Determine pivot point and vector to rotate based on focus behavior
    XMVECTOR pivotPoint;
    XMVECTOR vectorToRotate;
    XMFLOAT3 newPosition;
    
    if (FocusOnTarget)
    {
        // When focusing on target, rotate camera around the current target position
        pivotPoint = currentTarget;
        vectorToRotate = currentPos - pivotPoint;
        
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"[Camera] RotateY: Focusing on target, rotating camera around target (%.2f, %.2f, %.2f)", 
                target.x, target.y, target.z);
        #endif
    }
    else
    {
        // When not focusing, rotate target around camera position
        pivotPoint = currentPos;
        vectorToRotate = currentTarget - pivotPoint;
        newPosition = position; // Camera stays in place for free-look
        
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"[Camera] RotateY: Free-look mode, rotating target around camera (%.2f, %.2f, %.2f)", 
                position.x, position.y, position.z);
        #endif
    }
    
    // Calculate the distance for rotation consistency
    float rotationDistance = XMVectorGetX(XMVector3Length(vectorToRotate));
    
    // If distance is too small, use a default distance to prevent issues
    if (rotationDistance < 0.1f)
    {
        rotationDistance = 5.0f; // Default rotation distance
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"[Camera] Using default rotation distance for RotateY: %.2f", rotationDistance);
        #endif
    }

    // Convert degrees to radians for rotation calculation
    float angleRadians = XMConvertToRadians(degrees);
    
    // Create rotation matrix for Y-axis rotation
    XMMATRIX rotationMatrix = XMMatrixRotationY(angleRadians);
    
    // Apply rotation to the vector
    XMVECTOR rotatedVector = XMVector3Transform(vectorToRotate, rotationMatrix);
    rotatedVector = XMVector3Normalize(rotatedVector) * rotationDistance;
    
    if (FocusOnTarget)
    {
        // Calculate new camera position after rotation around target
        XMVECTOR newCameraPos = pivotPoint + rotatedVector;
        XMStoreFloat3(&newPosition, newCameraPos);
    }
    else
    {
        // Update target position after rotation around camera
        XMVECTOR newTargetPos = pivotPoint + rotatedVector;
        XMStoreFloat3(&target, newTargetPos);
        
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"[Camera] RotateY (free-look): Updated target to (%.2f, %.2f, %.2f)", 
                target.x, target.y, target.z);
        #endif
    }
    
    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, 
            L"[Camera] RotateY: old pos(%.2f, %.2f, %.2f) -> new pos(%.2f, %.2f, %.2f), focus=%s, angle=%.2f°", 
            position.x, position.y, position.z, newPosition.x, newPosition.y, newPosition.z,
            FocusOnTarget ? L"maintained" : L"free", degrees);
    #endif
    
    // Use JumpTo() to smoothly move to the new position with specified focus behavior
    JumpTo(newPosition.x, newPosition.y, newPosition.z, speed, FocusOnTarget);
}

void Camera::RotateZ(float degrees, int speed, bool FocusOnTarget)
{
    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"[Camera] RotateZ called: degrees=%.2f, speed=%d, focusOnTarget=%s", 
            degrees, speed, FocusOnTarget ? L"true" : L"false");
    #endif

    // Validate speed parameter
    if (speed <= 0)
    {
        speed = 2; // Default to medium speed for rotations
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, 
                L"[Camera] Invalid speed parameter for RotateZ, defaulting to speed=2");
        #endif
    }

    // Check if rotation angle is effectively zero
    if (abs(degrees) < 0.001f)
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"[Camera] RotateZ: Angle is zero, no rotation needed");
        #endif
        return;
    }

    // Get current camera position and target for rotation calculations
    XMVECTOR currentPos = XMLoadFloat3(&position);
    XMVECTOR currentTarget = XMLoadFloat3(&target);
    
    // Determine pivot point and vector to rotate based on focus behavior
    XMVECTOR pivotPoint;
    XMVECTOR vectorToRotate;
    XMFLOAT3 newPosition;
    
    if (FocusOnTarget)
    {
        // When focusing on target, rotate camera around the current target position
        pivotPoint = currentTarget;
        vectorToRotate = currentPos - pivotPoint;
        
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"[Camera] RotateZ: Focusing on target, rotating camera around target (%.2f, %.2f, %.2f)", 
                target.x, target.y, target.z);
        #endif
    }
    else
    {
        // When not focusing, rotate target around camera position
        pivotPoint = currentPos;
        vectorToRotate = currentTarget - pivotPoint;
        newPosition = position; // Camera stays in place for free-look
        
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"[Camera] RotateZ: Free-look mode, rotating target around camera (%.2f, %.2f, %.2f)", 
                position.x, position.y, position.z);
        #endif
    }
    
    // Calculate the distance for rotation consistency
    float rotationDistance = XMVectorGetX(XMVector3Length(vectorToRotate));
    
    // If distance is too small, use a default distance to prevent issues
    if (rotationDistance < 0.1f)
    {
        rotationDistance = 5.0f; // Default rotation distance
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"[Camera] Using default rotation distance for RotateZ: %.2f", rotationDistance);
        #endif
    }

    // Convert degrees to radians for rotation calculation
    float angleRadians = XMConvertToRadians(degrees);
    
    // Create rotation matrix for Z-axis rotation
    XMMATRIX rotationMatrix = XMMatrixRotationZ(angleRadians);
    
    // Apply rotation to the vector
    XMVECTOR rotatedVector = XMVector3Transform(vectorToRotate, rotationMatrix);
    rotatedVector = XMVector3Normalize(rotatedVector) * rotationDistance;
    
    if (FocusOnTarget)
    {
        // Calculate new camera position after rotation around target
        XMVECTOR newCameraPos = pivotPoint + rotatedVector;
        XMStoreFloat3(&newPosition, newCameraPos);
    }
    else
    {
        // Update target position after rotation around camera
        XMVECTOR newTargetPos = pivotPoint + rotatedVector;
        XMStoreFloat3(&target, newTargetPos);
        
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"[Camera] RotateZ (free-look): Updated target to (%.2f, %.2f, %.2f)", 
                target.x, target.y, target.z);
        #endif
    }
    
    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, 
            L"[Camera] RotateZ: old pos(%.2f, %.2f, %.2f) -> new pos(%.2f, %.2f, %.2f), focus=%s, angle=%.2f°", 
            position.x, position.y, position.z, newPosition.x, newPosition.y, newPosition.z,
            FocusOnTarget ? L"maintained" : L"free", degrees);
    #endif
    
    // Use JumpTo() to smoothly move to the new position with specified focus behavior
    JumpTo(newPosition.x, newPosition.y, newPosition.z, speed, FocusOnTarget);
}

void Camera::RotateXYZ(float xDegrees, float yDegrees, float zDegrees, int speed, bool FocusOnTarget)
{
    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"[Camera] RotateXYZ called: X=%.2f°, Y=%.2f°, Z=%.2f°, speed=%d, focusOnTarget=%s",
            xDegrees, yDegrees, zDegrees, speed, FocusOnTarget ? L"true" : L"false");
    #endif

    // Validate speed parameter
    if (speed <= 0)
    {
        speed = 2; // Default to medium speed for multi-axis rotations
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_WARNING,
                L"[Camera] Invalid speed parameter for RotateXYZ, defaulting to speed=2");
        #endif
    }

    // Check if all rotation angles are zero (no rotation needed)
    if (abs(xDegrees) < 0.001f && abs(yDegrees) < 0.001f && abs(zDegrees) < 0.001f)
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"[Camera] RotateXYZ: All angles are zero, no rotation needed");
        #endif
        return;
    }

    // Get current camera position and target for rotation calculations
    XMVECTOR currentPos = XMLoadFloat3(&position);
    XMVECTOR currentTarget = XMLoadFloat3(&target);

    // Determine the pivot point based on focus behavior
    XMVECTOR pivotPoint;
    if (FocusOnTarget)
    {
        // When focusing on target, rotate around the current target position
        pivotPoint = currentTarget;

        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"[Camera] RotateXYZ: Using target as pivot point (%.2f, %.2f, %.2f)",
                target.x, target.y, target.z);
        #endif
    }
    else
    {
        // When not focusing, rotate around the camera's current position
        pivotPoint = currentPos;

        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"[Camera] RotateXYZ: Using camera position as pivot point (%.2f, %.2f, %.2f)",
                position.x, position.y, position.z);
        #endif
    }

    // Calculate the vector from pivot to the position we want to rotate
    XMVECTOR vectorToRotate;
    if (FocusOnTarget)
    {
        // Rotate the camera position around the target
        vectorToRotate = currentPos - pivotPoint;
    }
    else
    {
        // Rotate the target around the camera position
        vectorToRotate = currentTarget - pivotPoint;
    }

    // Calculate the distance from pivot to the point being rotated
    float rotationDistance = XMVectorGetX(XMVector3Length(vectorToRotate));

    // If distance is too small, use a default distance to prevent issues
    if (rotationDistance < 0.1f)
    {
        rotationDistance = 5.0f; // Default rotation distance
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"[Camera] Using default rotation distance for RotateXYZ: %.2f", rotationDistance);
        #endif
    }

    // Convert degrees to radians for rotation calculation
    float radX = XMConvertToRadians(xDegrees);
    float radY = XMConvertToRadians(yDegrees);
    float radZ = XMConvertToRadians(zDegrees);

    // Create combined rotation matrix using MathPrecalculation for optimization
    // The rotation order is X, then Y, then Z (can be adjusted if needed)
    XMMATRIX rotationMatrixX = XMMatrixRotationX(radX);
    XMMATRIX rotationMatrixY = XMMatrixRotationY(radY);
    XMMATRIX rotationMatrixZ = XMMatrixRotationZ(radZ);

    // Combine rotations in XYZ order
    XMMATRIX combinedRotation = rotationMatrixX * rotationMatrixY * rotationMatrixZ;

    // Apply rotation to the vector
    XMVECTOR rotatedVector = XMVector3Transform(vectorToRotate, combinedRotation);
    rotatedVector = XMVector3Normalize(rotatedVector) * rotationDistance;

    // Calculate the new position based on focus behavior
    XMFLOAT3 newPosition;
    if (FocusOnTarget)
    {
        // Camera rotates around target, so calculate new camera position
        XMVECTOR newCameraPos = pivotPoint + rotatedVector;
        XMStoreFloat3(&newPosition, newCameraPos);
    }
    else
    {
        // Target rotates around camera, so camera position stays the same
        newPosition = position;

        // Update the target position
        XMVECTOR newTargetPos = pivotPoint + rotatedVector;
        XMStoreFloat3(&target, newTargetPos);

        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"[Camera] RotateXYZ (free-look): New target position (%.2f, %.2f, %.2f)",
                target.x, target.y, target.z);
        #endif
    }

    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"[Camera] RotateXYZ: old pos(%.2f, %.2f, %.2f) -> new pos(%.2f, %.2f, %.2f), focus=%s",
            position.x, position.y, position.z,
            newPosition.x, newPosition.y, newPosition.z,
            FocusOnTarget ? L"maintained" : L"free");
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"[Camera] RotateXYZ: Combined rotation - X:%.2f° Y:%.2f° Z:%.2f°, distance:%.2f",
            xDegrees, yDegrees, zDegrees, rotationDistance);
    #endif

    // Use JumpTo() to smoothly move to the new position with specified focus behavior
    JumpTo(newPosition.x, newPosition.y, newPosition.z, speed, FocusOnTarget);
}

void Camera::RotateToOppositeSide(int speed)
{
    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"[Camera] RotateToOppositeSide (enhanced) called: speed=%d", speed);
    #endif

    // Validate speed parameter
    if (speed <= 0)
    {
        speed = 2; // Default to medium speed for opposite side rotation
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, 
                L"[Camera] Invalid speed parameter for RotateToOppositeSide, defaulting to speed=2");
        #endif
    }

    // Check if we're currently jumping - cannot start rotation during active jump
    if (m_isJumping)
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, 
                L"[Camera] Cannot rotate to opposite side while camera is currently jumping");
        #endif
        return;
    }

    // Get current camera position and target for analysis
    XMVECTOR currentPos = XMLoadFloat3(&position);
    XMVECTOR currentTarget = XMLoadFloat3(&target);
    
    // Calculate the look direction vector (from camera to target)
    XMVECTOR lookDirection = XMVector3Normalize(currentTarget - currentPos);
    
    // Store look direction as XMFLOAT3 for analysis
    XMFLOAT3 lookDir;
    XMStoreFloat3(&lookDir, lookDirection);

    // Calculate current pitch angle to help determine best rotation axis
    float currentPitch = FAST_ASIN(lookDir.y);
    float pitchDegrees = XMConvertToDegrees(currentPitch);
    
    // Calculate current yaw angle
    float currentYaw = FAST_ATAN2(lookDir.x, lookDir.z);
    float yawDegrees = XMConvertToDegrees(currentYaw);

    // Determine which axis has the strongest component to decide rotation axis
    char primaryAxis = DeterminePrimaryLookDirection();
    
    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, 
            L"[Camera] Current orientation: pitch=%.1f°, yaw=%.1f°, look=(%.3f, %.3f, %.3f), primary axis: %c", 
            pitchDegrees, yawDegrees, lookDir.x, lookDir.y, lookDir.z, primaryAxis);
    #endif

    // Smart axis selection based on current camera orientation and look direction
    float rotationAngle = 180.0f;
    
    // Check if camera is looking mostly up or down (high pitch angle)
    if (abs(pitchDegrees) > 60.0f)
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"[Camera] High pitch angle detected (%.1f°), rotating around Z-axis for opposite side", 
                pitchDegrees);
        #endif
        
        // For steep up/down views, Z-axis rotation provides the most natural opposite view
        RotateZ(rotationAngle, speed, true);
    }
    // Check if camera is at moderate angles (most common scenario)
    else if (abs(pitchDegrees) < 30.0f)
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"[Camera] Moderate pitch angle (%.1f°), rotating around Y-axis for opposite side", 
                pitchDegrees);
        #endif
        
        // For moderate angles, Y-axis rotation is most natural (horizontal orbit)
        RotateY(rotationAngle, speed, true);
    }
    else
    {
        // For medium pitch angles, use the primary axis determination
        switch (primaryAxis)
        {
            case 'X': // Primary look direction is along X-axis (left/right)
            {
                #if defined(_DEBUG_CAMERA_)
                    debug.logDebugMessage(LogLevel::LOG_INFO, 
                        L"[Camera] X-dominant view with medium pitch, rotating around Y-axis");
                #endif
                RotateY(rotationAngle, speed, true);
                break;
            }
            
            case 'Y': // Primary look direction is along Y-axis (up/down)
            {
                #if defined(_DEBUG_CAMERA_)
                    debug.logDebugMessage(LogLevel::LOG_INFO, 
                        L"[Camera] Y-dominant view, rotating around X-axis for vertical flip");
                #endif
                RotateX(rotationAngle, speed, true);
                break;
            }
            
            case 'Z': // Primary look direction is along Z-axis (forward/backward)
            {
                #if defined(_DEBUG_CAMERA_)
                    debug.logDebugMessage(LogLevel::LOG_INFO, 
                        L"[Camera] Z-dominant view, rotating around Y-axis for front/back flip");
                #endif
                RotateY(rotationAngle, speed, true);
                break;
            }
            
            default: // Diagonal or unclear
            {
                #if defined(_DEBUG_CAMERA_)
                    debug.logDebugMessage(LogLevel::LOG_INFO, 
                        L"[Camera] Diagonal view, using Y-axis rotation as default");
                #endif
                RotateY(rotationAngle, speed, true);
                break;
            }
        }
    }

    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"[Camera] RotateToOppositeSide completed: 180° rotation initiated based on pitch=%.1f° and primary axis=%c", 
            pitchDegrees, primaryAxis);
    #endif
}

char Camera::DeterminePrimaryLookDirection() const
{
    // Get current camera position and target for direction analysis
    XMVECTOR currentPos = XMLoadFloat3(&position);
    XMVECTOR currentTarget = XMLoadFloat3(&target);
    
    // Calculate the look direction vector (from camera to target)
    XMVECTOR lookDirection = XMVector3Normalize(currentTarget - currentPos);
    
    // Extract individual components and get their absolute values
    float absX = abs(XMVectorGetX(lookDirection));
    float absY = abs(XMVectorGetY(lookDirection));
    float absZ = abs(XMVectorGetZ(lookDirection));
    
    // Define threshold for considering components as dominant or equal
    const float dominanceThreshold = 0.1f; // 10% difference required for clear dominance
    
    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, 
            L"[Camera] Look direction components: |X|=%.3f, |Y|=%.3f, |Z|=%.3f", 
            absX, absY, absZ);
    #endif
    
    // Find the maximum component
    float maxComponent = std::max({absX, absY, absZ});
    
    // Check for clear dominance (one component significantly larger than others)
    if (absX >= maxComponent && absX > absY + dominanceThreshold && absX > absZ + dominanceThreshold)
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"[Camera] X-axis dominant: %.3f (Y:%.3f, Z:%.3f)", absX, absY, absZ);
        #endif
        return 'X'; // X-axis is dominant (looking primarily left/right)
    }
    else if (absY >= maxComponent && absY > absX + dominanceThreshold && absY > absZ + dominanceThreshold)
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"[Camera] Y-axis dominant: %.3f (X:%.3f, Z:%.3f)", absY, absX, absZ);
        #endif
        return 'Y'; // Y-axis is dominant (looking primarily up/down)
    }
    else if (absZ >= maxComponent && absZ > absX + dominanceThreshold && absZ > absY + dominanceThreshold)
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"[Camera] Z-axis dominant: %.3f (X:%.3f, Y:%.3f)", absZ, absX, absY);
        #endif
        return 'Z'; // Z-axis is dominant (looking primarily forward/backward)
    }
    else
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"[Camera] No clear dominance - diagonal view detected (max:%.3f)", maxComponent);
        #endif
        return 'D'; // Diagonal or no clear dominance
    }
}

XMFLOAT3 Camera::CalculateRotatedPosition(const XMFLOAT3& currentPos, const XMFLOAT3& pivot, float angleX, float angleY, float angleZ) const
{
    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"[Camera] CalculateRotatedPosition called: pos(%.2f, %.2f, %.2f), pivot(%.2f, %.2f, %.2f), angles(X:%.2f°, Y:%.2f°, Z:%.2f°)",
            currentPos.x, currentPos.y, currentPos.z,
            pivot.x, pivot.y, pivot.z,
            angleX, angleY, angleZ);
    #endif

    // Convert angles from degrees to radians for calculation
    float radX = XMConvertToRadians(angleX);
    float radY = XMConvertToRadians(angleY);
    float radZ = XMConvertToRadians(angleZ);

    // Calculate vector from pivot to current position (relative position)
    XMVECTOR relativePosition = XMVectorSet(
        currentPos.x - pivot.x,
        currentPos.y - pivot.y,
        currentPos.z - pivot.z,
        0.0f
    );

    // Create individual rotation matrices for each axis
    XMMATRIX rotationX = XMMatrixRotationX(radX);
    XMMATRIX rotationY = XMMatrixRotationY(radY);
    XMMATRIX rotationZ = XMMatrixRotationZ(radZ);

    // Combine rotations in XYZ order (rotate around X first, then Y, then Z)
    XMMATRIX combinedRotation = rotationX * rotationY * rotationZ;

    // Apply combined rotation to the relative position vector
    XMVECTOR rotatedRelativePosition = XMVector3Transform(relativePosition, combinedRotation);

    // Convert rotated vector back to world coordinates by adding pivot position
    XMVECTOR worldPosition = XMVectorAdd(rotatedRelativePosition, XMLoadFloat3(&pivot));

    // Convert result back to XMFLOAT3 format
    XMFLOAT3 result;
    XMStoreFloat3(&result, worldPosition);

    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"[Camera] CalculateRotatedPosition result: (%.2f, %.2f, %.2f) -> (%.2f, %.2f, %.2f)",
            currentPos.x, currentPos.y, currentPos.z,
            result.x, result.y, result.z);
    #endif

    return result;
}

// === STATUS AND UTILITY FUNCTIONS ===

bool Camera::IsJumping() const
{
    return m_isJumping;
}

float Camera::GetJumpProgress() const
{
    if (!m_isJumping || m_totalJumpTime <= 0.0f)
    {
        return 0.0f;
    }

    float progress = m_jumpAnimationTimer / m_totalJumpTime;
    return std::clamp(progress, 0.0f, 1.0f);
}

void Camera::CancelJump()
{
    if (m_isJumping)
    {
        #if defined(_DEBUG_CAMERA_)
                debug.logDebugMessage(LogLevel::LOG_INFO, L"[Camera] Jump cancelled at progress %.1f%%",
                    GetJumpProgress() * 100.0f);
        #endif

        m_isJumping = false;
        m_currentPathIndex = 0;
        m_jumpAnimationTimer = 0.0f;
        m_currentTravelPath.clear();
    }
}

const std::vector<CameraJumpHistoryEntry>& Camera::GetJumpHistory() const
{
    return m_jumpHistory;
}

void Camera::ClearJumpHistory()
{
    m_jumpHistory.clear();

    #if defined(_DEBUG_CAMERA_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[Camera] Jump history cleared");
    #endif
}

// === PRIVATE HELPER FUNCTIONS ===

void Camera::UpdateYawPitchFromDirection(const XMFLOAT3& direction)
{
    // Calculate yaw (rotation around Y-axis) from direction vector
    // Yaw is the angle between the forward direction projected onto the XZ plane
    m_yaw = FAST_ATAN2(direction.x, direction.z);

    // Calculate pitch (rotation around X-axis) from direction vector
    // Pitch is the angle between the forward direction and the XZ plane
    float horizontalLength = FAST_SQRT(direction.x * direction.x + direction.z * direction.z);
    m_pitch = FAST_ATAN2(direction.y, horizontalLength);

    // Clamp pitch to prevent gimbal lock issues
    const float maxPitch = XM_PIDIV2 - 0.01f; // Just under 90 degrees
    m_pitch = std::clamp(m_pitch, -maxPitch, maxPitch);

    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"[Camera] Updated yaw/pitch from direction(%.3f, %.3f, %.3f): yaw=%.3f, pitch=%.3f",
            direction.x, direction.y, direction.z, m_yaw, m_pitch);
    #endif
}

std::vector<XMFLOAT3> Camera::CalculateSmoothTravelPath(const XMFLOAT3& start, const XMFLOAT3& end, int pathPoints) const
{
    std::vector<XMFLOAT3> path;
    path.reserve(pathPoints);

    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"[Camera] Calculating smooth path: start(%.2f,%.2f,%.2f) -> end(%.2f,%.2f,%.2f), points=%d",
            start.x, start.y, start.z, end.x, end.y, end.z, pathPoints);
    #endif

    // Calculate control points for smooth Bezier-like curve
    // Add slight arc to the path for more natural movement
    XMFLOAT3 controlPoint1, controlPoint2;

    // Calculate midpoint
    XMFLOAT3 midpoint = {
        (start.x + end.x) * 0.5f,
        (start.y + end.y) * 0.5f,
        (start.z + end.z) * 0.5f
    };

    // Add slight elevation to midpoint for arc effect
    float distance = FAST_MATH.FastDistance(XMFLOAT2(start.x, start.z), XMFLOAT2(end.x, end.z));
    float arcHeight = distance * 0.1f; // 10% of horizontal distance

    controlPoint1 = {
        start.x + (midpoint.x - start.x) * 0.3f,
        start.y + (midpoint.y - start.y) * 0.3f + arcHeight,
        start.z + (midpoint.z - start.z) * 0.3f
    };

    controlPoint2 = {
        start.x + (midpoint.x - start.x) * 0.7f,
        start.y + (midpoint.y - start.y) * 0.7f + arcHeight,
        start.z + (midpoint.z - start.z) * 0.7f
    };

    // Generate smooth curve points using cubic Bezier interpolation
    for (int i = 0; i < pathPoints; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(pathPoints - 1);

        // Use MathPrecalculation for smooth interpolation
        float smoothT = FAST_MATH.FastSmoothStep(0.0f, 1.0f, t);

        // Cubic Bezier interpolation: B(t) = (1-t)³P₀ + 3(1-t)²tP₁ + 3(1-t)t²P₂ + t³P₃
        float invT = 1.0f - smoothT;
        float invT2 = invT * invT;
        float invT3 = invT2 * invT;
        float t2 = smoothT * smoothT;
        float t3 = t2 * smoothT;

        XMFLOAT3 point;
        point.x = invT3 * start.x + 3 * invT2 * smoothT * controlPoint1.x +
            3 * invT * t2 * controlPoint2.x + t3 * end.x;
        point.y = invT3 * start.y + 3 * invT2 * smoothT * controlPoint1.y +
            3 * invT * t2 * controlPoint2.y + t3 * end.y;
        point.z = invT3 * start.z + 3 * invT2 * smoothT * controlPoint1.z +
            3 * invT * t2 * controlPoint2.z + t3 * end.z;

        path.push_back(point);
    }

    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"[Camera] Smooth path calculated with %d points, arc height=%.2f",
            static_cast<int>(path.size()), arcHeight);
    #endif

    return path;
}

float Camera::CalculateJumpAnimationSpeed(float progress, int speed) const
{
    // Create enhanced ease-in-out animation curve for much faster movement
    // Lower speed values now result in significantly faster movement
    float speedMultiplier = 1.0f / (static_cast<float>(speed) * 0.3f); // Increased multiplier effect
    speedMultiplier = std::clamp(speedMultiplier, 0.1f, 3.0f); // Allow for very fast speeds

    // Apply enhanced ease-in-out curve using MathPrecalculation for smooth but fast animation
    float easedProgress = FAST_MATH.FastEaseInOut(0.0f, 1.0f, progress);

    // Apply speed multiplier with enhanced responsiveness
    float baseSpeed = easedProgress * speedMultiplier;
    float boostSpeed = progress * (2.0f - speedMultiplier); // Additional boost for higher speeds

    return std::clamp(baseSpeed + boostSpeed, 0.0f, 1.0f);
}

void Camera::AddToJumpHistory(const XMFLOAT3& start, const XMFLOAT3& end, const std::vector<XMFLOAT3>& path, int speed, bool focusOnTarget)
{
    // Don't add history entries for history jumps to avoid recursive history
    if (m_isJumpingBackInHistory)
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"[Camera] Skipping history entry addition during history jump");
        #endif
        return;
    }

    // Create new history entry
    CameraJumpHistoryEntry entry;
    entry.startPosition = start;
    entry.endPosition = end;
    entry.travelPath = path; // Copy the entire path
    entry.speed = speed;
    entry.focusOnTarget = focusOnTarget; // Store focus behavior
    entry.originalTarget = m_originalTarget; // Store the target that was focused on
    entry.timestamp = std::chrono::system_clock::now();

    // Calculate total distance for this jump
    entry.totalDistance = FAST_MATH.FastDistance(XMFLOAT2(start.x, start.z), XMFLOAT2(end.x, end.z)) +
        abs(start.y - end.y);

    // Add to history
    m_jumpHistory.push_back(entry);

    // Maintain maximum history size (remove oldest entries)
    while (m_jumpHistory.size() > MAX_JUMP_HISTORY)
    {
        m_jumpHistory.erase(m_jumpHistory.begin());
    }

    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"[Camera] Added jump to history: distance=%.2f, focus=%s, target(%.2f, %.2f, %.2f), total entries=%d",
            entry.totalDistance, focusOnTarget ? L"maintained" : L"free",
            entry.originalTarget.x, entry.originalTarget.y, entry.originalTarget.z,
            static_cast<int>(m_jumpHistory.size()));
    #endif
}

void Camera::JumpBackHistory(int numOfJumps)
{
    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"[Camera] JumpBackHistory called: numOfJumps=%d, current history size=%d",
            numOfJumps, static_cast<int>(m_jumpHistory.size()));
    #endif

    // Validate input parameters
    if (numOfJumps <= 0)
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_WARNING,
                L"[Camera] Invalid numOfJumps parameter: %d. Must be positive.", numOfJumps);
        #endif
        return;
    }

    // Check if we're already jumping - cannot start history jump during active jump
    if (m_isJumping)
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_WARNING,
                L"[Camera] Cannot jump back in history while camera is currently jumping");
        #endif
        return;
    }

    // Check if we have enough history entries
    if (m_jumpHistory.empty())
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_WARNING,
                L"[Camera] No jump history available to go back to");
        #endif
        return;
    }

    // Clamp numOfJumps to available history size
    int maxJumps = static_cast<int>(m_jumpHistory.size());
    int actualJumps = std::min(numOfJumps, maxJumps);

    if (actualJumps != numOfJumps)
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_WARNING,
                L"[Camera] Requested %d jumps back, but only %d entries available. Using %d",
                numOfJumps, maxJumps, actualJumps);
        #endif
    }

    // Calculate the target history entry index (from the end, going backwards)
    int targetHistoryIndex = maxJumps - actualJumps;
    const CameraJumpHistoryEntry& targetEntry = m_jumpHistory[targetHistoryIndex];

    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"[Camera] Jumping back to history entry %d: pos(%.2f, %.2f, %.2f), focus=%s",
            targetHistoryIndex,
            targetEntry.startPosition.x, targetEntry.startPosition.y, targetEntry.startPosition.z,
            targetEntry.focusOnTarget ? L"maintained" : L"free");
    #endif

    // Set up history jump tracking
    m_isJumpingBackInHistory = true;
    m_historyJumpStepsRemaining = actualJumps;

    // Store the current position as a temporary history entry for potential future forward navigation
    // This allows users to potentially implement a "forward" function later
    XMFLOAT3 currentPos = position;
    XMFLOAT3 currentTgt = target;

    // Use the target entry's original settings for the jump back
    // Jump to the START position of the target history entry (where we were before that jump)
    bool useFocus = targetEntry.focusOnTarget;
    int useSpeed = std::max(1, targetEntry.speed); // Ensure valid speed

    // If the target entry had focus, restore the original target from that entry
    if (useFocus && targetEntry.originalTarget.x != 0.0f || targetEntry.originalTarget.y != 0.0f || targetEntry.originalTarget.z != 0.0f)
    {
        // Set the target to what it was during that historical jump
        target = targetEntry.originalTarget;
        m_originalTarget = targetEntry.originalTarget;

        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"[Camera] Restoring historical target: (%.2f, %.2f, %.2f)",
                targetEntry.originalTarget.x, targetEntry.originalTarget.y, targetEntry.originalTarget.z);
        #endif
    }

    // Initiate the jump back to the historical position
    JumpTo(targetEntry.startPosition.x, targetEntry.startPosition.y, targetEntry.startPosition.z, useSpeed, useFocus);

    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"[Camera] History jump initiated: going back %d steps, target entry timestamp: %lld",
            actualJumps,
            std::chrono::duration_cast<std::chrono::milliseconds>(targetEntry.timestamp.time_since_epoch()).count());
    #endif
}

void Camera::RemoveForwardHistoryEntries(int fromIndex)
{
    // Validate the fromIndex parameter
    if (fromIndex < 0 || fromIndex >= static_cast<int>(m_jumpHistory.size()))
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_WARNING,
                L"[Camera] Invalid fromIndex for RemoveForwardHistoryEntries: %d (history size: %d)",
                fromIndex, static_cast<int>(m_jumpHistory.size()));
        #endif
        return;
    }

    // Calculate how many entries to remove
    int entriesToRemove = static_cast<int>(m_jumpHistory.size()) - fromIndex;

    if (entriesToRemove > 0)
    {
        // Remove entries from fromIndex to the end
        m_jumpHistory.erase(m_jumpHistory.begin() + fromIndex, m_jumpHistory.end());

        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"[Camera] Removed %d forward history entries from index %d. Remaining entries: %d",
                entriesToRemove, fromIndex, static_cast<int>(m_jumpHistory.size()));
        #endif
    }
}

int Camera::GetJumpHistoryCount() const
{
    return static_cast<int>(m_jumpHistory.size());
}

// === EXISTING CAMERA FUNCTIONS (MAINTAINED) ===

void Camera::SetLookDirection(XMVECTOR newForward, XMVECTOR newUp)
{
    XMStoreFloat3(&forward, XMVector3Normalize(newForward));
    XMStoreFloat3(&up, XMVector3Normalize(newUp));

    XMVECTOR pos = XMLoadFloat3(&position);
    XMVECTOR tgt = XMVectorAdd(pos, newForward);
    XMStoreFloat3(&target, tgt);

    viewMatrix = XMMatrixLookAtLH(pos, tgt, newUp);

    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[CAMERA]: SetLookDirection() applied");
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"  Forward: (%.2f, %.2f, %.2f)  Up: (%.2f, %.2f, %.2f)  Target: (%.2f, %.2f, %.2f)",
            forward.x, forward.y, forward.z,
            up.x, up.y, up.z,
            target.x, target.y, target.z);
    #endif
}

void Camera::SetYawPitch(float newYaw, float newPitch)
{
    m_yaw = newYaw;
    m_pitch = newPitch;

    // Compute forward vector from yaw and pitch


    XMVECTOR fwd = XMVectorSet(
        FAST_COS(m_pitch) * FAST_SIN(m_yaw),
        FAST_SIN(m_pitch),
        FAST_COS(m_pitch) * FAST_COS(m_yaw),
        0.0f
    );
    fwd = XMVector3Normalize(fwd);
    XMStoreFloat3(&forward, fwd);

    // World up
    up = XMFLOAT3(0.0f, 1.0f, 0.0f);

    // Compute target = position + forward
    XMVECTOR pos = XMLoadFloat3(&position);
    XMVECTOR tgt = XMVectorAdd(pos, fwd);
    XMStoreFloat3(&target, tgt);

    // Update view matrix
    XMVECTOR upVec = XMLoadFloat3(&up);
    viewMatrix = XMMatrixLookAtLH(pos, tgt, upVec);

    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"[CAMERA]: SetYawPitch() → Yaw: %.3f Pitch: %.3f", m_yaw, m_pitch);
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"  Eye: (%.2f, %.2f, %.2f)  Forward: (%.2f, %.2f, %.2f)  Target: (%.2f, %.2f, %.2f)",
            position.x, position.y, position.z,
            forward.x, forward.y, forward.z,
            target.x, target.y, target.z);
    #endif
}

void Camera::MoveUp(float distance) {
    position.y += distance;
    UpdateViewMatrix();
}

void Camera::MoveDown(float distance) {
    position.y -= distance;
    UpdateViewMatrix();
}

void Camera::MoveRight(float distance) {
    position.x += distance;
    UpdateViewMatrix();
}

void Camera::MoveLeft(float distance) {
    position.x -= distance;
    UpdateViewMatrix();
}

void Camera::MoveIn(float distance)
{
    XMVECTOR fwd = XMLoadFloat3(&forward);
    XMVECTOR pos = XMLoadFloat3(&position);
    pos = XMVectorAdd(pos, XMVectorScale(fwd, distance));

    XMStoreFloat3(&position, pos);
    target = { position.x + forward.x, position.y + forward.y, position.z + forward.z };
    UpdateViewMatrix();
}

void Camera::MoveOut(float distance)
{
    XMVECTOR fwd = XMLoadFloat3(&forward);
    XMVECTOR pos = XMLoadFloat3(&position);
    pos = XMVectorSubtract(pos, XMVectorScale(fwd, distance));

    XMStoreFloat3(&position, pos);
    target = { position.x + forward.x, position.y + forward.y, position.z + forward.z };
    UpdateViewMatrix();
}

void Camera::SetPosition(float x, float y, float z)
{
    position = XMFLOAT3(x, y, z);
    UpdateViewMatrix();  // uses existing target + up
}

XMFLOAT3 Camera::GetPosition() const {
    return position;
}

void Camera::SetTarget(const XMFLOAT3& newTarget)
{
    target = newTarget;

    XMVECTOR eye = XMLoadFloat3(&position);
    XMVECTOR tgt = XMLoadFloat3(&target);
    XMVECTOR upVec = XMLoadFloat3(&up);

    viewMatrix = XMMatrixLookAtLH(eye, tgt, upVec);

    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"[CAMERA] SetTarget(): Eye(%.2f, %.2f, %.2f) → Target(%.2f, %.2f, %.2f)",
            position.x, position.y, position.z, target.x, target.y, target.z);
    #endif
}

void Camera::SetNearFar(float nearPlane, float farPlane)
{
    float aspect = static_cast<float>(config.myConfig.aspectRatio);
    float fovY = 2.0f * atanf(FAST_TAN(static_cast<float>(config.myConfig.fov) * 0.5f * XM_PI / 180.0f) / aspect);

    projectionMatrix = XMMatrixPerspectiveFovLH(fovY, aspect, nearPlane, farPlane);

    config.myConfig.nearPlane = nearPlane;
    config.myConfig.farPlane = farPlane;

    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"[CAMERA] Projection updated with near=%.3f, far=%.3f, fovY=%.2f",
            nearPlane, farPlane, fovY);
    #endif
}

XMMATRIX Camera::GetViewMatrix() const {
    return viewMatrix;
}

XMMATRIX Camera::GetProjectionMatrix() const {
    return projectionMatrix;
}

void Camera::SetViewMatrix(const XMMATRIX& matrix)
{
    viewMatrix = matrix;

    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[CAMERA]: SetViewMatrix() - Eye(%.2f, %.2f, %.2f), Forward(%.2f, %.2f, %.2f)",
            position.x, position.y, position.z, forward.x, forward.y, forward.z);
    #endif
}

//==============================================================================
// Updated Camera View/Projection Setup to Center on Cube Cluster
//==============================================================================
void Camera::SetupDefaultCamera(float windowWidth, float windowHeight)
{
    // Center of cube cluster (cube1, cube2, cube3 placed along X-axis at 0, 4, 8)
    XMVECTOR eye = XMVectorSet(4.0f, 0.0f, -15.0f, 0.0f);     // Back up from the cluster
    XMVECTOR lookAt = XMVectorSet(0.0f, 0.01f, 0.0f, 0.0f);   // Look at center of cluster
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX view = XMMatrixLookAtLH(eye, lookAt, up);
    SetViewMatrix(view);

    float fovX_deg = config.myConfig.fov;
    float aspect = windowWidth / windowHeight;

    // Convert FOV-X to radians
    float fovX_rad = XMConvertToRadians(fovX_deg);

    // Compute vertical FOV
    float fovY_rad = 2.0f * atanf(tanf(fovX_rad / 2.0f) / aspect);

    // Set the Projection Matrix
    SetProjectionMatrix(XMMatrixPerspectiveFovLH(fovY_rad, aspect, config.myConfig.nearPlane, config.myConfig.farPlane));

    XMFLOAT3 eyePos, lookPos;
    XMStoreFloat3(&eyePos, eye);
    XMStoreFloat3(&lookPos, lookAt);

    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Camera eye: (%.2f, %.2f, %.2f) → lookAt: (%.2f, %.2f, %.2f)",
            eyePos.x, eyePos.y, eyePos.z,
            lookPos.x, lookPos.y, lookPos.z);
    #endif
}

void Camera::SetProjectionMatrix(const XMMATRIX& matrix) {
    projectionMatrix = matrix;

    #if defined(_DEBUG_CAMERA_)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[CAMERA]: Projection matrix updated.");
    #endif
}

void Camera::UpdateViewMatrix()
{
    // --- Calculate new forward from yaw and pitch ---
    float cosPitch = FAST_COS(m_pitch);
    float sinPitch = FAST_SIN(m_pitch);
    float cosYaw = FAST_COS(m_yaw);
    float sinYaw = FAST_SIN(m_yaw);

    XMVECTOR fwd = XMVectorSet(
        cosPitch * sinYaw,
        sinPitch,
        cosPitch * cosYaw,
        0.0f
    );

    XMStoreFloat3(&forward, fwd);

    // --- Update target from forward vector ---
    XMVECTOR pos = XMLoadFloat3(&position);
    XMVECTOR tgt = XMVectorAdd(pos, fwd);
    XMStoreFloat3(&target, tgt);

    // --- Compute view matrix from position/target/up
    XMVECTOR upVec = XMLoadFloat3(&up);
    viewMatrix = XMMatrixLookAtLH(pos, tgt, upVec);

    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[Camera] View updated. Pos(%.2f, %.2f, %.2f), Fwd(%.2f, %.2f, %.2f), Yaw=%.2f, Pitch=%.2f",
            position.x, position.y, position.z,
            forward.x, forward.y, forward.z,
            m_yaw, m_pitch);
    #endif

#if defined(_DEBUG_CAMERA_) && defined(_DEBUG)
    {
        XMMATRIX view = viewMatrix;
        XMVECTOR camPos = XMLoadFloat3(&position);

        debug.logDebugMessage(LogLevel::LOG_INFO, L"[CAMERA] View Matrix Updated:");
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[VIEW] [%.2f %.2f %.2f %.2f]", view.r[0].m128_f32[0], view.r[0].m128_f32[1], view.r[0].m128_f32[2], view.r[0].m128_f32[3]);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[VIEW] [%.2f %.2f %.2f %.2f]", view.r[1].m128_f32[0], view.r[1].m128_f32[1], view.r[1].m128_f32[2], view.r[1].m128_f32[3]);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[VIEW] [%.2f %.2f %.2f %.2f]", view.r[2].m128_f32[0], view.r[2].m128_f32[1], view.r[2].m128_f32[2], view.r[2].m128_f32[3]);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[VIEW] [%.2f %.2f %.2f %.2f]", view.r[3].m128_f32[0], view.r[3].m128_f32[1], view.r[3].m128_f32[2], view.r[3].m128_f32[3]);

        debug.logDebugMessage(LogLevel::LOG_INFO, L"[CAMERA] Position: %.2f %.2f %.2f", camPos.m128_f32[0], camPos.m128_f32[1], camPos.m128_f32[2]);
    }
#endif

}

void Camera::SetYawPitchFromForward()
{
    XMVECTOR fwd = XMLoadFloat3(&forward);
    m_pitch = asinf(fwd.m128_f32[1]);                   // y = sin(pitch)
    m_yaw = atan2f(fwd.m128_f32[0], fwd.m128_f32[2]);   // x and z
    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"[CAMERA]: YawPitch initialized from forward vector → Yaw: %.3f, Pitch: %.3f",
            m_yaw, m_pitch);
    #endif
}

void Camera::MoveAroundTarget(bool x, bool y, bool z, bool continuous)
{
    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"[Camera] MoveAroundTarget called: X=%s, Y=%s, Z=%s, continuous=%s", 
            x ? L"true" : L"false", y ? L"true" : L"false", z ? L"true" : L"false", 
            continuous ? L"true" : L"false");
    #endif

    // Check if at least one axis is selected for rotation
    if (!x && !y && !z)
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, 
                L"[Camera] MoveAroundTarget: No rotation axes selected (X, Y, Z all false)");
        #endif
        return;
    }

    // Check if we're currently jumping - cannot start rotation during active jump
    if (m_isJumping)
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, 
                L"[Camera] Cannot start MoveAroundTarget while camera is currently jumping");
        #endif
        return;
    }

    // Stop any existing rotation before starting new one
    if (m_isRotatingAroundTarget)
    {
        StopRotating();
        
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"[Camera] Stopped existing rotation to start new MoveAroundTarget");
        #endif
    }

    // Store current state for rotation calculations
    m_rotationStartPosition = position;
    m_rotationTarget = target;
    
    // Calculate distance from camera to target for maintaining orbit radius
    XMVECTOR currentPos = XMLoadFloat3(&position);
    XMVECTOR currentTarget = XMLoadFloat3(&target);
    XMVECTOR distanceVector = currentPos - currentTarget;
    m_rotationDistance = XMVectorGetX(XMVector3Length(distanceVector));
    
    // If distance is too small, use a default distance to prevent issues
    if (m_rotationDistance < 0.1f)
    {
        m_rotationDistance = 5.0f; // Default orbit radius
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"[Camera] Using default rotation distance: %.2f", m_rotationDistance);
        #endif
    }

    // Set up rotation parameters
    m_isRotatingAroundTarget = true;
    m_continuousRotation = continuous;
    m_rotateAroundX = x;
    m_rotateAroundY = y;
    m_rotateAroundZ = z;
    
    // Reset current rotation angles
    m_currentRotationX = 0.0f;
    m_currentRotationY = 0.0f;
    m_currentRotationZ = 0.0f;
    
    // Set target rotations - 360 degrees for full rotation on each selected axis
    m_targetRotationX = x ? 360.0f : 0.0f;
    m_targetRotationY = y ? 360.0f : 0.0f;
    m_targetRotationZ = z ? 360.0f : 0.0f;
    
    // Adjust rotation speeds based on number of active axes for balanced movement
    int activeAxes = (x ? 1 : 0) + (y ? 1 : 0) + (z ? 1 : 0);
    float baseSpeed = 60.0f; // Base speed in degrees per second
    
    // For multiple axes, reduce individual speeds to prevent overly fast combined rotation
    float speedMultiplier = 1.0f;
    if (activeAxes > 1)
    {
        speedMultiplier = 0.7f; // Reduce speed for multi-axis rotations
    }
    
    m_rotationSpeedX = x ? baseSpeed * speedMultiplier : 0.0f;
    m_rotationSpeedY = y ? baseSpeed * speedMultiplier : 0.0f;
    m_rotationSpeedZ = z ? baseSpeed * speedMultiplier : 0.0f;

    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"[Camera] MoveAroundTarget started: distance=%.2f, active axes=%d, speeds(X:%.1f, Y:%.1f, Z:%.1f)", 
            m_rotationDistance, activeAxes, m_rotationSpeedX, m_rotationSpeedY, m_rotationSpeedZ);
        debug.logDebugMessage(LogLevel::LOG_DEBUG, 
            L"[Camera] Rotation target: (%.2f, %.2f, %.2f), start position: (%.2f, %.2f, %.2f)", 
            m_rotationTarget.x, m_rotationTarget.y, m_rotationTarget.z,
            m_rotationStartPosition.x, m_rotationStartPosition.y, m_rotationStartPosition.z);
    #endif
}

void Camera::MoveAroundTarget(bool x, bool y, bool z, float rotationSpeed, bool continuous)
{
    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"[Camera] MoveAroundTarget (with speed) called: X=%s, Y=%s, Z=%s, speed=%.1f, continuous=%s", 
            x ? L"true" : L"false", y ? L"true" : L"false", z ? L"true" : L"false", 
            rotationSpeed, continuous ? L"true" : L"false");
    #endif

    // Validate rotation speed parameter
    if (rotationSpeed <= 0.0f)
    {
        rotationSpeed = 60.0f; // Default speed in degrees per second
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, 
                L"[Camera] Invalid rotation speed, defaulting to %.1f degrees/second", rotationSpeed);
        #endif
    }

    // Check if at least one axis is selected for rotation
    if (!x && !y && !z)
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, 
                L"[Camera] MoveAroundTarget: No rotation axes selected (X, Y, Z all false)");
        #endif
        return;
    }

    // Check if we're currently jumping - cannot start rotation during active jump
    if (m_isJumping)
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, 
                L"[Camera] Cannot start MoveAroundTarget while camera is currently jumping");
        #endif
        return;
    }

    // Stop any existing rotation before starting new one
    if (m_isRotatingAroundTarget)
    {
        StopRotating();
        
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"[Camera] Stopped existing rotation to start new MoveAroundTarget with custom speed");
        #endif
    }

    // Store current state for rotation calculations
    m_rotationStartPosition = position;
    m_rotationTarget = target;
    
    // Calculate distance from camera to target for maintaining orbit radius
    XMVECTOR currentPos = XMLoadFloat3(&position);
    XMVECTOR currentTarget = XMLoadFloat3(&target);
    XMVECTOR distanceVector = currentPos - currentTarget;
    m_rotationDistance = XMVectorGetX(XMVector3Length(distanceVector));
    
    // If distance is too small, use a default distance to prevent issues
    if (m_rotationDistance < 0.1f)
    {
        m_rotationDistance = 5.0f; // Default orbit radius
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"[Camera] Using default rotation distance: %.2f", m_rotationDistance);
        #endif
    }

    // Set up rotation parameters
    m_isRotatingAroundTarget = true;
    m_continuousRotation = continuous;
    m_rotateAroundX = x;
    m_rotateAroundY = y;
    m_rotateAroundZ = z;
    
    // Reset current rotation angles
    m_currentRotationX = 0.0f;
    m_currentRotationY = 0.0f;
    m_currentRotationZ = 0.0f;
    
    // Set target rotations - 360 degrees for full rotation on each selected axis
    m_targetRotationX = x ? 360.0f : 0.0f;
    m_targetRotationY = y ? 360.0f : 0.0f;
    m_targetRotationZ = z ? 360.0f : 0.0f;
    
    // Set custom rotation speeds for each active axis
    m_rotationSpeedX = x ? rotationSpeed : 0.0f;
    m_rotationSpeedY = y ? rotationSpeed : 0.0f;
    m_rotationSpeedZ = z ? rotationSpeed : 0.0f;

    #if defined(_DEBUG_CAMERA_)
        int activeAxes = (x ? 1 : 0) + (y ? 1 : 0) + (z ? 1 : 0);
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"[Camera] MoveAroundTarget started with custom speed: distance=%.2f, active axes=%d, speed=%.1f°/s", 
            m_rotationDistance, activeAxes, rotationSpeed);
        debug.logDebugMessage(LogLevel::LOG_DEBUG, 
            L"[Camera] Rotation target: (%.2f, %.2f, %.2f), start position: (%.2f, %.2f, %.2f)", 
            m_rotationTarget.x, m_rotationTarget.y, m_rotationTarget.z,
            m_rotationStartPosition.x, m_rotationStartPosition.y, m_rotationStartPosition.z);
    #endif
}

void Camera::StopRotating()
{
    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"[Camera] StopRotating called - current rotation state: %s", 
            m_isRotatingAroundTarget ? L"active" : L"inactive");
    #endif

    // Check if we're actually rotating
    if (!m_isRotatingAroundTarget)
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"[Camera] StopRotating: No active rotation to stop");
        #endif
        return;
    }

    // Log current rotation progress before stopping
    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"[Camera] Stopping rotation - progress: X=%.1f°/%.1f°, Y=%.1f°/%.1f°, Z=%.1f°/%.1f°", 
            m_currentRotationX, m_targetRotationX,
            m_currentRotationY, m_targetRotationY,
            m_currentRotationZ, m_targetRotationZ);
    #endif

    // Stop the rotation by clearing all rotation flags and variables
    m_isRotatingAroundTarget = false;
    m_continuousRotation = false;
    m_rotateAroundX = false;
    m_rotateAroundY = false;
    m_rotateAroundZ = false;
    
    // Clear rotation speeds
    m_rotationSpeedX = 0.0f;
    m_rotationSpeedY = 0.0f;
    m_rotationSpeedZ = 0.0f;
    
    // Clear current rotation progress
    m_currentRotationX = 0.0f;
    m_currentRotationY = 0.0f;
    m_currentRotationZ = 0.0f;
    
    // Clear target rotations
    m_targetRotationX = 0.0f;
    m_targetRotationY = 0.0f;
    m_targetRotationZ = 0.0f;
    
    // Update view matrix to ensure proper final positioning
    UpdateViewMatrix();

    #if defined(_DEBUG_CAMERA_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[Camera] Rotation stopped successfully");
    #endif
}

bool Camera::IsRotatingAroundTarget() const
{
    return m_isRotatingAroundTarget;
}

float Camera::GetRotationProgress() const
{
    if (!m_isRotatingAroundTarget)
    {
        return 0.0f;
    }

    // Calculate overall progress based on active axes
    float totalProgress = 0.0f;
    int activeAxes = 0;

    if (m_rotateAroundX && m_targetRotationX > 0.0f)
    {
        totalProgress += (m_currentRotationX / m_targetRotationX);
        activeAxes++;
    }

    if (m_rotateAroundY && m_targetRotationY > 0.0f)
    {
        totalProgress += (m_currentRotationY / m_targetRotationY);
        activeAxes++;
    }

    if (m_rotateAroundZ && m_targetRotationZ > 0.0f)
    {
        totalProgress += (m_currentRotationZ / m_targetRotationZ);
        activeAxes++;
    }

    if (activeAxes > 0)
    {
        totalProgress /= activeAxes; // Average progress across active axes
    }

    return std::clamp(totalProgress, 0.0f, 1.0f);
}

XMFLOAT3 Camera::CalculateRotationPosition(float angleX, float angleY, float angleZ) const
{
    // Start with the original relative position (camera position relative to target)
    XMVECTOR originalPos = XMLoadFloat3(&m_rotationStartPosition);
    XMVECTOR targetPos = XMLoadFloat3(&m_rotationTarget);
    XMVECTOR relativePosition = originalPos - targetPos;

    // Normalize to the rotation distance to maintain consistent orbit radius
    relativePosition = XMVector3Normalize(relativePosition) * m_rotationDistance;

    // Convert angles to radians
    float radX = XMConvertToRadians(angleX);
    float radY = XMConvertToRadians(angleY);
    float radZ = XMConvertToRadians(angleZ);

    // Create rotation matrices for each axis
    XMMATRIX rotationX = XMMatrixRotationX(radX);
    XMMATRIX rotationY = XMMatrixRotationY(radY);
    XMMATRIX rotationZ = XMMatrixRotationZ(radZ);

    // Combine rotations in XYZ order
    XMMATRIX combinedRotation = rotationX * rotationY * rotationZ;

    // Apply rotation to the relative position
    XMVECTOR rotatedPosition = XMVector3Transform(relativePosition, combinedRotation);

    // Convert back to world coordinates by adding the target position
    XMVECTOR worldPosition = rotatedPosition + targetPos;

    // Convert result to XMFLOAT3
    XMFLOAT3 result;
    XMStoreFloat3(&result, worldPosition);

    return result;
}

void Camera::UpdateContinuousRotation()
{
    // Only process if we're currently rotating around target
    if (!m_isRotatingAroundTarget)
    {
        return;
    }

    // Calculate delta time - using fixed time step for now (should be replaced with actual delta time)
    float deltaTime = 1.0f / 60.0f; // Assuming 60 FPS
    
    // Update rotation angles based on speed and delta time
    bool rotationComplete = true;
    
    if (m_rotateAroundX)
    {
        m_currentRotationX += m_rotationSpeedX * deltaTime;
        
        // Check if X rotation is complete (for non-continuous rotation)
        if (!m_continuousRotation && m_currentRotationX >= m_targetRotationX)
        {
            m_currentRotationX = m_targetRotationX; // Clamp to exact target
        }
        else if (m_continuousRotation)
        {
            // For continuous rotation, wrap around after 360 degrees
            if (m_currentRotationX >= 360.0f)
            {
                m_currentRotationX -= 360.0f;
            }
            rotationComplete = false; // Continuous rotation never completes
        }
        else
        {
            rotationComplete = false; // Still rotating towards target
        }
    }
    
    if (m_rotateAroundY)
    {
        m_currentRotationY += m_rotationSpeedY * deltaTime;
        
        // Check if Y rotation is complete (for non-continuous rotation)
        if (!m_continuousRotation && m_currentRotationY >= m_targetRotationY)
        {
            m_currentRotationY = m_targetRotationY; // Clamp to exact target
        }
        else if (m_continuousRotation)
        {
            // For continuous rotation, wrap around after 360 degrees
            if (m_currentRotationY >= 360.0f)
            {
                m_currentRotationY -= 360.0f;
            }
            rotationComplete = false; // Continuous rotation never completes
        }
        else
        {
            rotationComplete = false; // Still rotating towards target
        }
    }
    
    if (m_rotateAroundZ)
    {
        m_currentRotationZ += m_rotationSpeedZ * deltaTime;
        
        // Check if Z rotation is complete (for non-continuous rotation)
        if (!m_continuousRotation && m_currentRotationZ >= m_targetRotationZ)
        {
            m_currentRotationZ = m_targetRotationZ; // Clamp to exact target
        }
        else if (m_continuousRotation)
        {
            // For continuous rotation, wrap around after 360 degrees
            if (m_currentRotationZ >= 360.0f)
            {
                m_currentRotationZ -= 360.0f;
            }
            rotationComplete = false; // Continuous rotation never completes
        }
        else
        {
            rotationComplete = false; // Still rotating towards target
        }
    }

    // Calculate new camera position based on current rotation angles
    XMFLOAT3 newPosition = CalculateRotationPosition(m_currentRotationX, m_currentRotationY, m_currentRotationZ);
    
    // Update camera position
    position = newPosition;
    
    // Maintain focus on the target during rotation
    XMVECTOR currentPos = XMLoadFloat3(&position);
    XMVECTOR targetPos = XMLoadFloat3(&m_rotationTarget);
    XMVECTOR upVec = XMLoadFloat3(&up);
    
    // Update view matrix to maintain focus on target
    viewMatrix = XMMatrixLookAtLH(currentPos, targetPos, upVec);
    
    // Update forward direction for consistency
    XMVECTOR focusDirection = XMVector3Normalize(targetPos - currentPos);
    XMStoreFloat3(&forward, focusDirection);
    
    // Update yaw and pitch to match new orientation
    float newYaw = FAST_ATAN2(XMVectorGetX(focusDirection), XMVectorGetZ(focusDirection));
    float forwardLength = FAST_SQRT(XMVectorGetX(focusDirection) * XMVectorGetX(focusDirection) + 
                                  XMVectorGetZ(focusDirection) * XMVectorGetZ(focusDirection));
    float newPitch = FAST_ATAN2(XMVectorGetY(focusDirection), forwardLength);
    
    m_yaw = newYaw;
    m_pitch = newPitch;

    // Check if non-continuous rotation is complete
    if (!m_continuousRotation && rotationComplete)
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"[Camera] MoveAroundTarget completed: Final rotations X=%.1f°, Y=%.1f°, Z=%.1f°", 
                m_currentRotationX, m_currentRotationY, m_currentRotationZ);
        #endif
        
        // Stop rotation when complete
        StopRotating();
    }

    #if defined(_DEBUG_CAMERA_)
        // Log rotation progress every 60 degrees (1 second at 60 degrees/second)
        static float lastLoggedRotationY = 0.0f;
        if (m_rotateAroundY && (m_currentRotationY - lastLoggedRotationY) >= 60.0f)
        {
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"[Camera] Rotation progress: X=%.1f°, Y=%.1f°, Z=%.1f°, position(%.2f, %.2f, %.2f)", 
                m_currentRotationX, m_currentRotationY, m_currentRotationZ,
                position.x, position.y, position.z);
            lastLoggedRotationY = m_currentRotationY;
        }
    #endif
}

void Camera::PauseRotation()
{
    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"[Camera] PauseRotation called - current rotation state: %s", 
            m_isRotatingAroundTarget ? L"active" : L"inactive");
    #endif

    if (!m_isRotatingAroundTarget)
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"[Camera] PauseRotation: No active rotation to pause");
        #endif
        return;
    }

    // Temporarily store current speeds
    static float pausedSpeedX = 0.0f;
    static float pausedSpeedY = 0.0f;
    static float pausedSpeedZ = 0.0f;
    
    // Store current speeds for later resume
    pausedSpeedX = m_rotationSpeedX;
    pausedSpeedY = m_rotationSpeedY;
    pausedSpeedZ = m_rotationSpeedZ;
    
    // Set speeds to zero to pause rotation
    m_rotationSpeedX = 0.0f;
    m_rotationSpeedY = 0.0f;
    m_rotationSpeedZ = 0.0f;

    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"[Camera] Rotation paused - stored speeds: X=%.1f, Y=%.1f, Z=%.1f", 
            pausedSpeedX, pausedSpeedY, pausedSpeedZ);
    #endif
}

void Camera::ResumeRotation()
{
    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"[Camera] ResumeRotation called");
    #endif

    if (!m_isRotatingAroundTarget)
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"[Camera] ResumeRotation: No rotation to resume");
        #endif
        return;
    }

    // Restore previously stored speeds
    static float pausedSpeedX = 60.0f; // Default fallback values
    static float pausedSpeedY = 60.0f;
    static float pausedSpeedZ = 60.0f;
    
    // Restore rotation speeds for active axes
    m_rotationSpeedX = m_rotateAroundX ? pausedSpeedX : 0.0f;
    m_rotationSpeedY = m_rotateAroundY ? pausedSpeedY : 0.0f;
    m_rotationSpeedZ = m_rotateAroundZ ? pausedSpeedZ : 0.0f;

    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"[Camera] Rotation resumed - restored speeds: X=%.1f, Y=%.1f, Z=%.1f", 
            m_rotationSpeedX, m_rotationSpeedY, m_rotationSpeedZ);
    #endif
}

void Camera::SetRotationSpeed(float degreesPerSecond)
{
    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"[Camera] SetRotationSpeed called: %.1f degrees/second", degreesPerSecond);
    #endif

    // Validate speed parameter
    if (degreesPerSecond < 0.0f)
    {
        degreesPerSecond = 0.0f; // Clamp to minimum zero
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, 
                L"[Camera] Negative rotation speed clamped to 0.0");
        #endif
    }

    if (!m_isRotatingAroundTarget)
    {
        #if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"[Camera] SetRotationSpeed: No active rotation, storing for future use");
        #endif
        return;
    }

    // Update rotation speeds for all active axes
    if (m_rotateAroundX)
    {
        m_rotationSpeedX = degreesPerSecond;
    }
    
    if (m_rotateAroundY)
    {
        m_rotationSpeedY = degreesPerSecond;
    }
    
    if (m_rotateAroundZ)
    {
        m_rotationSpeedZ = degreesPerSecond;
    }

    #if defined(_DEBUG_CAMERA_)
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"[Camera] Rotation speed updated - active speeds: X=%.1f, Y=%.1f, Z=%.1f", 
            m_rotationSpeedX, m_rotationSpeedY, m_rotationSpeedZ);
    #endif
}

XMFLOAT3 Camera::GetCurrentRotationAngles() const
{
    return XMFLOAT3(m_currentRotationX, m_currentRotationY, m_currentRotationZ);
}

XMFLOAT3 Camera::GetRotationSpeeds() const
{
    return XMFLOAT3(m_rotationSpeedX, m_rotationSpeedY, m_rotationSpeedZ);
}

bool Camera::IsRotationPaused() const
{
    return m_isRotatingAroundTarget &&
        (m_rotationSpeedX == 0.0f && m_rotationSpeedY == 0.0f && m_rotationSpeedZ == 0.0f) &&
        (m_rotateAroundX || m_rotateAroundY || m_rotateAroundZ);
}

float Camera::GetEstimatedTimeToComplete() const
{
    if (!m_isRotatingAroundTarget || m_continuousRotation)
    {
        return -1.0f; // Infinite or not rotating
    }

    float maxTime = 0.0f;

    if (m_rotateAroundX && m_rotationSpeedX > 0.0f)
    {
        float remainingX = m_targetRotationX - m_currentRotationX;
        float timeX = remainingX / m_rotationSpeedX;
        maxTime = std::max(maxTime, timeX);
    }

    if (m_rotateAroundY && m_rotationSpeedY > 0.0f)
    {
        float remainingY = m_targetRotationY - m_currentRotationY;
        float timeY = remainingY / m_rotationSpeedY;
        maxTime = std::max(maxTime, timeY);
    }

    if (m_rotateAroundZ && m_rotationSpeedZ > 0.0f)
    {
        float remainingZ = m_targetRotationZ - m_currentRotationZ;
        float timeZ = remainingZ / m_rotationSpeedZ;
        maxTime = std::max(maxTime, timeZ);
    }

    return maxTime;
}

#endif // End of #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
