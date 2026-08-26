#include "BuiltInComponentsInternal.h"

namespace ReplayEngine::Core::Detail
{
    void RegisterAINavigation()
    {
        ComponentRegistry::Register<NavAgentComponent>(
            ComponentTypeInfo::Describe("Nav Agent", "Navigation")
                .InModule("RePlayEngine.BuiltIn")
                .WithTooltip(
                    "指定したワールド座標へ移動する汎用 Agent。Phase 2 は既存衝突世界を"
                    "グリッド探索し、MoveTo / Stop / Arrived の API は維持する。"));

        PropertyRegistry::Register<NavAgentComponent>(
            MakeProperty("move_speed", &NavAgentComponent::move_speed)
                .Display("移動速度").Range(0.0, 1000.0).Step(0.1));
        PropertyRegistry::Register<NavAgentComponent>(
            MakeProperty("turn_speed_degrees", &NavAgentComponent::turn_speed_degrees)
                .Display("旋回速度（度/秒）").Range(0.0, 10000.0).Step(1.0));
        PropertyRegistry::Register<NavAgentComponent>(
            MakeProperty("stopping_distance", &NavAgentComponent::stopping_distance)
                .Display("停止距離").Range(0.0, 1000.0).Step(0.05));
        PropertyRegistry::Register<NavAgentComponent>(
            MakeProperty("path_grid_size", &NavAgentComponent::path_grid_size)
                .Display("経路グリッド間隔").Range(0.05, 100.0).Step(0.05)
                .Tooltip("衝突世界を標本化する間隔。小さいほど細かいが探索コストが増える。"));
        PropertyRegistry::Register<NavAgentComponent>(
            MakeProperty("path_max_range", &NavAgentComponent::path_max_range)
                .Display("経路探索最大範囲").Range(1.0, 1000.0).Step(1.0)
                .Tooltip("開始地点から X/Z 各方向へ探索する上限。上限外は直進へフォールバックする。"));
        PropertyRegistry::Register<NavAgentComponent>(
            MakeProperty("path_max_search_cells", &NavAgentComponent::path_max_search_cells)
                .Display("経路探索最大升目数").Range(16.0, 65536.0).Step(16.0)
                .Tooltip("A* が生成してよい升目数。超えたら探索を打ち切り直進へフォールバックする。"));

        ComponentRegistry::Register<EnemyBehaviourComponent>(
            ComponentTypeInfo::Describe("Enemy Behaviour", "Gameplay")
                .InModule("RePlayEngine.Template.ActionPlatformer")
                .WithTooltip(
                    "巡回・索敵・追跡・攻撃・帰還を行う Action Platformer 用の敵挙動。"
                    "移動は Nav Agent、ダメージは既存 Damage Area / Health を使う。")
                .Recommends<NavAgentComponent>()
                .Recommends<DamageAreaComponent>());

        PropertyRegistry::Register<EnemyBehaviourComponent>(
            MakeProperty("target", &EnemyBehaviourComponent::target)
                .Display("索敵対象")
                .Tooltip("未指定なら Player Controller を持つ有効な GameObject を自動探索する。"));
        PropertyRegistry::Register<EnemyBehaviourComponent>(
            MakeProperty("patrol_waypoints", &EnemyBehaviourComponent::patrol_waypoints)
                .Display("巡回ウェイポイント")
                .Tooltip("0 件なら Idle のまま待機する。無効な参照は安全に読み飛ばす。"));
        PropertyRegistry::Register<EnemyBehaviourComponent>(
            MakeProperty("detection_range", &EnemyBehaviourComponent::detection_range)
                .Display("索敵距離").Range(0.05, 10000.0).Step(0.1));
        PropertyRegistry::Register<EnemyBehaviourComponent>(
            MakeProperty("field_of_view_degrees", &EnemyBehaviourComponent::field_of_view_degrees)
                .Display("視野角（度）").Range(0.0, 360.0).Step(1.0));
        PropertyRegistry::Register<EnemyBehaviourComponent>(
            MakeProperty("attack_range", &EnemyBehaviourComponent::attack_range)
                .Display("攻撃距離").Range(0.05, 10000.0).Step(0.1));
        PropertyRegistry::Register<EnemyBehaviourComponent>(
            MakeProperty("lose_sight_delay", &EnemyBehaviourComponent::lose_sight_delay)
                .Display("見失い猶予（秒）").Range(0.0, 120.0).Step(0.05));
        PropertyRegistry::Register<EnemyBehaviourComponent>(
            MakeProperty("eye_height", &EnemyBehaviourComponent::eye_height)
                .Display("目の高さ").Range(-100.0, 100.0).Step(0.05));
        PropertyRegistry::Register<EnemyBehaviourComponent>(
            MakeProperty("target_height", &EnemyBehaviourComponent::target_height)
                .Display("対象を見る高さ").Range(-100.0, 100.0).Step(0.05));
        PropertyRegistry::Register<EnemyBehaviourComponent>(
            MakeProperty("visibility_layer", &EnemyBehaviourComponent::visibility_layer)
                .Display("視線レイヤー").AsCollisionLayer());
        PropertyRegistry::Register<EnemyBehaviourComponent>(
            MakeProperty("visibility_mask", &EnemyBehaviourComponent::visibility_mask)
                .Display("視線マスク").AsCollisionMask());
        PropertyRegistry::Register<EnemyBehaviourComponent>(
            MakeProperty("attack_controls_damage_area",
                &EnemyBehaviourComponent::attack_controls_damage_area)
                .Display("攻撃中だけ Damage Area を有効化")
                .Tooltip("OFF は接触ダメージ型で Damage Area に一切触れない。"
                    "ON は Windup / Active / Recovery の Active 中だけ有効化する。"));
        PropertyRegistry::Register<EnemyBehaviourComponent>(
            MakeProperty("attack_windup_seconds", &EnemyBehaviourComponent::attack_windup_seconds)
                .Display("攻撃 Windup（秒）").Range(0.0, 30.0).Step(0.01));
        PropertyRegistry::Register<EnemyBehaviourComponent>(
            MakeProperty("attack_active_seconds", &EnemyBehaviourComponent::attack_active_seconds)
                .Display("攻撃 Active（秒）").Range(0.0, 30.0).Step(0.01));
        PropertyRegistry::Register<EnemyBehaviourComponent>(
            MakeProperty("attack_recovery_seconds", &EnemyBehaviourComponent::attack_recovery_seconds)
                .Display("攻撃 Recovery（秒）").Range(0.0, 30.0).Step(0.01));
        PropertyRegistry::Register<EnemyBehaviourComponent>(
            MakeProperty("debug_draw", &EnemyBehaviourComponent::debug_draw)
                .Display("Scene View デバッグ表示"));

        PropertyRegistry::Register<EnemyBehaviourComponent>(
            MakeAccessorProperty<EnemyBehaviourComponent>("current_state", PropertyType::String,
                [](const EnemyBehaviourComponent& component)
                { return PropertyValue::MakeString(component.CurrentStateName()); },
                [](EnemyBehaviourComponent&, const PropertyValue&) {})
                .Display("現在の状態").ReadOnly().RuntimeOnly().NotSerializable());
        PropertyRegistry::Register<EnemyBehaviourComponent>(
            MakeAccessorProperty<EnemyBehaviourComponent>("attack_phase", PropertyType::String,
                [](const EnemyBehaviourComponent& component)
                { return PropertyValue::MakeString(component.CurrentAttackPhaseName()); },
                [](EnemyBehaviourComponent&, const PropertyValue&) {})
                .Display("攻撃段階").ReadOnly().RuntimeOnly().NotSerializable());
    }
}
