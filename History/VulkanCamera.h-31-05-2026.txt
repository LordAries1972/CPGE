#pragma once

//-------------------------------------------------------------------------------------------------
// VulkanCamera.h - GLM-based camera class for Vulkan rendering
//
// Key differences from OpenGLCamera:
//   - Projection matrix Y-axis is flipped to match Vulkan NDC (Y points down)
//   - Depth range is [0, 1] (Vulkan convention)
//-------------------------------------------------------------------------------------------------

#include "Includes.h"

#ifdef __USE_VULKAN__

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Forward declaration for MathPrecalculation
class MathPrecalculation;

// Structure to store camera jump history
struct CameraJumpHistoryEntry {
    glm::vec3 startPosition;                                      // Starting position of the jump
    glm::vec3 endPosition;                                        // Ending position of the jump
    std::vector<glm::vec3> travelPath;                            // Calculated smooth travel path
    float totalDistance;                                          // Total distance of the jump
    int speed;                                                    // Speed setting used for this jump
    bool focusOnTarget;                                           // Whether focus was maintained during jump
    glm::vec3 originalTarget;                                     // The target that was focused on during this jump
    std::chrono::system_clock::time_point timestamp;              // When this jump occurred
};

// Camera state preservation structure for resize operations
struct CameraResizeState {
    glm::vec3 position;                                           // Camera world position
    glm::vec3 target;                                             // Camera look-at target
    glm::vec3 up;                                                 // Camera up vector
    float yaw;                                                    // Camera yaw rotation
    float pitch;                                                  // Camera pitch rotation
    float fieldOfView;                                            // Camera field of view
    float nearPlane;                                              // Camera near clipping plane
    float farPlane;                                               // Camera far clipping plane
    bool isValid;                                                 // Whether this state is valid

    CameraResizeState() :
        position(0.0f, 0.0f, 0.0f),
        target(0.0f, 0.0f, 1.0f),
        up(0.0f, 1.0f, 0.0f),
        yaw(0.0f),
        pitch(0.0f),
        fieldOfView(45.0f),
        nearPlane(0.1f),
        farPlane(1000.0f),
        isValid(false)
    {
    }
};

// Global camera state preservation instance
static CameraResizeState savedCameraState;

class Camera {
public:
    Camera();
    ~Camera();

    glm::vec3 forward;
    glm::vec3 position;
    glm::vec3 target;
    glm::vec3 up;
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
    glm::mat4 worldMatrix;
    float m_yaw   = 0.0f;
    float m_pitch = 0.0f;

    // Existing movement functions
    void MoveUp(float distance);
    void MoveDown(float distance);
    void MoveRight(float distance);
    void MoveLeft(float distance);
    void MoveIn(float distance);
    void MoveOut(float distance);
    void SetPosition(float x, float y, float z);
    glm::vec3  GetPosition() const;
    glm::mat4  GetViewMatrix() const;
    glm::mat4  GetProjectionMatrix() const;
    void SetProjectionMatrix(const glm::mat4& matrix);
    void SetViewMatrix(const glm::mat4& matrix);
    void UpdateViewMatrix();
    void UpdateProjectionMatrix();
    void SetLookDirection(glm::vec3 forward, glm::vec3 up);
    void SetupDefaultCamera(float windowWidth, float windowHeight);
    void SetYawPitch(float newYaw, float newPitch);
    void SetYawPitchFromForward();
    void SetTarget(const glm::vec3& newTarget);
    void SetNearFar(float nearPlane, float farPlane);

    // Field of View, Up Vector, and Clipping Plane Management Functions
    float     GetFieldOfView() const;
    glm::vec3 GetUpVector() const;
    float     GetFarPlane() const;
    float     GetNearPlane() const;
    void SetFieldOfView(float fovDegrees);
    void SetUpVector(const glm::vec3& newUp);
    void SetNearFarPlanes(float nearPlane, float farPlane);
    void UpdateCameraMatrices();

    // Method to update camera's internal direction vectors after mouse or joystick movement
    void UpdateDirectionVectors(const glm::vec3& forwardVec, const glm::vec3& rightVec, const glm::vec3& upVec);
    // Method to calculate direction vectors from yaw and pitch angles
    void CalculateDirectionVectors(float yaw, float pitch, glm::vec3& outForward, glm::vec3& outRight, glm::vec3& outUp) const;
    // Method to update camera direction from yaw and pitch angles
    void UpdateCameraDirectionFromAngles(float yaw, float pitch);
    // Method to calculate direction vectors from current camera state and mouse delta
    void CalculateDirectionVectorsFromMouseDelta(float mouseDeltaX, float mouseDeltaY, float sensitivity,
        glm::vec3& outForward, glm::vec3& outRight, glm::vec3& outUp);

    // Method to update camera direction from mouse movement while preserving current view
    void UpdateCameraFromMouseMovement(float mouseDeltaX, float mouseDeltaY, float sensitivity);

