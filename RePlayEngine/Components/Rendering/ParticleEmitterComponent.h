#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/References.h"

#include <DirectXMath.h>

#include <algorithm>

namespace ReplayEngine::Components
{
    class ParticleEmitterComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(ParticleEmitterComponent)

    public:
        ParticleEmitterComponent() = default;

        bool emitting = true;
        int priority = 0;
        float spawn_rate = 200.0f;
        float lifetime = 1.5f;
        float start_speed = 2.0f;
        float gravity = 1.8f;
        float drag = 0.5f;
        float start_size = 0.10f;
        float end_size = 0.02f;
        DirectX::XMFLOAT4 start_color{ 1.0f, 0.8f, 0.4f, 1.0f };
        DirectX::XMFLOAT4 end_color{ 1.0f, 0.2f, 0.05f, 0.0f };
        DirectX::XMFLOAT3 direction{ 0.0f, 1.0f, 0.0f };
        float cone_angle = 0.4f;
        Reflection::AssetReference sprite;
        int blend_mode = 1;
        int max_particles = 10000;

        void Emit(int count) const noexcept
        {
            if (count <= 0) return;
            const int limit = (std::max)(1, max_particles);
            const long long combined = static_cast<long long>(pending_burst_) +
                static_cast<long long>((std::min)(count, limit));
            pending_burst_ = static_cast<int>((std::min)(
                static_cast<long long>(limit), combined));
        }
        void Clear() const noexcept { clear_requested_ = true; }
        int ConsumeBurst() const noexcept
        {
            const int result = pending_burst_;
            pending_burst_ = 0;
            return result;
        }
        bool ConsumeClearRequest() const noexcept
        {
            const bool result = clear_requested_;
            clear_requested_ = false;
            return result;
        }
        bool HasPendingRequest() const noexcept
        {
            return pending_burst_ > 0 || clear_requested_;
        }

    private:
        mutable int pending_burst_ = 0;
        mutable bool clear_requested_ = false;
    };
}
