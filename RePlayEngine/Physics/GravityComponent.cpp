#include "../Core/Components/GravityComponent.h"

#include <algorithm>
#include <cmath>

namespace ReplayEngine::Core
{
    void GravityComponent::SetDirection(const DirectX::XMFLOAT3& direction) noexcept
    {
        const float length = std::sqrt(
            direction.x * direction.x +
            direction.y * direction.y +
            direction.z * direction.z);
        if (length <= 0.000001f) return;

        direction_ = {
            direction.x / length,
            direction.y / length,
            direction.z / length
        };
    }

    void GravityComponent::SetStrength(float strength) noexcept
    {
        strength_ = std::max(strength, 0.0f);
    }

    void GravityComponent::SetScale(float scale) noexcept
    {
        scale_ = std::max(scale, 0.0f);
    }

    void GravityComponent::SetTerminalSpeed(float speed) noexcept
    {
        terminal_speed_ = std::max(speed, 0.0f);
    }

    DirectX::XMFLOAT3 GravityComponent::Acceleration() const noexcept
    {
        const float magnitude = strength_ * scale_;
        return {
            direction_.x * magnitude,
            direction_.y * magnitude,
            direction_.z * magnitude
        };
    }

    void GravityComponent::Reset() noexcept
    {
        direction_ = { 0.0f, -1.0f, 0.0f };
        strength_ = 9.80665f;
        scale_ = 1.0f;
        terminal_speed_ = 55.0f;
        use_terminal_speed_ = true;
    }
}
