#include "CollisionEventDispatcher.h"

#include "../API/RuntimeContext.h"
#include "../Behaviour/BehaviourComponent.h"
#include "../Events/EventBus.h"
#include "../Handles/HandleResolver.h"
#include "../../Components/Gameplay/CharacterMotorComponent.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Scene/Runtime/Scene.h"

#include <algorithm>

namespace ReplayEngine::Runtime
{
    using Components::CharacterMotorComponent;
    using Core::GameObject;
    using Core::ObjectID;

    void CollisionEventDispatcher::Reset() noexcept
    {
        contacts_.clear();
        world_instance_ = Core::invalid_world_instance_id;
    }

    namespace
    {
        // 接触を EventBus へも流す。C++ の OnCollisionXxx と同じ瞬間に出すので、
        // C# / Lua など Behaviour を継承できない購読者も同じ情報を受け取れる。
        void PublishContactEvent(Scene::Scene& world, const CollisionEvent& event)
        {
            RuntimeContext* runtime = world.Services().Runtime();
            if (runtime == nullptr) return;

            Reflection::TypeGUID type;
            const char* type_name = nullptr;
            switch (event.phase)
            {
            case ContactPhase::Enter:
                type = EngineEvents::CollisionEnter;
                type_name = "CollisionEnter";
                break;
            case ContactPhase::Stay:
                type = EngineEvents::CollisionStay;
                type_name = "CollisionStay";
                break;
            default:
                type = EngineEvents::CollisionExit;
                type_name = "CollisionExit";
                break;
            }

            // 購読者がいなければ PropertyBag を組む前に抜ける。
            // 接触は毎フレーム起きるので、組んでから捨てると確保が減らない。
            if (!runtime->Events().HasSubscribers(type)) return;

            EventRecord record;
            record.type = type;
            record.type_name = type_name;
            record.source = event.self;
            record.target = event.other;
            record.payload.Set("point_x",
                Reflection::PropertyValue::MakeFloat(event.contact_point.x));
            record.payload.Set("point_y",
                Reflection::PropertyValue::MakeFloat(event.contact_point.y));
            record.payload.Set("point_z",
                Reflection::PropertyValue::MakeFloat(event.contact_point.z));
            record.payload.Set("normal_x",
                Reflection::PropertyValue::MakeFloat(event.contact_normal.x));
            record.payload.Set("normal_y",
                Reflection::PropertyValue::MakeFloat(event.contact_normal.y));
            record.payload.Set("normal_z",
                Reflection::PropertyValue::MakeFloat(event.contact_normal.z));
            record.payload.Set("hit_kind",
                Reflection::PropertyValue::MakeInt(static_cast<int>(event.hit_kind)));
            record.payload.Set("self_collider",
                Reflection::PropertyValue::MakeInt(static_cast<int>(event.self_collider)));
            record.payload.Set("other_collider",
                Reflection::PropertyValue::MakeInt(
                    static_cast<int>(event.other_collider)));
            record.payload.Set("relative_velocity_x",
                Reflection::PropertyValue::MakeFloat(event.relative_velocity.x));
            record.payload.Set("relative_velocity_y",
                Reflection::PropertyValue::MakeFloat(event.relative_velocity.y));
            record.payload.Set("relative_velocity_z",
                Reflection::PropertyValue::MakeFloat(event.relative_velocity.z));
            record.payload.Set("penetration_depth",
                Reflection::PropertyValue::MakeFloat(event.penetration_depth));
            record.payload.Set("other_valid",
                Reflection::PropertyValue::MakeBool(event.other_valid));
            runtime->Events().Publish(std::move(record));
        }
    }

