#include "CompositionPlayerComponent.h"

#include "../../Object/GameObject/GameObject.h"
#include "../../Reflection/Property/PropertyValue.h"
#include "../../Runtime/API/RuntimeContext.h"
#include "../../Scene/Runtime/Scene.h"

#include <algorithm>
#include <cmath>

namespace ReplayEngine::Components
{
    void CompositionPlayerComponent::OnRuntimeAwake()
    {
        time = 0.0f;
        state = play_on_start ? Playing : Stopped;
        EnsureStateSubscription();
    }

    void CompositionPlayerComponent::OnEnable()
    {
        EnsureStateSubscription();
        if (play_on_start && state == Stopped)
        {
            time = 0.0f;
            state = Playing;
        }
    }

    void CompositionPlayerComponent::OnDisable()
    {
        ReleaseStateSubscription();
    }

    void CompositionPlayerComponent::OnRuntimeDestroy()
    {
        ReleaseStateSubscription();
    }

    void CompositionPlayerComponent::EnsureStateSubscription()
    {
        if (!play_on_state_change || state_changed_subscription_.Valid()) return;
        Scene::Scene* scene = GetScene();
        Core::GameObject* owner = Owner();
        Runtime::RuntimeContext* runtime = scene != nullptr
            ? scene->Services().Runtime() : nullptr;
        if (runtime == nullptr || owner == nullptr) return;
        const Runtime::ObjectHandle owner_handle = runtime->Resolver().MakeHandle(owner);
        if (owner_handle.IsEmpty()) return;
        state_changed_subscription_ = runtime->Events().Subscribe(
            Runtime::EngineEvents::StateChanged,
            [this](const Runtime::EventRecord& record)
            {
                HandleStateChanged(record);
            }, owner_handle);
    }

    void CompositionPlayerComponent::ReleaseStateSubscription() noexcept
    {
        state_changed_subscription_.Release();
    }

    bool CompositionPlayerComponent::MatchesStateSource(
        const Runtime::EventRecord& record) const noexcept
    {
        if (record.source.IsEmpty()) return false;
        Scene::Scene* scene = GetScene();
        Core::GameObject* owner = Owner();
        if (scene == nullptr || owner == nullptr ||
            record.source.world != scene->WorldInstanceID()) return false;
        if (!state_source.IsAssigned()) return record.source.object == owner->ID();
        if (record.source.object != state_source.owner) return false;
        Core::GameObject* source_owner = scene->FindGameObjectByID(state_source.owner);
        if (source_owner == nullptr || source_owner->PendingDestroy() ||
            source_owner->FindComponentByStableID(state_source.component) == nullptr)
            return false;
        const Reflection::PropertyValue* component =
            record.payload.Find("state_component");
        return component == nullptr ||
            component->AsUInt64() == state_source.component;
    }

    void CompositionPlayerComponent::HandleStateChanged(
        const Runtime::EventRecord& record)
    {
        if (!play_on_state_change || !ActiveInHierarchy() ||
            !MatchesStateSource(record)) return;
        if (!state_name.empty())
        {
            const Reflection::PropertyValue* value = record.payload.Find("state");
            if (value == nullptr || value->AsString() != state_name) return;
        }
        time = 0.0f;
        state = Playing;
    }

    void CompositionPlayerComponent::Play() noexcept
    {
        state = Playing;
    }

    void CompositionPlayerComponent::Pause() noexcept
    {
        if (state == Playing) state = Paused;
    }

    void CompositionPlayerComponent::Resume() noexcept
    {
        if (state == Paused) state = Playing;
    }

    void CompositionPlayerComponent::Stop() noexcept
    {
        state = Stopped;
        time = 0.0f;
    }

    void CompositionPlayerComponent::SetTime(float seconds) noexcept
    {
        time = (std::max)(0.0f, seconds);
    }

    void CompositionPlayerComponent::Advance(float duration, float delta_time) noexcept
    {
        if (state != Playing || duration <= 0.0f || delta_time == 0.0f || speed == 0.0f)
            return;

        const double next = static_cast<double>(time) +
            static_cast<double>(delta_time) * static_cast<double>(speed);
        const double d = static_cast<double>(duration);
        if (loop)
        {
            double wrapped = std::fmod(next, d);
            if (wrapped < 0.0) wrapped += d;
            time = static_cast<float>(wrapped);
            return;
        }

        if (next <= 0.0)
        {
            time = 0.0f;
            state = hold_on_end ? Paused : Stopped;
        }
        else if (next >= d)
        {
            time = duration;
            state = hold_on_end ? Paused : Stopped;
        }
        else
        {
            time = static_cast<float>(next);
        }
    }
}
