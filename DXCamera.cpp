#include "Includes.h"
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
	SetupDefaultCamera(800.0f, 600.0f); // Default window size
    UpdateViewMatrix();

	debug.logLevelMessage(LogLevel::LOG_INFO, L"DX Camera created successfully");
}

Camera::~Camera() {
	debug.logLevelMessage(LogLevel::LOG_INFO, L"DX Camera destroyed!");
}

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
        cosf(m_pitch) * sinf(m_yaw),
        sinf(m_pitch),
        cosf(m_pitch) * cosf(m_yaw),
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
    float fovY = 2.0f * atanf(tanf(static_cast<float>(config.myConfig.fov) * 0.5f * XM_PI / 180.0f) / aspect);

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
    float cosPitch = cosf(m_pitch);
    float sinPitch = sinf(m_pitch);
    float cosYaw = cosf(m_yaw);
    float sinYaw = sinf(m_yaw);

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
    m_pitch = asinf(fwd.m128_f32[1]);  // y = sin(pitch)
    m_yaw = atan2f(fwd.m128_f32[0], fwd.m128_f32[2]);  // x and z
#if defined(_DEBUG_CAMERA_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG,
        L"[CAMERA]: YawPitch initialized from forward vector → Yaw: %.3f, Pitch: %.3f",
        m_yaw, m_pitch);
#endif
}

