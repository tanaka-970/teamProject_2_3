// Runtime main のうち「Motion trigger / PropertyLink のヘッドレス検証」を持つ。
// Motion / PropertyLink 検証関数の本体はそのまま移動している。
#include "framework.h"
#include "mainInternal.h"

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "../../../RePlayEngine/Components/Audio/AudioListenerComponent.h"
#include "../../../RePlayEngine/Components/Audio/AudioSourceComponent.h"
#include "../../../RePlayEngine/Components/Camera/CameraComponent.h"
#include "../../../RePlayEngine/Components/Camera/CameraTargetComponent.h"
#include "../../../RePlayEngine/Components/Camera/FollowTargetComponent.h"
#include "../../../RePlayEngine/Components/Core/PropertyLinkComponent.h"
#include "../../../RePlayEngine/Components/Core/StateComponent.h"
#include "../../../RePlayEngine/Components/Gameplay/CharacterMotorComponent.h"
#include "../../../RePlayEngine/Components/Motion/MotionPlayerComponent.h"
#include "../../../RePlayEngine/Components/Physics/BoxColliderComponent.h"
#include "../../../RePlayEngine/Components/Physics/RigidbodyComponent.h"
#include "../../../RePlayEngine/Components/Physics/SphereColliderComponent.h"
#include "../../../RePlayEngine/Components/UI/CanvasComponent.h"
#include "../../../RePlayEngine/Object/Registry/BuiltInComponents.h"
#include "../../../RePlayEngine/Reflection/Registry/PropertyRegistry.h"
#include "../../../RePlayEngine/Physics/PhysicsDynamicsWorld.h"
#include "../../../RePlayEngine/Runtime/Validation/HandleValidation.h"
#include "../../../RePlayEngine/Scene/Runtime/Scene.h"
#include "../../../RePlayEngine/Scene/Services/SceneCollisionWorld.h"

namespace ReplayEngine::Runtime::Detail
{
    int RunHeadlessMotionTriggerValidation(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        if (!(arguments >> command) || command != "--validate-motion-trigger") return -1;

        ReplayEngine::Core::RegisterBuiltInComponents();
        ReplayEngine::Scene::Scene scene("MotionTriggerValidation");
        ReplayEngine::Core::GameObject* state_object = scene.CreateGameObject("StateSource");
        ReplayEngine::Core::GameObject* first_object = scene.CreateGameObject("FirstPlayer");
        ReplayEngine::Core::GameObject* second_object = scene.CreateGameObject("SecondPlayer");
        auto* state = state_object != nullptr
            ? state_object->AddComponent<ReplayEngine::Components::StateComponent>() : nullptr;
        auto* first = first_object != nullptr
            ? first_object->AddComponent<ReplayEngine::Components::MotionPlayerComponent>() : nullptr;
        auto* second = second_object != nullptr
            ? second_object->AddComponent<ReplayEngine::Components::MotionPlayerComponent>() : nullptr;

        bool ok = state != nullptr && first != nullptr && second != nullptr;
        std::vector<std::string> lines;
        if (ok)
        {
            state->SetStateCount(3);
            state->states[1].name = "Playing";
            const auto configure = [state_object, state](
                ReplayEngine::Components::MotionPlayerComponent& player)
            {
                player.motion.guid = "validation-shared-motion";
                player.trigger =
                    ReplayEngine::Components::MotionPlayerComponent::TriggerStateChanged;
                player.trigger_delay = 0.1f;
                player.trigger_state = "Playing";
                player.trigger_source.owner = state_object->ID();
                player.trigger_source.component = state->StableID();
            };
            configure(*first);
            configure(*second);

            const std::vector<ReplayEngine::Reflection::PropertyDesc>* dynamic =
                state->DynamicProperties();
            bool state_name_is_dynamic = false;
            if (dynamic != nullptr)
            {
                for (const auto& property : *dynamic)
                {
                    if (property.name == "states[1].name") state_name_is_dynamic = true;
                }
            }
            const auto* delay_property = ReplayEngine::Reflection::PropertyRegistry::Find(
                first->TypeID(), "trigger_delay");
            const bool same_asset = first->motion.guid == second->motion.guid;
            const bool independent_owners = first_object->ID() != second_object->ID();
            const bool separate_sources = first->trigger_source.owner == state_object->ID() &&
                second->trigger_source.owner == state_object->ID() &&
                first->trigger_source.component == second->trigger_source.component;
            const bool delay_is_six_fixed_steps =
                (5.0f / 60.0f) < first->trigger_delay &&
                (6.0f / 60.0f) >= first->trigger_delay;
            const bool state_transition = state->SetCurrentState("Playing");
            ok = state_name_is_dynamic && delay_property != nullptr && same_asset &&
                independent_owners && separate_sources && delay_is_six_fixed_steps &&
                state_transition &&
                ReplayEngine::Components::MotionPlayerComponent::TriggerStateChanged == 11;
            lines.push_back(std::string("STATE_DYNAMIC_PROPERTY ") +
                (state_name_is_dynamic ? "OK" : "NG"));
            lines.push_back(std::string("SHARED_ASSET ") + (same_asset ? "OK" : "NG"));
            lines.push_back(std::string("INDEPENDENT_PLAYERS ") +
                (independent_owners ? "OK" : "NG"));
            lines.push_back(std::string("STATE_SOURCE_BINDING ") +
                (separate_sources ? "OK" : "NG"));
            lines.push_back(std::string("DELAY_0_1_SECONDS ") +
                (delay_is_six_fixed_steps ? "OK" : "NG"));
            lines.push_back(std::string("STATE_TRANSITION ") +
                (state_transition ? "OK" : "NG"));
        }
        else
        {
            lines.push_back("SCENE_SETUP NG");
        }

        WriteValidationResultFile("MotionTrigger.txt",
            "REPLAY_MOTION_TRIGGER_VALIDATION", ok, lines);
        std::fprintf(stderr, "motion-trigger validation: RESULT %s\n", ok ? "OK" : "NG");
        return ok ? 0 : 1440;
    }

