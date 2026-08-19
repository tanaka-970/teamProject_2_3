#include "EnemyBehaviourComponent.h"

#include "CharacterMotorComponent.h"
#include "PlayerControllerComponent.h"
#include "StageGameplayComponents.h"
#include "../Navigation/NavAgentComponent.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Runtime/API/RuntimeContext.h"
#include "../../Scene/Runtime/Scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace ReplayEngine::Components
{
    namespace
    {
        constexpr float Epsilon = 0.0001f;

        float PlanarDistanceSquared(const DirectX::XMFLOAT3& a,
            const DirectX::XMFLOAT3& b) noexcept
        {
            const float dx = a.x - b.x;
            const float dz = a.z - b.z;
            return dx * dx + dz * dz;
        }
    }

    const char* EnemyBehaviourComponent::CurrentStateName() const noexcept
    {
        switch (state_)
        {
        case EnemyState::Idle: return "Idle";
        case EnemyState::Patrol: return "Patrol";
        case EnemyState::Chase: return "Chase";
        case EnemyState::Attack: return "Attack";
        case EnemyState::ReturnHome: return "Return Home";
        default: return "Unknown";
        }
    }

    const char* EnemyBehaviourComponent::CurrentAttackPhaseName() const noexcept
    {
        switch (attack_phase_)
        {
        case EnemyAttackPhase::None: return "None";
        case EnemyAttackPhase::Windup: return "Windup";
        case EnemyAttackPhase::Active: return "Active";
        case EnemyAttackPhase::Recovery: return "Recovery";
        default: return "Unknown";
        }
    }

    NavAgentComponent* EnemyBehaviourComponent::ResolveAgent() const
    {
        Core::GameObject* owner = Owner();
        return owner != nullptr ? owner->GetComponent<NavAgentComponent>() : nullptr;
    }

    DamageAreaComponent* EnemyBehaviourComponent::ResolveDamageArea() const
    {
        Core::GameObject* owner = Owner();
        return owner != nullptr ? owner->GetComponent<DamageAreaComponent>() : nullptr;
    }

    Core::GameObject* EnemyBehaviourComponent::ResolveTargetObject() const
    {
        Scene::Scene* scene = GetScene();
        if (scene == nullptr) return nullptr;

        if (target.IsAssigned())
        {
            Core::GameObject* explicit_target = scene->FindGameObjectByID(target.object);
            if (explicit_target == nullptr || explicit_target->PendingDestroy() ||
                !explicit_target->ActiveInHierarchy()) return nullptr;
            return explicit_target;
        }

        // 明示参照が未指定のときだけ自動探索する。名前やタグに依存せず、
        // 既存の PlayerControllerComponent を「操作対象らしさ」の根拠にする。
        for (std::size_t index = 0; index < scene->GameObjectCount(); ++index)
        {
            Core::GameObject* candidate = scene->GameObjectAt(index);
            if (candidate == nullptr || candidate == Owner() || candidate->PendingDestroy() ||
                !candidate->ActiveInHierarchy()) continue;
            if (candidate->GetComponent<PlayerControllerComponent>() != nullptr) return candidate;
        }
        return nullptr;
    }

    Core::GameObject* EnemyBehaviourComponent::ResolveWaypoint(std::size_t index) const
    {
        if (index >= patrol_waypoints.size()) return nullptr;
        const Reflection::ObjectReference& reference = patrol_waypoints[index];
        if (!reference.IsAssigned()) return nullptr;
        Scene::Scene* scene = GetScene();
        if (scene == nullptr) return nullptr;
        Core::GameObject* waypoint = scene->FindGameObjectByID(reference.object);
        if (waypoint == nullptr || waypoint->PendingDestroy() || !waypoint->ActiveInHierarchy())
            return nullptr;
        return waypoint;
    }

    bool EnemyBehaviourComponent::HasUsableWaypoint() const
    {
        for (std::size_t index = 0; index < patrol_waypoints.size(); ++index)
            if (ResolveWaypoint(index) != nullptr) return true;
        return false;
    }

    bool EnemyBehaviourComponent::AdvanceToUsableWaypoint()
    {
        if (patrol_waypoints.empty()) return false;
        for (std::size_t count = 0; count < patrol_waypoints.size(); ++count)
        {
            current_waypoint_index_ = (current_waypoint_index_ + 1) % patrol_waypoints.size();
            if (ResolveWaypoint(current_waypoint_index_) != nullptr) return true;
        }
        return false;
    }

    DirectX::XMFLOAT3 EnemyBehaviourComponent::TargetPosition(
        const Core::GameObject& target_object) const
    {
        return target_object.GetTransform().WorldPosition();
    }

    bool EnemyBehaviourComponent::TargetInAttackRange(
        const Core::GameObject& target_object) const
    {
        const Core::GameObject* owner = Owner();
        if (owner == nullptr) return false;
        const float range = (std::max)(0.0f, attack_range);
        return PlanarDistanceSquared(owner->GetTransform().WorldPosition(),
            target_object.GetTransform().WorldPosition()) <= range * range;
    }

    bool EnemyBehaviourComponent::CanSeeTarget(Core::GameObject& target_object)
    {
        last_los_tested_ = false;
        last_los_clear_ = false;

        Core::GameObject* owner = Owner();
        if (owner == nullptr) return false;

        const DirectX::XMFLOAT3 owner_position = owner->GetTransform().WorldPosition();
        const DirectX::XMFLOAT3 target_position = target_object.GetTransform().WorldPosition();
        const float range = (std::max)(0.0f, detection_range);
        if (PlanarDistanceSquared(owner_position, target_position) > range * range) return false;

        DirectX::XMFLOAT3 eye = owner_position;
        eye.y += eye_height;
        DirectX::XMFLOAT3 target_eye = target_position;
        target_eye.y += target_height;
        last_los_start_ = eye;
        last_los_end_ = target_eye;
        last_los_hit_ = target_eye;

        DirectX::XMFLOAT3 to_target{
            target_eye.x - eye.x,
            target_eye.y - eye.y,
            target_eye.z - eye.z };
        const float distance_squared = to_target.x * to_target.x +
            to_target.y * to_target.y + to_target.z * to_target.z;
        if (distance_squared <= Epsilon * Epsilon)
        {
            last_los_tested_ = true;
            last_los_clear_ = true;
            return true;
        }

        // FOV は水平面で判定する。上下差は Raycast へ任せ、坂や段差で
        // 視野角が不自然に狭くならないようにする。
        DirectX::XMFLOAT3 planar{ to_target.x, 0.0f, to_target.z };
        const float planar_length = std::sqrt(planar.x * planar.x + planar.z * planar.z);
        if (planar_length > Epsilon)
        {
            planar.x /= planar_length;
            planar.z /= planar_length;

            const DirectX::XMFLOAT4 rotation = owner->GetTransform().WorldRotationQuaternion();
            const DirectX::XMVECTOR forward_vector = DirectX::XMVector3Rotate(
                DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), DirectX::XMLoadFloat4(&rotation));
            DirectX::XMFLOAT3 forward{};
            DirectX::XMStoreFloat3(&forward, forward_vector);
            const float forward_length = std::sqrt(forward.x * forward.x + forward.z * forward.z);
            if (forward_length > Epsilon)
            {
                forward.x /= forward_length;
                forward.z /= forward_length;
                const float dot = (std::max)(-1.0f, (std::min)(1.0f,
                    forward.x * planar.x + forward.z * planar.z));
                const float half_fov = (std::max)(0.0f, (std::min)(360.0f,
                    field_of_view_degrees)) * 0.5f * (DirectX::XM_PI / 180.0f);
                if (std::acos(dot) > half_fov) return false;
            }
        }

        Runtime::RuntimeContext* runtime = Runtime();
        if (runtime == nullptr || !runtime->PhysicsAvailable()) return false;

        const float distance = std::sqrt(distance_squared);
        DirectX::XMFLOAT3 direction{
            to_target.x / distance,
            to_target.y / distance,
            to_target.z / distance };
        Scene::RaycastHit hit{};
        const Runtime::RuntimeStatus status = runtime->Raycast(
            eye, direction, distance, visibility_layer, visibility_mask, SelfHandle(), hit);
        if (status != Runtime::RuntimeStatus::Ok) return false;

        last_los_tested_ = true;
        if (!hit.valid)
        {
            last_los_clear_ = true;
            return true;
        }

        last_los_hit_ = hit.point;
        last_los_clear_ = hit.source.object == target_object.ID();
        return last_los_clear_;
    }

    void EnemyBehaviourComponent::WarnMissingAgentOnce()
    {
        if (missing_agent_warning_logged_) return;
        missing_agent_warning_logged_ = true;
        if (Runtime::RuntimeContext* runtime = Runtime())
            runtime->LogWarning("Enemy Behaviour に Nav Agent がありません。移動要求を出せません。",
                SelfHandle());
    }

    void EnemyBehaviourComponent::WarnMissingDamageAreaOnce()
    {
        if (missing_damage_area_warning_logged_) return;
        missing_damage_area_warning_logged_ = true;
        if (Runtime::RuntimeContext* runtime = Runtime())
            runtime->LogWarning(
                "attack_controls_damage_area が有効ですが Damage Area がありません。",
                SelfHandle());
    }

    void EnemyBehaviourComponent::CaptureDamageAreaState()
    {
        if (!attack_controls_damage_area || damage_area_state_captured_) return;
        DamageAreaComponent* damage_area = ResolveDamageArea();
        if (damage_area == nullptr)
        {
            WarnMissingDamageAreaOnce();
            return;
        }
        missing_damage_area_warning_logged_ = false;
        damage_area_original_enabled_ = damage_area->Enabled();
        damage_area_state_captured_ = true;
    }

    void EnemyBehaviourComponent::RestoreDamageAreaState()
    {
        if (!damage_area_state_captured_) return;
        if (DamageAreaComponent* damage_area = ResolveDamageArea())
            damage_area->SetEnabled(damage_area_original_enabled_);
        damage_area_state_captured_ = false;
    }

    void EnemyBehaviourComponent::SetDamageAreaForAttack(bool enabled)
    {
        // false のモードでは SetEnabled を一度も呼ばないことが仕様。
        if (!attack_controls_damage_area) return;
        CaptureDamageAreaState();
        if (DamageAreaComponent* damage_area = ResolveDamageArea())
            damage_area->SetEnabled(enabled);
    }

    void EnemyBehaviourComponent::OnStart()
    {
        Core::GameObject* owner = Owner();
        if (owner == nullptr) return;
        started_ = true;
        home_position_ = owner->GetTransform().WorldPosition();
        last_known_target_position_ = home_position_;
        current_waypoint_index_ = 0;
        time_since_target_visible_ = 0.0f;
        attack_phase_ = EnemyAttackPhase::None;
        attack_phase_remaining_ = 0.0f;

        if (attack_controls_damage_area)
        {
            CaptureDamageAreaState();
            SetDamageAreaForAttack(false);
        }

        SetState(HasUsableWaypoint() ? EnemyState::Patrol : EnemyState::Idle);
    }

    void EnemyBehaviourComponent::OnEnable()
    {
        if (!started_ || !attack_controls_damage_area) return;
        CaptureDamageAreaState();
        SetDamageAreaForAttack(state_ == EnemyState::Attack &&
            attack_phase_ == EnemyAttackPhase::Active);
    }

    void EnemyBehaviourComponent::OnDisable()
    {
        if (attack_controls_damage_area) RestoreDamageAreaState();
        if (NavAgentComponent* agent = ResolveAgent()) agent->Stop();
    }

    void EnemyBehaviourComponent::OnDestroy()
    {
        if (attack_controls_damage_area) RestoreDamageAreaState();
    }

    void EnemyBehaviourComponent::OnPropertyChanged(const char* property_name)
    {
        if (property_name == nullptr || std::strcmp(property_name,
            "attack_controls_damage_area") != 0) return;

        if (!started_) return;
        if (attack_controls_damage_area)
        {
            CaptureDamageAreaState();
            SetDamageAreaForAttack(state_ == EnemyState::Attack &&
                attack_phase_ == EnemyAttackPhase::Active);
        }
        else
        {
            // true -> false へ切り替えた瞬間だけ、AI が握っていた状態を返す。
            // 以後 false モードでは DamageArea に触らない。
            RestoreDamageAreaState();
        }
    }

    void EnemyBehaviourComponent::SetState(EnemyState next_state)
    {
        if (state_ == next_state) return;

        if (state_ == EnemyState::Attack && attack_controls_damage_area)
            SetDamageAreaForAttack(false);

        state_ = next_state;
        attack_phase_ = EnemyAttackPhase::None;
        attack_phase_remaining_ = 0.0f;

        NavAgentComponent* agent = ResolveAgent();
        if (next_state == EnemyState::Attack || next_state == EnemyState::Idle)
        {
            if (agent != nullptr) agent->Stop();
        }
        if (next_state == EnemyState::Attack && attack_controls_damage_area)
            BeginAttackCycle();
    }

    void EnemyBehaviourComponent::BeginAttackCycle()
    {
        if (!attack_controls_damage_area) return;
        attack_phase_ = EnemyAttackPhase::Windup;
        attack_phase_remaining_ = (std::max)(0.0f, attack_windup_seconds);
        SetDamageAreaForAttack(false);
    }

    void EnemyBehaviourComponent::MoveToWaypoint()
    {
        NavAgentComponent* agent = ResolveAgent();
        if (agent == nullptr)
        {
            WarnMissingAgentOnce();
            return;
        }
        missing_agent_warning_logged_ = false;

        Core::GameObject* waypoint = ResolveWaypoint(current_waypoint_index_);
        if (waypoint == nullptr)
        {
            if (!AdvanceToUsableWaypoint())
            {
                SetState(EnemyState::Idle);
                return;
            }
            waypoint = ResolveWaypoint(current_waypoint_index_);
        }
        if (waypoint != nullptr) agent->MoveTo(waypoint->GetTransform().WorldPosition());
    }

    void EnemyBehaviourComponent::UpdateAttack(float delta_time,
        Core::GameObject* target_object, bool visible)
    {
        if (!attack_controls_damage_area) return;

        if (attack_phase_ == EnemyAttackPhase::None) BeginAttackCycle();
        float remaining_delta = (std::max)(0.0f, delta_time);

        // 0 秒設定でも 1 フレームで段階を進められるよう、小さな有限回ループにする。
        for (int transition_count = 0; transition_count < 4; ++transition_count)
        {
            if (attack_phase_remaining_ > remaining_delta)
            {
                attack_phase_remaining_ -= remaining_delta;
                return;
            }
            remaining_delta -= attack_phase_remaining_;

            if (attack_phase_ == EnemyAttackPhase::Windup)
            {
                attack_phase_ = EnemyAttackPhase::Active;
                attack_phase_remaining_ = (std::max)(0.0f, attack_active_seconds);
                SetDamageAreaForAttack(true);
                continue;
            }
            if (attack_phase_ == EnemyAttackPhase::Active)
            {
                attack_phase_ = EnemyAttackPhase::Recovery;
                attack_phase_remaining_ = (std::max)(0.0f, attack_recovery_seconds);
                SetDamageAreaForAttack(false);
                continue;
            }
            if (attack_phase_ == EnemyAttackPhase::Recovery)
            {
                if (target_object != nullptr && visible && TargetInAttackRange(*target_object))
                {
                    BeginAttackCycle();
                    continue;
                }
                if (target_object != nullptr && visible)
                {
                    SetState(EnemyState::Chase);
                    return;
                }
                if (time_since_target_visible_ >= (std::max)(0.0f, lose_sight_delay))
                {
                    SetState(EnemyState::ReturnHome);
                    return;
                }
                BeginAttackCycle();
                continue;
            }
            return;
        }
    }

    void EnemyBehaviourComponent::OnUpdate(float delta_time)
    {
        Core::GameObject* target_object = ResolveTargetObject();
        const bool visible = target_object != nullptr && CanSeeTarget(*target_object);
        if (visible)
        {
            time_since_target_visible_ = 0.0f;
            last_known_target_position_ = TargetPosition(*target_object);
        }
        else
        {
            time_since_target_visible_ += (std::max)(0.0f, delta_time);
        }

        if (visible && state_ != EnemyState::Chase && state_ != EnemyState::Attack)
            SetState(EnemyState::Chase);

        switch (state_)
        {
        case EnemyState::Idle:
            if (visible) SetState(EnemyState::Chase);
            else if (HasUsableWaypoint()) SetState(EnemyState::Patrol);
            break;

        case EnemyState::Patrol:
        {
            if (visible)
            {
                SetState(EnemyState::Chase);
                break;
            }
            if (!HasUsableWaypoint())
            {
                SetState(EnemyState::Idle);
                break;
            }
            MoveToWaypoint();
            NavAgentComponent* agent = ResolveAgent();
            if (agent != nullptr && agent->Arrived())
            {
                AdvanceToUsableWaypoint();
                MoveToWaypoint();
            }
            break;
        }

        case EnemyState::Chase:
        {
            if (visible && target_object != nullptr && TargetInAttackRange(*target_object))
            {
                SetState(EnemyState::Attack);
                break;
            }

            NavAgentComponent* agent = ResolveAgent();
            if (agent == nullptr)
            {
                WarnMissingAgentOnce();
            }
            else
            {
                missing_agent_warning_logged_ = false;
                agent->MoveTo(visible && target_object != nullptr
                    ? TargetPosition(*target_object) : last_known_target_position_);
            }

            if (!visible && time_since_target_visible_ >= (std::max)(0.0f, lose_sight_delay))
                SetState(EnemyState::ReturnHome);
            break;
        }

        case EnemyState::Attack:
        {
            if (NavAgentComponent* agent = ResolveAgent()) agent->Stop();

            if (!attack_controls_damage_area)
            {
                // 接触ダメージ型では DamageArea の enabled に一度も触らない。
                if (visible && target_object != nullptr && !TargetInAttackRange(*target_object))
                    SetState(EnemyState::Chase);
                else if (!visible && time_since_target_visible_ >=
                    (std::max)(0.0f, lose_sight_delay))
                    SetState(EnemyState::ReturnHome);
                break;
            }

            UpdateAttack(delta_time, target_object, visible);
            break;
        }

        case EnemyState::ReturnHome:
        {
            if (visible)
            {
                SetState(EnemyState::Chase);
                break;
            }
            NavAgentComponent* agent = ResolveAgent();
            if (agent == nullptr)
            {
                WarnMissingAgentOnce();
                break;
            }
            missing_agent_warning_logged_ = false;
            agent->MoveTo(home_position_);
            if (agent->Arrived())
            {
                current_waypoint_index_ = 0;
                SetState(HasUsableWaypoint() ? EnemyState::Patrol : EnemyState::Idle);
            }
            break;
        }
        }
    }
}
