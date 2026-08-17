#pragma once

#include "../../Runtime/Behaviour/BehaviourComponent.h"
#include "../../Reflection/Property/References.h"

#include <DirectXMath.h>

#include <cstddef>
#include <vector>

namespace ReplayEngine::Components
{
    class DamageAreaComponent;
    class NavAgentComponent;

    enum class EnemyState : int
    {
        Idle = 0,
        Patrol = 1,
        Chase = 2,
        Attack = 3,
        ReturnHome = 4,
    };

    // Attack state 内部の実行時段階。保存形式へは出さない。
    enum class EnemyAttackPhase : int
    {
        None = 0,
        Windup = 1,
        Active = 2,
        Recovery = 3,
    };

    // Action / Platformer テンプレート用の最小 Enemy Behaviour。
    // 移動は NavAgent、ダメージは既存 DamageArea -> Health の経路へ委譲し、
    // この Component 自身は移動・重力・HP の仕組みを持たない。
    class EnemyBehaviourComponent final : public Runtime::BehaviourComponent
    {
        REPLAY_COMPONENT_BODY(EnemyBehaviourComponent)

    public:
        EnemyBehaviourComponent() = default;

        void OnStart() override;
        void OnEnable() override;
        void OnDisable() override;
        void OnUpdate(float delta_time) override;
        void OnPropertyChanged(const char* property_name) override;

        EnemyState CurrentState() const noexcept { return state_; }
        const char* CurrentStateName() const noexcept;
        EnemyAttackPhase CurrentAttackPhase() const noexcept { return attack_phase_; }
        const char* CurrentAttackPhaseName() const noexcept;

        bool LastLineOfSightTested() const noexcept { return last_los_tested_; }
        bool LastLineOfSightClear() const noexcept { return last_los_clear_; }
        const DirectX::XMFLOAT3& LastLineOfSightStart() const noexcept { return last_los_start_; }
        const DirectX::XMFLOAT3& LastLineOfSightEnd() const noexcept { return last_los_end_; }
        const DirectX::XMFLOAT3& LastLineOfSightHit() const noexcept { return last_los_hit_; }
        std::size_t CurrentWaypointIndex() const noexcept { return current_waypoint_index_; }

        // ---- 保存される設定 -------------------------------------------------
        // 明示参照が指定されていればそれを最優先する。未指定時だけ最初の
        // PlayerControllerComponent を持つ active GameObject を自動探索する。
        Reflection::ObjectReference target;
        std::vector<Reflection::ObjectReference> patrol_waypoints;

        float detection_range = 12.0f;
        float field_of_view_degrees = 120.0f;
        float attack_range = 1.5f;
        float lose_sight_delay = 2.0f;
        float eye_height = 1.2f;
        float target_height = 1.0f;
        int visibility_layer = 0;
        int visibility_mask = -1;

        // false: 接触ダメージ型。DamageArea の enabled には一度も触らない。
        // true : Windup -> Active -> Recovery の Active 中だけ DamageArea を有効化する。
        bool attack_controls_damage_area = false;
        float attack_windup_seconds = 0.3f;
        float attack_active_seconds = 0.2f;
        float attack_recovery_seconds = 0.5f;

        bool debug_draw = true;

    protected:
        void OnDestroy() override;

    private:
        Core::GameObject* ResolveTargetObject() const;
        Core::GameObject* ResolveWaypoint(std::size_t index) const;
        bool HasUsableWaypoint() const;
        bool AdvanceToUsableWaypoint();
        bool CanSeeTarget(Core::GameObject& target_object);
        bool TargetInAttackRange(const Core::GameObject& target_object) const;
        DirectX::XMFLOAT3 TargetPosition(const Core::GameObject& target_object) const;

        NavAgentComponent* ResolveAgent() const;
        DamageAreaComponent* ResolveDamageArea() const;

        void SetState(EnemyState next_state);
        void BeginAttackCycle();
        void UpdateAttack(float delta_time, Core::GameObject* target_object, bool visible);
        void CaptureDamageAreaState();
        void RestoreDamageAreaState();
        void SetDamageAreaForAttack(bool enabled);
        void MoveToWaypoint();
        void WarnMissingAgentOnce();
        void WarnMissingDamageAreaOnce();

        EnemyState state_ = EnemyState::Idle;
        EnemyAttackPhase attack_phase_ = EnemyAttackPhase::None;
        DirectX::XMFLOAT3 home_position_{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 last_known_target_position_{ 0.0f, 0.0f, 0.0f };
        std::size_t current_waypoint_index_ = 0;
        float time_since_target_visible_ = 0.0f;
        float attack_phase_remaining_ = 0.0f;
        bool started_ = false;

        bool damage_area_state_captured_ = false;
        bool damage_area_original_enabled_ = false;
        bool missing_agent_warning_logged_ = false;
        bool missing_damage_area_warning_logged_ = false;

        bool last_los_tested_ = false;
        bool last_los_clear_ = false;
        DirectX::XMFLOAT3 last_los_start_{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 last_los_end_{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 last_los_hit_{ 0.0f, 0.0f, 0.0f };
    };
}
