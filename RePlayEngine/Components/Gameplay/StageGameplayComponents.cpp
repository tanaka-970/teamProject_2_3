#include "StageGameplayComponents.h"

#include "CharacterMotorComponent.h"
#include "HealthComponent.h"
#include "StageGameplayCommon.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Runtime/API/RuntimeContext.h"
#include "../../Scene/Runtime/Scene.h"

#include <algorithm>
#include <cmath>

namespace ReplayEngine::Components
{
    namespace
    {
        Scene::RespawnService::Point MakePoint(const Core::GameObject& owner,
            const DirectX::XMFLOAT3& offset, const DirectX::XMFLOAT3& rotation)
        {
            const DirectX::XMFLOAT3 world = owner.GetTransform().WorldPosition();
            Scene::RespawnService::Point point;
            point.source = owner.ID();
            point.position = { world.x + offset.x, world.y + offset.y, world.z + offset.z };
            point.rotation = rotation;
            point.valid = true;
            return point;
        }

        bool AcceptContact(const Core::GameObject* owner, const Core::TriggerContact& contact,
            int target_mask, Core::GameObject*& other)
        {
            if (!StageGameplay::IsTriggerSide(owner, contact)) return false;
            other = StageGameplay::ResolveOther(owner, contact);
            return other != nullptr &&
                StageGameplay::MatchesTargetMask(*other, contact.other_collider, target_mask);
        }
    }

    void SpawnPointComponent::RegisterPoint()
    {
        Core::GameObject* owner = Owner();
        Scene::Scene* scene = GetScene();
        if (owner == nullptr || scene == nullptr || !ActiveInHierarchy()) return;
        Scene::RespawnService::Point point = MakePoint(*owner,
            { 0.0f, 0.0f, 0.0f }, owner->GetTransform().LocalRotationEuler());
        point.identifier = spawn_id;
        point.team = team;
        scene->Services().Respawn().OfferSpawnPoint(point, priority);
    }

    void SpawnPointComponent::OnStart() { RegisterPoint(); }
    void SpawnPointComponent::OnEnable() { RegisterPoint(); }

    void CheckpointComponent::OnTriggerEnter(const Core::TriggerContact& contact)
    {
        if (one_shot && activated_) return;
        Core::GameObject* other = nullptr;
        if (!AcceptContact(Owner(), contact, target_mask, other)) return;

        Core::GameObject* owner = Owner();
        Scene::Scene* scene = GetScene();
        if (owner == nullptr || scene == nullptr) return;

        scene->Services().Respawn().SetCheckpoint(
            MakePoint(*owner, respawn_position_offset, respawn_rotation));

        Scene::GameplayEventLog::Event event;
        event.kind = Scene::GameplayEventLog::Kind::CheckpointActivated;
        event.source = owner->ID();
        event.subject = other->ID();
        event.identifier = checkpoint_id;
        scene->Services().GameplayEvents().Push(event);
        activated_ = true;
    }

    void GoalComponent::OnTriggerEnter(const Core::TriggerContact& contact)
    {
        if (one_shot && activated_) return;
        Core::GameObject* other = nullptr;
        if (!AcceptContact(Owner(), contact, target_mask, other)) return;

        Core::GameObject* owner = Owner();
        Scene::Scene* scene = GetScene();
        if (owner == nullptr || scene == nullptr) return;

        Scene::GameplayEventLog::Event event;
        event.kind = Scene::GameplayEventLog::Kind::GoalReached;
        event.source = owner->ID();
        event.subject = other->ID();
        event.identifier = goal_id;
        scene->Services().GameplayEvents().Push(event);
        if (!completion_event.empty())
        {
            if (Runtime::RuntimeContext* runtime = scene->Services().Runtime())
                runtime->TriggerSceneFlow(completion_event);
        }
        activated_ = true;
    }

    void KillVolumeComponent::OnTriggerEnter(const Core::TriggerContact& contact)
    {
        Core::GameObject* other = nullptr;
        if (!AcceptContact(Owner(), contact, target_mask, other)) return;

        Core::GameObject* owner = Owner();
        Scene::Scene* scene = GetScene();
        if (owner == nullptr || scene == nullptr) return;

        if (HealthComponent* health = other->GetComponent<HealthComponent>())
        {
            health->ApplyDamage((std::max)(damage_amount, 0));
        }

        if (respawn_at_checkpoint)
        {
            const Scene::RespawnService::Point& point = scene->Services().Respawn().ActivePoint();
            if (point.valid)
            {
                StageGameplay::TeleportObject(*other, point.position, point.rotation, true);
                if (HealthComponent* health = other->GetComponent<HealthComponent>())
                    health->ResetToFull();
            }
        }

        Scene::GameplayEventLog::Event event;
        event.kind = Scene::GameplayEventLog::Kind::KillVolumeEntered;
        event.source = owner->ID();
        event.subject = other->ID();
        scene->Services().GameplayEvents().Push(event);
    }

