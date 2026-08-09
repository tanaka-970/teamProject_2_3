#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/References.h"

namespace ReplayEngine::Components
{
    class MotionPlayerComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(MotionPlayerComponent)

    public:
        MotionPlayerComponent() = default;

        void OnRuntimeAwake() override;

        bool ShouldContribute() const noexcept;
        void ResetPlayback() noexcept;
        void Advance(float duration, float delta_time) noexcept;

        Reflection::AssetReference motion;
        bool play_on_start = true;
        bool loop = false;
        float speed = 1.0f;
        float weight = 1.0f;

        // Motion Mixer が外部更新点で進める読み取り専用の再生時刻。
        float time = 0.0f;
    };
}
