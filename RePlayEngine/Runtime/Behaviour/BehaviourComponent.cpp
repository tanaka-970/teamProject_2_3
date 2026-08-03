#include "BehaviourComponent.h"

#include "../API/RuntimeContext.h"
#include "../Events/EventBus.h"
#include "../Handles/HandleResolver.h"
#include "../../Components/Physics/ColliderComponent.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Scene/Runtime/Scene.h"

namespace ReplayEngine::Runtime
{
    const char* ToString(ContactPhase phase) noexcept
    {
        switch (phase)
        {
        case ContactPhase::Enter: return "Enter";
        case ContactPhase::Stay:  return "Stay";
        case ContactPhase::Exit:  return "Exit";
        }
        return "Unknown";
    }

    const char* ToString(CollisionHitKind kind) noexcept
    {
        switch (kind)
        {
        case CollisionHitKind::Unknown:         return "Unknown";
        case CollisionHitKind::CharacterGround: return "CharacterGround";
        case CollisionHitKind::CharacterWall:   return "CharacterWall";
        }
        return "Unknown";
    }

    // ---- 参照 --------------------------------------------------------------

    ObjectHandle BehaviourComponent::SelfHandle() const noexcept
    {
        Scene::Scene* world = GetScene();
        if (world == nullptr) return ObjectHandle::None();

        const HandleResolver resolver(*world);
        return resolver.MakeHandle(Owner());
    }

    ComponentHandle BehaviourComponent::SelfComponentHandle() const noexcept
    {
        Scene::Scene* world = GetScene();
        if (world == nullptr) return ComponentHandle::None();

        const HandleResolver resolver(*world);
        return resolver.MakeHandle(this);
    }

    RuntimeContext* BehaviourComponent::Runtime() const noexcept
    {
        Scene::Scene* world = GetScene();
        return world != nullptr ? world->Services().Runtime() : nullptr;
    }

    // ---- ライフサイクル ------------------------------------------------------

    void BehaviourComponent::OnRuntimeAwake()
    {
        // Component 側で一度しか呼ばれないよう管理されているが、
        // Behaviour 自身でも印を持っておく。Diagnostics で
        // 「Awake 済みか」を型を問わず見られるようにするため。
        if (awake_called_) return;
        awake_called_ = true;
        OnAwake();
    }

    void BehaviourComponent::OnRuntimeDestroy()
    {
        // 購読の解除を先に済ませる。
        // OnDestroy の中から発行されたイベントが、消えかけの自分へ
        // 配送されることを避ける。
        if (Scene::Scene* world = GetScene())
        {
            if (RuntimeContext* runtime = world->Services().Runtime())
            {
                runtime->Events().UnsubscribeOwner(SelfHandle());
            }
        }
        OnDestroy();
    }

    // ---- Trigger -------------------------------------------------------------

    bool BehaviourComponent::BuildTriggerEvent(const Core::TriggerContact& contact,
        ContactPhase phase, TriggerEvent& out) const
    {
        Core::GameObject* owner = Owner();
        Scene::Scene* world = GetScene();
        if (owner == nullptr || world == nullptr) return false;

        const HandleResolver resolver(*world);

        // 自分がどちら側かを判別する。
        // 同じ接触ペアが Trigger 側と入った側の両方へ配送されるため、
        // 受け取った側から見た「自分」「相手」へ組み直す必要がある。
        const bool self_is_trigger = contact.trigger_object == owner->ID();
        const Core::ObjectID other_id =
            self_is_trigger ? contact.other_object : contact.trigger_object;

        out.phase = phase;
        out.self_is_trigger = self_is_trigger;
        out.self = resolver.MakeHandle(owner);
        out.self_collider = self_is_trigger ? contact.trigger_collider : contact.other_collider;
        out.other_collider = self_is_trigger ? contact.other_collider : contact.trigger_collider;
        out.frame_index = 0;

        Core::GameObject* other = world->FindGameObjectByID(other_id);
        if (other != nullptr && !other->PendingDestroy())
        {
            out.other = resolver.MakeHandle(other);
            out.other_valid = true;
        }
        else
        {
            // Exit は相手が消えたことで起きる場合がある。
            // その場合でもイベント自体は届ける。相手 Handle は空のまま。
            out.other = ObjectHandle::None();
            out.other_valid = false;
        }

        // Layer は Collider から引く。引けなければ -1 のまま。
        // 「Layer 0」と「不明」を取り違えないようにするため 0 で埋めない。
        const auto layer_of = [](Core::GameObject* object, Scene::ColliderID collider) -> int
        {
            if (object == nullptr || collider == Scene::invalid_collider_id) return -1;
            const Components::ColliderComponent* found =
                Components::FindColliderByKey(*object, static_cast<int>(collider));
            return found != nullptr ? found->collision_layer : -1;
        };
        out.self_layer = layer_of(owner, out.self_collider);
        out.other_layer = layer_of(other, out.other_collider);

        if (RuntimeContext* runtime = world->Services().Runtime())
        {
            out.frame_index = runtime->FrameIndex();
        }
        return true;
    }

    void BehaviourComponent::OnTriggerEnter(const Core::TriggerContact& contact)
    {
        // 削除予約済みへは配送しない。
        // 予約から実際の破棄までの間に処理が走ると、
        // 次の同期点で消えるオブジェクトに副作用を残すことになる。
        if (PendingDestroy()) return;

        TriggerEvent event;
        if (BuildTriggerEvent(contact, ContactPhase::Enter, event)) OnTriggerEnter(event);
    }

    void BehaviourComponent::OnTriggerStay(const Core::TriggerContact& contact)
    {
        if (PendingDestroy()) return;

        TriggerEvent event;
        if (BuildTriggerEvent(contact, ContactPhase::Stay, event)) OnTriggerStay(event);
    }

    void BehaviourComponent::OnTriggerExit(const Core::TriggerContact& contact)
    {
        if (PendingDestroy()) return;

        TriggerEvent event;
        if (BuildTriggerEvent(contact, ContactPhase::Exit, event)) OnTriggerExit(event);
    }

    // ---- Collision の受け渡し -------------------------------------------------

    void BehaviourEventDispatch::Collision(BehaviourComponent& behaviour,
        const CollisionEvent& event)
    {
        switch (event.phase)
        {
        case ContactPhase::Enter: behaviour.OnCollisionEnter(event); break;
        case ContactPhase::Stay:  behaviour.OnCollisionStay(event);  break;
        case ContactPhase::Exit:  behaviour.OnCollisionExit(event);  break;
        }
    }
}
