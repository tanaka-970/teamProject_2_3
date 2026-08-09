#pragma once

#include "../../Object/Component/Component.h"

#include <DirectXMath.h>

namespace ReplayEngine::Components
{
    class FollowTargetComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(FollowTargetComponent)

    public:
        FollowTargetComponent() = default;

        float follow_distance = 6.5f;
        float follow_height = 2.25f;
        float follow_lag = 12.0f;
        bool rotation_input_enabled = true;

        float yaw_offset = 0.0f;
        float pitch_offset = 0.0f;

        void OnLateUpdate(float delta_time) override;
        void OnDisable() override;

    private:
        void UpdateRotationInput(float delta_time);
        void ResetCursorTracking() noexcept;

        bool cursor_initialized_ = false;
        long previous_cursor_x_ = 0;
        long previous_cursor_y_ = 0;
    };
}
