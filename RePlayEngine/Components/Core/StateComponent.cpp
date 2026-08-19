#include "StateComponent.h"

#include "../../Object/GameObject/GameObject.h"
#include "../../Reflection/Property/PropertyValue.h"
#include "../../Runtime/API/RuntimeContext.h"
#include "../../Runtime/Events/EventBus.h"
#include "../../Scene/Runtime/Scene.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <utility>

namespace ReplayEngine::Components
{
    namespace
    {
        constexpr int minimum_state_count = 2;
        constexpr int maximum_state_count = 16;

        std::string IndexedName(int index)
        {
            char buffer[64]{};
            std::snprintf(buffer, sizeof(buffer), "states[%d].name", index);
            return std::string(buffer);
        }
    }

    StateComponent::StateComponent()
    {
        SetStateCount(state_count);
    }

    void StateComponent::OnRuntimeAwake()
    {
        SetStateCount(state_count);
        if (!HasState(current_state))
            current_state = states.empty() ? std::string{} : states.front().name;
    }

    void StateComponent::SetStateCount(int count)
    {
        state_count = (std::max)(minimum_state_count,
            (std::min)(maximum_state_count, count));
        const std::size_t old_size = states.size();
        states.resize(static_cast<std::size_t>(state_count));
        for (std::size_t index = old_size; index < states.size(); ++index)
        {
            states[index].name = "State" + std::to_string(index);
        }
        if (!HasState(current_state) && !states.empty())
            current_state = states.front().name;
        RebuildDynamicProperties();
    }

    bool StateComponent::HasState(const std::string& name) const noexcept
    {
        for (const StateEntry& state : states)
        {
            if (state.name == name) return true;
        }
        return false;
    }

    bool StateComponent::SetCurrentState(const std::string& name)
    {
        if (!HasState(name) || current_state == name) return false;
        const std::string previous = current_state;
        current_state = name;
        PublishStateChanged(previous);
        return true;
    }

    const std::vector<Reflection::PropertyDesc>*
        StateComponent::DynamicProperties() const noexcept
    {
        RebuildDynamicProperties();
        return dynamic_properties_.empty() ? nullptr : &dynamic_properties_;
    }

    void StateComponent::RebuildDynamicProperties() const
    {
        dynamic_properties_.clear();
        dynamic_properties_.reserve(states.size());
        for (std::size_t index = 0; index < states.size(); ++index)
        {
            Reflection::PropertyDesc property;
            property.name = IndexedName(static_cast<int>(index));
            property.display_name = "状態名";
            property.category = "State " + std::to_string(index + 1);
            property.tooltip = "Motion Player の状態トリガーが参照する名前。";
            property.type = Reflection::PropertyType::String;
            property.animatable = Reflection::Animatable::None;
            property.getter = [index](const Core::Component& component)
            {
                const StateComponent& state = static_cast<const StateComponent&>(component);
                if (index >= state.states.size()) return Reflection::PropertyValue{};
                return Reflection::PropertyValue::MakeString(state.states[index].name);
            };
            property.setter = [index](Core::Component& component,
                const Reflection::PropertyValue& value)
            {
                StateComponent& state = static_cast<StateComponent&>(component);
                if (index >= state.states.size()) return;
                state.states[index].name = value.AsString();
                if (state.current_state.empty()) state.current_state = state.states[index].name;
            };
            dynamic_properties_.push_back(std::move(property));
        }
    }

    void StateComponent::PublishStateChanged(const std::string& previous_state)
    {
        Core::GameObject* owner = Owner();
        Scene::Scene* scene = GetScene();
        Runtime::RuntimeContext* runtime = scene != nullptr
            ? scene->Services().Runtime() : nullptr;
        if (owner == nullptr || runtime == nullptr) return;

        Runtime::EventRecord record;
        record.type = Runtime::EngineEvents::StateChanged;
        record.type_name = "StateChanged";
        record.source = runtime->Resolver().MakeHandle(owner);
        record.payload.Set("previous_state",
            Reflection::PropertyValue::MakeString(previous_state));
        record.payload.Set("state",
            Reflection::PropertyValue::MakeString(current_state));
        record.payload.Set("state_component",
            Reflection::PropertyValue::MakeUInt64(
                static_cast<std::uint64_t>(StableID())));
        record.frame_index = runtime->FrameIndex();
        runtime->Events().Publish(std::move(record));
    }
}
