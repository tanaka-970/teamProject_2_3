#include "StageGameplayCommon.h"

#include "CharacterMotorComponent.h"
#include "../Physics/ColliderComponent.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Physics/CollisionLayers.h"
#include "../../Scene/Runtime/Scene.h"

namespace ReplayEngine::Components::StageGameplay
{
    bool IsTriggerSide(const Core::GameObject* owner,
        const Core::TriggerContact& contact) noexcept
    {
        return owner != nullptr && contact.trigger_object == owner->ID();
    }

    Core::GameObject* ResolveOther(const Core::GameObject* owner,
        const Core::TriggerContact& contact)
    {
        if (owner == nullptr) return nullptr;

        Scene::Scene* scene = owner->GetScene();
        if (scene == nullptr) return nullptr;

        // ObjectID から引き直す。TriggerContact は生ポインタを持たないので、
        // 配送の途中で消えた GameObject を掴むことがない。
        Core::GameObject* other = scene->FindGameObjectByID(contact.other_object);
        if (other == nullptr || other->PendingDestroy()) return nullptr;
        return other;
    }

    bool MatchesTargetMask(const Core::GameObject& other,
        std::uint32_t other_collider_id, int target_mask) noexcept
    {
        // -1 と全ビット立ては「すべてに反応する」。旧データ互換も兼ねる。
        if (target_mask == -1) return true;

        // 接触した Collider そのもののレイヤーを見る。
        // GameObject 単位ではなく Collider 単位なのは、
        // 1 つの GameObject が複数レイヤーの Collider を持てるため。
        const ColliderComponent* collider =
            FindColliderByID(const_cast<Core::GameObject&>(other), other_collider_id);

        // 実行時 ColliderID で引けなかった場合は GameObject 上の
        // どれか 1 つでも一致すれば通す（Collider が差し替わった直後など）。
        if (collider != nullptr)
        {
            return Physics::CollisionLayers::MaskContains(target_mask,
                Physics::CollisionLayers::ClampLayer(collider->collision_layer));
        }

        for (std::size_t index = 0; index < other.ComponentCount(); ++index)
        {
            const Core::Component* component = other.ComponentAt(index);
            if (component == nullptr || component->PendingDestroy()) continue;
            const auto* candidate = dynamic_cast<const ColliderComponent*>(component);
            if (candidate == nullptr) continue;
            if (Physics::CollisionLayers::MaskContains(target_mask,
                Physics::CollisionLayers::ClampLayer(candidate->collision_layer)))
            {
                return true;
            }
        }
        return false;
    }

    void TeleportObject(Core::GameObject& target,
        const DirectX::XMFLOAT3& world_position,
        const DirectX::XMFLOAT3& world_rotation_radians,
        bool apply_rotation)
    {
        // CharacterMotor があればそちらへ委ねる。
        // Transform を直接書き換えると、Motor が持つ速度と接地状態が
        // 前の位置のまま残り、復帰直後に落下したり壁へめり込んだりする。
        if (auto* motor = target.GetComponent<CharacterMotorComponent>())
        {
            motor->Teleport(world_position);
        }
        else
        {
            target.GetTransform().SetWorldPosition(world_position);
        }

        if (apply_rotation)
        {
            // 回転はローカルへ入れる。復帰点を持つ GameObject が
            // 親の下にある場合でも、見た目の向きは親に従うのが自然なため。
            target.GetTransform().SetLocalRotationEuler(world_rotation_radians);
        }
    }
}
