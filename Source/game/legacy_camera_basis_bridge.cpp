#include "legacy_camera_basis_bridge.h"

#include "camera.h"

DirectX::XMFLOAT3 LegacyCameraBasisBridge::CameraForward() const
{
    if (camera_ == nullptr) return DirectX::XMFLOAT3{ 0.0f, 0.0f, 1.0f };
    return camera_->GetFront();
}

DirectX::XMFLOAT3 LegacyCameraBasisBridge::CameraRight() const
{
    if (camera_ == nullptr) return DirectX::XMFLOAT3{ 1.0f, 0.0f, 0.0f };
    return camera_->GetRight();
}
