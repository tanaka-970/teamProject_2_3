#include "MotionPlayerComponent.h"

#include "../../Object/GameObject/GameObject.h"
#include "../../Runtime/API/RuntimeContext.h"
#include "../../Scene/Runtime/Scene.h"

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
            return a.origin == b.origin &&
                a.object == b.object &&
                a.component_type == b.component_type &&
                a.component_index == b.component_index &&
                a.property == b.property &&
                a.relative_path == b.relative_path;
        }
    }

    void MotionPlayerComponent::OnRuntimeAwake()
    {
        // 壊れた Scene でも自動再生が無言で消えないよう、既定値へ戻す。
        if (trigger < TriggerStart || trigger > TriggerManualOnly)
            trigger = TriggerStart;
        StopAndKeep();
        EnsureTriggerSubscriptions();
        if (play_on_start && trigger == TriggerStart)
            RequestTrigger();
    }

    void MotionPlayerComponent::OnEnable()
    {
        EnsureTriggerSubscriptions();
        if (trigger == TriggerEnabled) RequestTrigger();
    }

    void MotionPlayerComponent::OnDisable()
    {
        if (trigger == TriggerDisabled) RequestTrigger();
        ReleaseTriggerSubscriptions();
    }

    void MotionPlayerComponent::OnRuntimeDestroy()
    {
        ReleaseTriggerSubscriptions();
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
        trigger_pending_ = false;
        trigger_elapsed_ = 0.0f;
        duration_ = 0.0f;
        snapshot_values_.clear();
    }

    void MotionPlayerComponent::AdvanceTriggerDelay(float delta_time) noexcept
    {
        if (!trigger_pending_) return;
        if (motion.guid.empty())
        {
            trigger_pending_ = false;
            trigger_elapsed_ = 0.0f;
            return;
        }

        const float safe_delta = std::isfinite(delta_time)
            ? (std::max)(0.0f, delta_time) : 0.0f;
        const float safe_delay = std::isfinite(trigger_delay)
            ? (std::max)(0.0f, (std::min)(10.0f, trigger_delay)) : 0.0f;
        trigger_elapsed_ += safe_delta;
        if (safe_delay <= 0.0f || trigger_elapsed_ >= safe_delay)
        {
            trigger_pending_ = false;
            trigger_elapsed_ = 0.0f;
            PlayFrom(0.0f);
        }
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
        trigger_pending_ = false;
        trigger_elapsed_ = 0.0f;
        stop_restore_requested_ = snapshot_valid_;
    }

    void MotionPlayerComponent::StopAndKeep() noexcept
    {
        state = Stopped;
        time = 0.0f;
        trigger_pending_ = false;
        trigger_elapsed_ = 0.0f;
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

    void MotionPlayerComponent::RequestTrigger() noexcept
    {
        if (motion.guid.empty()) return;

        const float safe_delay = std::isfinite(trigger_delay)
            ? (std::max)(0.0f, (std::min)(10.0f, trigger_delay)) : 0.0f;
        trigger_elapsed_ = 0.0f;
        stop_restore_requested_ = false;
        snapshot_valid_ = false;
        snapshot_values_.clear();

        if (safe_delay <= 0.0f)
        {
            trigger_pending_ = false;
            PlayFrom(0.0f);
            return;
        }

        // 待機中は旧 Motion の寄与を止める。遅延後の PlayFrom が
        // 新しい開始状態を capture できるよう、旧 snapshot も捨てる。
        trigger_pending_ = true;
        state = Stopped;
        time = 0.0f;
    }

    void MotionPlayerComponent::EnsureTriggerSubscriptions()
    {
        Scene::Scene* scene = GetScene();
        Core::GameObject* owner = Owner();
        Runtime::RuntimeContext* runtime =
            scene != nullptr ? scene->Services().Runtime() : nullptr;
        if (scene == nullptr || owner == nullptr || runtime == nullptr) return;

        const Runtime::ObjectHandle owner_handle =
            runtime->Resolver().MakeHandle(owner);
        if (owner_handle.IsEmpty()) return;

        if ((trigger == TriggerPressed || trigger == TriggerReleased ||
            trigger == TriggerHoverEnter || trigger == TriggerHoverExit) &&
            !button_state_subscription_.Valid())
        {
            button_state_subscription_ = runtime->Events().Subscribe(
                Runtime::EngineEvents::ButtonStateChanged,
                [this](const Runtime::EventRecord& record)
                {
                    HandleButtonStateChanged(record);
                }, owner_handle);
        }

        if (trigger == TriggerEventReceived && !motion_event_subscription_.Valid())
        {
            motion_event_subscription_ = runtime->Events().Subscribe(
                Runtime::EngineEvents::MotionEvent,
                [this](const Runtime::EventRecord& record)
                {
                    HandleMotionEvent(record);
                }, owner_handle);
        }

        if (trigger == TriggerSceneStarted && !scene_started_subscription_.Valid())
        {
            scene_started_subscription_ = Runtime::EventBus::Global().Subscribe(
                Runtime::EngineEvents::SceneLoadStarted,
                [this](const Runtime::EventRecord& record)
                {
                    HandleSceneTransition(record);
                });
        }

        if (trigger == TriggerSceneCompleted && !scene_completed_subscription_.Valid())
        {
            scene_completed_subscription_ = Runtime::EventBus::Global().Subscribe(
                Runtime::EngineEvents::SceneLoaded,
                [this](const Runtime::EventRecord& record)
                {
                    HandleSceneTransition(record);
                });
        }
    }

    void MotionPlayerComponent::ReleaseTriggerSubscriptions() noexcept
    {
        button_state_subscription_.Release();
        motion_event_subscription_.Release();
        scene_started_subscription_.Release();
        scene_completed_subscription_.Release();
    }

    bool MotionPlayerComponent::MatchesTriggerSource(
        const Runtime::EventRecord& record) const noexcept
    {
        if (record.source.IsEmpty()) return false;

        Scene::Scene* scene = GetScene();
        Core::GameObject* owner = Owner();
        if (scene == nullptr || owner == nullptr ||
            record.source.world != scene->WorldInstanceID()) return false;

        if (!trigger_source.IsAssigned())
            return record.source.object == owner->ID();

        if (record.source.object != trigger_source.owner) return false;
        Core::GameObject* source_owner =
            scene->FindGameObjectByID(trigger_source.owner);
        if (source_owner == nullptr || source_owner->PendingDestroy() ||
            source_owner->FindComponentByStableID(trigger_source.component) == nullptr)
        {
            return false;
        }

        // ButtonStateChanged は発行元 Component の StableID も持つ。
        // 古い／汎用イベントにこの値が無い場合は Object 単位の参照として扱う。
        if (const Reflection::PropertyValue* component =
            record.payload.Find("button_component"))
        {
            return component->AsUInt64() == trigger_source.component;
        }
        return true;
    }

    void MotionPlayerComponent::HandleButtonStateChanged(
        const Runtime::EventRecord& record)
    {
        if (!ActiveInHierarchy() || !MatchesTriggerSource(record)) return;

        const Reflection::PropertyValue* previous = record.payload.Find("previous_state");
        const Reflection::PropertyValue* current = record.payload.Find("state");
        if (previous == nullptr || current == nullptr) return;

        const int previous_state = previous->AsInt();
        const int current_state = current->AsInt();
        bool should_trigger = false;
        switch (trigger)
        {
        case TriggerPressed:
            should_trigger = current_state == 2;
            break;
        case TriggerReleased:
            should_trigger = previous_state == 2 && current_state != 2;
            break;
        case TriggerHoverEnter:
            should_trigger = previous_state != 1 && current_state == 1;
            break;
        case TriggerHoverExit:
            should_trigger = previous_state == 1 && current_state != 2;
            break;
        default:
            break;
        }
        if (should_trigger) RequestTrigger();
    }

    void MotionPlayerComponent::HandleMotionEvent(const Runtime::EventRecord& record)
    {
        if (trigger != TriggerEventReceived || !ActiveInHierarchy()) return;

        bool matches = MatchesTriggerSource(record);
        if (!matches && trigger_source.IsAssigned() && !record.target.IsEmpty())
        {
            Scene::Scene* scene = GetScene();
            Core::GameObject* target_owner = scene != nullptr
                ? scene->FindGameObjectByID(trigger_source.owner) : nullptr;
            matches = scene != nullptr &&
                record.target.world == scene->WorldInstanceID() &&
                record.target.object == trigger_source.owner &&
                target_owner != nullptr && !target_owner->PendingDestroy() &&
                target_owner->FindComponentByStableID(trigger_source.component) != nullptr;
        }
        if (!matches) return;
        RequestTrigger();
    }

    void MotionPlayerComponent::HandleSceneTransition(
        const Runtime::EventRecord& record)
    {
        if (!ActiveInHierarchy()) return;
        if (trigger == TriggerSceneStarted &&
            record.type == Runtime::EngineEvents::SceneLoadStarted)
        {
            RequestTrigger();
        }
        else if (trigger == TriggerSceneCompleted &&
            record.type == Runtime::EngineEvents::SceneLoaded)
        {
            RequestTrigger();
        }
    }
}
