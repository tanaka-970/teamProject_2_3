// SceneCollisionWorld のうち「Trigger の重なり判定とイベント配送」だけを持つ。
//
// 【Enter / Stay / Exit の作り方】
//   接触しているペアを覚えておき、フレーム番号で「今回も見えたか」を判定する。
//     初めて見えた   -> Enter
//     前回も見えた   -> Stay
//     今回見えなかった -> Exit（そしてペアを捨てる）
//   接触している間ずっと Enter が飛ぶことはない。
//
// 【削除への強さ】
//   ペアが持つのは ObjectID と ColliderID だけ。生ポインタは持たない。
//   配送のたびに Scene から引き直すので、途中で削除されても
//   「引けなかった」で終わるだけ。無効ポインタへ触ることがない。

#include "SceneCollisionWorld.h"

#include "../Runtime/Scene.h"
#include "../../Components/Physics/BoxColliderComponent.h"
#include "../../Components/Physics/CapsuleColliderComponent.h"
#include "../../Components/Physics/SphereColliderComponent.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Physics/CollisionLayers.h"
#include "../../Runtime/API/RuntimeContext.h"
#include "../../Runtime/Events/EventBus.h"
#include "../../Physics/ShapeSweep.h"

#include <cmath>

using namespace DirectX;

namespace ReplayEngine::Scene
{
    namespace Layers = Physics::CollisionLayers;

    namespace
    {
        // Trigger の接触を EventBus へも流す。C++ の OnTriggerXxx と同じ瞬間に出す。
        // source が受け取る側、target が相手。
        void PublishTriggerEvent(Scene& scene, Core::ObjectID receiver,
            const Core::TriggerContact& contact, int phase)
        {
            Runtime::RuntimeContext* runtime = scene.Services().Runtime();
            if (runtime == nullptr) return;

            const bool receiver_is_trigger = receiver == contact.trigger_object;
            const Core::ObjectID other_id = receiver_is_trigger
                ? contact.other_object : contact.trigger_object;

            Runtime::EventRecord record;
            switch (phase)
            {
            case 0:
                record.type = Runtime::EngineEvents::TriggerEnter;
                record.type_name = "TriggerEnter";
                break;
            case 1:
                record.type = Runtime::EngineEvents::TriggerStay;
                record.type_name = "TriggerStay";
                break;
            default:
                record.type = Runtime::EngineEvents::TriggerExit;
                record.type_name = "TriggerExit";
                break;
            }

            const Runtime::HandleResolver resolver(scene);
            Core::GameObject* receiver_object = scene.FindGameObjectByID(receiver);
            Core::GameObject* other_object = scene.FindGameObjectByID(other_id);
            if (receiver_object == nullptr) return;
            record.source = resolver.MakeHandle(receiver_object);
            record.target = other_object != nullptr && !other_object->PendingDestroy()
                ? resolver.MakeHandle(other_object) : Runtime::ObjectHandle::None();

            record.payload.Set("self_collider", Reflection::PropertyValue::MakeInt(
                static_cast<int>(receiver_is_trigger
                    ? contact.trigger_collider : contact.other_collider)));
            record.payload.Set("other_collider", Reflection::PropertyValue::MakeInt(
                static_cast<int>(receiver_is_trigger
                    ? contact.other_collider : contact.trigger_collider)));
            record.payload.Set("self_is_trigger",
                Reflection::PropertyValue::MakeBool(receiver_is_trigger));
            record.payload.Set("other_valid",
                Reflection::PropertyValue::MakeBool(other_object != nullptr));
            runtime->Events().Publish(std::move(record));
        }

