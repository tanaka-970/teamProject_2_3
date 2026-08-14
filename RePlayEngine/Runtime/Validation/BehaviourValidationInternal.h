#pragma once

#include "BehaviourValidation.h"

#include "../API/RuntimeContext.h"
#include "../Behaviour/BehaviourComponent.h"
#include "../Behaviour/BehaviourRegistry.h"
#include "../Events/CollisionEventDispatcher.h"
#include "../Events/EventBus.h"
#include "../Handles/HandleResolver.h"
#include "../../Components/Gameplay/CharacterMotorComponent.h"
#include "../../Components/Physics/SphereColliderComponent.h"
#include "../../Object/Registry/BuiltInComponents.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Reflection/Registry/PropertyRegistry.h"
#include "../../Scene/Runtime/Scene.h"

#include <cstdio>
#include <string>
#include <vector>

// BehaviourValidation の分割内部で共有する検証型であり、外部から使うものではない。
namespace ReplayEngine::Runtime::Validation::Detail::BehaviourValidation
{
        using Core::ObjectID;

        class Checker final
        {
        public:
            explicit Checker(int first_code) : next_code_(first_code) {}

            void Expect(bool condition, const char* what)
            {
                const int code = next_code_++;
                ++total_;
                if (condition) return;
                ++failures_;
                if (first_failure_ == 0) first_failure_ = code;
                std::fprintf(stderr, "  [FAIL %d] %s\n", code, what);
            }

            int Report(const char* title) const
            {
                if (first_failure_ == 0)
                {
                    std::fprintf(stderr, "%s OK: %d checks passed\n", title, total_);
                    return 0;
                }
                std::fprintf(stderr, "%s FAILED: %d/%d checks failed (first=%d)\n",
                    title, failures_, total_, first_failure_);
                return first_failure_;
            }

        private:
            int next_code_ = 0;
            int first_failure_ = 0;
            int total_ = 0;
            int failures_ = 0;
        };

        // ライフサイクルの呼び出し順を記録するだけの検証用 Behaviour。
        // 本番コードへは登録せず、この検証の中だけで Registry へ入れる。
        class LifecycleProbeBehaviour final : public BehaviourComponent
        {
            REPLAY_COMPONENT_BODY(LifecycleProbeBehaviour)

        public:
            static constexpr Reflection::TypeGUID StaticTypeGUID() noexcept
            {
                return Reflection::MakeTypeGUID("c0000000000000000000000000000001");
            }

            // 呼ばれた順に記録する。全インスタンスで共有すると順序が混ざるため、
            // インスタンスごとに持つ。
            std::vector<std::string> calls;

            int awake_count = 0;
            int start_count = 0;
            int enable_count = 0;
            int disable_count = 0;
            int update_count = 0;
            int fixed_update_count = 0;
            int late_update_count = 0;
            int destroy_count = 0;

            // Scene の execution_order 検証用。通常の Lifecycle 検証では未設定のまま。
            std::vector<std::string>* phase_trace = nullptr;
            std::string trace_name;
            bool add_probe_in_update = false;
            LifecycleProbeBehaviour* destroy_target_in_update = nullptr;
            LifecycleProbeBehaviour* added_probe = nullptr;
            bool update_mutation_done = false;

            // Awake の中で自分の破棄を要求するか。
            bool destroy_in_awake = false;

        protected:
            void OnAwake() override
            {
                ++awake_count;
                calls.push_back("Awake");
                if (destroy_in_awake) Destroy();
            }
            void OnEnable() override { ++enable_count; calls.push_back("Enable"); }
            void OnDisable() override { ++disable_count; calls.push_back("Disable"); }
            void OnStart() override { ++start_count; calls.push_back("Start"); }
            void OnUpdate(float) override
            {
                ++update_count;
                Trace("U:");
                if (update_mutation_done) return;

                if (add_probe_in_update && Owner() != nullptr)
                {
                    added_probe = Owner()->AddComponent<LifecycleProbeBehaviour>();
                    if (added_probe != nullptr && added_probe != this)
                    {
                        added_probe->phase_trace = phase_trace;
                        added_probe->trace_name = trace_name + ".added";
                        added_probe->execution_order = -200;
                    }
                }
                if (destroy_target_in_update != nullptr)
                {
                    destroy_target_in_update->Destroy();
                }
                update_mutation_done = add_probe_in_update ||
                    destroy_target_in_update != nullptr;
            }
            void OnFixedUpdate(float) override
            {
                ++fixed_update_count;
                Trace("F:");
            }
            void OnLateUpdate(float) override
            {
                ++late_update_count;
                Trace("L:");
            }
            void OnDestroy() override { ++destroy_count; calls.push_back("Destroy"); }

        private:
            void Trace(const char* phase)
            {
                if (phase_trace != nullptr)
                    phase_trace->push_back(std::string(phase) + trace_name);
            }
        };

        // ComponentDependencyRules と Scene 復元の検証専用型。
        class DependencyBaseComponent final : public Core::Component
        {
            REPLAY_COMPONENT_BODY(DependencyBaseComponent)
        };

        class DependencyMiddleComponent final : public Core::Component
        {
            REPLAY_COMPONENT_BODY(DependencyMiddleComponent)
        };

        class DependencyLeafComponent final : public Core::Component
        {
            REPLAY_COMPONENT_BODY(DependencyLeafComponent)

        public:
            bool dependencies_ready_at_runtime_awake = false;

            void OnRuntimeAwake() override
            {
                dependencies_ready_at_runtime_awake = Owner() != nullptr &&
                    Owner()->GetComponent<DependencyBaseComponent>() != nullptr &&
                    Owner()->GetComponent<DependencyMiddleComponent>() != nullptr;
            }
        };

        class DependencyMissingComponent final : public Core::Component
        {
            REPLAY_COMPONENT_BODY(DependencyMissingComponent)
        };

        class DependencyBrokenComponent final : public Core::Component
        {
            REPLAY_COMPONENT_BODY(DependencyBrokenComponent)
        };

        class DependencyCycleAComponent final : public Core::Component
        {
            REPLAY_COMPONENT_BODY(DependencyCycleAComponent)
        };

        class DependencyCycleBComponent final : public Core::Component
        {
            REPLAY_COMPONENT_BODY(DependencyCycleBComponent)
        };

    void RegisterProbe();
    int RunExecutionOrderValidation();
    int RunComponentDependencyValidation();
    bool ContainsInOrder(const std::vector<std::string>& calls,
        const char* first, const char* second);
}
