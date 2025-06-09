#pragma once

//-------------------------------------------------------------------------------------------------
// DXCamera.h - DirectX Math-based camera class for 3D rendering
//-------------------------------------------------------------------------------------------------

#include "Includes.h"

#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)

#include <DirectXMath.h>
using namespace DirectX;

// Forward declaration for MathPrecalculation
class MathPrecalculation;

// Structure to store camera jump history
struct CameraJumpHistoryEntry {
    XMFLOAT3 startPosition;                                     // Starting position of the jump
    XMFLOAT3 endPosition;                                       // Ending position of the jump  
    std::vector<XMFLOAT3> travelPath;                           // Calculated smooth travel path
    float totalDistance;                                        // Total distance of the jump
    int speed;                                                  // Speed setting used for this jump
    bool focusOnTarget;                                         // Whether focus was maintained during jump
    XMFLOAT3 originalTarget;                                    // The target that was focused on during this jump
    std::chrono::system_clock::time_point timestamp;            // When this jump occurred
};

// Camera state preservation structure for resize operations
struct CameraResizeState {
    XMFLOAT3 position;                                          // Camera world position
    XMFLOAT3 target;                                            // Camera look-at target
    XMFLOAT3 up;                                                // Camera up vector
    float yaw;                                                  // Camera yaw rotation
    float pitch;                                                // Camera pitch rotation
    float fieldOfView;                                          // Camera field of view
    float nearPlane;                                            // Camera near clipping plane
    float farPlane;                                             // Camera far clipping plane
    bool isValid;                                               // Whether this state is valid

    // Constructor to initialize with safe defaults
    CameraResizeState() :
        position(XMFLOAT3(0.0f, 0.0f, 0.0f)),
        target(XMFLOAT3(0.0f, 0.0f, 1.0f)),
        up(XMFLOAT3(0.0f, 1.0f, 0.0f)),
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

    XMFLOAT3 forward;
    XMFLOAT3 position;
    XMFLOAT3 target;
    XMFLOAT3 up;
    XMMATRIX viewMatrix;
    XMMATRIX projectionMatrix;
    XMMATRIX worldMatrix;
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;

    // Existing movement functions
    void MoveUp(float distance);
    void MoveDown(float distance);
    void MoveRight(float distance);
    void MoveLeft(float distance);
    void MoveIn(float distance);
    void MoveOut(float distance);
    void SetPosition(float x, float y, float z);
    XMFLOAT3 GetPosition() const;
    XMMATRIX GetViewMatrix() const;
    XMMATRIX GetProjectionMatrix() const;
    void SetProjectionMatrix(const XMMATRIX& matrix);
    void SetViewMatrix(const XMMATRIX& matrix);
    void UpdateViewMatrix();
    void UpdateProjectionMatrix();
    void SetLookDirection(XMVECTOR forward, XMVECTOR up);
    void SetupDefaultCamera(float windowWidth, float windowHeight);
    void SetYawPitch(float newYaw, float newPitch);
    void SetYawPitchFromForward();
    void SetTarget(const XMFLOAT3& newTarget);
    void SetNearFar(float nearPlane, float farPlane);

    // Field of View, Up Vector, and Clipping Plane Management Functions
    float GetFieldOfView() const;
    XMFLOAT3 GetUpVector() const;
    float GetFarPlane() const;
    float GetNearPlane() const;
    void SetFieldOfView(float fovDegrees);
    void SetUpVector(const XMFLOAT3& newUp);
    void SetNearFarPlanes(float nearPlane, float farPlane);
    void UpdateCameraMatrices();

    // Method to update camera's internal direction vectors after mouse or joystick movement
    void UpdateDirectionVectors(const XMVECTOR& forward, const XMVECTOR& right, const XMVECTOR& up);
    // Method to calculate direction vectors from yaw and pitch angles
    void CalculateDirectionVectors(float yaw, float pitch, XMVECTOR& outForward, XMVECTOR& outRight, XMVECTOR& outUp) const;
    // Method to update camera direction from yaw and pitch angles
    void UpdateCameraDirectionFromAngles(float yaw, float pitch);

    // Resize functions for Camera state preservation
    void SaveCameraStateForResize();
    void RestoreCameraStateAfterResize();

    // Updated camera movement functions with focus control
    void JumpTo(float new_x, float new_y, float new_z, int speed, bool FocusOnTarget);
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
    bool IsRotatingAroundTarget() const;
    bool IsRotationPaused() const;
    float GetJumpProgress() const;
    float GetRotationProgress() const;
    float GetEstimatedTimeToComplete() const;
    XMFLOAT3 GetCurrentRotationAngles() const;
    XMFLOAT3 GetRotationSpeeds() const;