    // Resize functions for Camera state preservation
    void SaveCameraStateForResize();
    void RestoreCameraStateAfterResize();

    // Updated camera movement functions with focus control
    void JumpTo(float new_x, float new_y, float new_z, int speed, bool FocusOnTarget);
    void JumpToWithYawPitch(float new_x, float new_y, float new_z, float newYaw, float newPitch, int speed, bool FocusOnTarget);

    void JumpBackHistory(int numOfJumps);
    void RotateX(float degrees, int speed, bool FocusOnTarget = true);
    void RotateY(float degrees, int speed, bool FocusOnTarget = true);
    void RotateZ(float degrees, int speed, bool FocusOnTarget = true);
    void RotateXYZ(float xDegrees, float yDegrees, float zDegrees, int speed, bool FocusOnTarget = true);
    void RotateToOppositeSide(int speed = 2);
    void MoveAroundTarget(bool x, bool y, bool z, bool continuous = true);
    void MoveAroundTarget(bool x, bool y, bool z, float rotationSpeed, bool continuous = true);
    void StopRotating();
    void PauseRotation();
    void ResumeRotation();
    void SetRotationSpeed(float degreesPerSecond);

    // Enhanced rotation status and utility functions
    bool      IsRotatingAroundTarget() const;
    bool      IsRotationPaused() const;
    float     GetJumpProgress() const;
    float     GetRotationProgress() const;
    float     GetEstimatedTimeToComplete() const;
    glm::vec3 GetCurrentRotationAngles() const;
    glm::vec3 GetRotationSpeeds() const;

    // Jump status and utility functions
    bool IsJumping() const;
    void CancelJump();
    const std::vector<CameraJumpHistoryEntry>& GetJumpHistory() const;
    void ClearJumpHistory();
    int  GetJumpHistoryCount() const;

    // Update function to be called each frame for jump animation
    void UpdateJumpAnimation();

    // Update camera resolution and aspect ratio after window resize
    void UpdateResolution(uint32_t newWidth, uint32_t newHeight, float newAspectRatio);

private:
    // Jump animation variables
    bool m_isJumping;
    bool m_focusOnTarget;
    bool m_isJumpingBackInHistory;
    int  m_historyJumpStepsRemaining;
    glm::vec3 m_jumpStartPosition;
    glm::vec3 m_jumpTargetPosition;
    glm::vec3 m_originalTarget;
    std::vector<glm::vec3> m_currentTravelPath;
    int   m_jumpSpeed;
    int   m_currentPathIndex;
    float m_jumpAnimationTimer;
    float m_totalJumpTime;

    // Continuous rotation variables
    bool  m_isRotatingAroundTarget;
    bool  m_continuousRotation;
    bool  m_rotateAroundX;
    bool  m_rotateAroundY;
    bool  m_rotateAroundZ;
    float m_rotationSpeedX;
    float m_rotationSpeedY;
    float m_rotationSpeedZ;
    float m_currentRotationX;
    float m_currentRotationY;
    float m_currentRotationZ;
    float m_targetRotationX;
    float m_targetRotationY;
    float m_targetRotationZ;
    glm::vec3 m_rotationStartPosition;
    glm::vec3 m_rotationTarget;
    float m_rotationDistance;

    // Jump history storage (last 10 jumps)
    std::vector<CameraJumpHistoryEntry> m_jumpHistory;
    static const int MAX_JUMP_HISTORY = 10;

    // Resolution and projection parameters
    float m_screenWidth;
    float m_screenHeight;
    float m_aspectRatio;
    float m_fieldOfView;
    float m_nearPlane;
    float m_farPlane;

    // Private helper functions for jump animation
    std::vector<glm::vec3> CalculateSmoothTravelPath(const glm::vec3& start, const glm::vec3& end, int pathPoints) const;
    float     CalculateJumpAnimationSpeed(float progress, int speed) const;
    void      AddToJumpHistory(const glm::vec3& start, const glm::vec3& end, const std::vector<glm::vec3>& path, int speed, bool focusOnTarget);
    glm::vec3 CalculateRotatedPosition(const glm::vec3& currentPos, const glm::vec3& pivot, float angleX, float angleY, float angleZ) const;
    void      UpdateYawPitchFromDirection(const glm::vec3& direction);
    void      RemoveForwardHistoryEntries(int fromIndex);
    char      DeterminePrimaryLookDirection() const;
    void      UpdateContinuousRotation();
    glm::vec3 CalculateRotationPosition(float angleX, float angleY, float angleZ) const;

    // Vulkan-specific helper: builds a projection matrix with Y-flip and [0,1] depth
    glm::mat4 MakeVulkanProjection(float fovYRadians, float aspect, float nearPlane, float farPlane) const;
};

#endif // __USE_VULKAN__
