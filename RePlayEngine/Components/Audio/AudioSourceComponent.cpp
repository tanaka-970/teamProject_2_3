#include "AudioSourceComponent.h"

#include "../../Object/GameObject/GameObject.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Scene/Services/SceneServices.h"

#include <algorithm>
#include <cmath>

namespace ReplayEngine::Components
{
    namespace
    {
        float ClampFinite(float value, float fallback, float minimum, float maximum) noexcept
        {
            if (!std::isfinite(value)) return fallback;
            return (std::max)(minimum, (std::min)(maximum, value));
        }
    }

    void AudioSourceComponent::Play()
    {
        Audio::IAudioPlaybackService* audio = AudioService();
        if (audio == nullptr) return;

        Stop();
        voice_ = audio->Play(BuildParams());
    }

    void AudioSourceComponent::Stop() noexcept
    {
        if (!voice_.Valid()) return;

        if (Audio::IAudioPlaybackService* audio = AudioService())
        {
            audio->Stop(voice_);
        }
        voice_.Reset();
    }

    void AudioSourceComponent::OnEnable()
    {
        play_on_start_consumed_ = false;
    }

    void AudioSourceComponent::OnDisable()
    {
        Stop();
        play_on_start_consumed_ = false;
    }

    void AudioSourceComponent::OnUpdate(float /*delta_time*/)
    {
        const Scene::Scene* scene = GetScene();
        const bool playing = scene != nullptr && scene->Services().Playing();
        if (!playing)
        {
            Stop();
            return;
        }

        if (play_on_start && !play_on_start_consumed_)
        {
            play_on_start_consumed_ = true;
            Play();
        }

        if (voice_.Valid())
        {
            if (Audio::IAudioPlaybackService* audio = AudioService())
                audio->UpdateVoice(voice_, BuildParams());
            else
                voice_.Reset();
        }
    }

    void AudioSourceComponent::OnRuntimeDestroy()
    {
        Stop();
    }

    Audio::IAudioPlaybackService* AudioSourceComponent::AudioService() const noexcept
    {
        Scene::Scene* scene = GetScene();
        return scene != nullptr ? scene->Services().Audio() : nullptr;
    }

    Audio::AudioPlaybackParams AudioSourceComponent::BuildParams() const
    {
        Audio::AudioPlaybackParams params{};
        params.clip_path = clip_path;
        params.loop = loop;
        params.volume = ClampFinite(volume, 1.0f, 0.0f, 4.0f);
        params.pitch = ClampFinite(pitch, 1.0f, 0.25f, 4.0f);
        params.spatial_mode = spatial == static_cast<int>(Audio::AudioSpatialMode::ThreeD)
            ? Audio::AudioSpatialMode::ThreeD
            : Audio::AudioSpatialMode::TwoD;
        params.min_distance = ClampFinite(min_distance, 1.0f, 0.0f, 100000.0f);
        params.max_distance = ClampFinite(max_distance, 30.0f,
            params.min_distance + 0.001f, 100000.0f);

        const Core::GameObject* owner = Owner();
        if (owner != nullptr)
        {
            params.position = owner->GetTransform().WorldPosition();
        }
        return params;
    }
}
