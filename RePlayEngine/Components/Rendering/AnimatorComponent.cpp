#include "AnimatorComponent.h"

#include "../Gameplay/CharacterMotorComponent.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Reflection/Property/PropertyValue.h"
#include "../../Runtime/API/RuntimeContext.h"
#include "../../Runtime/Events/EventBus.h"
#include "../../Scene/Runtime/Scene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace ReplayEngine::Components
{
    namespace
    {
        constexpr int max_state_count = 64;
        constexpr int max_transition_count = 256;

        std::string IndexedPropertyName(const char* prefix, int index, const char* property)
        {
            char buffer[96]{};
            std::snprintf(buffer, sizeof(buffer), "%s[%d].%s", prefix, index, property);
            return std::string(buffer);
        }

        Reflection::PropertyDesc MakeAnimatorDynamicProperty(
            const char* prefix, int index, std::string property,
            Reflection::PropertyType type, const char* category)
        {
            Reflection::PropertyDesc desc;
            desc.name = IndexedPropertyName(prefix, index, property.c_str());
            desc.display_name = property;
            desc.category = category;
            desc.type = type;
            desc.animatable = Reflection::Animatable::None;
            desc.serializable = true;
            return desc;
        }
    }

    float AnimatorComponent::BlendFactor() const noexcept
    {
        if (!IsBlending()) return 1.0f;
        if (transition_duration_ <= 0.0f) return 1.0f;
        const float factor = transition_time_ / transition_duration_;
        return (std::max)(0.0f, (std::min)(1.0f, factor));
    }

    void AnimatorComponent::SetStateCount(int count)
    {
        count = (std::max)(0, (std::min)(max_state_count, count));
        const std::size_t old_size = states.size();
        states.resize(static_cast<std::size_t>(count));
        for (std::size_t i = old_size; i < states.size(); ++i)
        {
            states[i].name = "State" + std::to_string(i);
        }
        RebuildDynamicProperties();
    }

    void AnimatorComponent::SetTransitionCount(int count)
    {
        count = (std::max)(0, (std::min)(max_transition_count, count));
        transitions.resize(static_cast<std::size_t>(count));
        RebuildDynamicProperties();
    }

    const std::vector<Reflection::PropertyDesc>*
        AnimatorComponent::DynamicProperties() const noexcept
    {
        // Count の setter だけでなく、複製や古い Scene の復元から vector サイズが
        // 変わった場合にも Inspector/Serializer の顔ぶれを即座に合わせる。
        RebuildDynamicProperties();
        return dynamic_properties_.empty() ? nullptr : &dynamic_properties_;
    }

    void AnimatorComponent::RebuildDynamicProperties() const
    {
        dynamic_properties_.clear();
        dynamic_properties_.reserve(states.size() * 4 + transitions.size() * 6);

        for (std::size_t i = 0; i < states.size(); ++i)
        {
            const int index = static_cast<int>(i);
            const std::string category = "State " + std::to_string(index + 1);

            Reflection::PropertyDesc name = MakeAnimatorDynamicProperty(
                "states", index, "name", Reflection::PropertyType::String, category.c_str());
            name.display_name = "状態名";
            name.tooltip = "Transition の from / to と default_state が参照する名前。";
            name.getter = [index](const Core::Component& component)
            {
                const AnimatorComponent& animator = static_cast<const AnimatorComponent&>(component);
                if (index < 0 || static_cast<std::size_t>(index) >= animator.states.size())
                    return Reflection::PropertyValue{};
                return Reflection::PropertyValue::MakeString(animator.states[static_cast<std::size_t>(index)].name);
            };
            name.setter = [index](Core::Component& component, const Reflection::PropertyValue& value)
            {
                AnimatorComponent& animator = static_cast<AnimatorComponent&>(component);
                if (index < 0 || static_cast<std::size_t>(index) >= animator.states.size()) return;
                animator.states[static_cast<std::size_t>(index)].name = value.AsString();
            };
            dynamic_properties_.push_back(std::move(name));

            Reflection::PropertyDesc clip = MakeAnimatorDynamicProperty(
                "states", index, "clip", Reflection::PropertyType::Int, category.c_str());
            clip.display_name = "クリップ";
            clip.has_range = true; clip.minimum = -1.0; clip.maximum = 255.0; clip.step = 1.0;
            clip.getter = [index](const Core::Component& component)
            {
                const AnimatorComponent& animator = static_cast<const AnimatorComponent&>(component);
                if (index < 0 || static_cast<std::size_t>(index) >= animator.states.size())
                    return Reflection::PropertyValue{};
                return Reflection::PropertyValue::MakeInt(animator.states[static_cast<std::size_t>(index)].clip);
            };
            clip.setter = [index](Core::Component& component, const Reflection::PropertyValue& value)
            {
                AnimatorComponent& animator = static_cast<AnimatorComponent&>(component);
                if (index < 0 || static_cast<std::size_t>(index) >= animator.states.size()) return;
                animator.states[static_cast<std::size_t>(index)].clip = value.AsInt(-1);
            };
            dynamic_properties_.push_back(std::move(clip));

            Reflection::PropertyDesc loop_desc = MakeAnimatorDynamicProperty(
                "states", index, "loop", Reflection::PropertyType::Bool, category.c_str());
            loop_desc.display_name = "ループ";
            loop_desc.getter = [index](const Core::Component& component)
            {
                const AnimatorComponent& animator = static_cast<const AnimatorComponent&>(component);
                if (index < 0 || static_cast<std::size_t>(index) >= animator.states.size())
                    return Reflection::PropertyValue{};
                return Reflection::PropertyValue::MakeBool(animator.states[static_cast<std::size_t>(index)].loop);
            };
            loop_desc.setter = [index](Core::Component& component, const Reflection::PropertyValue& value)
            {
                AnimatorComponent& animator = static_cast<AnimatorComponent&>(component);
                if (index < 0 || static_cast<std::size_t>(index) >= animator.states.size()) return;
                animator.states[static_cast<std::size_t>(index)].loop = value.AsBool(true);
            };
            dynamic_properties_.push_back(std::move(loop_desc));

            Reflection::PropertyDesc speed = MakeAnimatorDynamicProperty(
                "states", index, "speed", Reflection::PropertyType::Float, category.c_str());
            speed.display_name = "状態再生速度";
            speed.has_range = true; speed.minimum = 0.0; speed.maximum = 5.0; speed.step = 0.01;
            speed.getter = [index](const Core::Component& component)
            {
                const AnimatorComponent& animator = static_cast<const AnimatorComponent&>(component);
                if (index < 0 || static_cast<std::size_t>(index) >= animator.states.size())
                    return Reflection::PropertyValue{};
                return Reflection::PropertyValue::MakeFloat(animator.states[static_cast<std::size_t>(index)].speed);
            };
            speed.setter = [index](Core::Component& component, const Reflection::PropertyValue& value)
            {
                AnimatorComponent& animator = static_cast<AnimatorComponent&>(component);
                if (index < 0 || static_cast<std::size_t>(index) >= animator.states.size()) return;
                animator.states[static_cast<std::size_t>(index)].speed = (std::max)(0.0f, value.AsFloat(1.0f));
            };
            dynamic_properties_.push_back(std::move(speed));
        }

        for (std::size_t i = 0; i < transitions.size(); ++i)
        {
            const int index = static_cast<int>(i);
            const std::string category = "Transition " + std::to_string(index + 1);

            auto make_string = [&](const char* property, const char* display,
                std::string AnimationTransition::* member)
            {
                Reflection::PropertyDesc desc = MakeAnimatorDynamicProperty(
                    "transitions", index, property, Reflection::PropertyType::String, category.c_str());
                desc.display_name = display;
                desc.getter = [index, member](const Core::Component& component)
                {
                    const AnimatorComponent& animator = static_cast<const AnimatorComponent&>(component);
                    if (index < 0 || static_cast<std::size_t>(index) >= animator.transitions.size())
                        return Reflection::PropertyValue{};
                    return Reflection::PropertyValue::MakeString(
                        animator.transitions[static_cast<std::size_t>(index)].*member);
                };
                desc.setter = [index, member](Core::Component& component,
                    const Reflection::PropertyValue& value)
                {
                    AnimatorComponent& animator = static_cast<AnimatorComponent&>(component);
                    if (index < 0 || static_cast<std::size_t>(index) >= animator.transitions.size()) return;
                    animator.transitions[static_cast<std::size_t>(index)].*member = value.AsString();
                };
                dynamic_properties_.push_back(std::move(desc));
            };
            make_string("from", "遷移元", &AnimationTransition::from);
            make_string("to", "遷移先", &AnimationTransition::to);
            make_string("parameter", "パラメーター", &AnimationTransition::parameter);

            Reflection::PropertyDesc condition = MakeAnimatorDynamicProperty(
                "transitions", index, "condition", Reflection::PropertyType::Enum, category.c_str());
            condition.display_name = "条件";
            condition.enum_labels = { "常に", "接地中", "空中", "水平速度 >", "水平速度 <=",
                "垂直速度 >", "垂直速度 <=", "Bool true", "Bool false",
                "Float >", "Float <=", "Trigger" };
            condition.getter = [index](const Core::Component& component)
            {
                const AnimatorComponent& animator = static_cast<const AnimatorComponent&>(component);
                if (index < 0 || static_cast<std::size_t>(index) >= animator.transitions.size())
                    return Reflection::PropertyValue{};
                return Reflection::PropertyValue::MakeEnum(static_cast<int>(
                    animator.transitions[static_cast<std::size_t>(index)].condition));
            };
            condition.setter = [index](Core::Component& component, const Reflection::PropertyValue& value)
            {
                AnimatorComponent& animator = static_cast<AnimatorComponent&>(component);
                if (index < 0 || static_cast<std::size_t>(index) >= animator.transitions.size()) return;
                int raw = value.AsInt(0);
                raw = (std::max)(0, (std::min)(11, raw));
                animator.transitions[static_cast<std::size_t>(index)].condition =
                    static_cast<TransitionCondition>(raw);
            };
            dynamic_properties_.push_back(std::move(condition));

            Reflection::PropertyDesc threshold = MakeAnimatorDynamicProperty(
                "transitions", index, "threshold", Reflection::PropertyType::Float, category.c_str());
            threshold.display_name = "しきい値";
            threshold.step = 0.01;
            threshold.getter = [index](const Core::Component& component)
            {
                const AnimatorComponent& animator = static_cast<const AnimatorComponent&>(component);
                if (index < 0 || static_cast<std::size_t>(index) >= animator.transitions.size())
                    return Reflection::PropertyValue{};
                return Reflection::PropertyValue::MakeFloat(
                    animator.transitions[static_cast<std::size_t>(index)].threshold);
            };
            threshold.setter = [index](Core::Component& component, const Reflection::PropertyValue& value)
            {
                AnimatorComponent& animator = static_cast<AnimatorComponent&>(component);
                if (index < 0 || static_cast<std::size_t>(index) >= animator.transitions.size()) return;
                animator.transitions[static_cast<std::size_t>(index)].threshold = value.AsFloat();
            };
            dynamic_properties_.push_back(std::move(threshold));

            Reflection::PropertyDesc blend = MakeAnimatorDynamicProperty(
                "transitions", index, "blend_time", Reflection::PropertyType::Float, category.c_str());
            blend.display_name = "ブレンド時間";
            blend.has_range = true; blend.minimum = 0.0; blend.maximum = 5.0; blend.step = 0.01;
            blend.getter = [index](const Core::Component& component)
            {
                const AnimatorComponent& animator = static_cast<const AnimatorComponent&>(component);
                if (index < 0 || static_cast<std::size_t>(index) >= animator.transitions.size())
                    return Reflection::PropertyValue{};
                return Reflection::PropertyValue::MakeFloat(
                    animator.transitions[static_cast<std::size_t>(index)].blend_time);
            };
            blend.setter = [index](Core::Component& component, const Reflection::PropertyValue& value)
            {
                AnimatorComponent& animator = static_cast<AnimatorComponent&>(component);
                if (index < 0 || static_cast<std::size_t>(index) >= animator.transitions.size()) return;
                animator.transitions[static_cast<std::size_t>(index)].blend_time =
                    (std::max)(0.0f, value.AsFloat(0.15f));
            };
            dynamic_properties_.push_back(std::move(blend));
        }
    }

    void AnimatorComponent::OnPropertyChanged(const char* /*property_name*/)
    {
        RebuildDynamicProperties();
    }

    void AnimatorComponent::ResetRuntimeState()
    {
        legacy_state_ = State::Idle;
        current_state_index_ = -1;
        current_state_name_.clear();
        current_clip_ = -1;
        animation_time_ = 0.0f;
        current_loop_ = loop;
        current_speed_ = 1.0f;
        previous_clip_ = -1;
        previous_animation_time_ = 0.0f;
        previous_loop_ = true;
        previous_speed_ = 1.0f;
        transition_time_ = 0.0f;
        transition_duration_ = 0.0f;
        bool_parameters_.clear();
        float_parameters_.clear();
        trigger_parameters_.clear();
    }

    void AnimatorComponent::OnStart()
    {
        ResetRuntimeState();
        if (!states.empty())
        {
            int initial = FindStateIndex(default_state);
            if (initial < 0) initial = 0;
            EnterState(initial, 0.0f);
            return;
        }

        current_clip_ = ClipForLegacyState(State::Idle);
    }

    void AnimatorComponent::OnDisable()
    {
        // Component を無効化しても現在姿勢を保持する。
        // Renderer は最後の時刻を描き続けるので、再有効化時に姿勢が飛ばない。
    }

    CharacterMotorComponent* AnimatorComponent::FindMotor() const
    {
        Core::GameObject* owner = Owner();
        return owner != nullptr ? owner->GetComponent<CharacterMotorComponent>() : nullptr;
    }

    int AnimatorComponent::ClipForLegacyState(State state) const noexcept
    {
        switch (state)
        {
        case State::Jump: return jump_clip;
        case State::Walk: return walk_clip;
        case State::Idle: break;
        }
        return idle_clip;
    }

    int AnimatorComponent::FindStateIndex(const std::string& name) const noexcept
    {
        if (name.empty()) return -1;
        for (std::size_t i = 0; i < states.size(); ++i)
        {
            if (states[i].name == name) return static_cast<int>(i);
        }
        return -1;
    }

    bool AnimatorComponent::TransitionMatches(const AnimationTransition& transition,
        const CharacterMotorComponent* motor) const noexcept
    {
        switch (transition.condition)
        {
        case TransitionCondition::Always:
            return true;
        case TransitionCondition::Grounded:
            return motor != nullptr && motor->ActiveInHierarchy() && motor->Grounded();
        case TransitionCondition::Airborne:
            return motor != nullptr && motor->ActiveInHierarchy() && !motor->Grounded();
        case TransitionCondition::PlanarSpeedGreater:
            return motor != nullptr && motor->ActiveInHierarchy() &&
                motor->PlanarSpeed() > transition.threshold;
        case TransitionCondition::PlanarSpeedLessEqual:
            return motor != nullptr && motor->ActiveInHierarchy() &&
                motor->PlanarSpeed() <= transition.threshold;
        case TransitionCondition::VerticalSpeedGreater:
            return motor != nullptr && motor->ActiveInHierarchy() &&
                motor->Velocity().y > transition.threshold;
        case TransitionCondition::VerticalSpeedLessEqual:
            return motor != nullptr && motor->ActiveInHierarchy() &&
                motor->Velocity().y <= transition.threshold;
        case TransitionCondition::BoolTrue:
            return !transition.parameter.empty() && GetBool(transition.parameter);
        case TransitionCondition::BoolFalse:
            return !transition.parameter.empty() && !GetBool(transition.parameter);
        case TransitionCondition::FloatGreater:
            return !transition.parameter.empty() &&
                GetFloat(transition.parameter) > transition.threshold;
        case TransitionCondition::FloatLessEqual:
            return !transition.parameter.empty() &&
                GetFloat(transition.parameter) <= transition.threshold;
        case TransitionCondition::Trigger:
            return !transition.parameter.empty() &&
                trigger_parameters_.find(transition.parameter) != trigger_parameters_.end();
        }
        return false;
    }

    void AnimatorComponent::EnterState(int state_index, float blend_time)
    {
        if (state_index < 0 || static_cast<std::size_t>(state_index) >= states.size()) return;
        if (state_index == current_state_index_) return;

        const AnimationState& next = states[static_cast<std::size_t>(state_index)];
        const std::string previous_state = current_state_name_;
        const int previous_clip = current_clip_;

        if (current_clip_ >= 0 && blend_time > 0.0f)
        {
            previous_clip_ = current_clip_;
            previous_animation_time_ = animation_time_;
            previous_loop_ = current_loop_;
            previous_speed_ = current_speed_;
            transition_duration_ = blend_time;
            transition_time_ = 0.0f;
        }
        else
        {
            previous_clip_ = -1;
            previous_animation_time_ = 0.0f;
            transition_duration_ = 0.0f;
            transition_time_ = 0.0f;
        }

        current_state_index_ = state_index;
        current_state_name_ = next.name;
        current_clip_ = next.clip;
        animation_time_ = 0.0f;
        current_loop_ = next.loop;
        current_speed_ = (std::max)(0.0f, next.speed);

        Scene::Scene* scene = GetScene();
        Runtime::RuntimeContext* runtime = scene != nullptr
            ? scene->Services().Runtime() : nullptr;
        if (runtime != nullptr && Owner() != nullptr &&
            runtime->Events().HasSubscribers(Runtime::EngineEvents::AnimatorStateChanged))
        {
            Runtime::EventRecord record;
            record.type = Runtime::EngineEvents::AnimatorStateChanged;
            record.type_name = "AnimatorStateChanged";
            record.source = runtime->Resolver().MakeHandle(Owner());
            record.frame_index = runtime->FrameIndex();
            record.payload.Set("previous_state",
                Reflection::PropertyValue::MakeString(previous_state));
            record.payload.Set("state",
                Reflection::PropertyValue::MakeString(current_state_name_));
            record.payload.Set("previous_clip",
                Reflection::PropertyValue::MakeInt(previous_clip));
            record.payload.Set("clip",
                Reflection::PropertyValue::MakeInt(current_clip_));
            record.payload.Set("blend_time",
                Reflection::PropertyValue::MakeFloat(transition_duration_));
            record.payload.Set("animator_component",
                Reflection::PropertyValue::MakeUInt64(StableID()));
            runtime->Events().Publish(std::move(record));
        }
    }

    bool AnimatorComponent::PlayState(const std::string& state_name,
        float blend_time, float start_time)
    {
        const int state = FindStateIndex(state_name);
        if (state < 0 || !std::isfinite(blend_time) || !std::isfinite(start_time))
            return false;
        if (state == current_state_index_)
        {
            animation_time_ = (std::max)(0.0f, start_time);
        }
        else
        {
            EnterState(state, (std::max)(0.0f, blend_time));
            animation_time_ = (std::max)(0.0f, start_time);
        }
        playing = true;
        return true;
    }

    void AnimatorComponent::Stop() noexcept
    {
        playing = false;
        animation_time_ = 0.0f;
        previous_clip_ = -1;
        transition_time_ = 0.0f;
        transition_duration_ = 0.0f;
    }

    void AnimatorComponent::SetBool(const std::string& name, bool value)
    {
        if (!name.empty()) bool_parameters_[name] = value;
    }

    bool AnimatorComponent::GetBool(const std::string& name) const noexcept
    {
        const auto found = bool_parameters_.find(name);
        return found != bool_parameters_.end() && found->second;
    }

    void AnimatorComponent::SetFloat(const std::string& name, float value)
    {
        if (!name.empty() && std::isfinite(value)) float_parameters_[name] = value;
    }

    float AnimatorComponent::GetFloat(const std::string& name) const noexcept
    {
        const auto found = float_parameters_.find(name);
        return found != float_parameters_.end() ? found->second : 0.0f;
    }

    void AnimatorComponent::SetTrigger(const std::string& name)
    {
        if (!name.empty()) trigger_parameters_.insert(name);
    }

    void AnimatorComponent::ResetTrigger(const std::string& name) noexcept
    {
        trigger_parameters_.erase(name);
    }

    void AnimatorComponent::OnUpdate(float delta_time)
    {
        const CharacterMotorComponent* motor = FindMotor();

        if (!states.empty())
        {
            if (current_state_index_ < 0 ||
                static_cast<std::size_t>(current_state_index_) >= states.size())
            {
                int initial = FindStateIndex(default_state);
                if (initial < 0) initial = 0;
                EnterState(initial, 0.0f);
            }

            // 保存順が優先順位になる。1 フレームに遷移は 1 回だけに限定し、
            // A->B->C が同じ Update 内で一気に走る予測不能な挙動を避ける。
            for (const AnimationTransition& transition : transitions)
            {
                const bool source_matches = transition.from.empty() || transition.from == "*" ||
                    transition.from == current_state_name_;
                if (!source_matches) continue;

                const int target = FindStateIndex(transition.to);
                if (target < 0 || target == current_state_index_) continue;
                if (!TransitionMatches(transition, motor)) continue;

                EnterState(target, transition.blend_time);
                if (transition.condition == TransitionCondition::Trigger)
                    trigger_parameters_.erase(transition.parameter);
                break;
            }

            if (playing)
            {
                const float dt = (std::max)(0.0f, delta_time) * (std::max)(0.0f, playback_speed);
                animation_time_ += dt * current_speed_;
                if (IsBlending())
                {
                    previous_animation_time_ += dt * previous_speed_;
                    transition_time_ += dt;
                    if (transition_time_ >= transition_duration_)
                    {
                        transition_time_ = transition_duration_;
                        previous_clip_ = -1;
                    }
                }
            }
            return;
        }

        // ---- 旧 Scene 互換経路 ---------------------------------------------
        State desired = State::Idle;
        if (motor != nullptr && motor->ActiveInHierarchy())
        {
            if (!motor->Grounded()) desired = State::Jump;
            else if (motor->PlanarSpeed() > walk_speed_threshold) desired = State::Walk;
        }

        if (desired != legacy_state_)
        {
            legacy_state_ = desired;
            animation_time_ = 0.0f;
        }

        const int clip = ClipForLegacyState(legacy_state_);
        if (clip >= 0 && clip != current_clip_)
        {
            current_clip_ = clip;
            animation_time_ = 0.0f;
        }
        else if (clip >= 0)
        {
            current_clip_ = clip;
        }

        current_loop_ = loop;
        if (playing) animation_time_ += (std::max)(0.0f, delta_time) * (std::max)(0.0f, playback_speed);
        if (loop && animation_time_ > 3600.0f) animation_time_ = 0.0f;
        previous_clip_ = -1;
    }
}
