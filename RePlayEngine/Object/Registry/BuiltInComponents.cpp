#include "BuiltInComponents.h"

#include "ComponentRegistry.h"
#include "../../Reflection/Registry/PropertyRegistry.h"

#include "../../Components/Camera/CameraTargetComponent.h"
#include "../../Components/Core/TransformComponent.h"
#include "../../Components/Gameplay/CharacterMotorComponent.h"
#include "../../Components/Gameplay/HealthComponent.h"
#include "../../Components/Gameplay/PlayerControllerComponent.h"
#include "../../Components/Gameplay/PlayerInputComponent.h"
#include "../../Components/Gameplay/RotatorComponent.h"
#include "../../Components/Physics/SphereColliderComponent.h"
#include "../../Components/Rendering/AnimatorComponent.h"
#include "../../Components/Rendering/MeshRendererComponent.h"
#include "../../Components/Rendering/SkinnedMeshRendererComponent.h"

namespace ReplayEngine::Core
{
    namespace
    {
        using Components::AnimatorComponent;
        using Components::CameraTargetComponent;
        using Components::CharacterMotorComponent;
        using Components::HealthComponent;
        using Components::MeshRendererComponent;
        using Components::PlayerControllerComponent;
        using Components::PlayerInputComponent;
        using Components::RotatorComponent;
        using Components::SkinnedMeshRendererComponent;
        using Components::SphereColliderComponent;
        using Components::TransformComponent;

        using Reflection::MakeAccessorProperty;
        using Reflection::MakeProperty;
        using Reflection::PropertyRegistry;
        using Reflection::PropertyType;
        using Reflection::PropertyValue;

        void RegisterTransform()
        {
            ComponentRegistry::Register<TransformComponent>(
                ComponentTypeInfo::Describe("Transform", "Core")
                    .WithTooltip("位置・回転・拡大率。GameObject が実体を持つ。")
                    .AsBuiltIn()
                    .NotRemovable()
                    // Transform は GameObject 側の情報として保存される。
                    // ここでも保存すると同じ値がファイル内に 2 度出てしまう。
                    .NotSerializable());

            PropertyRegistry::Register<TransformComponent>(
                MakeAccessorProperty<TransformComponent>("position", PropertyType::Vector3,
                    [](const TransformComponent& component)
                    { return PropertyValue::MakeVector3(component.Position()); },
                    [](TransformComponent& component, const PropertyValue& value)
                    { component.SetPosition(value.AsVector3()); })
                .Display("位置").Step(0.05).NotSerializable());

            PropertyRegistry::Register<TransformComponent>(
                MakeAccessorProperty<TransformComponent>("rotation", PropertyType::Vector3,
                    [](const TransformComponent& component)
                    { return PropertyValue::MakeVector3(component.RotationDegrees()); },
                    [](TransformComponent& component, const PropertyValue& value)
                    { component.SetRotationDegrees(value.AsVector3()); })
                .Display("回転 (度)").Step(0.5).NotSerializable());

            PropertyRegistry::Register<TransformComponent>(
                MakeAccessorProperty<TransformComponent>("scale", PropertyType::Vector3,
                    [](const TransformComponent& component)
                    { return PropertyValue::MakeVector3(component.Scale()); },
                    [](TransformComponent& component, const PropertyValue& value)
                    { component.SetScale(value.AsVector3()); })
                .Display("拡大率").Step(0.01).NotSerializable());
        }

        void RegisterMeshRenderer()
        {
            ComponentRegistry::Register<MeshRendererComponent>(
                ComponentTypeInfo::Describe("Mesh Renderer", "Rendering")
                    .WithTooltip("Asset を指定して既存レンダラーへ描画を依頼する。"));

            PropertyRegistry::Register<MeshRendererComponent>(
                MakeProperty("mesh_asset", &MeshRendererComponent::mesh_asset)
                    .Display("メッシュ").AsAssetPath()
                    .Tooltip("AssetDatabase の GUID。プロジェクトパネルから指定する。"));

            PropertyRegistry::Register<MeshRendererComponent>(
                MakeProperty("material_asset", &MeshRendererComponent::material_asset)
                    .Display("マテリアル").AsAssetPath()
                    .Tooltip("将来 Material を分離するための枠。現時点では未使用。"));

            PropertyRegistry::Register<MeshRendererComponent>(
                MakeProperty("tint", &MeshRendererComponent::tint)
                    .Display("色").AsColor());

            PropertyRegistry::Register<MeshRendererComponent>(
                MakeProperty("shading_model", &MeshRendererComponent::shading_model)
                    .Display("描画方式")
                    .AsEnum({ "FBX標準", "PBR", "トゥーン", "アンリット" }));

            PropertyRegistry::Register<MeshRendererComponent>(
                MakeProperty("outline", &MeshRendererComponent::outline).Display("輪郭線"));

            PropertyRegistry::Register<MeshRendererComponent>(
                MakeProperty("cast_shadow", &MeshRendererComponent::cast_shadow)
                    .Display("影を落とす"));

            PropertyRegistry::Register<MeshRendererComponent>(
                MakeProperty("visible", &MeshRendererComponent::visible).Display("表示"));
        }

