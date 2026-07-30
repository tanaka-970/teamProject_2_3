#include "camera.h"

Camera::Camera()
{
    SetPerspectiveFov(DirectX::XMConvertToRadians(45.0f), 1600.0f / 900.0f, 0.1f, 1000.0f);
    SetLookAt({ 0, 5, -10 }, { 0, 0, 0 }, { 0, 1, 0 });
}

void Camera::SetLookAt(const DirectX::XMFLOAT3& eyeIn,
                       const DirectX::XMFLOAT3& focusIn,
                       const DirectX::XMFLOAT3& upIn)
{
    DirectX::XMVECTOR E = DirectX::XMLoadFloat3(&eyeIn);
    DirectX::XMVECTOR F = DirectX::XMLoadFloat3(&focusIn);
    DirectX::XMVECTOR U = DirectX::XMLoadFloat3(&upIn);
    DirectX::XMMATRIX V = DirectX::XMMatrixLookAtLH(E, F, U);
    DirectX::XMStoreFloat4x4(&view, V);

    DirectX::XMMATRIX W = DirectX::XMMatrixInverse(nullptr, V);
    DirectX::XMFLOAT4X4 world;
    DirectX::XMStoreFloat4x4(&world, W);

    right.x = world._11; right.y = world._12; right.z = world._13;
    up.x    = world._21; up.y    = world._22; up.z    = world._23;
    front.x = world._31; front.y = world._32; front.z = world._33;

    eye   = eyeIn;
    focus = focusIn;
}

void Camera::SetPerspectiveFov(float fovY, float aspect, float nearZ, float farZ)
{
    DirectX::XMMATRIX P = DirectX::XMMatrixPerspectiveFovLH(fovY, aspect, nearZ, farZ);
    DirectX::XMStoreFloat4x4(&projection, P);
}
