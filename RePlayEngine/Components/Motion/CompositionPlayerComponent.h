#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/References.h"
#include "../../Runtime/Events/EventBus.h"

#include <string>

namespace ReplayEngine::Components
{
    class CompositionPlayerComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(CompositionPlayerComponent)

    public:
        enum PlayState : int
        {
            Stopped = 0,
            Playing = 1,
            Paused = 2,
        };

        void OnRuntimeAwake() override;
        void OnEnable() override;
        void OnDisable() override;
        void OnRuntimeDestroy() override;

        void Play() noexcept;
        void Pause() noexcept;
        void Resume() noexcept;
        void Stop() noexcept;
        void SetTime(float seconds) noexcept;
        void Advance(float duration, float delta_time) noexcept;
        bool IsPlaying() const noexcept { return state == Playing; }
        bool ShouldContribute() const noexcept { return state != Stopped; }

        Reflection::AssetReference composition;
        std::string key;
        bool play_on_start = true;
        // StateComponent の StateChanged をそのまま利用する。MotionPlayer と別の
        // State Machine は作らず、Composition の開始だけを既存 EventBus へ接続する。
        bool play_on_state_change = false;
        Reflection::ComponentReference state_source;
        std::string state_name;
        bool loop = false;
        bool ignore_time_scale = false;
        bool hold_on_end = true;
        float speed = 1.0f;
        float weight = 1.0f;

        float time = 0.0f;
        int state = Stopped;

    private:
        void EnsureStateSubscription();
        void ReleaseStateSubscription() noexcept;
        bool MatchesStateSource(const Runtime::EventRecord& record) const noexcept;
        void HandleStateChanged(const Runtime::EventRecord& record);

        Runtime::ScopedSubscription state_changed_subscription_;
    };
}
