#pragma once

#include <DirectXMath.h>

class Camera;

class FreeCameraController
{
public:
    void SyncCameraToController(const Camera& camera);
    void SyncControllerToCamera(Camera& camera);
    void Update(float elapsed_time);

private:
    DirectX::XMFLOAT3 eye  { 0, 5, -10 };
    DirectX::XMFLOAT3 focus{ 0, 0, 0 };
    DirectX::XMFLOAT3 up   { 0, 1, 0 };
    DirectX::XMFLOAT3 right{ 1, 0, 0 };
    float distance = 10.0f;
    float angleX = 0.0f;
    float angleY = 0.0f;
};