        // 相手の形を「これを内包する球」へ落とす。
        //
        // Trigger の入り口判定に必要なのは「触れたか」であって、
        // どこで触れたかではない。近似は必ず本来より大きい側へ倒してあるので、
        // 「Trigger の中に入ったのに反応しない」ことは起きない
        // （代わりに、ごくわずかに早く反応することがある）。
        bool BuildProbeSphere(const Components::ColliderComponent& collider,
            XMFLOAT3& center, float& radius)
        {
            switch (collider.Shape())
            {
            case Components::ColliderShape::Sphere:
            {
                const auto& sphere =
                    static_cast<const Components::SphereColliderComponent&>(collider);
                center = sphere.WorldCenter();
                radius = sphere.EffectiveRadius();
                return radius > 0.0f;
            }
            case Components::ColliderShape::Capsule:
            {
                const auto& capsule =
                    static_cast<const Components::CapsuleColliderComponent&>(collider);
                XMFLOAT3 segment_start{};
                XMFLOAT3 segment_end{};
                capsule.WorldSegment(segment_start, segment_end);
                const float half_length = 0.5f * std::sqrt(
                    (segment_end.x - segment_start.x) * (segment_end.x - segment_start.x) +
                    (segment_end.y - segment_start.y) * (segment_end.y - segment_start.y) +
                    (segment_end.z - segment_start.z) * (segment_end.z - segment_start.z));
                center = XMFLOAT3{
                    (segment_start.x + segment_end.x) * 0.5f,
                    (segment_start.y + segment_end.y) * 0.5f,
                    (segment_start.z + segment_end.z) * 0.5f };
                radius = capsule.EffectiveRadius() + half_length;
                return radius > 0.0f;
            }
            case Components::ColliderShape::Box:
            {
                const auto& box = static_cast<const Components::BoxColliderComponent&>(collider);
                const XMFLOAT3 half = box.WorldHalfExtents();
                center = box.WorldCenter();
                radius = std::sqrt(half.x * half.x + half.y * half.y + half.z * half.z);
                return radius > 0.0f;
            }
            case Components::ColliderShape::Mesh:
            case Components::ColliderShape::Landscape:
            {
                XMFLOAT3 minimum{};
                XMFLOAT3 maximum{};
                if (!collider.ComputeWorldBounds(minimum, maximum)) return false;
                center = XMFLOAT3{
                    (minimum.x + maximum.x) * 0.5f,
                    (minimum.y + maximum.y) * 0.5f,
                    (minimum.z + maximum.z) * 0.5f };
                radius = 0.5f * std::sqrt(
                    (maximum.x - minimum.x) * (maximum.x - minimum.x) +
                    (maximum.y - minimum.y) * (maximum.y - minimum.y) +
                    (maximum.z - minimum.z) * (maximum.z - minimum.z));
                return radius > 0.0f;
            }
            }
            return false;
        }

        // 球 vs 回転した直方体。中に入っている場合も重なりとして返す。
        bool SphereOverlapsBox(const Components::BoxColliderComponent& box,
            const XMFLOAT3& sphere_center, float sphere_radius)
        {
            const XMFLOAT3 half = box.WorldHalfExtents();
            if (half.x <= 0.0f || half.y <= 0.0f || half.z <= 0.0f) return false;

            // 球の中心を箱のローカル空間へ移す。
            XMFLOAT3 local = sphere_center;
            const XMFLOAT3 center = box.WorldCenter();
            local.x -= center.x;
            local.y -= center.y;
            local.z -= center.z;

            if (const Core::GameObject* owner = box.Owner())
            {
                const XMFLOAT3 euler = owner->GetTransform().LocalRotationEuler();
                const XMMATRIX rotation =
                    XMMatrixRotationRollPitchYaw(euler.x, euler.y, euler.z);
                // 回転行列の逆行列は転置。
                XMStoreFloat3(&local, XMVector3TransformNormal(
                    XMLoadFloat3(&local), XMMatrixTranspose(rotation)));
            }

            // 箱の中で最も近い点までの距離で判定する。
            // 中に入っていれば各軸の差が 0 になり、距離 0 で必ず重なりになる。
            const float dx = (std::max)(0.0f, std::fabs(local.x) - half.x);
            const float dy = (std::max)(0.0f, std::fabs(local.y) - half.y);
            const float dz = (std::max)(0.0f, std::fabs(local.z) - half.z);
            return (dx * dx + dy * dy + dz * dz) <= sphere_radius * sphere_radius;
        }
    }