        void RegisterRotator()
        {
            ComponentRegistry::Register<RotatorComponent>(
                ComponentTypeInfo::Describe("Rotator", "Gameplay")
                    .WithTooltip("GameObject を一定速度で回し続ける。動作確認用。"));

            PropertyRegistry::Register<RotatorComponent>(
                MakeProperty("axis", &RotatorComponent::axis)
                    .Display("回転軸").Step(0.01));

            PropertyRegistry::Register<RotatorComponent>(
                MakeProperty("degrees_per_second", &RotatorComponent::degrees_per_second)
                    .Display("回転速度 (度/秒)")
                    .Range(-1440.0, 1440.0).Step(1.0));
        }

        void RegisterHealth()
        {
            ComponentRegistry::Register<HealthComponent>(
                ComponentTypeInfo::Describe("Health", "Gameplay")
                    .WithTooltip("体力。将来はプレイヤーと敵で共有する。"));

            PropertyRegistry::Register<HealthComponent>(
                MakeProperty("max_health", &HealthComponent::max_health)
                    .Display("最大体力").Range(1.0, 100000.0).Step(1.0));

            PropertyRegistry::Register<HealthComponent>(
                MakeProperty("current_health", &HealthComponent::current_health)
                    .Display("現在体力").Range(0.0, 100000.0).Step(1.0));

            PropertyRegistry::Register<HealthComponent>(
                MakeProperty("invulnerable", &HealthComponent::invulnerable)
                    .Display("無敵"));
        }

        void RegisterSkinnedMeshRenderer()
        {
            ComponentRegistry::Register<SkinnedMeshRendererComponent>(
                ComponentTypeInfo::Describe("Skinned Mesh Renderer", "Rendering")
                    .WithTooltip("アニメーション付きモデルを描画する。Animator のクリップと時刻を提出する。"));

            PropertyRegistry::Register<SkinnedMeshRendererComponent>(
                MakeProperty("mesh_asset", &SkinnedMeshRendererComponent::mesh_asset)
                    .Display("メッシュ").AsAssetPath()
                    .Tooltip("AssetDatabase の GUID。空なら描画しない。"));

            PropertyRegistry::Register<SkinnedMeshRendererComponent>(
                MakeProperty("tint", &SkinnedMeshRendererComponent::tint).Display("色").AsColor());

            PropertyRegistry::Register<SkinnedMeshRendererComponent>(
                MakeProperty("shading_model", &SkinnedMeshRendererComponent::shading_model)
                    .Display("描画方式")
                    .AsEnum({ "FBX標準", "PBR", "トゥーン", "アンリット" }));

            PropertyRegistry::Register<SkinnedMeshRendererComponent>(
                MakeProperty("outline", &SkinnedMeshRendererComponent::outline).Display("輪郭線"));

            PropertyRegistry::Register<SkinnedMeshRendererComponent>(
                MakeProperty("cast_shadow", &SkinnedMeshRendererComponent::cast_shadow)
                    .Display("影を落とす"));

            PropertyRegistry::Register<SkinnedMeshRendererComponent>(
                MakeProperty("visible", &SkinnedMeshRendererComponent::visible).Display("表示"));

            PropertyRegistry::Register<SkinnedMeshRendererComponent>(
                MakeProperty("visual_rotation_offset",
                    &SkinnedMeshRendererComponent::visual_rotation_offset)
                    .Display("表示姿勢補正 (度)").Step(0.5)
                    .Tooltip("FBX の基準姿勢を正立させるための補正。論理的な向きとは別物。"));

            PropertyRegistry::Register<SkinnedMeshRendererComponent>(
                MakeProperty("apply_fbx_coordinate_transform",
                    &SkinnedMeshRendererComponent::apply_fbx_coordinate_transform)
                    .Display("FBX 座標系補正"));
        }

