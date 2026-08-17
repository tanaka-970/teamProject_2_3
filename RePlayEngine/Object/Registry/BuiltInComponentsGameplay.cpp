#include "BuiltInComponentsInternal.h"

namespace ReplayEngine::Core::Detail
{
        void RegisterRotator()
        {
            ComponentRegistry::Register<RotatorComponent>(
                ComponentTypeInfo::Describe("Rotator", "Gameplay")
                    .WithTooltip("GameObject を一定速度で回し続ける。動作確認用。")
                    .InModule("RePlayEngine.Template.Samples"));

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
                    .WithTooltip("体力。将来はプレイヤーと敵で共有する。")
                    .InModule("RePlayEngine.Template.ActionPlatformer"));

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

        void RegisterCharacterMotor()
        {
            ComponentRegistry::Register<CharacterMotorComponent>(
                ComponentTypeInfo::Describe("Character Motor", "Gameplay")
                    .WithTooltip("移動・重力・ジャンプ・接地。プレイヤーと敵の双方から使える。")
                    .InModule("RePlayEngine.Template.ActionPlatformer"));

            // 移動用 Collider は明示的に選ぶ。暗黙の自動選択はしない。
            // 一覧には Sphere / Capsule / Box だけが出る（Mesh と Trigger は除外）。
            PropertyRegistry::Register<CharacterMotorComponent>(
                MakeProperty("primary_collider_key",
                    &CharacterMotorComponent::primary_collider_key)
                    .Display("移動用 Collider").AsColliderReference()
                    .Tooltip("移動・接地・押し戻しに使う Collider。"
                        "未設定だと地形との当たり判定を行わない。"));

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
                    .WithTooltip("入力の取得だけを担当する。Transform も速度も触らない。")
                    .InModule("RePlayEngine.Template.ActionPlatformer"));

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
                    .WithTooltip("Player Input と Character Motor をつなぐ。両方が必要。")
                    .InModule("RePlayEngine.Template.ActionPlatformer"));

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

        void RegisterStageGameplay()
        {
            ComponentRegistry::Register<SpawnPointComponent>(
                ComponentTypeInfo::Describe("Spawn Point", "Gameplay")
                    .WithTooltip("開始地点またはチェックポイント未通過時の復帰地点。")
                    .InModule("RePlayEngine.Template.ActionPlatformer"));
            PropertyRegistry::Register<SpawnPointComponent>(
                MakeProperty("spawn_id", &SpawnPointComponent::spawn_id)
                    .Display("スポーンID").Step(1.0));
            PropertyRegistry::Register<SpawnPointComponent>(
                MakeProperty("team", &SpawnPointComponent::team)
                    .Display("チーム").Step(1.0));
            PropertyRegistry::Register<SpawnPointComponent>(
                MakeProperty("priority", &SpawnPointComponent::priority)
                    .Display("優先度").Step(1.0));
            PropertyRegistry::Register<SpawnPointComponent>(
                MakeProperty("debug_draw", &SpawnPointComponent::debug_draw)
                    .Display("Scene Viewに表示"));

            ComponentRegistry::Register<CheckpointComponent>(
                ComponentTypeInfo::Describe("Checkpoint", "Gameplay")
                    .WithTooltip("Triggerへ入った対象の復帰地点を更新する。")
                    .InModule("RePlayEngine.Template.ActionPlatformer"));
            PropertyRegistry::Register<CheckpointComponent>(
                MakeProperty("checkpoint_id", &CheckpointComponent::checkpoint_id)
                    .Display("チェックポイントID").Step(1.0));
            PropertyRegistry::Register<CheckpointComponent>(
                MakeProperty("respawn_position_offset",
                    &CheckpointComponent::respawn_position_offset)
                    .Display("復帰位置オフセット").Step(0.05));
            PropertyRegistry::Register<CheckpointComponent>(
                MakeProperty("respawn_rotation", &CheckpointComponent::respawn_rotation)
                    .Display("復帰時の回転").Step(0.5));
            PropertyRegistry::Register<CheckpointComponent>(
                MakeProperty("target_mask", &CheckpointComponent::target_mask)
                    .Display("反応する対象").AsCollisionMask());
            PropertyRegistry::Register<CheckpointComponent>(
                MakeProperty("one_shot", &CheckpointComponent::one_shot)
                    .Display("一度だけ"));

            ComponentRegistry::Register<GoalComponent>(
                ComponentTypeInfo::Describe("Goal", "Gameplay")
                    .WithTooltip("到達イベントを発行する。Scene遷移は行わない。")
                    .InModule("RePlayEngine.Template.ActionPlatformer"));
            PropertyRegistry::Register<GoalComponent>(
                MakeProperty("goal_id", &GoalComponent::goal_id)
                    .Display("ゴールID").Step(1.0));
            PropertyRegistry::Register<GoalComponent>(
                MakeProperty("target_mask", &GoalComponent::target_mask)
                    .Display("反応する対象").AsCollisionMask());
            PropertyRegistry::Register<GoalComponent>(
                MakeProperty("one_shot", &GoalComponent::one_shot).Display("一度だけ"));
            PropertyRegistry::Register<GoalComponent>(
                MakeProperty("completion_event", &GoalComponent::completion_event)
                    .Display("完了イベント"));

            ComponentRegistry::Register<KillVolumeComponent>(
                ComponentTypeInfo::Describe("Kill Volume", "Gameplay")
                    .WithTooltip("対象へダメージを与え、設定時は復帰地点へ戻す。")
                    .InModule("RePlayEngine.Template.ActionPlatformer"));
            PropertyRegistry::Register<KillVolumeComponent>(
                MakeProperty("target_mask", &KillVolumeComponent::target_mask)
                    .Display("反応する対象").AsCollisionMask());
            PropertyRegistry::Register<KillVolumeComponent>(
                MakeProperty("respawn_at_checkpoint",
                    &KillVolumeComponent::respawn_at_checkpoint)
                    .Display("チェックポイントへ復帰"));
            PropertyRegistry::Register<KillVolumeComponent>(
                MakeProperty("damage_amount", &KillVolumeComponent::damage_amount)
                    .Display("ダメージ").Range(0.0, 1000000.0).Step(1.0));

            ComponentRegistry::Register<JumpPadComponent>(
                ComponentTypeInfo::Describe("Jump Pad", "Gameplay")
                    .WithTooltip("Character Motorへ指定方向の速度を加えるTrigger。")
                    .InModule("RePlayEngine.Template.ActionPlatformer"));
            PropertyRegistry::Register<JumpPadComponent>(
                MakeProperty("direction", &JumpPadComponent::direction)
                    .Display("射出方向").Step(0.01));
            PropertyRegistry::Register<JumpPadComponent>(
                MakeProperty("force", &JumpPadComponent::force)
                    .Display("力").Range(0.0, 1000.0).Step(0.1));
            PropertyRegistry::Register<JumpPadComponent>(
                MakeProperty("target_mask", &JumpPadComponent::target_mask)
                    .Display("反応する対象").AsCollisionMask());
            PropertyRegistry::Register<JumpPadComponent>(
                MakeProperty("one_shot", &JumpPadComponent::one_shot).Display("一度だけ"));
            PropertyRegistry::Register<JumpPadComponent>(
                MakeProperty("cooldown", &JumpPadComponent::cooldown)
                    .Display("再使用待ち時間 (秒)").Range(0.0, 60.0).Step(0.05));
            PropertyRegistry::Register<JumpPadComponent>(
                MakeProperty("debug_draw", &JumpPadComponent::debug_draw)
                    .Display("Scene Viewに表示"));

            ComponentRegistry::Register<DamageAreaComponent>(
                ComponentTypeInfo::Describe("Damage Area", "Gameplay")
                    .WithTooltip("Trigger内のHealthへ一定間隔でダメージを与える。")
                    .InModule("RePlayEngine.Template.ActionPlatformer"));
            PropertyRegistry::Register<DamageAreaComponent>(
                MakeProperty("damage", &DamageAreaComponent::damage)
                    .Display("ダメージ").Range(0.0, 1000000.0).Step(1.0));
            PropertyRegistry::Register<DamageAreaComponent>(
                MakeProperty("interval", &DamageAreaComponent::interval)
                    .Display("間隔 (秒)").Range(0.0, 60.0).Step(0.05));
            PropertyRegistry::Register<DamageAreaComponent>(
                MakeProperty("target_mask", &DamageAreaComponent::target_mask)
                    .Display("反応する対象").AsCollisionMask());
            PropertyRegistry::Register<DamageAreaComponent>(
                MakeProperty("one_shot", &DamageAreaComponent::one_shot)
                    .Display("一度だけ"));
        }
}
