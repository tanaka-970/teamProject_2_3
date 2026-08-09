// SceneCollisionWorld のうち「接続」と「登録表の管理」だけを持つ。
//
// 実装を 3 つのファイルへ分けている:
//   SceneCollisionWorld.cpp         … 接続・登録表・毎フレームの更新（このファイル）
//   SceneCollisionWorldQuery.cpp    … スイープと IPhysicsQueryService の実装
//   SceneCollisionWorldTrigger.cpp  … Trigger の重なり判定とイベント配送
//
// 1 ファイルへ全部入れると 700 行を超え、
// 「登録表を直したいだけなのに衝突計算の中を読む」ことになるため分けた。

#include "SceneCollisionWorld.h"

#include "../Runtime/Scene.h"
#include "../../Components/Physics/MeshColliderComponent.h"
#include "../../Components/Landscape/LandscapeColliderComponent.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Physics/CollisionLayers.h"

#include <algorithm>

namespace ReplayEngine::Scene
{
    namespace Layers = Physics::CollisionLayers;

    const char* ToString(CollisionBackend backend) noexcept
    {
        switch (backend)
        {
        case CollisionBackend::SceneCollider: return "SceneCollider";
        case CollisionBackend::None:          break;
        }
        return "None";
    }

    // -----------------------------------------------------------------------
    // 接続
    // -----------------------------------------------------------------------

    void SceneCollisionWorld::AttachScene(Scene* scene)
    {
        if (scene_ == scene) return;

        // Scene が変わったら、古い Scene の ObjectID / ColliderID を 1 件も残さない。
        // ここを怠ると、Play 終了後に実行用 Scene の ID を編集 Scene へ
        // 問い合わせてしまい、まったく別の GameObject へ当たることになる。
        entries_.clear();
        pairs_.clear();
        trigger_frame_ = 0;
        has_generation_ = false;
        last_generation_ = 0;
        active_collider_count_ = 0;
        blocking_collider_count_ = 0;
        trigger_collider_count_ = 0;
        mesh_collider_count_ = 0;
        last_ground_source_ = CollisionSourceInfo{};
        last_sweep_source_ = CollisionSourceInfo{};
        last_ray_source_ = CollisionSourceInfo{};

        scene_ = scene;
    }

    // -----------------------------------------------------------------------
    // 登録表
    // -----------------------------------------------------------------------

    Components::ColliderComponent* SceneCollisionWorld::Resolve(const Registration& entry) const
    {
        if (scene_ == nullptr) return nullptr;

        Core::GameObject* object = scene_->FindGameObjectByID(entry.object);
        if (object == nullptr || object->PendingDestroy()) return nullptr;

        return Components::FindColliderByID(*object, entry.collider);
    }