    void CollisionEventDispatcher::Deliver(Scene::Scene& world, const Contact& contact,
        ContactPhase phase, std::uint64_t frame_index)
    {
        const HandleResolver resolver(world);

        GameObject* self = resolver.Resolve(contact.self);
        if (self == nullptr)
        {
            // 自分が既に居ない。配送先が無いので数えて終わる。
            ++skipped_count_;
            return;
        }

        CollisionEvent event;
        event.phase = phase;
        event.hit_kind = contact.kind;
        event.self = contact.self;
        event.self_collider = contact.self_collider;
        event.other_collider = contact.other_collider;
        event.contact_point = contact.point;
        event.contact_normal = contact.normal;
        event.relative_velocity = contact.relative_velocity;
        event.penetration_depth = contact.penetration;
        event.frame_index = frame_index;

        // 相手は「居ればつなぐ」。Exit は相手が消えたことで起きる場合があるので、
        // 相手が引けなくてもイベント自体は届ける。
        GameObject* other = world.FindGameObjectByID(contact.other);
        if (other != nullptr && !other->PendingDestroy())
        {
            event.other = resolver.MakeHandle(other);
            event.other_valid = true;
        }
        else
        {
            event.other = ObjectHandle::None();
            event.other_valid = false;
        }

        PublishContactEvent(world, event);

        // 添字で回す。コールバックの中から Component が追加されても
        // 参照が壊れないようにするため。
        // 開始時点の件数で止めるので、この回で増えたぶんは配らない。
        const std::size_t count = self->ComponentCount();
        for (std::size_t index = 0; index < count && index < self->ComponentCount(); ++index)
        {
            Core::Component* component = self->ComponentAt(index);
            if (component == nullptr) continue;

            // 削除予約済みへは配送しない。
            // 予約から実際の破棄までの間に処理が走ると、
            // 次の同期点で消える相手に副作用を残すことになる。
            if (component->PendingDestroy()) { ++skipped_count_; continue; }

            // 無効な Behaviour へは配送しない。
            // ActiveInHierarchy は自分と所有 GameObject の階層をまとめて見る。
            if (!component->ActiveInHierarchy()) { ++skipped_count_; continue; }

            auto* behaviour = dynamic_cast<BehaviourComponent*>(component);
            if (behaviour == nullptr) continue;

            BehaviourEventDispatch::Collision(*behaviour, event);
        }
    }

    void CollisionEventDispatcher::Observe(Scene::Scene& world, const ObjectHandle& self,
        CollisionHitKind kind, Scene::ColliderID self_collider, ObjectID other,
        Scene::ColliderID other_collider,
        const DirectX::XMFLOAT3& point, const DirectX::XMFLOAT3& normal,
        const DirectX::XMFLOAT3& relative_velocity, float penetration,
        std::uint64_t frame_index)
    {
        // 同じ「自分 × 接触の種類」の組で既に接触中かを探す。
        //
        // 種類ごとに 1 件しか持たない理由:
        //   Motor は 1 FixedUpdate につき床を 1 つ、壁を 1 つしか確定させない。
        //   複数持てるようにしても、埋まらない枠が増えるだけで意味が無い。
        const auto found = std::find_if(contacts_.begin(), contacts_.end(),
            [&self, kind, self_collider, other, other_collider](const Contact& contact)
            {
                if (contact.self != self || contact.kind != kind) return false;
                if (kind != CollisionHitKind::Rigidbody) return true;
                return contact.self_collider == self_collider && contact.other == other &&
                    contact.other_collider == other_collider;
            });

        if (found == contacts_.end())
        {
            Contact contact;
            contact.self = self;
            contact.kind = kind;
            contact.self_collider = self_collider;
            contact.other = other;
            contact.other_collider = other_collider;
            contact.point = point;
            contact.normal = normal;
            contact.relative_velocity = relative_velocity;
            contact.penetration = penetration;
            contact.seen_this_frame = true;

            contacts_.push_back(contact);
            ++enter_count_;
            Deliver(world, contacts_.back(), ContactPhase::Enter, frame_index);
            return;
        }

        // 接触相手が入れ替わった場合は、必ず Exit を出してから Enter を出す。
        // Stay のまま相手だけ差し替えると、受け取った側から見て
        // 「離れていないのに別の物に乗っている」状態になる。
        if (found->other != other || found->other_collider != other_collider)
        {
            Contact previous = *found;
            ++exit_count_;
            Deliver(world, previous, ContactPhase::Exit, frame_index);

            found->other = other;
            found->other_collider = other_collider;
            found->point = point;
            found->normal = normal;
            found->relative_velocity = relative_velocity;
            found->penetration = penetration;
            found->seen_this_frame = true;

            ++enter_count_;
            Deliver(world, *found, ContactPhase::Enter, frame_index);
            return;
        }

        found->point = point;
        found->normal = normal;
        found->relative_velocity = relative_velocity;
        found->penetration = penetration;
        found->seen_this_frame = true;

        ++stay_count_;
        Deliver(world, *found, ContactPhase::Stay, frame_index);
    }

