#pragma once

#include "IComponent.h"

#include <DirectXMath.h>

namespace ReplayEngine::Core
{
    // 重力の設定だけを保持するコンポーネント。
    // Transformや速度へは自動適用せず、物理更新側が必要なときにAcceleration()を参照する。
    class GravityComponent final : public IComponent
    {
    public:
        GravityComponent() = default;

        const DirectX::XMFLOAT3& Direction() const noexcept { return direction_; }
        float Strength() const noexcept { return strength_; }
        float Scale() const noexcept { return scale_; }
        float TerminalSpeed() const noexcept { return terminal_speed_; }
        bool UsesTerminalSpeed() const noexcept { return use_terminal_speed_; }

        void SetDirection(const DirectX::XMFLOAT3& direction) noexcept;
        void SetStrength(float strength) noexcept;
        void SetScale(float scale) noexcept;
        void SetTerminalSpeed(float speed) noexcept;
        void SetUseTerminalSpeed(bool enabled) noexcept { use_terminal_speed_ = enabled; }

        DirectX::XMFLOAT3 Acceleration() const noexcept;
        void Reset() noexcept;

    private:
        DirectX::XMFLOAT3 direction_{ 0.0f, -1.0f, 0.0f };
        float strength_{ 9.80665f };
        float scale_{ 1.0f };
        float terminal_speed_{ 55.0f };
        bool use_terminal_speed_{ true };
    };
}