    bool SceneCollisionWorld::Overlaps(const Components::ColliderComponent& trigger,
        const Components::ColliderComponent& other) const
    {
        XMFLOAT3 probe_center{ 0.0f, 0.0f, 0.0f };
        float probe_radius = 0.0f;
        if (!BuildProbeSphere(other, probe_center, probe_radius)) return false;

        // 【重要】Trigger の形ごとに判定を変える。
        //
        //   以前は「移動量 0 のスイープを Cook 三角形へ掛ける」方式にしていたが、
        //   これは表面に触れたときしか当たらない。
        //   Trigger の内側へ完全に入ってしまうと、どの三角形とも交わらず
        //   「中に居るのに反応しない」ことになる。Trigger としては致命的なので、
        //   Box は内包を含む距離判定へ、Sphere / Capsule は解析解へ切り替えた。
        switch (trigger.Shape())
        {
        case Components::ColliderShape::Sphere:
        {
            const auto& sphere =
                static_cast<const Components::SphereColliderComponent&>(trigger);
            const XMFLOAT3 center = sphere.WorldCenter();
            const float combined = sphere.EffectiveRadius() + probe_radius;
            const float dx = probe_center.x - center.x;
            const float dy = probe_center.y - center.y;
            const float dz = probe_center.z - center.z;
            return (dx * dx + dy * dy + dz * dz) <= combined * combined;
        }
        case Components::ColliderShape::Capsule:
        {
            const auto& capsule =
                static_cast<const Components::CapsuleColliderComponent&>(trigger);
            XMFLOAT3 segment_start{};
            XMFLOAT3 segment_end{};
            capsule.WorldSegment(segment_start, segment_end);

            // 移動量 0 のスイープ。解析解は「開始時点で重なっている」を必ず拾う。
            Physics::SphereCastHit hit{};
            return Physics::SweepSphereAgainstCapsule(probe_center, probe_center,
                probe_radius, segment_start, segment_end, capsule.EffectiveRadius(), hit);
        }
        case Components::ColliderShape::Box:
        {
            const auto& box = static_cast<const Components::BoxColliderComponent&>(trigger);
            return SphereOverlapsBox(box, probe_center, probe_radius);
        }
        case Components::ColliderShape::Mesh:
        case Components::ColliderShape::Landscape:
        {
            // 【制限】Mesh Trigger は内外判定を行わない。
            //   閉じたメッシュとは限らないため、内側かどうかを正しく決められない。
            //   ここでは「World Bounds の内側にある」ことをもって重なりとみなす。
            //   凹んだ形の Trigger では、実際には外側の場所でも反応しうる。
            //   正確さが要る場所には Box / Sphere / Capsule の Trigger を使うこと。
            XMFLOAT3 minimum{};
            XMFLOAT3 maximum{};
            if (!trigger.ComputeWorldBounds(minimum, maximum)) return false;

            const XMFLOAT3 expanded_min{
                minimum.x - probe_radius, minimum.y - probe_radius, minimum.z - probe_radius };
            const XMFLOAT3 expanded_max{
                maximum.x + probe_radius, maximum.y + probe_radius, maximum.z + probe_radius };
            return probe_center.x >= expanded_min.x && probe_center.x <= expanded_max.x &&
                   probe_center.y >= expanded_min.y && probe_center.y <= expanded_max.y &&
                   probe_center.z >= expanded_min.z && probe_center.z <= expanded_max.z;
        }
        }
        return false;
    }

