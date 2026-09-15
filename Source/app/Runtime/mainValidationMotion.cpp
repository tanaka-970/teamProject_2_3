// Runtime main のうち「Motion trigger / PropertyLink のヘッドレス検証」を持つ。
// Motion / PropertyLink 検証関数の本体はそのまま移動している。
#include "framework.h"
#include "mainInternal.h"

#include <cmath>
#include <filesystem>
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
#include "../../../RePlayEngine/Components/UI/RectTransformComponent.h"
#include "../../../RePlayEngine/Motion/MotionAsset.h"
#include "../../../RePlayEngine/Motion/MotionEasing.h"
#include "../../../RePlayEngine/UI/UILayout.h"
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

        // EaseInPower は pow(t, 指数) と一致し、POWER が保存で往復する。
        {
            using ReplayEngine::Motion::MotionEasing;
            const float eased = ReplayEngine::Motion::ApplyEasing(
                MotionEasing::EaseInPower, 0.5f, {}, 2.6f);
            const bool power_value = std::fabs(eased - std::pow(0.5f, 2.6f)) <= 1.0e-6f;
            MotionEasing parsed = MotionEasing::Linear;
            const bool power_name =
                ReplayEngine::Motion::TryParseMotionEasing("EaseInPower", parsed) &&
                parsed == MotionEasing::EaseInPower &&
                std::string(ReplayEngine::Motion::ToString(parsed)) == "EaseInPower";

            ReplayEngine::Motion::MotionAsset source;
            ReplayEngine::Motion::MotionTrack power_track;
            power_track.name = "PowerTrack";
            power_track.binding.origin = static_cast<int>(
                ReplayEngine::Motion::MotionBindingOrigin::Self);
            power_track.binding.component_type =
                ReplayEngine::Components::RectTransformComponent::StaticTypeID();
            power_track.binding.property = "scale";
            power_track.value_type = ReplayEngine::Reflection::PropertyType::Vector2;
            ReplayEngine::Motion::MotionKeyframe power_key;
            power_key.time = 0.0f;
            power_key.value = ReplayEngine::Reflection::PropertyValue::MakeVector2({ 1.0f, 1.0f });
            power_key.easing = MotionEasing::EaseInPower;
            power_key.power = 2.6f;
            ReplayEngine::Motion::MotionKeyframe end_key = power_key;
            end_key.time = 1.0f;
            end_key.easing = MotionEasing::Linear;
            end_key.power = 2.0f;
            power_track.keys = { power_key, end_key };
            source.tracks.push_back(power_track);
            const std::filesystem::path power_path =
                std::filesystem::temp_directory_path() / "replay_power_easing.replaymotion";
            std::string power_error;
            ReplayEngine::Motion::MotionAsset loaded;
            const bool power_roundtrip =
                ReplayEngine::Motion::MotionAsset::SaveToFile(power_path, source, power_error) &&
                ReplayEngine::Motion::MotionAsset::LoadFromFile(power_path, loaded, power_error) &&
                loaded.tracks.size() == 1 && loaded.tracks[0].keys.size() == 2 &&
                loaded.tracks[0].keys[0].easing == MotionEasing::EaseInPower &&
                std::fabs(loaded.tracks[0].keys[0].power - 2.6f) <= 1.0e-5f;
            std::error_code remove_error;
            std::filesystem::remove(power_path, remove_error);

            ok = ok && power_value && power_name && power_roundtrip;
            lines.push_back(std::string("EASE_IN_POWER_VALUE ") + (power_value ? "OK" : "NG"));
            lines.push_back(std::string("EASE_IN_POWER_NAME ") + (power_name ? "OK" : "NG"));
            lines.push_back(std::string("EASE_IN_POWER_ROUNDTRIP ") + (power_roundtrip ? "OK" : "NG"));
        }

        // 親の回転・拡大率は propagate_transform がオンのときだけ子へ伝わる。
        {
            using ReplayEngine::Components::RectTransformComponent;
            const auto child_offset = [](bool propagate, float& out_dx, float& out_dy,
                float& out_width)
            {
                ReplayEngine::Scene::Scene ui_scene("PropagateTransformValidation");
                auto* canvas_object = ui_scene.CreateGameObject("Canvas");
                auto* parent_object = ui_scene.CreateGameObject("Group");
                auto* child_object = ui_scene.CreateGameObject("Child");
                if (canvas_object == nullptr || parent_object == nullptr || child_object == nullptr)
                    return false;
                auto* canvas = canvas_object->AddComponent<ReplayEngine::Components::CanvasComponent>();
                auto* canvas_rect = canvas_object->AddComponent<RectTransformComponent>();
                auto* parent_rect = parent_object->AddComponent<RectTransformComponent>();
                auto* child_rect = child_object->AddComponent<RectTransformComponent>();
                if (canvas == nullptr || canvas_rect == nullptr || parent_rect == nullptr ||
                    child_rect == nullptr) return false;
                canvas->reference_resolution = { 1920.0f, 1080.0f };
                canvas_rect->anchor_min = { 0.0f, 0.0f };
                canvas_rect->anchor_max = { 1.0f, 1.0f };
                canvas_rect->size_delta = { 0.0f, 0.0f };
                parent_object->SetParent(canvas_object);
                child_object->SetParent(parent_object);
                parent_rect->size_delta = { 0.0f, 0.0f };
                parent_rect->rotation = 90.0f;
                parent_rect->scale = { 2.0f, 2.0f };
                parent_rect->propagate_transform = propagate;
                child_rect->anchored_position = { 10.0f, 0.0f };
                child_rect->size_delta = { 4.0f, 4.0f };
                ReplayEngine::UI::UILayout::Resolve(ui_scene, 1920.0f, 1080.0f);

                const auto to_world = [](const RectTransformComponent& rect, float x, float y)
                {
                    const DirectX::XMVECTOR point = DirectX::XMVector3TransformCoord(
                        DirectX::XMVectorSet(x, y, 0.0f, 1.0f),
                        DirectX::XMLoadFloat4x4(&rect.ResolvedMatrix()));
                    return DirectX::XMFLOAT2{ DirectX::XMVectorGetX(point), DirectX::XMVectorGetY(point) };
                };
                const DirectX::XMFLOAT4 pr = parent_rect->ResolvedRect();
                const DirectX::XMFLOAT4 cr = child_rect->ResolvedRect();
                const DirectX::XMFLOAT2 parent_pivot = to_world(*parent_rect,
                    pr.x + pr.z * 0.5f, pr.y + pr.w * 0.5f);
                const DirectX::XMFLOAT2 child_center = to_world(*child_rect,
                    cr.x + cr.z * 0.5f, cr.y + cr.w * 0.5f);
                const DirectX::XMFLOAT2 child_left = to_world(*child_rect, cr.x, cr.y + cr.w * 0.5f);
                const DirectX::XMFLOAT2 child_right = to_world(*child_rect, cr.x + cr.z, cr.y + cr.w * 0.5f);
                out_dx = child_center.x - parent_pivot.x;
                out_dy = child_center.y - parent_pivot.y;
                out_width = std::sqrt((child_right.x - child_left.x) * (child_right.x - child_left.x) +
                    (child_right.y - child_left.y) * (child_right.y - child_left.y));
                return true;
            };
            float on_dx = 0.0f, on_dy = 0.0f, on_width = 0.0f;
            float off_dx = 0.0f, off_dy = 0.0f, off_width = 0.0f;
            const bool on_built = child_offset(true, on_dx, on_dy, on_width);
            const bool off_built = child_offset(false, off_dx, off_dy, off_width);
            const bool propagate_on = on_built && std::fabs(on_dx) <= 0.01f &&
                std::fabs(std::fabs(on_dy) - 20.0f) <= 0.01f && std::fabs(on_width - 8.0f) <= 0.01f;
            const bool propagate_off = off_built && std::fabs(off_dx - 10.0f) <= 0.01f &&
                std::fabs(off_dy) <= 0.01f && std::fabs(off_width - 4.0f) <= 0.01f;
            ok = ok && propagate_on && propagate_off;
            lines.push_back(std::string("PROPAGATE_TRANSFORM_ON ") + (propagate_on ? "OK" : "NG") +
                " dx=" + std::to_string(on_dx) + " dy=" + std::to_string(on_dy) +
                " width=" + std::to_string(on_width));
            lines.push_back(std::string("PROPAGATE_TRANSFORM_OFF ") + (propagate_off ? "OK" : "NG") +
                " dx=" + std::to_string(off_dx) + " dy=" + std::to_string(off_dy) +
                " width=" + std::to_string(off_width));
            if (!propagate_on || !propagate_off)
                std::fprintf(stderr, "%s\n%s\n", lines[lines.size() - 2].c_str(), lines.back().c_str());
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
