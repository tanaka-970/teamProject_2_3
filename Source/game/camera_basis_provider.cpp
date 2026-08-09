#include "camera_basis_provider.h"

#include "camera.h"

#include <cmath>

namespace
{
    DirectX::XMFLOAT3 NormalizeOr(
        const DirectX::XMFLOAT3& value, const DirectX::XMFLOAT3& fallback) noexcept
    {
        DirectX::XMVECTOR v = DirectX::XMLoadFloat3(&value);
        const float length_sq = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(v));
        if (!std::isfinite(length_sq) || length_sq <= 1.0e-8f) return fallback;

        v = DirectX::XMVector3Normalize(v);
        DirectX::XMFLOAT3 result{};
        DirectX::XMStoreFloat3(&result, v);
        if (!std::isfinite(result.x) || !std::isfinite(result.y) ||
            !std::isfinite(result.z))
        {
            return fallback;
        }
        return result;
    }
}

void CameraBasisProvider::AttachBasis(const DirectX::XMFLOAT3& forward,
    const DirectX::XMFLOAT3& right) noexcept
{
    camera_ = nullptr;
    has_explicit_basis_ = true;
    explicit_forward_ = NormalizeOr(forward, { 0.0f, 0.0f, 1.0f });
    explicit_right_ = NormalizeOr(right, { 1.0f, 0.0f, 0.0f });
}

DirectX::XMFLOAT3 CameraBasisProvider::CameraForward() const
{
    if (has_explicit_basis_) return explicit_forward_;
    if (camera_ == nullptr) return DirectX::XMFLOAT3{ 0.0f, 0.0f, 1.0f };
    return camera_->GetFront();
}

DirectX::XMFLOAT3 CameraBasisProvider::CameraRight() const
{
    if (has_explicit_basis_) return explicit_right_;
    if (camera_ == nullptr) return DirectX::XMFLOAT3{ 1.0f, 0.0f, 0.0f };
    return camera_->GetRight();
}
