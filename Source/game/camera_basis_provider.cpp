#include "camera_basis_provider.h"

#include "camera.h"

DirectX::XMFLOAT3 CameraBasisProvider::CameraForward() const
{
    if (camera_ == nullptr) return DirectX::XMFLOAT3{ 0.0f, 0.0f, 1.0f };
    return camera_->GetFront();
}

DirectX::XMFLOAT3 CameraBasisProvider::CameraRight() const
{
    if (camera_ == nullptr) return DirectX::XMFLOAT3{ 1.0f, 0.0f, 0.0f };
    return camera_->GetRight();
}