    void JumpPadComponent::OnUpdate(float delta_time)
    {
        const float elapsed = (std::max)(delta_time, 0.0f);
        for (CooldownEntry& entry : cooldowns_)
            entry.remaining = (std::max)(0.0f, entry.remaining - elapsed);
        cooldowns_.erase(std::remove_if(cooldowns_.begin(), cooldowns_.end(),
            [](const CooldownEntry& entry) { return entry.remaining <= 0.0f; }), cooldowns_.end());
    }

    void JumpPadComponent::TryLaunch(const Core::TriggerContact& contact)
    {
        if (one_shot && activated_) return;
        Core::GameObject* other = nullptr;
        if (!AcceptContact(Owner(), contact, target_mask, other)) return;

        const auto cooling = std::find_if(cooldowns_.begin(), cooldowns_.end(),
            [other](const CooldownEntry& entry) { return entry.object == other->ID(); });
        if (cooling != cooldowns_.end()) return;

        CharacterMotorComponent* motor = other->GetComponent<CharacterMotorComponent>();
        if (motor == nullptr || !motor->ActiveInHierarchy()) return;

        DirectX::XMVECTOR vector = DirectX::XMLoadFloat3(&direction);
        const float length = DirectX::XMVectorGetX(DirectX::XMVector3Length(vector));
        if (!std::isfinite(length) || length <= 0.0001f) return;
        vector = DirectX::XMVectorScale(vector, (std::max)(force, 0.0f) / length);
        DirectX::XMFLOAT3 impulse;
        DirectX::XMStoreFloat3(&impulse, vector);
        motor->ApplyImpulse(impulse);

        CooldownEntry entry;
        entry.object = other->ID();
        entry.remaining = (std::max)(cooldown, 0.0f);
        if (entry.remaining > 0.0f) cooldowns_.push_back(entry);
        activated_ = true;
    }

    void JumpPadComponent::OnTriggerEnter(const Core::TriggerContact& contact) { TryLaunch(contact); }
    void JumpPadComponent::OnTriggerStay(const Core::TriggerContact& contact) { TryLaunch(contact); }

    void DamageAreaComponent::OnUpdate(float delta_time)
    {
        const float elapsed = (std::max)(delta_time, 0.0f);
        for (ContactState& state : contacts_)
            state.remaining = (std::max)(0.0f, state.remaining - elapsed);
    }

    void DamageAreaComponent::TryApply(const Core::TriggerContact& contact, bool entering)
    {
        Core::GameObject* other = nullptr;
        if (!AcceptContact(Owner(), contact, target_mask, other)) return;

        auto found = std::find_if(contacts_.begin(), contacts_.end(),
            [other](const ContactState& state) { return state.object == other->ID(); });
        if (found == contacts_.end())
        {
            ContactState state;
            state.object = other->ID();
            contacts_.push_back(state);
            found = contacts_.end() - 1;
        }

        if (one_shot && found->applied) return;
        if (!entering && found->remaining > 0.0f) return;

        if (HealthComponent* health = other->GetComponent<HealthComponent>())
        {
            health->ApplyDamage((std::max)(damage, 0));
            found->applied = true;
            found->remaining = (std::max)(interval, 0.0f);
        }
    }

    void DamageAreaComponent::OnTriggerEnter(const Core::TriggerContact& contact)
    {
        TryApply(contact, true);
    }

    void DamageAreaComponent::OnTriggerStay(const Core::TriggerContact& contact)
    {
        TryApply(contact, false);
    }

    void DamageAreaComponent::OnTriggerExit(const Core::TriggerContact& contact)
    {
        if (!StageGameplay::IsTriggerSide(Owner(), contact)) return;
        contacts_.erase(std::remove_if(contacts_.begin(), contacts_.end(),
            [&contact](const ContactState& state) { return state.object == contact.other_object; }),
            contacts_.end());
    }
}
