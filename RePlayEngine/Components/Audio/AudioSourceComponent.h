#pragma once

#include "../../Audio/AudioService.h"
#include "../../Object/Component/Component.h"
#include "../../Runtime/Events/EventBus.h"

#include <string>

namespace ReplayEngine::Components
{
    class AudioSourceComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(AudioSourceComponent)

    public:
        AudioSourceComponent() = default;

        std::string clip_path;
        bool loop = false;
        float volume = 1.0f;
        float pitch = 1.0f;
        bool play_on_start = false;
        int spatial = static_cast<int>(Audio::AudioSpatialMode::TwoD);
        float min_distance = 1.0f;
        float max_distance = 30.0f;

        void Play();
        void Stop() noexcept;

        void OnEnable() override;
        void OnDisable() override;
        void OnUpdate(float delta_time) override;
        void OnRuntimeDestroy() override;

    private:
        void EnsureMotionEventSubscription();
        void HandleMotionEvent(const Runtime::EventRecord& record);

        Audio::IAudioPlaybackService* AudioService() const noexcept;
        Audio::AudioPlaybackParams BuildParams() const;

        Audio::AudioVoiceHandle voice_;
        Runtime::ScopedSubscription motion_event_subscription_;
        bool play_on_start_consumed_ = false;
    };
}
