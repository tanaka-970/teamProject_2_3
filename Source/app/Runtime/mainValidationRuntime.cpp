// Runtime main のうち「Runtime Component のヘッドレス検証」を持つ。
//
//   mainValidationRuntime.cpp … Handle / Camera / Player speed 検証（このファイル）
//   mainValidationPhysics.cpp … Physics 検証
//   mainValidationMotion.cpp  … Motion trigger / PropertyLink 検証
//
// 各検証関数の本体は分割前のまま移動し、結果コードと実行順序は変更しない。
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
    // D3D11 も Window も使わないため、ビルド直後にそのまま実行できる。
    int RunHeadlessHandleValidation(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        if (!(arguments >> command) || command != "--validate-handles") return -1;

        return ReplayEngine::Runtime::Validation::RunHandleValidation();
    }

    // Stabilization Phase A-2。
    //
    // CameraTargetComponent の値が Runtime Camera へ反映されることを、
    // D3D11 / Window なしで検証する。終了コード帯は 860-899。
    int RunHeadlessCameraComponentValidation(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        if (!(arguments >> command) || command != "--validate-camera-component") return -1;

        namespace Components = ReplayEngine::Components;
        namespace Core = ReplayEngine::Core;
        namespace Reflection = ReplayEngine::Reflection;
        namespace Scene = ReplayEngine::Scene;

        int next_code = 860;
        int first_failure = 0;
        int failures = 0;
        int total = 0;

        const auto expect = [&](bool condition, const char* what)
        {
            const int code = next_code++;
            ++total;
            if (condition) return;
            ++failures;
            if (first_failure == 0) first_failure = code;
            std::fprintf(stderr, "  [FAIL %d] %s\n", code, what);
        };

        const auto close = [](float a, float b)
        {
            return std::fabs(a - b) <= 0.0005f;
        };

        Core::RegisterBuiltInComponents();

        const Core::ComponentTypeID camera_target_type =
            Components::CameraTargetComponent::StaticTypeID();
        expect(Reflection::PropertyRegistry::Find(camera_target_type, "target_offset") != nullptr,
            "CameraTargetComponent exposes target_offset");
        expect(Reflection::PropertyRegistry::Find(camera_target_type, "look_at_offset") != nullptr,
            "CameraTargetComponent exposes look_at_offset");
        expect(Reflection::PropertyRegistry::Find(camera_target_type, "priority") != nullptr,
            "CameraTargetComponent exposes priority");
        expect(Reflection::PropertyRegistry::Find(camera_target_type,
            "field_of_view_degrees") == nullptr,
            "CameraTargetComponent no longer owns field_of_view_degrees");
        expect(Reflection::PropertyRegistry::Find(camera_target_type, "near_clip") == nullptr,
            "CameraTargetComponent no longer owns near_clip");
        expect(Reflection::PropertyRegistry::Find(camera_target_type, "far_clip") == nullptr,
            "CameraTargetComponent no longer owns far_clip");
        expect(Reflection::PropertyRegistry::Find(camera_target_type, "follow_distance") == nullptr,
            "CameraTargetComponent no longer owns follow_distance");

        const Core::ComponentTypeID camera_type =
            Components::CameraComponent::StaticTypeID();
        expect(Reflection::PropertyRegistry::Find(camera_type, "projection_mode") != nullptr,
            "CameraComponent exposes projection_mode");
        expect(Reflection::PropertyRegistry::Find(camera_type,
            "field_of_view_degrees") != nullptr,
            "CameraComponent exposes field_of_view_degrees");
        expect(Reflection::PropertyRegistry::Find(camera_type, "orthographic_size") != nullptr,
            "CameraComponent exposes orthographic_size");
        expect(Reflection::PropertyRegistry::Find(camera_type, "near_clip") != nullptr,
            "CameraComponent exposes near_clip");
        expect(Reflection::PropertyRegistry::Find(camera_type, "far_clip") != nullptr,
            "CameraComponent exposes far_clip");
        expect(Reflection::PropertyRegistry::Find(camera_type, "priority") != nullptr,
            "CameraComponent exposes priority");
        expect(Reflection::PropertyRegistry::Find(camera_type, "viewport_rect") != nullptr,
            "CameraComponent exposes viewport_rect");

        const Core::ComponentTypeID follow_type =
            Components::FollowTargetComponent::StaticTypeID();
        expect(Reflection::PropertyRegistry::Find(follow_type, "follow_distance") != nullptr,
            "FollowTargetComponent exposes follow_distance");
        expect(Reflection::PropertyRegistry::Find(follow_type, "follow_height") != nullptr,
            "FollowTargetComponent exposes follow_height");
        expect(Reflection::PropertyRegistry::Find(follow_type, "follow_lag") != nullptr,
            "FollowTargetComponent exposes follow_lag");
        expect(Reflection::PropertyRegistry::Find(follow_type,
            "rotation_input_enabled") != nullptr,
            "FollowTargetComponent exposes rotation_input_enabled");

        const Core::ComponentTypeID listener_type =
            Components::AudioListenerComponent::StaticTypeID();
        expect(Reflection::PropertyRegistry::Find(listener_type, "priority") != nullptr,
            "AudioListenerComponent exposes priority");

        const Core::ComponentTypeID audio_source_type =
            Components::AudioSourceComponent::StaticTypeID();
        expect(Reflection::PropertyRegistry::Find(audio_source_type, "clip_path") != nullptr,
            "AudioSourceComponent exposes clip_path");
        expect(Reflection::PropertyRegistry::Find(audio_source_type, "loop") != nullptr,
            "AudioSourceComponent exposes loop");
        expect(Reflection::PropertyRegistry::Find(audio_source_type, "volume") != nullptr,
            "AudioSourceComponent exposes volume");
        expect(Reflection::PropertyRegistry::Find(audio_source_type, "pitch") != nullptr,
            "AudioSourceComponent exposes pitch");
        expect(Reflection::PropertyRegistry::Find(audio_source_type, "play_on_start") != nullptr,
            "AudioSourceComponent exposes play_on_start");
        expect(Reflection::PropertyRegistry::Find(audio_source_type, "spatial") != nullptr,
            "AudioSourceComponent exposes spatial");
        expect(Reflection::PropertyRegistry::Find(audio_source_type, "min_distance") != nullptr,
            "AudioSourceComponent exposes min_distance");
        expect(Reflection::PropertyRegistry::Find(audio_source_type, "max_distance") != nullptr,
            "AudioSourceComponent exposes max_distance");

        Scene::Scene world("CameraComponentValidation");
        Core::GameObject* low = world.CreateGameObject("LowPriorityTarget");
        Core::GameObject* high = world.CreateGameObject("HighPriorityTarget");
        expect(low != nullptr && high != nullptr, "camera target objects can be created");
        if (low == nullptr || high == nullptr)
        {
            return first_failure != 0 ? first_failure : 860;
        }

        auto* low_target = low->AddComponent<Components::CameraTargetComponent>();
        auto* high_target = high->AddComponent<Components::CameraTargetComponent>();
        expect(low_target != nullptr && high_target != nullptr,
            "CameraTargetComponent can be attached");
        if (low_target == nullptr || high_target == nullptr)
        {
            return first_failure != 0 ? first_failure : 861;
        }

        low_target->priority = 1;
        high_target->priority = 5;

        Components::CameraTargetSelection selection =
            Components::ResolveCameraTargetSelection(world, Core::ObjectID::Invalid());
        expect(selection.object == high && selection.component == high_target &&
            !selection.used_controlled_object,
            "without controlled object, highest priority active camera target is selected");

        selection = Components::ResolveCameraTargetSelection(world, low->ID());
        expect(selection.object == low && selection.component == low_target &&
            selection.used_controlled_object,
            "controlled object camera target wins over priority fallback");

        low_target->SetEnabled(false);
        selection = Components::ResolveCameraTargetSelection(world, low->ID());
        expect(selection.object == high && selection.component == high_target &&
            !selection.used_controlled_object,
            "disabled controlled camera target falls back to highest priority target");

        high_target->SetEnabled(false);
        selection = Components::ResolveCameraTargetSelection(world, low->ID());
        expect(!selection.Valid(), "no active camera target produces no selection");

        Core::GameObject* camera_low = world.CreateGameObject("LowPriorityCamera");
        Core::GameObject* camera_high = world.CreateGameObject("HighPriorityCamera");
        expect(camera_low != nullptr && camera_high != nullptr,
            "camera objects can be created");
        if (camera_low == nullptr || camera_high == nullptr)
        {
            return first_failure != 0 ? first_failure : 862;
        }

        auto* low_camera = camera_low->AddComponent<Components::CameraComponent>();
        auto* high_camera = camera_high->AddComponent<Components::CameraComponent>();
        expect(low_camera != nullptr && high_camera != nullptr,
            "CameraComponent can be attached");
        if (low_camera == nullptr || high_camera == nullptr)
        {
            return first_failure != 0 ? first_failure : 863;
        }

        low_camera->priority = 2;
        high_camera->priority = 9;
        Components::CameraSelection camera_selection =
            Components::ResolveActiveCameraSelection(world);
        expect(camera_selection.object == camera_high &&
            camera_selection.component == high_camera,
            "highest priority active CameraComponent is selected");
        high_camera->SetEnabled(false);
        camera_selection = Components::ResolveActiveCameraSelection(world);
        expect(camera_selection.object == camera_low &&
            camera_selection.component == low_camera,
            "disabled CameraComponent falls back to next priority");

        camera_low->GetTransform().SetLocalPosition({ 1.0f, 2.0f, 3.0f });
        const DirectX::XMFLOAT3 eye = low_camera->EyePosition();
        expect(close(eye.x, 1.0f) && close(eye.y, 2.0f) && close(eye.z, 3.0f),
            "CameraComponent eye position comes from Transform");

        camera_low->GetTransform().SetLocalRotationEuler(
            { 0.0f, DirectX::XM_PIDIV2, 0.0f });
        const DirectX::XMFLOAT3 forward = low_camera->Forward();
        expect(close(forward.x, 1.0f) && close(forward.z, 0.0f),
            "CameraComponent forward direction comes from Transform rotation");

        DirectX::XMFLOAT4X4 default_projection{};
        DirectX::XMStoreFloat4x4(&default_projection,
            low_camera->ProjectionMatrix(16.0f / 9.0f));

        low_camera->field_of_view_degrees = 30.0f;
        low_camera->near_clip = 0.5f;
        low_camera->far_clip = 200.0f;
        DirectX::XMFLOAT4X4 narrow_projection{};
        DirectX::XMStoreFloat4x4(&narrow_projection,
            low_camera->ProjectionMatrix(16.0f / 9.0f));

        expect(narrow_projection._22 > default_projection._22,
            "field_of_view_degrees rebuilds the Runtime Camera projection");
        const float expected_33 = 200.0f / (200.0f - 0.5f);
        const float expected_43 = -(0.5f * 200.0f) / (200.0f - 0.5f);
        expect(close(narrow_projection._33, expected_33),
            "near_clip/far_clip update projection depth scale");
        expect(close(narrow_projection._43, expected_43),
            "near_clip/far_clip update projection depth offset");

        DirectX::XMFLOAT4X4 resized_projection{};
        DirectX::XMStoreFloat4x4(&resized_projection,
            low_camera->ProjectionMatrix(4.0f / 3.0f));
        expect(close(resized_projection._22, narrow_projection._22),
            "projection preserves vertical field_of_view_degrees when aspect changes");
        expect(!close(resized_projection._11, narrow_projection._11),
            "projection reapplies the new aspect ratio");

        low_camera->projection_mode =
            static_cast<int>(Components::CameraProjectionMode::Orthographic);
        low_camera->orthographic_size = 20.0f;
        DirectX::XMFLOAT4X4 orthographic_projection{};
        DirectX::XMStoreFloat4x4(&orthographic_projection,
            low_camera->ProjectionMatrix(2.0f));
        expect(close(orthographic_projection._11, 2.0f / 40.0f),
            "orthographic projection uses orthographic_size and aspect width");
        expect(close(orthographic_projection._22, 2.0f / 20.0f),
            "orthographic projection uses orthographic_size height");

        Core::GameObject* listener_low = world.CreateGameObject("LowPriorityListener");
        Core::GameObject* listener_high = world.CreateGameObject("HighPriorityListener");
        expect(listener_low != nullptr && listener_high != nullptr,
            "audio listener objects can be created");
        if (listener_low == nullptr || listener_high == nullptr)
        {
            return first_failure != 0 ? first_failure : 864;
        }

        auto* low_listener =
            listener_low->AddComponent<Components::AudioListenerComponent>();
        auto* high_listener =
            listener_high->AddComponent<Components::AudioListenerComponent>();
        expect(low_listener != nullptr && high_listener != nullptr,
            "AudioListenerComponent can be attached");
        if (low_listener == nullptr || high_listener == nullptr)
        {
            return first_failure != 0 ? first_failure : 865;
        }

        low_listener->priority = 3;
        high_listener->priority = 7;
        Components::AudioListenerSelection listener_selection =
            Components::ResolveAudioListenerSelection(world);
        expect(listener_selection.object == listener_high &&
            listener_selection.component == high_listener,
            "highest priority active AudioListenerComponent is selected");

        listener_low->GetTransform().SetLocalPosition({ -1.0f, 4.0f, 8.0f });
        const DirectX::XMFLOAT3 listener_position = low_listener->Position();
        expect(close(listener_position.x, -1.0f) &&
            close(listener_position.y, 4.0f) &&
            close(listener_position.z, 8.0f),
            "AudioListenerComponent position comes from Transform");

        if (first_failure == 0)
        {
            std::fprintf(stderr, "camera-component OK: %d checks passed\n", total);
            return 0;
        }
        std::fprintf(stderr, "camera-component FAILED: %d/%d checks failed (first=%d)\n",
            failures, total, first_failure);
        return first_failure;
    }

    // Stabilization Phase A-3。
    //
    // PlayerControllerComponent の Dash 倍率と CharacterMotorComponent::move_speed が
    // 実際の水平速度へ反映されることを、固定更新だけで検証する。
    // 終了コード帯は 900-939。
    int RunHeadlessPlayerSpeedValidation(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        if (!(arguments >> command) || command != "--validate-player-speed") return -1;

        namespace Components = ReplayEngine::Components;
        namespace Core = ReplayEngine::Core;
        namespace Reflection = ReplayEngine::Reflection;
        namespace Scene = ReplayEngine::Scene;

        int next_code = 900;
        int first_failure = 0;
        int failures = 0;
        int total = 0;

        const auto expect = [&](bool condition, const char* what)
        {
            const int code = next_code++;
            ++total;
            if (condition) return;
            ++failures;
            if (first_failure == 0) first_failure = code;
            std::fprintf(stderr, "  [FAIL %d] %s\n", code, what);
        };

        const auto close = [](float a, float b)
        {
            return std::fabs(a - b) <= 0.0005f;
        };

        Core::RegisterBuiltInComponents();

        const Core::ComponentTypeID motor_type =
            Components::CharacterMotorComponent::StaticTypeID();
        expect(Reflection::PropertyRegistry::Find(motor_type, "move_speed") != nullptr,
            "CharacterMotorComponent exposes move_speed");

        Scene::Scene world("PlayerSpeedValidation");
        Core::GameObject* actor = world.CreateGameObject("Actor");
        expect(actor != nullptr, "player speed test object can be created");
        if (actor == nullptr) return first_failure != 0 ? first_failure : 900;

        auto* motor = actor->AddComponent<Components::CharacterMotorComponent>();
        expect(motor != nullptr, "CharacterMotorComponent can be attached");
        if (motor == nullptr) return first_failure != 0 ? first_failure : 901;

        motor->move_speed = 6.0f;
        motor->acceleration = 1000.0f;
        motor->deceleration = 1000.0f;
        motor->vertical_physics = false;

        world.Start();

        constexpr float fixed_delta = 1.0f / 60.0f;
        motor->Move(DirectX::XMFLOAT3{ 1.0f, 0.0f, 0.0f });
        world.FixedUpdate(fixed_delta);
        expect(close(motor->PlanarSpeed(), 6.0f),
            "move_speed caps the actual planar speed");

        motor->Move(DirectX::XMFLOAT3{ 1.0f, 0.0f, 1.0f });
        world.FixedUpdate(fixed_delta);
        expect(close(motor->PlanarSpeed(), 6.0f),
            "diagonal movement does not exceed move_speed");

        motor->Move(DirectX::XMFLOAT3{ 1.0f, 0.0f, 0.0f }, 2.0f);
        world.FixedUpdate(fixed_delta);
        expect(close(motor->PlanarSpeed(), 12.0f),
            "speed_multiplier raises the actual planar speed cap");

        motor->Move(DirectX::XMFLOAT3{ 1.0f, 0.0f, 0.0f }, 0.5f);
        world.FixedUpdate(fixed_delta);
        expect(close(motor->PlanarSpeed(), 3.0f),
            "speed_multiplier lowers the actual planar speed cap");

        motor->Move(DirectX::XMFLOAT3{ 1.0f, 0.0f, 0.0f }, 0.0f);
        world.FixedUpdate(fixed_delta);
        expect(close(motor->PlanarSpeed(), 0.0f),
            "zero speed_multiplier behaves like no movement request");

        if (first_failure == 0)
        {
            std::fprintf(stderr, "player-speed OK: %d checks passed\n", total);
            return 0;
        }
        std::fprintf(stderr, "player-speed FAILED: %d/%d checks failed (first=%d)\n",
            failures, total, first_failure);
        return first_failure;
    }

}
