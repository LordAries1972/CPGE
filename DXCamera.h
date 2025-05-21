#pragma once

//-------------------------------------------------------------------------------------------------
// DXCamera.h - DirectX Math-based camera class for 3D rendering
//-------------------------------------------------------------------------------------------------

#include "Includes.h"

#include <DirectXMath.h>
using namespace DirectX;

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
    void SetLookDirection(XMVECTOR forward, XMVECTOR up);
    void SetupDefaultCamera(float windowWidth, float windowHeight);
    void SetYawPitch(float newYaw, float newPitch);
    void SetYawPitchFromForward();
    void SetTarget(const XMFLOAT3& newTarget);
    void SetNearFar(float nearPlane, float farPlane);

private:


};
