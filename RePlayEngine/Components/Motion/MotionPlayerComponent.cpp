#include "MotionPlayerComponent.h"

#include <algorithm>
#include <cmath>

namespace ReplayEngine::Components
{
    void MotionPlayerComponent::OnRuntimeAwake()
    {
        ResetPlayback();
    }

    bool MotionPlayerComponent::ShouldContribute() const noexcept
    {
        return play_on_start && weight > 0.0f && !motion.guid.empty();
    }

    void MotionPlayerComponent::ResetPlayback() noexcept
    {
        time = 0.0f;
    }

    void MotionPlayerComponent::Advance(float duration, float delta_time) noexcept
    {
        if (!ShouldContribute()) return;

        time += delta_time * speed;
        if (duration <= 0.0f)
        {
            time = 0.0f;
            return;
        }

        if (loop)
        {
            time = std::fmod(time, duration);
            if (time < 0.0f) time += duration;
        }
        else
        {
            time = std::clamp(time, 0.0f, duration);
        }
    }
}