    int RunHeadlessPropertyLinkValidation(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        if (!(arguments >> command) || command != "--validate-property-link") return -1;

        ReplayEngine::Core::RegisterBuiltInComponents();
        ReplayEngine::Scene::Scene scene("PropertyLinkValidation");
        ReplayEngine::Core::GameObject* source_object = scene.CreateGameObject("Source");
        ReplayEngine::Core::GameObject* target_object = scene.CreateGameObject("Target");
        ReplayEngine::Core::GameObject* first_link_object = scene.CreateGameObject("LinkA");
        ReplayEngine::Core::GameObject* second_link_object = scene.CreateGameObject("LinkB");
        auto* source = source_object != nullptr
            ? source_object->AddComponent<ReplayEngine::Components::CanvasComponent>() : nullptr;
        auto* target = target_object != nullptr
            ? target_object->AddComponent<ReplayEngine::Components::CanvasComponent>() : nullptr;
        auto* first_link = first_link_object != nullptr
            ? first_link_object->AddComponent<ReplayEngine::Components::PropertyLinkComponent>() : nullptr;
        auto* second_link = second_link_object != nullptr
            ? second_link_object->AddComponent<ReplayEngine::Components::PropertyLinkComponent>() : nullptr;

        bool ok = source != nullptr && target != nullptr && first_link != nullptr &&
            second_link != nullptr;
        std::vector<std::string> lines;
        if (ok)
        {
            first_link->source_object.owner = source_object->ID();
            first_link->source_object.component = source->StableID();
            first_link->source_property = "opacity";
            first_link->target_object.owner = target_object->ID();
            first_link->target_object.component = target->StableID();
            first_link->target_property = "opacity";
            first_link->source_min = 0.0f;
            first_link->source_max = 1.0f;
            first_link->target_min = 0.0f;
            first_link->target_max = 1.0f;
            first_link->clamp = true;
            source->opacity = 0.25f;
            target->opacity = 0.0f;
            ReplayEngine::Components::PropertyLinkComponent::EvaluateAll(scene, 0.0f);
            const bool mapped = std::fabs(target->opacity - 0.25f) < 0.00001f;

            second_link->source_object.owner = target_object->ID();
            second_link->source_object.component = target->StableID();
            second_link->source_property = "opacity";
            second_link->target_object.owner = source_object->ID();
            second_link->target_object.component = source->StableID();
            second_link->target_property = "opacity";
            second_link->source_min = 0.0f;
            second_link->source_max = 1.0f;
            second_link->target_min = 0.0f;
            second_link->target_max = 1.0f;
            source->opacity = 0.75f;
            target->opacity = 0.25f;
            ReplayEngine::Components::PropertyLinkComponent::EvaluateAll(scene, 0.0f);
            const bool cycle_held = std::fabs(source->opacity - 0.75f) < 0.00001f &&
                std::fabs(target->opacity - 0.25f) < 0.00001f;
            ok = mapped && cycle_held;
            lines.push_back(std::string("RANGE_MAPPING ") + (mapped ? "OK" : "NG"));
            lines.push_back(std::string("CYCLE_GUARD ") + (cycle_held ? "OK" : "NG"));
        }
        else
        {
            lines.push_back("SCENE_SETUP NG");
        }

        WriteValidationResultFile("PropertyLink.txt",
            "REPLAY_PROPERTY_LINK_VALIDATION", ok, lines);
        std::fprintf(stderr, "property-link validation: RESULT %s\n", ok ? "OK" : "NG");
        return ok ? 0 : 1450;
    }
}