    void CollisionEventDispatcher::Dispatch(Scene::Scene& world, std::uint64_t frame_index)
    {
        // World が入れ替わっていたら、前の World の接触状態は全部捨てる。
        //
        // Exit を配らない理由:
        //   その World の GameObject も Behaviour も既に存在しない。
        //   配送先が無いものに Exit を配ろうとすると、
        //   「消えた実体を引き直す」経路を作ることになる。
        const Core::WorldInstanceID current = world.WorldInstanceID();
        if (current != world_instance_)
        {
            contacts_.clear();
            world_instance_ = current;
        }

        for (Contact& contact : contacts_) contact.seen_this_frame = false;

        const HandleResolver resolver(world);

        // Scene 上の CharacterMotor を走査して、今フレームの接触を拾う。
        const std::size_t object_count = world.GameObjectCount();
        for (std::size_t index = 0; index < object_count && index < world.GameObjectCount();
            ++index)
        {
            GameObject* object = world.GameObjectAt(index);
            if (object == nullptr || object->PendingDestroy()) continue;

            const CharacterMotorComponent* motor =
                object->GetComponent<CharacterMotorComponent>();
            if (motor == nullptr) continue;

            // Motor 自体が無効なら接触は起きていない。
            // seen_this_frame が立たないので、続く走査で Exit になる。
            if (!motor->ActiveInHierarchy()) continue;

            const ObjectHandle self = resolver.MakeHandle(object);
            if (self.IsEmpty()) continue;

            if (motor->HasGround() && motor->LastGroundSource().object.Valid())
            {
                Observe(world, self, CollisionHitKind::CharacterGround,
                    Scene::invalid_collider_id, motor->LastGroundSource().object,
                    motor->LastGroundSource().collider,
                    motor->LastGroundPoint(), motor->GroundNormal(),
                    DirectX::XMFLOAT3{}, 0.0f, frame_index);
            }

            if (motor->HasWallContact() && motor->LastWallSource().object.Valid())
            {
                Observe(world, self, CollisionHitKind::CharacterWall,
                    Scene::invalid_collider_id, motor->LastWallSource().object,
                    motor->LastWallSource().collider,
                    motor->LastWallPoint(), motor->LastWallNormal(),
                    DirectX::XMFLOAT3{}, 0.0f, frame_index);
            }
        }

        // Rigidbody Solver が実際に解いた接触を両側の視点へ配送する。
        const Scene::IPhysicsDynamicsService* dynamics = world.Services().PhysicsDynamics();
        if (dynamics != nullptr)
        {
            for (const Scene::PhysicsContact& physics : dynamics->Contacts())
            {
                GameObject* object_a = world.FindGameObjectByID(physics.object_a);
                GameObject* object_b = world.FindGameObjectByID(physics.object_b);
                if (object_a == nullptr || object_b == nullptr ||
                    object_a->PendingDestroy() || object_b->PendingDestroy()) continue;

                const ObjectHandle handle_a = resolver.MakeHandle(object_a);
                const ObjectHandle handle_b = resolver.MakeHandle(object_b);
                if (!handle_a.IsEmpty())
                {
                    Observe(world, handle_a, CollisionHitKind::Rigidbody,
                        physics.collider_a, physics.object_b, physics.collider_b,
                        physics.point, physics.normal, physics.relative_velocity,
                        physics.penetration, frame_index);
                }
                if (!handle_b.IsEmpty())
                {
                    const DirectX::XMFLOAT3 opposite_normal{
                        -physics.normal.x, -physics.normal.y, -physics.normal.z };
                    const DirectX::XMFLOAT3 opposite_velocity{
                        -physics.relative_velocity.x, -physics.relative_velocity.y,
                        -physics.relative_velocity.z };
                    Observe(world, handle_b, CollisionHitKind::Rigidbody,
                        physics.collider_b, physics.object_a, physics.collider_a,
                        physics.point, opposite_normal, opposite_velocity,
                        physics.penetration, frame_index);
                }
            }
        }

        // 今フレーム見つからなかった接触は離れたということ。
        //
        // ここが「地面から離れたら Exit」と「Motor / GameObject が消えたら Exit」の
        // 両方を兼ねる。接触が続いているかを Motor 側へ聞きに行く経路が
        // 1 本しかないので、離れ方の種類ごとに処理を書き分けなくてよい。
        std::vector<Contact> ended;
        for (const Contact& contact : contacts_)
        {
            if (!contact.seen_this_frame) ended.push_back(contact);
        }

        contacts_.erase(
            std::remove_if(contacts_.begin(), contacts_.end(),
                [](const Contact& contact) { return !contact.seen_this_frame; }),
            contacts_.end());

        for (const Contact& contact : ended)
        {
            ++exit_count_;
            Deliver(world, contact, ContactPhase::Exit, frame_index);
        }
    }
}
