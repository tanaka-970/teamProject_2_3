// Runtime main のうち「Physics のヘッドレス検証」を持つ。
// Physics 検証関数の本体はそのまま移動している。
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
    int RunHeadlessPhysicsValidation(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        if (!(arguments >> command) || command != "--validate-physics") return -1;

        ReplayEngine::Core::RegisterBuiltInComponents();
        ReplayEngine::Scene::Scene scene("PhysicsValidation");
        ReplayEngine::Core::GameObject* ground = scene.CreateGameObject("Ground");
        ReplayEngine::Core::GameObject* body = scene.CreateGameObject("Body");
        auto* ground_rigidbody = ground != nullptr
            ? ground->AddComponent<ReplayEngine::Components::RigidbodyComponent>() : nullptr;
        auto* ground_collider = ground != nullptr
            ? ground->AddComponent<ReplayEngine::Components::BoxColliderComponent>() : nullptr;
        auto* body_rigidbody = body != nullptr
            ? body->AddComponent<ReplayEngine::Components::RigidbodyComponent>() : nullptr;
        auto* body_collider = body != nullptr
            ? body->AddComponent<ReplayEngine::Components::SphereColliderComponent>() : nullptr;

        bool ok = ground != nullptr && body != nullptr && ground_rigidbody != nullptr &&
            ground_collider != nullptr && body_rigidbody != nullptr && body_collider != nullptr;
        bool query_consistent = false;
        std::vector<std::string> lines;
        if (ok)
        {
            ground->GetTransform().SetLocalPosition({ 0.0f, -1.0f, 0.0f });
            ground_rigidbody->body_type =
                ReplayEngine::Components::RigidbodyComponent::BodyType_Static;
            ground_collider->size = { 20.0f, 1.0f, 20.0f };
            body->GetTransform().SetLocalPosition({ 0.0f, 3.0f, 0.0f });
            body_rigidbody->body_type =
                ReplayEngine::Components::RigidbodyComponent::BodyType_Dynamic;
            body_rigidbody->gravity_scale = 1.0f;
            body_collider->radius = 0.5f;

            ReplayEngine::Scene::PhysicsDynamicsWorld physics;
            physics.AttachScene(&scene);
            const float fixed_delta = 1.0f / 60.0f;
            physics.Step(fixed_delta);
            const float start_y = body->GetTransform().LocalPosition().y;
            std::vector<float> plain_trajectory;
            for (int frame = 0; frame < 180; ++frame)
            {
                physics.Step(fixed_delta);
                plain_trajectory.push_back(body->GetTransform().LocalPosition().y);
            }
            const float settled_y = body->GetTransform().LocalPosition().y;
            const float before_pause = settled_y;
            physics.Step(0.0f);
            const float after_pause = body->GetTransform().LocalPosition().y;

            const bool fell = settled_y < start_y - 0.1f;
            const bool stopped_at_ground = settled_y > -0.25f && settled_y < 0.75f;
            const bool pause_stopped = std::fabs(after_pause - before_pause) < 0.00001f;
            const bool body_table_ok = physics.BodyCount() == 2 &&
                physics.DynamicBodyCount() == 1;
            ok = fell && stopped_at_ground && pause_stopped && body_table_ok;
            lines.push_back(std::string("FALL ") + (fell ? "OK" : "NG"));
            lines.push_back(std::string("GROUND_CONTACT ") +
                (stopped_at_ground ? "OK" : "NG"));
            lines.push_back(std::string("TIME_SCALE_ZERO ") +
                (pause_stopped ? "OK" : "NG"));
            lines.push_back(std::string("BODY_TABLE ") +
                (body_table_ok ? "OK" : "NG"));
            physics.DetachScene();
            const bool detached = physics.BodyCount() == 0 && physics.AttachedScene() == nullptr;
            ok = ok && detached;
            lines.push_back(std::string("DETACH ") + (detached ? "OK" : "NG"));

            ReplayEngine::Scene::Scene query_scene("PhysicsValidationWithQuery");
            ReplayEngine::Core::GameObject* query_ground =
                query_scene.CreateGameObject("Ground");
            ReplayEngine::Core::GameObject* query_body =
                query_scene.CreateGameObject("Body");
            auto* query_ground_rigidbody = query_ground != nullptr
                ? query_ground->AddComponent<ReplayEngine::Components::RigidbodyComponent>() : nullptr;
            auto* query_ground_collider = query_ground != nullptr
                ? query_ground->AddComponent<ReplayEngine::Components::BoxColliderComponent>() : nullptr;
            auto* query_body_rigidbody = query_body != nullptr
                ? query_body->AddComponent<ReplayEngine::Components::RigidbodyComponent>() : nullptr;
            auto* query_body_collider = query_body != nullptr
                ? query_body->AddComponent<ReplayEngine::Components::SphereColliderComponent>() : nullptr;
            const bool query_setup = query_ground != nullptr && query_body != nullptr &&
                query_ground_rigidbody != nullptr && query_ground_collider != nullptr &&
                query_body_rigidbody != nullptr && query_body_collider != nullptr;
            if (query_setup)
            {
                query_ground->GetTransform().SetLocalPosition({ 0.0f, -1.0f, 0.0f });
                query_ground_rigidbody->body_type =
                    ReplayEngine::Components::RigidbodyComponent::BodyType_Static;
                query_ground_collider->size = { 20.0f, 1.0f, 20.0f };
                query_body->GetTransform().SetLocalPosition({ 0.0f, 3.0f, 0.0f });
                query_body_rigidbody->body_type =
                    ReplayEngine::Components::RigidbodyComponent::BodyType_Dynamic;
                query_body_rigidbody->gravity_scale = 1.0f;
                query_body_collider->radius = 0.5f;

                ReplayEngine::Scene::PhysicsDynamicsWorld query_physics;
                ReplayEngine::Scene::SceneCollisionWorld world;
                query_physics.AttachScene(&query_scene);
                world.AttachScene(&query_scene);
                query_scene.Services().SetPhysics(&world);
                world.Refresh();
                query_physics.Step(fixed_delta);
                float max_trajectory_deviation = 0.0f;
                for (int frame = 0; frame < 180; ++frame)
                {
                    world.Refresh();
                    query_physics.Step(fixed_delta);
                    const float here = query_body->GetTransform().LocalPosition().y;
                    const float difference =
                        std::fabs(here - plain_trajectory[static_cast<std::size_t>(frame)]);
                    if (difference > max_trajectory_deviation)
                    {
                        max_trajectory_deviation = difference;
                    }
                }
                query_physics.DetachScene();
                world.DetachScene();
                query_scene.Services().SetPhysics(nullptr);

                // 静止位置ではなく軌跡全体を比較する。二重解決は着地の瞬間だけ現れ、
                // 静止するころには収束して差が消えるため、静止位置の比較では検出できない。
                // 修正後は完全一致（0.000000）なので、0.0005 は浮動小数の揺らぎに対する
                // 余裕であり、かつ不具合時の 0.009593 に対して 19 倍の余裕がある。
                query_consistent = max_trajectory_deviation < 0.0005f;
            }
            lines.push_back(std::string("QUERY_CONSISTENCY ") +
                (query_consistent ? "OK" : "NG"));
            ok = ok && query_consistent;

            bool restitution_ok = false;
            {
                ReplayEngine::Scene::Scene restitution_scene("PhysicsValidationRestitution");
                ReplayEngine::Core::GameObject* restitution_ground =
                    restitution_scene.CreateGameObject("Ground");
                ReplayEngine::Core::GameObject* restitution_body =
                    restitution_scene.CreateGameObject("Body");
                auto* restitution_ground_rigidbody = restitution_ground != nullptr
                    ? restitution_ground->AddComponent<ReplayEngine::Components::RigidbodyComponent>()
                    : nullptr;
                auto* restitution_ground_collider = restitution_ground != nullptr
                    ? restitution_ground->AddComponent<ReplayEngine::Components::BoxColliderComponent>()
                    : nullptr;
                auto* restitution_body_rigidbody = restitution_body != nullptr
                    ? restitution_body->AddComponent<ReplayEngine::Components::RigidbodyComponent>()
                    : nullptr;
                auto* restitution_body_collider = restitution_body != nullptr
                    ? restitution_body->AddComponent<ReplayEngine::Components::SphereColliderComponent>()
                    : nullptr;
                const bool restitution_setup = restitution_ground != nullptr &&
                    restitution_body != nullptr && restitution_ground_rigidbody != nullptr &&
                    restitution_ground_collider != nullptr &&
                    restitution_body_rigidbody != nullptr && restitution_body_collider != nullptr;
                if (restitution_setup)
                {
                    restitution_ground->GetTransform().SetLocalPosition(
                        { 0.0f, -1.0f, 0.0f });
                    restitution_ground_rigidbody->body_type =
                        ReplayEngine::Components::RigidbodyComponent::BodyType_Static;
                    // Solver は両側の小さい方の反発を使うため、球の 0.8 が接触へ届くよう
                    // 床にも同じ材質値を設定する。修正前は双方とも BuildShape() の固定 0.0f になる。
                    restitution_ground_rigidbody->restitution = 0.8f;
                    restitution_ground_collider->size = { 20.0f, 1.0f, 20.0f };
                    restitution_body->GetTransform().SetLocalPosition(
                        { 0.0f, 3.0f, 0.0f });
                    restitution_body_rigidbody->body_type =
                        ReplayEngine::Components::RigidbodyComponent::BodyType_Dynamic;
                    restitution_body_rigidbody->gravity_scale = 1.0f;
                    restitution_body_rigidbody->restitution = 0.8f;
                    restitution_body_collider->radius = 0.5f;

                    ReplayEngine::Scene::PhysicsDynamicsWorld restitution_physics;
                    restitution_physics.AttachScene(&restitution_scene);
                    bool landed = false;
                    float rebound_peak = 0.0f;
                    constexpr float fixed_delta = 1.0f / 60.0f;
                    for (int frame = 0; frame < 180; ++frame)
                    {
                        restitution_physics.Step(fixed_delta);
                        const float y = restitution_body->GetTransform().LocalPosition().y;
                        if (!landed && y < 0.1f) landed = true;
                        if (landed && y > rebound_peak) rebound_peak = y;
                    }
                    restitution_physics.DetachScene();

                    // 着地時の落下速度は約 7.06 m/s（実測値）。反発 0.8 なら約 5.6 m/s
                    // で跳ね返り、到達高さは v^2 / 2g ≒ 1.6 m になる。0.5 m はその
                    // 3 分の 1 以下なので、浮動小数の揺らぎに対して十分な余裕がある。
                    // 修正前は反発が 0 に固定され、rebound_peak は静止高さ（≒0）のままになる。
                    restitution_ok = landed && rebound_peak > 0.5f;
                }
            }
            lines.push_back(std::string("RESTITUTION ") +
                (restitution_ok ? "OK" : "NG"));
            ok = ok && restitution_ok;

            bool friction_ok = false;
            {
                ReplayEngine::Scene::Scene friction_scene("PhysicsValidationFriction");
                ReplayEngine::Core::GameObject* friction_ground =
                    friction_scene.CreateGameObject("Ground");
                ReplayEngine::Core::GameObject* sliding_body =
                    friction_scene.CreateGameObject("SlidingBody");
                ReplayEngine::Core::GameObject* stopping_body =
                    friction_scene.CreateGameObject("StoppingBody");
                auto* friction_ground_rigidbody = friction_ground != nullptr
                    ? friction_ground->AddComponent<ReplayEngine::Components::RigidbodyComponent>()
                    : nullptr;
                auto* friction_ground_collider = friction_ground != nullptr
                    ? friction_ground->AddComponent<ReplayEngine::Components::BoxColliderComponent>()
                    : nullptr;
                auto* sliding_rigidbody = sliding_body != nullptr
                    ? sliding_body->AddComponent<ReplayEngine::Components::RigidbodyComponent>()
                    : nullptr;
                auto* sliding_collider = sliding_body != nullptr
                    ? sliding_body->AddComponent<ReplayEngine::Components::SphereColliderComponent>()
                    : nullptr;
                auto* stopping_rigidbody = stopping_body != nullptr
                    ? stopping_body->AddComponent<ReplayEngine::Components::RigidbodyComponent>()
                    : nullptr;
                auto* stopping_collider = stopping_body != nullptr
                    ? stopping_body->AddComponent<ReplayEngine::Components::SphereColliderComponent>()
                    : nullptr;
                const bool friction_setup = friction_ground != nullptr &&
                    sliding_body != nullptr && stopping_body != nullptr &&
                    friction_ground_rigidbody != nullptr && friction_ground_collider != nullptr &&
                    sliding_rigidbody != nullptr && sliding_collider != nullptr &&
                    stopping_rigidbody != nullptr && stopping_collider != nullptr;
                if (friction_setup)
                {
                    friction_ground->GetTransform().SetLocalPosition(
                        { 0.0f, -1.0f, 0.0f });
                    friction_ground_rigidbody->body_type =
                        ReplayEngine::Components::RigidbodyComponent::BodyType_Static;
                    friction_ground_rigidbody->friction = 0.5f;
                    friction_ground_collider->size = { 200.0f, 1.0f, 200.0f };

                    sliding_body->GetTransform().SetLocalPosition(
                        { 0.0f, 0.0f, 0.0f });
                    sliding_rigidbody->body_type =
                        ReplayEngine::Components::RigidbodyComponent::BodyType_Dynamic;
                    sliding_rigidbody->gravity_scale = 1.0f;
                    sliding_rigidbody->friction = 0.0f;
                    sliding_rigidbody->linear_velocity = { 3.0f, 0.0f, 0.0f };
                    sliding_collider->radius = 0.5f;

                    stopping_body->GetTransform().SetLocalPosition(
                        { 0.0f, 0.0f, 5.0f });
                    stopping_rigidbody->body_type =
                        ReplayEngine::Components::RigidbodyComponent::BodyType_Dynamic;
                    stopping_rigidbody->gravity_scale = 1.0f;
                    stopping_rigidbody->friction = 1.0f;
                    stopping_rigidbody->linear_velocity = { 3.0f, 0.0f, 0.0f };
                    stopping_collider->radius = 0.5f;

                    ReplayEngine::Scene::PhysicsDynamicsWorld friction_physics;
                    friction_physics.AttachScene(&friction_scene);
                    constexpr float fixed_delta = 1.0f / 60.0f;
                    for (int frame = 0; frame < 120; ++frame)
                        friction_physics.Step(fixed_delta);
                    const float sliding_x =
                        sliding_body->GetTransform().LocalPosition().x;
                    const float stopping_x =
                        stopping_body->GetTransform().LocalPosition().x;
                    friction_physics.DetachScene();

                    // 床は Static Rigidbody の既定値 0.5。相乗平均は、滑る側が
                    // sqrt(0.0 * 0.5) = 0、止まる側が sqrt(1.0 * 0.5) ≒ 0.707 になり、
                    // 2 秒後の移動距離に明確な差が出る。修正前は両方 0.5 固定なので
                    // 差はほぼ 0 になり、この判定は NG になる。
                    // 床を 200 四方にしたのは、滑る側が端から落ちず比較できるようにするため。
                    friction_ok = sliding_x - stopping_x > 1.0f;
                }
            }
            lines.push_back(std::string("FRICTION ") +
                (friction_ok ? "OK" : "NG"));
            ok = ok && friction_ok;
        }
        else
        {
            lines.push_back("SCENE_SETUP NG");
        }

        WriteValidationResultFile("Physics.txt", "REPLAY_PHYSICS_VALIDATION", ok, lines);
        std::fprintf(stderr, "physics validation: RESULT %s\n", ok ? "OK" : "NG");
        return ok ? 0 : 1430;
    }

}
