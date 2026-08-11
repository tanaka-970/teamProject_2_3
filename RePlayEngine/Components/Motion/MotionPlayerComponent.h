#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/References.h"
#include "../../Reflection/Property/PropertyValue.h"
#include "../../Motion/MotionAsset.h"
#include "../../Runtime/Events/EventBus.h"

#include <string>
#include <cstdint>
#include <vector>

namespace ReplayEngine::Components
{
    class MotionPlayerComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(MotionPlayerComponent)

    public:
        enum PlayState : int
        {
            Stopped = 0,
            Playing = 1,
            Paused = 2,
        };

        enum WrapMode : int
        {
            Once = 0,
            Loop = 1,
            PingPong = 2,
            ClampForever = 3,
        };

        // 保存互換のため、既存値を途中へ挿入しない。
        enum Trigger : int
        {
            TriggerStart = 0,
            TriggerPressed = 1,
            TriggerReleased = 2,
            TriggerHoverEnter = 3,
            TriggerHoverExit = 4,
            TriggerEnabled = 5,
            TriggerDisabled = 6,
            TriggerSceneStarted = 7,
            TriggerSceneCompleted = 8,
            TriggerEventReceived = 9,
            TriggerManualOnly = 10,
            TriggerStateChanged = 11,
        };

        struct SnapshotValue
        {
            Motion::MotionBinding binding;
            Reflection::PropertyValue value;
        };

        MotionPlayerComponent() = default;

        void OnRuntimeAwake() override;
        void OnEnable() override;
        void OnDisable() override;
        void OnRuntimeDestroy() override;

        bool ShouldContribute() const noexcept;
        void ResetPlayback() noexcept;
        void Advance(float duration, float delta_time) noexcept;
        void AdvanceTriggerDelay(float delta_time) noexcept;

        void Play() noexcept;
        void PlayFrom(float seconds) noexcept;
        void Pause() noexcept;
        void Resume() noexcept;
        void Stop() noexcept;
        void StopAndKeep() noexcept;
        void Reverse() noexcept;
        void SetTime(float seconds) noexcept;
        void SetSpeed(float value) noexcept;
        void SetWeight(float value) noexcept;
        bool IsPlaying() const noexcept;
        float Time() const noexcept;
        float Duration() const noexcept;

        // Event Track は Advance 前の位置から「実際に通過した区間」を復元するために
        // wrap と PingPong の進行方向だけを読む。再生状態の所有権は Player に残す。
        int RuntimeWrapMode() const noexcept { return EffectiveWrapMode(); }
        int PlaybackDirection() const noexcept
        {
            if (effective_speed_ == 0.0f) return 0;
            return EffectiveWrapMode() == PingPong
                ? ping_pong_direction_ : (effective_speed_ < 0.0f ? -1 : 1);
        }

        bool NeedsSnapshot() const noexcept;
        void StoreSnapshot(std::vector<SnapshotValue> values);
        const Reflection::PropertyValue* SnapshotFor(
            const Motion::MotionBinding& binding) const noexcept;
        float BlendInAlpha() const noexcept;
        bool HasStopRestoreRequest() const noexcept;
        const std::vector<SnapshotValue>& SnapshotValues() const noexcept;
        void ConsumeStopRestoreRequest() noexcept;

        Reflection::AssetReference motion;
        std::string key;
        bool play_on_start = true;
        int trigger = TriggerStart;
        float trigger_delay = 0.0f;
        Reflection::ComponentReference trigger_source;
        std::string trigger_state;
        bool loop = false;
        int wrap_mode = Once;
        bool auto_stop_on_end = false;
        // Pause Menu や演出用。true の Player だけ unscaled delta で進める。
        bool ignore_time_scale = false;
        float blend_in_seconds = 0.0f;
        float speed = 1.0f;
        float weight = 1.0f;
        int state = Stopped;

        float time = 0.0f;
        int random_seed = 0;
        float time_offset_random = 0.0f;
        float speed_random = 0.0f;

    private:
        int EffectiveWrapMode() const noexcept;
        void RequestTrigger() noexcept;
        void EnsureTriggerSubscriptions();
        void ReleaseTriggerSubscriptions() noexcept;
        bool MatchesTriggerSource(const Runtime::EventRecord& record) const noexcept;
        void HandleButtonStateChanged(const Runtime::EventRecord& record);
        void HandleMotionEvent(const Runtime::EventRecord& record);
        void HandleSceneTransition(const Runtime::EventRecord& record);
        void HandleStateChanged(const Runtime::EventRecord& record);
        void PrepareRandomizedPlayback() noexcept;

        std::vector<SnapshotValue> snapshot_values_;
        Runtime::ScopedSubscription button_state_subscription_;
        Runtime::ScopedSubscription motion_event_subscription_;
        Runtime::ScopedSubscription state_changed_subscription_;
        Runtime::ScopedSubscription scene_started_subscription_;
        Runtime::ScopedSubscription scene_completed_subscription_;
        bool snapshot_valid_ = false;
        bool stop_restore_requested_ = false;
        bool trigger_pending_ = false;
        float blend_in_elapsed_ = 0.0f;
        float trigger_elapsed_ = 0.0f;
        int ping_pong_direction_ = 1;
        float duration_ = 0.0f;
        float effective_speed_ = 1.0f;
        float random_speed_factor_ = 1.0f;
        float start_time_offset_ = 0.0f;
        std::uint64_t random_play_count_ = 0;
        std::uint64_t random_nonce_ = 0;
    };
}
