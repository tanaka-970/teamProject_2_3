#include "BehaviourValidationInternal.h"

#include "../../Motion/MotionMixer.h"

namespace ReplayEngine::Runtime::Validation::Detail::BehaviourValidation
{
    int RunExecutionOrderValidation()
    {
        Checker check(900);

        // 全値 0 は、修正前と同じ GameObject 順 x Component 順を通る。
        {
            Scene::Scene world("ExecutionOrderLegacy");
            std::vector<std::string> trace;
            Core::GameObject* first = world.CreateGameObject("First");
            Core::GameObject* second = world.CreateGameObject("Second");
            auto* first_a = first != nullptr
                ? first->AddComponent<LifecycleProbeBehaviour>() : nullptr;
            auto* first_b = first != nullptr
                ? first->AddComponent<LifecycleProbeBehaviour>() : nullptr;
            auto* second_a = second != nullptr
                ? second->AddComponent<LifecycleProbeBehaviour>() : nullptr;
            check.Expect(first_a != nullptr && first_b != nullptr && second_a != nullptr,
                "legacy fast path の検証用 Component を作れる");
            if (first_a != nullptr && first_b != nullptr && second_a != nullptr)
            {
                first_a->phase_trace = &trace;
                first_a->trace_name = "first.a";
                first_b->phase_trace = &trace;
                first_b->trace_name = "first.b";
                second_a->phase_trace = &trace;
                second_a->trace_name = "second.a";

                world.Start();
                trace.clear();
                world.Update(0.016f);
                world.FixedUpdate(0.02f);
                world.LateUpdate(0.016f);
                const std::vector<std::string> expected = {
                    "U:first.a", "U:first.b", "U:second.a",
                    "F:first.a", "F:first.b", "F:second.a",
                    "L:first.a", "L:first.b", "L:second.a",
                };
                check.Expect(trace == expected,
                    "全値 0 は Update/Fixed/Late とも従来の発見順を保つ");
            }
        }

        // 非 0 があれば昇順。同値は discovery index で固定する。
        {
            Scene::Scene world("ExecutionOrderSorted");
            std::vector<std::string> trace;
            Core::GameObject* first = world.CreateGameObject("First");
            Core::GameObject* second = world.CreateGameObject("Second");
            auto* high = first != nullptr
                ? first->AddComponent<LifecycleProbeBehaviour>() : nullptr;
            auto* tie_first = first != nullptr
                ? first->AddComponent<LifecycleProbeBehaviour>() : nullptr;
            auto* low = second != nullptr
                ? second->AddComponent<LifecycleProbeBehaviour>() : nullptr;
            auto* tie_second = second != nullptr
                ? second->AddComponent<LifecycleProbeBehaviour>() : nullptr;
            auto* disabled = second != nullptr
                ? second->AddComponent<LifecycleProbeBehaviour>() : nullptr;
            check.Expect(high != nullptr && tie_first != nullptr && low != nullptr &&
                tie_second != nullptr && disabled != nullptr,
                "ordered path の検証用 Component を作れる");
            if (high != nullptr && tie_first != nullptr && low != nullptr &&
                tie_second != nullptr && disabled != nullptr)
            {
                const auto configure = [&trace](LifecycleProbeBehaviour& probe,
                    const char* name, std::int32_t order)
                {
                    probe.phase_trace = &trace;
                    probe.trace_name = name;
                    probe.execution_order = order;
                };
                configure(*high, "high", 100);
                configure(*tie_first, "tie.first", 0);
                configure(*low, "low", -100);
                configure(*tie_second, "tie.second", 0);
                configure(*disabled, "disabled", -300);
                disabled->SetEnabled(false);

                world.Start();
                trace.clear();
                world.Update(0.016f);
                const std::vector<std::string> update_expected = {
                    "U:low", "U:tie.first", "U:tie.second", "U:high",
                };
                check.Expect(trace == update_expected,
                    "Update は -100, 0, 0, 100 で、同値は発見順を保つ");

                trace.clear();
                world.FixedUpdate(0.02f);
                world.FixedUpdate(0.02f);
                const std::vector<std::string> fixed_expected = {
                    "F:low", "F:tie.first", "F:tie.second", "F:high",
                    "F:low", "F:tie.first", "F:tie.second", "F:high",
                };
                check.Expect(trace == fixed_expected,
                    "複数 fixed substep の各回で同じ昇順を保つ");

                trace.clear();
                world.LateUpdate(0.016f);
                const std::vector<std::string> late_expected = {
                    "L:low", "L:tie.first", "L:tie.second", "L:high",
                };
                check.Expect(trace == late_expected,
                    "LateUpdate も execution_order 昇順になる");
            }
        }

        // 候補はフェーズ開始時 snapshot。削除予約と Active は呼び出し直前に再確認する。
        {
            Scene::Scene world("ExecutionOrderMutation");
            std::vector<std::string> trace;
            Core::GameObject* object = world.CreateGameObject("Mutating");
            auto* mutator = object != nullptr
                ? object->AddComponent<LifecycleProbeBehaviour>() : nullptr;
            auto* victim = object != nullptr
                ? object->AddComponent<LifecycleProbeBehaviour>() : nullptr;
            check.Expect(mutator != nullptr && victim != nullptr,
                "追加削除 snapshot の検証用 Component を作れる");
            if (mutator != nullptr && victim != nullptr)
            {
                mutator->phase_trace = &trace;
                mutator->trace_name = "mutator";
                mutator->execution_order = -100;
                mutator->add_probe_in_update = true;
                mutator->destroy_target_in_update = victim;
                victim->phase_trace = &trace;
                victim->trace_name = "victim";
                victim->execution_order = 100;

                world.Start();
                trace.clear();
                world.Update(0.016f);
                check.Expect(trace == std::vector<std::string>{ "U:mutator" },
                    "更新中の追加は同フェーズで呼ばず、削除予約は直前に除外する");

                trace.clear();
                world.Update(0.016f);
                const std::vector<std::string> next_expected = {
                    "U:mutator.added", "U:mutator",
                };
                check.Expect(trace == next_expected &&
                    mutator->update_count == 2 && mutator->added_probe != nullptr &&
                    mutator->added_probe->update_count == 1,
                    "追加 Component は次フレームから 1 回だけ実行される");
            }
        }

        // Motion は Scene の後段ステージのまま。複数寄与も Apply の setter は 1 回。
        {
            Scene::Scene world("MotionSetterCount");
            Core::GameObject* object = world.CreateGameObject("MotionTarget");
            auto* probe = object != nullptr
                ? object->AddComponent<LifecycleProbeBehaviour>() : nullptr;

            Reflection::PropertyDesc property;
            property.name = "motion_value";
            property.type = Reflection::PropertyType::Float;
            property.animatable = Reflection::Animatable::Interpolatable;
            property.getter = [](const Core::Component& component)
            {
                const auto& target = static_cast<const LifecycleProbeBehaviour&>(component);
                return Reflection::PropertyValue::MakeFloat(target.motion_value);
            };
            property.setter = [](Core::Component& component,
                const Reflection::PropertyValue& value)
            {
                auto& target = static_cast<LifecycleProbeBehaviour&>(component);
                target.motion_value = value.AsFloat(target.motion_value);
                ++target.motion_setter_count;
            };

            Motion::ResolvedMotionBinding binding{ probe, &property };
            Motion::MotionMixer mixer;
            mixer.BeginFrame();
            mixer.Contribute(binding, Reflection::PropertyValue::MakeFloat(2.0f), 1.0f);
            mixer.Contribute(binding, Reflection::PropertyValue::MakeFloat(6.0f), 1.0f);
            mixer.Apply();
            check.Expect(probe != nullptr && probe->motion_setter_count == 1 &&
                mixer.WasDriven(*probe, property.name),
                "同一 Property への複数 Motion 寄与でも setter は 1 回だけ");
        }

        return check.Report("Execution order validation");
    }
}