        void RegisterAnimator()
        {
            ComponentRegistry::Register<AnimatorComponent>(
                ComponentTypeInfo::Describe("Animator", "Rendering")
                    .WithTooltip("Character Motor の状態から Idle / Walk / Jump を切り替える。"));

            PropertyRegistry::Register<AnimatorComponent>(
                MakeProperty("idle_clip", &AnimatorComponent::idle_clip)
                    .Display("待機クリップ").Range(-1.0, 255.0).Step(1.0)
                    .Tooltip("-1 なら切り替えない。"));

            PropertyRegistry::Register<AnimatorComponent>(
                MakeProperty("walk_clip", &AnimatorComponent::walk_clip)
                    .Display("移動クリップ").Range(-1.0, 255.0).Step(1.0));

            PropertyRegistry::Register<AnimatorComponent>(
                MakeProperty("jump_clip", &AnimatorComponent::jump_clip)
                    .Display("ジャンプクリップ").Range(-1.0, 255.0).Step(1.0));

            PropertyRegistry::Register<AnimatorComponent>(
                MakeProperty("playback_speed", &AnimatorComponent::playback_speed)
                    .Display("再生速度").Range(0.0, 5.0).Step(0.01));

            PropertyRegistry::Register<AnimatorComponent>(
                MakeProperty("loop", &AnimatorComponent::loop).Display("ループ"));

            PropertyRegistry::Register<AnimatorComponent>(
                MakeProperty("walk_speed_threshold", &AnimatorComponent::walk_speed_threshold)
                    .Display("移動とみなす速度").Range(0.0, 10.0).Step(0.01));

            PropertyRegistry::Register<AnimatorComponent>(
                MakeProperty("playing", &AnimatorComponent::playing).Display("再生"));
        }

        void RegisterSphereCollider()
        {
            ComponentRegistry::Register<SphereColliderComponent>(
                ComponentTypeInfo::Describe("Sphere Collider", "Physics")
                    .WithTooltip("球状の衝突形状。中心は Owner の Transform から求める。")
                    .AllowMultipleInstances());

            PropertyRegistry::Register<SphereColliderComponent>(
                MakeProperty("radius", &SphereColliderComponent::radius)
                    .Display("半径").Range(0.01, 100.0).Step(0.01));

            PropertyRegistry::Register<SphereColliderComponent>(
                MakeProperty("center_offset", &SphereColliderComponent::center_offset)
                    .Display("中心オフセット").Step(0.01));

            PropertyRegistry::Register<SphereColliderComponent>(
                MakeProperty("skin_width", &SphereColliderComponent::skin_width)
                    .Display("接触余白").Range(0.0, 1.0).Step(0.001));

            PropertyRegistry::Register<SphereColliderComponent>(
                MakeProperty("walkable_normal_y", &SphereColliderComponent::walkable_normal_y)
                    .Display("歩ける傾斜のしきい値").Range(-1.0, 1.0).Step(0.01));
        }

        void RegisterCharacterMotor()
        {
            ComponentRegistry::Register<CharacterMotorComponent>(
                ComponentTypeInfo::Describe("Character Motor", "Gameplay")
                    .WithTooltip("移動・重力・ジャンプ・接地。プレイヤーと敵の双方から使える。"));

            PropertyRegistry::Register<CharacterMotorComponent>(
                MakeProperty("move_speed", &CharacterMotorComponent::move_speed)
                    .Display("移動速度").Range(0.0, 30.0).Step(0.1));

            PropertyRegistry::Register<CharacterMotorComponent>(
                MakeProperty("acceleration", &CharacterMotorComponent::acceleration)
                    .Display("加速度").Range(0.0, 120.0).Step(0.5));

            PropertyRegistry::Register<CharacterMotorComponent>(
                MakeProperty("deceleration", &CharacterMotorComponent::deceleration)
                    .Display("減速度").Range(0.0, 120.0).Step(0.5));

            PropertyRegistry::Register<CharacterMotorComponent>(
                MakeProperty("air_control", &CharacterMotorComponent::air_control)
                    .Display("空中制御").Range(0.0, 1.0).Step(0.01));

            PropertyRegistry::Register<CharacterMotorComponent>(
                MakeProperty("gravity", &CharacterMotorComponent::gravity)
                    .Display("重力").Range(0.0, 100.0).Step(0.1));

            PropertyRegistry::Register<CharacterMotorComponent>(
                MakeProperty("jump_power", &CharacterMotorComponent::jump_power)
                    .Display("ジャンプ力").Range(0.0, 50.0).Step(0.1));

            PropertyRegistry::Register<CharacterMotorComponent>(
                MakeProperty("maximum_fall_speed", &CharacterMotorComponent::maximum_fall_speed)
                    .Display("最大落下速度").Range(0.0, 200.0).Step(1.0));

            PropertyRegistry::Register<CharacterMotorComponent>(
                MakeProperty("fallback_ground_y", &CharacterMotorComponent::fallback_ground_y)
                    .Display("地形が無い時の床の高さ").Step(0.05));

            PropertyRegistry::Register<CharacterMotorComponent>(
                MakeProperty("vertical_physics", &CharacterMotorComponent::vertical_physics)
                    .Display("重力とジャンプを有効化")
                    .Tooltip("旧 Player は既定で無効だったため、初期値も無効にしてある。"));
        }

