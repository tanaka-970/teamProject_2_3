#include "CameraComponent.h"

#include "../../Object/GameObject/GameObject.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace ReplayEngine::Components
{
    namespace
    {
        constexpr float default_field_of_view_degrees = 50.0f;
        constexpr float default_near_clip = 0.1f;
        constexpr float default_far_clip = 10000.0f;

        float ClampFieldOfView(float degrees) noexcept
        {
            if (!std::isfinite(degrees)) return default_field_of_view_degrees;
            return std::clamp(degrees, 1.0f, 179.0f);
        }

        float ClampNearClip(float near_clip) noexcept
        {
            if (!std::isfinite(near_clip)) return default_near_clip;
            return (std::max)(1.0e-3f, near_clip);
        }

        float ClampFarClip(float far_clip, float near_clip) noexcept
        {
            if (!std::isfinite(far_clip)) return (std::max)(default_far_clip, near_clip * 10.0f);
            return (std::max)(far_clip, near_clip * 10.0f);
        }

        XMVECTOR OwnerRotation(const Core::GameObject* owner) noexcept
        {
            if (owner == nullptr) return XMQuaternionIdentity();

            XMVECTOR scale{};
            XMVECTOR rotation{};
            XMVECTOR translation{};
            if (XMMatrixDecompose(&scale, &rotation, &translation,
                owner->GetTransform().WorldMatrix()))
            {
                return XMQuaternionNormalize(rotation);
            }
            return XMQuaternionIdentity();
        }

        XMFLOAT3 RotateAxis(const Core::GameObject* owner,
            float x, float y, float z) noexcept
        {
            XMVECTOR axis = XMVectorSet(x, y, z, 0.0f);
            axis = XMVector3Rotate(axis, OwnerRotation(owner));
            axis = XMVector3Normalize(axis);

            XMFLOAT3 result{ x, y, z };
            XMFLOAT3 rotated{};
            XMStoreFloat3(&rotated, axis);
            if (std::isfinite(rotated.x) && std::isfinite(rotated.y) &&
                std::isfinite(rotated.z))
            {
                result = rotated;
            }
            return result;
        }
    }

    CameraProjectionMode CameraComponent::ProjectionMode() const noexcept
    {
        return projection_mode == static_cast<int>(CameraProjectionMode::Orthographic)
            ? CameraProjectionMode::Orthographic
            : CameraProjectionMode::Perspective;
    }

    XMMATRIX CameraComponent::ViewMatrix() const noexcept
    {
        const XMFLOAT3 eye = EyePosition();
        const XMFLOAT3 forward_value = Forward();
        const XMFLOAT3 up_value = Up();
        XMVECTOR position = XMLoadFloat3(&eye);
        XMVECTOR forward = XMLoadFloat3(&forward_value);
        XMVECTOR up = XMLoadFloat3(&up_value);

        if (XMVector3Equal(forward, XMVectorZero())) forward = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
        if (XMVector3Equal(up, XMVectorZero())) up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

        return XMMatrixLookToLH(position, forward, up);
    }

    XMMATRIX CameraComponent::ProjectionMatrix(float aspect) const noexcept
    {
        if (!(aspect > 0.0f) || !std::isfinite(aspect)) aspect = 16.0f / 9.0f;

        const float near_z = ClampNearClip(near_clip);
        const float far_z = ClampFarClip(far_clip, near_z);
        if (ProjectionMode() == CameraProjectionMode::Orthographic)
        {
            float height = orthographic_size;
            if (!(height > 0.0f) || !std::isfinite(height)) height = 10.0f;
            return XMMatrixOrthographicLH(height * aspect, height, near_z, far_z);
        }

        return XMMatrixPerspectiveFovLH(
            XMConvertToRadians(ClampFieldOfView(field_of_view_degrees)),
            aspect, near_z, far_z);
    }

    XMFLOAT3 CameraComponent::EyePosition() const noexcept
    {
        const Core::GameObject* owner = Owner();
        if (owner == nullptr) return XMFLOAT3{ 0.0f, 0.0f, 0.0f };
        return owner->GetTransform().WorldPosition();
    }

    XMFLOAT3 CameraComponent::Forward() const noexcept
    {
        return RotateAxis(Owner(), 0.0f, 0.0f, 1.0f);
    }

    XMFLOAT3 CameraComponent::Right() const noexcept
    {
        return RotateAxis(Owner(), 1.0f, 0.0f, 0.0f);
    }

    XMFLOAT3 CameraComponent::Up() const noexcept
    {
        return RotateAxis(Owner(), 0.0f, 1.0f, 0.0f);
    }
}
