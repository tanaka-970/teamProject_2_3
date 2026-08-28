#include "AudioSourceComponent.h"

#include "../../Object/GameObject/GameObject.h"
#include "../../Runtime/API/RuntimeContext.h"
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

    bool AudioSourceComponent::IsPlaying() const noexcept
    {
        if (!voice_.Valid()) return false;
        const Audio::IAudioPlaybackService* audio = AudioService();
        return audio != nullptr && audio->IsPlaying(voice_);
    }

    void AudioSourceComponent::OnEnable()
    {
        play_on_start_consumed_ = false;
    }

    void AudioSourceComponent::OnDisable()
    {
        motion_event_subscription_.Release();
        Stop();
        play_on_start_consumed_ = false;
    }

    void AudioSourceComponent::OnUpdate(float /*delta_time*/)
    {
        const Scene::Scene* scene = GetScene();
        const bool playing = scene != nullptr && scene->Services().Playing();
        if (!playing)
        {
            motion_event_subscription_.Release();
            Stop();
            return;
        }

        EnsureMotionEventSubscription();

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
        motion_event_subscription_.Release();
        Stop();
    }

    void AudioSourceComponent::EnsureMotionEventSubscription()
    {
        if (motion_event_subscription_.Valid()) return;

        Scene::Scene* scene = GetScene();
        Runtime::RuntimeContext* runtime =
            scene != nullptr ? scene->Services().Runtime() : nullptr;
        if (runtime == nullptr || Owner() == nullptr) return;

        const Runtime::ObjectHandle owner_handle =
            runtime->Resolver().MakeHandle(Owner());
        if (owner_handle.IsEmpty()) return;

        motion_event_subscription_ = runtime->Events().Subscribe(
            Runtime::EngineEvents::MotionEvent,
            [this](const Runtime::EventRecord& record)
            {
                HandleMotionEvent(record);
            },
            owner_handle);
    }

    void AudioSourceComponent::HandleMotionEvent(const Runtime::EventRecord& record)
    {
        Scene::Scene* scene = GetScene();
        Runtime::RuntimeContext* runtime =
            scene != nullptr ? scene->Services().Runtime() : nullptr;
        if (runtime == nullptr || Owner() == nullptr || !ActiveInHierarchy()) return;

        const Runtime::ObjectHandle owner_handle =
            runtime->Resolver().MakeHandle(Owner());
        if (!record.target.IsEmpty() && record.target != owner_handle) return;

        const Reflection::PropertyValue* name_value =
            record.payload.Find("name");
        const std::string name = name_value != nullptr
            ? name_value->AsString()
            : std::string();

        if (name == "StopSound")
        {
            Stop();
            return;
        }
        if (name != "PlaySound") return;

        const Reflection::PropertyValue* parameter_value =
            record.payload.Find("parameter");
        const std::string parameter = parameter_value != nullptr
            ? parameter_value->AsString()
            : std::string();
        if (parameter.empty())
        {
            Play();
            return;
        }

        const std::string previous_clip = clip_path;
        clip_path = parameter;
        Play();
        clip_path = previous_clip;
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