        void RegisterPlayerInput()
        {
            ComponentRegistry::Register<PlayerInputComponent>(
                ComponentTypeInfo::Describe("Player Input", "Gameplay")
                    .WithTooltip("入力の取得だけを担当する。Transform も速度も触らない。"));

            PropertyRegistry::Register<PlayerInputComponent>(
                MakeProperty("input_enabled", &PlayerInputComponent::input_enabled)
                    .Display("入力を受け付ける"));

            PropertyRegistry::Register<PlayerInputComponent>(
                MakeProperty("local_player_slot", &PlayerInputComponent::local_player_slot)
                    .Display("プレイヤー番号").Range(0.0, 3.0).Step(1.0));
        }

        void RegisterPlayerController()
        {
            ComponentRegistry::Register<PlayerControllerComponent>(
                ComponentTypeInfo::Describe("Player Controller", "Gameplay")
                    .WithTooltip("Player Input と Character Motor をつなぐ。両方が必要。"));

            PropertyRegistry::Register<PlayerControllerComponent>(
                MakeProperty("turn_speed_degrees", &PlayerControllerComponent::turn_speed_degrees)
                    .Display("旋回速度 (度/秒)").Range(0.0, 1440.0).Step(1.0));

            PropertyRegistry::Register<PlayerControllerComponent>(
                MakeProperty("dash_multiplier", &PlayerControllerComponent::dash_multiplier)
                    .Display("ダッシュ倍率").Range(0.1, 5.0).Step(0.05));

            PropertyRegistry::Register<PlayerControllerComponent>(
                MakeProperty("camera_relative", &PlayerControllerComponent::camera_relative)
                    .Display("カメラ基準で移動"));

            PropertyRegistry::Register<PlayerControllerComponent>(
                MakeProperty("rotate_towards_movement",
                    &PlayerControllerComponent::rotate_towards_movement)
                    .Display("進行方向へ向く"));
        }

        void RegisterCameraTarget()
        {
            ComponentRegistry::Register<CameraTargetComponent>(
                ComponentTypeInfo::Describe("Camera Target", "Camera")
                    .WithTooltip("カメラ追従の対象になる。自分ではカメラを動かさない。"));

            PropertyRegistry::Register<CameraTargetComponent>(
                MakeProperty("look_at_offset", &CameraTargetComponent::look_at_offset)
                    .Display("注視点オフセット").Step(0.05));

            PropertyRegistry::Register<CameraTargetComponent>(
                MakeProperty("follow_distance", &CameraTargetComponent::follow_distance)
                    .Display("追従距離").Range(0.5, 100.0).Step(0.1));

            PropertyRegistry::Register<CameraTargetComponent>(
                MakeProperty("follow_height", &CameraTargetComponent::follow_height)
                    .Display("追従高さ").Range(-10.0, 50.0).Step(0.1));

            PropertyRegistry::Register<CameraTargetComponent>(
                MakeProperty("follow_lag", &CameraTargetComponent::follow_lag)
                    .Display("追従の速さ").Range(0.1, 60.0).Step(0.1));

            PropertyRegistry::Register<CameraTargetComponent>(
                MakeProperty("priority", &CameraTargetComponent::priority)
                    .Display("優先度").Range(-100.0, 100.0).Step(1.0));
        }
    }

    void RegisterBuiltInComponents()
    {
        // 並び順がそのまま Add Component 一覧の並びになる。
        //
        // 新しい Component を足すときは、ここへ 1 行足すだけでよい。
        // それだけで Add Component 一覧・Inspector・Scene 保存・読み込み・
        // 複製・Undo/Redo・Prefab のすべてへ反映される。
        RegisterTransform();
        RegisterMeshRenderer();
        RegisterSkinnedMeshRenderer();
        RegisterAnimator();
        RegisterSphereCollider();
        RegisterCharacterMotor();
        RegisterPlayerInput();
        RegisterPlayerController();
        RegisterCameraTarget();
        RegisterRotator();
        RegisterHealth();
    }
}
