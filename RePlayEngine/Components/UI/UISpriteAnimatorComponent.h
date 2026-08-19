#pragma once

#include "../../Object/Component/Component.h"

namespace ReplayEngine::Motion
{
    class MotionMixer;
}

namespace ReplayEngine::Components
{
    class UISpriteAnimatorComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(UISpriteAnimatorComponent)

    public:
        enum PlayMode : int
        {
            Once = 0,
            Loop = 1,
            PingPong = 2,
            Reverse = 3,
        };

        UISpriteAnimatorComponent() = default;

        void OnAttach() override;
        void UpdateSprite(float delta_time,
            const Motion::MotionMixer* mixer = nullptr) noexcept;

        int columns = 4;
        int rows = 4;
        int start_frame = 0;
        int end_frame = -1;
        float frames_per_second = 12.0f;
        int play_mode = Loop;
        bool playing = true;
        float frame = 0.0f;

    private:
        int ping_pong_direction_ = 1;
    };
}
