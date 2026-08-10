#include "MotionPlayerComponent.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ReplayEngine::Components
{
    namespace
    {
        bool SameBinding(const Motion::MotionBinding& a,
            const Motion::MotionBinding& b) noexcept
        {
            return a.object == b.object &&
                a.component_type == b.component_type &&
                a.component_index == b.component_index &&
                a.property == b.property;
        }
    }

    void MotionPlayerComponent::OnRuntimeAwake()
    {
        if (play_on_start)
            PlayFrom(0.0f);
        else
            StopAndKeep();
    }

    bool MotionPlayerComponent::ShouldContribute() const noexcept
    {
        return (state == Playing || state == Paused) &&
            weight > 0.0f && !motion.guid.empty();
    }

    void MotionPlayerComponent::ResetPlayback() noexcept
    {
        time = 0.0f;
        blend_in_elapsed_ = 0.0f;
        ping_pong_direction_ = speed < 0.0f ? -1 : 1;
        snapshot_valid_ = false;
        stop_restore_requested_ = false;
        duration_ = 0.0f;
        snapshot_values_.clear();
    }

    void MotionPlayerComponent::Advance(float duration, float delta_time) noexcept
    {
        duration_ = (std::max)(0.0f, duration);
        if (state != Playing || motion.guid.empty()) return;

        blend_in_elapsed_ += (std::max)(0.0f, delta_time);
        if (duration <= 0.0f)
        {
            time = 0.0f;
            if (auto_stop_on_end) Stop();
            return;
        }

        const int mode = EffectiveWrapMode();
        if (mode == PingPong)
        {
            time += delta_time * std::fabs(speed) *
                static_cast<float>(ping_pong_direction_);
            while (time > duration || time < 0.0f)
            {
                if (time > duration)
                {
                    time = duration - (time - duration);
                    ping_pong_direction_ = -1;
                }
                else
                {
                    time = -time;
                    ping_pong_direction_ = 1;
                }
            }
            return;
        }

        time += delta_time * speed;
        if (mode == Loop)
        {
            time = std::fmod(time, duration);
            if (time < 0.0f) time += duration;
        }
        else if (mode == ClampForever)
        {
            time = (std::max)(0.0f, (std::min)(duration, time));
        }
        else
        {
            const bool reached_end = speed >= 0.0f ? time >= duration : time <= 0.0f;
            time = (std::max)(0.0f, (std::min)(duration, time));
            if (reached_end && auto_stop_on_end)
            {
                Stop();
            }
        }
    }

    void MotionPlayerComponent::Play() noexcept
    {
        PlayFrom(0.0f);
    }

    void MotionPlayerComponent::PlayFrom(float seconds) noexcept
    {
        ResetPlayback();
        time = (std::max)(0.0f, seconds);
        state = Playing;
    }

    void MotionPlayerComponent::Pause() noexcept
    {
        if (state == Playing) state = Paused;
    }

    void MotionPlayerComponent::Resume() noexcept
    {
        if (state == Paused) state = Playing;
    }

    void MotionPlayerComponent::Stop() noexcept
    {
        state = Stopped;
        time = 0.0f;
        stop_restore_requested_ = snapshot_valid_;
    }

    void MotionPlayerComponent::StopAndKeep() noexcept
    {
        state = Stopped;
        time = 0.0f;
        stop_restore_requested_ = false;
        snapshot_valid_ = false;
        snapshot_values_.clear();
    }

    void MotionPlayerComponent::Reverse() noexcept
    {
        speed = speed == 0.0f ? -1.0f : -speed;
        ping_pong_direction_ = -ping_pong_direction_;
    }

    void MotionPlayerComponent::SetTime(float seconds) noexcept
    {
        time = (std::max)(0.0f, seconds);
        if (duration_ > 0.0f)
        {
            time = (std::min)(duration_, time);
        }
    }

    void MotionPlayerComponent::SetSpeed(float value) noexcept
    {
        speed = value;
        if (speed != 0.0f)
        {
            ping_pong_direction_ = speed < 0.0f ? -1 : 1;
        }
    }

    void MotionPlayerComponent::SetWeight(float value) noexcept
    {
        weight = (std::max)(0.0f, (std::min)(1.0f, value));
    }

    bool MotionPlayerComponent::IsPlaying() const noexcept
    {
        return state == Playing;
    }

    float MotionPlayerComponent::Time() const noexcept
    {
        return time;
    }

    float MotionPlayerComponent::Duration() const noexcept
    {
        return duration_;
    }

    bool MotionPlayerComponent::NeedsSnapshot() const noexcept
    {
        return (state == Playing || state == Paused) && !snapshot_valid_;
    }

    void MotionPlayerComponent::StoreSnapshot(std::vector<SnapshotValue> values)
    {
        snapshot_values_ = std::move(values);
        snapshot_valid_ = true;
    }

    const Reflection::PropertyValue* MotionPlayerComponent::SnapshotFor(
        const Motion::MotionBinding& binding) const noexcept
    {
        for (const SnapshotValue& value : snapshot_values_)
        {
            if (SameBinding(value.binding, binding)) return &value.value;
        }
        return nullptr;
    }

    float MotionPlayerComponent::BlendInAlpha() const noexcept
    {
        if (blend_in_seconds <= 0.0f) return 1.0f;
        return (std::max)(0.0f,
            (std::min)(1.0f, blend_in_elapsed_ / blend_in_seconds));
    }

    bool MotionPlayerComponent::HasStopRestoreRequest() const noexcept
    {
        return stop_restore_requested_ && snapshot_valid_;
    }

    const std::vector<MotionPlayerComponent::SnapshotValue>&
        MotionPlayerComponent::SnapshotValues() const noexcept
    {
        return snapshot_values_;
    }

    void MotionPlayerComponent::ConsumeStopRestoreRequest() noexcept
    {
        stop_restore_requested_ = false;
        snapshot_valid_ = false;
        snapshot_values_.clear();
    }

    int MotionPlayerComponent::EffectiveWrapMode() const noexcept
    {
        if (loop) return Loop;
        if (wrap_mode < Once || wrap_mode > ClampForever) return Once;
        return wrap_mode;
    }
}