    void SceneCollisionWorld::DispatchTriggerEvents()
    {
        if (scene_ == nullptr) return;
        if (trigger_collider_count_ == 0 && pairs_.empty()) return;

        ++trigger_frame_;

        // ---- 現フレームの接触を調べる ----------------------------------------
        for (const Registration& trigger_entry : entries_)
        {
            if (!trigger_entry.active || !trigger_entry.trigger) continue;
            if (!trigger_entry.bounds_valid) continue;

            const Components::ColliderComponent* trigger = Resolve(trigger_entry);
            if (trigger == nullptr || !trigger->ActiveInHierarchy()) continue;

            for (const Registration& other_entry : entries_)
            {
                if (!other_entry.active || other_entry.trigger) continue;
                if (!other_entry.bounds_valid) continue;

                // 同じ GameObject 同士は無視する。
                // 自分の当たり判定が自分の Trigger を叩き続けるのは無意味なため。
                if (other_entry.object == trigger_entry.object) continue;

                if (!Layers::Interact(trigger_entry.layer, trigger_entry.mask,
                    other_entry.layer, other_entry.mask))
                {
                    continue;
                }

                if (!Physics::BoundsOverlap(trigger_entry.bounds_min, trigger_entry.bounds_max,
                    other_entry.bounds_min, other_entry.bounds_max))
                {
                    continue;
                }

                const Components::ColliderComponent* other = Resolve(other_entry);
                if (other == nullptr || !other->ActiveInHierarchy()) continue;

                if (!Overlaps(*trigger, *other)) continue;

                // 既知のペアなら Stay、初見なら Enter。
                Pair* existing = nullptr;
                for (Pair& pair : pairs_)
                {
                    if (pair.trigger_collider == trigger_entry.collider &&
                        pair.other_collider == other_entry.collider)
                    {
                        existing = &pair;
                        break;
                    }
                }

                Core::TriggerContact contact;
                contact.trigger_object = trigger_entry.object;
                contact.trigger_collider = trigger_entry.collider;
                contact.other_object = other_entry.object;
                contact.other_collider = other_entry.collider;

                if (existing != nullptr)
                {
                    existing->last_seen = trigger_frame_;
                    DispatchToPair(contact, ContactPhase::Stay);
                }
                else
                {
                    Pair pair;
                    pair.trigger_object = trigger_entry.object;
                    pair.trigger_collider = trigger_entry.collider;
                    pair.other_object = other_entry.object;
                    pair.other_collider = other_entry.collider;
                    pair.last_seen = trigger_frame_;
                    pairs_.push_back(pair);
                    DispatchToPair(contact, ContactPhase::Enter);
                }
            }
        }

        // ---- 今フレーム見えなかったペアは Exit -------------------------------
        //
        // GameObject や Collider が削除された場合もここで片付く。
        // 配送先は ObjectID から引き直すので、既に消えていれば何も起きない。
        for (std::size_t index = 0; index < pairs_.size();)
        {
            if (pairs_[index].last_seen == trigger_frame_)
            {
                ++index;
                continue;
            }

            Core::TriggerContact contact;
            contact.trigger_object = pairs_[index].trigger_object;
            contact.trigger_collider = pairs_[index].trigger_collider;
            contact.other_object = pairs_[index].other_object;
            contact.other_collider = pairs_[index].other_collider;

            // 先にペアを外してから配送する。
            // 配送先が Destroy を呼んでも、既に外れているので二重 Exit にならない。
            pairs_.erase(pairs_.begin() + static_cast<std::ptrdiff_t>(index));
            DispatchToPair(contact, ContactPhase::Exit);
        }
    }

    void SceneCollisionWorld::DispatchToPair(const Core::TriggerContact& contact,
        ContactPhase phase) const
    {
        DispatchToObject(contact.trigger_object, contact, phase);
        DispatchToObject(contact.other_object, contact, phase);
    }

    void SceneCollisionWorld::DispatchToObject(Core::ObjectID target,
        const Core::TriggerContact& contact, ContactPhase phase) const
    {
        if (scene_ == nullptr || !target.Valid()) return;

        Core::GameObject* object = scene_->FindGameObjectByID(target);
        if (object == nullptr || !object->ActiveInHierarchy()) return;

        PublishTriggerEvent(*scene_, target, contact, static_cast<int>(phase));

        // 添字で回す。配送中に Component が増えても添字は壊れない
        // （削除は予約のみで、実体は同期点まで残る）。
        for (std::size_t index = 0; index < object->ComponentCount(); ++index)
        {
            Core::Component* component = object->ComponentAt(index);
            if (component == nullptr || component->PendingDestroy()) continue;
            if (!component->ActiveInHierarchy()) continue;

            switch (phase)
            {
            case ContactPhase::Enter: component->OnTriggerEnter(contact); break;
            case ContactPhase::Stay:  component->OnTriggerStay(contact);  break;
            case ContactPhase::Exit:  component->OnTriggerExit(contact);  break;
            }
        }
    }
}