    void SceneCollisionWorld::ReconcileRegistrations()
    {
        if (scene_ == nullptr)
        {
            entries_.clear();
            return;
        }

        const std::uint32_t generation = scene_->StructureGeneration();
        if (has_generation_ && generation == last_generation_) return;

        last_generation_ = generation;
        has_generation_ = true;
        ++rescan_count_;

        // 構成が変わったフレームだけここへ来る。毎フレームではない。
        entries_.clear();

        for (std::size_t index = 0; index < scene_->GameObjectCount(); ++index)
        {
            Core::GameObject* object = scene_->GameObjectAt(index);
            if (object == nullptr || object->PendingDestroy()) continue;

            // 同じ GameObject の中で collider_key が重複していたら振り直す。
            // 壊れた Scene ファイルを読んだ場合の救済で、通常は起きない。
            int highest_key = 0;
            for (std::size_t slot = 0; slot < object->ComponentCount(); ++slot)
            {
                const auto* collider = dynamic_cast<const Components::ColliderComponent*>(
                    object->ComponentAt(slot));
                if (collider == nullptr || collider->PendingDestroy()) continue;
                highest_key = std::max(highest_key, collider->collider_key);
            }

            for (std::size_t slot = 0; slot < object->ComponentCount(); ++slot)
            {
                auto* collider = dynamic_cast<Components::ColliderComponent*>(
                    object->ComponentAt(slot));
                if (collider == nullptr || collider->PendingDestroy()) continue;

                if (collider->collider_key <= 0 ||
                    Components::FindColliderByKey(*object, collider->collider_key) != collider)
                {
                    collider->collider_key = ++highest_key;
                }

                Registration entry;
                entry.object = object->ID();
                entry.collider = collider->GetColliderID();
                entry.shape = collider->Shape();
                entry.layer = Layers::ClampLayer(collider->collision_layer);
                entry.mask = collider->collision_mask;
                entry.trigger = collider->is_trigger;
                entry.active = collider->ActiveInHierarchy();
                entries_.push_back(entry);
            }
        }

        // 登録表から消えた Collider の接触ペアを片付ける。
        // ここで捨てないと「消えた Trigger の Exit が二度と来ない」ことになる。
        pairs_.erase(std::remove_if(pairs_.begin(), pairs_.end(),
            [this](const Pair& pair)
            {
                const auto known = [this](ColliderID id)
                {
                    return std::any_of(entries_.begin(), entries_.end(),
                        [id](const Registration& entry) { return entry.collider == id; });
                };
                return !known(pair.trigger_collider) || !known(pair.other_collider);
            }), pairs_.end());
    }

    void SceneCollisionWorld::Refresh()
    {
        active_collider_count_ = 0;
        blocking_collider_count_ = 0;
        trigger_collider_count_ = 0;
        mesh_collider_count_ = 0;

        if (scene_ == nullptr) return;

        // 構成が変わったフレームだけ全走査する。
        ReconcileRegistrations();

        for (Registration& entry : entries_)
        {
            Components::ColliderComponent* collider = Resolve(entry);
            if (collider == nullptr)
            {
                // 実体が消えている。次の構成変更で登録表から外れる。
                entry.active = false;
                entry.bounds_valid = false;
                continue;
            }

            entry.layer = Layers::ClampLayer(collider->collision_layer);
            entry.mask = collider->collision_mask;
            entry.trigger = collider->is_trigger;
            entry.active = collider->ActiveInHierarchy();

            if (entry.shape == Components::ColliderShape::Mesh)
            {
                auto* mesh = static_cast<Components::MeshColliderComponent*>(collider);
                if (cook_cache_ != nullptr && entry.active)
                {
                    // Cook が走るのは「Asset か Cook 設定が変わったとき」だけ。
                    // Transform が変わっただけでは走らない。
                    const std::string guid = mesh->ResolveMeshAssetGuid();
                    const std::string revision = (revision_provider_ && !guid.empty())
                        ? revision_provider_(guid) : std::string();
                    mesh->EnsureCooked(*cook_cache_, loader_, revision);
                }
                // Transform が変わっていたときだけ World / Inverse / Bounds を作り直す。
                mesh->RefreshTransformIfChanged();
                if (mesh->ReadyForQuery()) ++mesh_collider_count_;
            }
            else if (entry.shape == Components::ColliderShape::Landscape)
            {
                auto* landscape = static_cast<Components::LandscapeColliderComponent*>(collider);
                // Asset共有cacheは介さず、LandscapeComponent の revision が変わった時だけ
                // local triangle + spatial cook を更新する。Transform 更新は同じ入口で扱う。
                landscape->RefreshGeometryIfChanged();
                if (landscape->ReadyForQuery()) ++mesh_collider_count_;
            }

            entry.bounds_valid = entry.active &&
                collider->ComputeWorldBounds(entry.bounds_min, entry.bounds_max);

            if (!entry.active) continue;
            ++active_collider_count_;
            if (entry.trigger) ++trigger_collider_count_;
            else if (entry.bounds_valid) ++blocking_collider_count_;
        }
    }
}