    // Jump status and utility functions
    bool IsJumping() const;
    void CancelJump();
    const std::vector<CameraJumpHistoryEntry>& GetJumpHistory() const;
    void ClearJumpHistory();
    int GetJumpHistoryCount() const;

    // Update function to be called each frame for jump animation
    void UpdateJumpAnimation();

    // Update camera resolution and aspect ratio after window resize
    void UpdateResolution(uint32_t newWidth, uint32_t newHeight, float newAspectRatio);

private:
    // Jump animation variables
    bool m_isJumping;                                                       // Flag to indicate if camera is currently jumping
    bool m_focusOnTarget;                                                   // Flag to maintain focus on target during jump
    bool m_isJumpingBackInHistory;                                          // Flag to indicate if currently jumping back through history
    int m_historyJumpStepsRemaining;                                        // Number of history steps remaining in current back-jump
    XMFLOAT3 m_jumpStartPosition;                                           // Starting position of current jump
    XMFLOAT3 m_jumpTargetPosition;                                          // Target position of current jump
    XMFLOAT3 m_originalTarget;                                              // Original target before jump (for focus maintenance)
    std::vector<XMFLOAT3> m_currentTravelPath;                              // Current smooth travel path points
    int m_jumpSpeed;                                                        // Current jump speed setting
    int m_currentPathIndex;                                                 // Current index in the travel path
    float m_jumpAnimationTimer;                                             // Timer for jump animation progress
    float m_totalJumpTime;                                                  // Total time calculated for this jump

    // Continuous rotation variables
    bool m_isRotatingAroundTarget;                                          // Flag to indicate if camera is continuously rotating around target
    bool m_continuousRotation;                                              // Flag to indicate if rotation should continue indefinitely
    bool m_rotateAroundX;                                                   // Flag to enable rotation around X-axis
    bool m_rotateAroundY;                                                   // Flag to enable rotation around Y-axis
    bool m_rotateAroundZ;                                                   // Flag to enable rotation around Z-axis
    float m_rotationSpeedX;                                                 // Rotation speed for X-axis (degrees per second)
    float m_rotationSpeedY;                                                 // Rotation speed for Y-axis (degrees per second)
    float m_rotationSpeedZ;                                                 // Rotation speed for Z-axis (degrees per second)
    float m_currentRotationX;                                               // Current accumulated rotation around X-axis
    float m_currentRotationY;                                               // Current accumulated rotation around Y-axis
    float m_currentRotationZ;                                               // Current accumulated rotation around Z-axis
    float m_targetRotationX;                                                // Target rotation for X-axis (360 degrees for full rotation)
    float m_targetRotationY;                                                // Target rotation for Y-axis (360 degrees for full rotation)
    float m_targetRotationZ;                                                // Target rotation for Z-axis (360 degrees for full rotation)
    XMFLOAT3 m_rotationStartPosition;                                       // Starting position when rotation began
    XMFLOAT3 m_rotationTarget;                                              // Target point to rotate around
    float m_rotationDistance;                                               // Distance from target during rotation

    // Jump history storage (last 10 jumps)
    std::vector<CameraJumpHistoryEntry> m_jumpHistory;
    static const int MAX_JUMP_HISTORY = 10;                                 // Maximum number of jumps to store in history

    // Resolution and projection parameters
    float m_screenWidth;                                                    // Current screen width in pixels
    float m_screenHeight;                                                   // Current screen height in pixels
    float m_aspectRatio;                                                    // Current aspect ratio (width/height)
    float m_fieldOfView;                                                    // Field of view in radians
    float m_nearPlane;                                                      // Near clipping plane distance
    float m_farPlane;                                                       // Far clipping plane distance
    
    // Private helper functions for jump animation
    std::vector<XMFLOAT3> CalculateSmoothTravelPath(const XMFLOAT3& start, const XMFLOAT3& end, int pathPoints) const;
    float CalculateJumpAnimationSpeed(float progress, int speed) const;
    void AddToJumpHistory(const XMFLOAT3& start, const XMFLOAT3& end, const std::vector<XMFLOAT3>& path, int speed, bool focusOnTarget);
    XMFLOAT3 CalculateRotatedPosition(const XMFLOAT3& currentPos, const XMFLOAT3& pivot, float angleX, float angleY, float angleZ) const;
    void UpdateYawPitchFromDirection(const XMFLOAT3& direction);
    void RemoveForwardHistoryEntries(int fromIndex);
    char DeterminePrimaryLookDirection() const;
    void UpdateContinuousRotation();
    XMFLOAT3 CalculateRotationPosition(float angleX, float angleY, float angleZ) const;
};

#endif // __USE_DIRECTX_11__ || __USE_DIRECTX_12__