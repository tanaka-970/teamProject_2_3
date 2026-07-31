#include "SceneCollisionWorld.h"

#include "../Runtime/Scene.h"
#include "../../Components/Physics/BoxColliderComponent.h"
#include "../../Components/Physics/CapsuleColliderComponent.h"
#include "../../Components/Physics/MeshColliderComponent.h"
#include "../../Components/Physics/SphereColliderComponent.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Physics/CollisionLayers.h"
#include "../../Physics/ShapeSweep.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace ReplayEngine::Scene
{
    namespace Layers = Physics::CollisionLayers;

    const char* ToString(CollisionBackend backend) noexcept
    {
        switch (backend)
        {
        case CollisionBackend::SceneCollider: return "SceneCollider";
        case CollisionBackend::LegacyStage:   return "LegacyStage";
        case CollisionBackend::None:          break;
        }
        return "None";
    }

    const char* SceneCollisionWorld::ToString(BackendMode mode) noexcept
    {
        switch (mode)
        {
        case BackendMode::LegacyOnly:         return "Legacy Only";
        case BackendMode::Hybrid:             return "Hybrid";
        case BackendMode::SceneCollidersOnly: return "Scene Colliders Only";
        }
        return "Hybrid";
    }

    SceneCollisionWorld::BackendMode SceneCollisionWorld::BackendModeFromInt(int value) noexcept
    {
        switch (value)
        {
        case 0: return BackendMode::LegacyOnly;
        case 2: return BackendMode::SceneCollidersOnly;
        case 1:
        default: break;
        }
        // 未知の値は Hybrid へ倒す。壊れた Scene ファイルでも安全側に寄せる。
        return BackendMode::Hybrid;
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

        scene_ = scene;
    }

    bool SceneCollisionWorld::ShouldConsultLegacy() const noexcept
    {
        if (legacy_ == nullptr) return false;

        switch (mode_)
        {
        case BackendMode::LegacyOnly:
            return true;

        case BackendMode::Hybrid:
            // この移行元が MeshCollider へ移行済みなら問い合わせない。
            // ここで弾くことで、同じ地形が新旧両方から Hit として返らない。
            //
            // 単一 bool ではなく移行元 ID ごとに見ているので、
            // 別の配置物がまだ未移行なら、そちらは引き続き Legacy が担当する。
            return !legacy_migration_.IsMigrated(legacy_source_);

        case BackendMode::SceneCollidersOnly:
            return false;
        }
        return false;
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
                auto* collider = dynamic_cast<Components::ColliderComponent*>(
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
        legacy_consulted_ = false;
        active_collider_count_ = 0;
        blocking_collider_count_ = 0;
        trigger_collider_count_ = 0;
        mesh_collider_count_ = 0;

        if (scene_ == nullptr) return;

        // 構成が変わったフレームだけ全走査する。
        ReconcileRegistrations();

        // Legacy Only の間は Scene 側の Cook を走らせない。
        // 使わないデータの読み込みでフレームを落とさないため。
        const bool use_scene_colliders = mode_ != BackendMode::LegacyOnly;

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
                if (use_scene_colliders && cook_cache_ != nullptr && entry.active)
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

            entry.bounds_valid = entry.active &&
                collider->ComputeWorldBounds(entry.bounds_min, entry.bounds_max);

            if (!entry.active) continue;
            ++active_collider_count_;
            if (entry.trigger) ++trigger_collider_count_;
            else if (entry.bounds_valid) ++blocking_collider_count_;
        }
    }

    // -----------------------------------------------------------------------
    // スイープ
    // -----------------------------------------------------------------------

    bool SceneCollisionWorld::SweepLocalTriangles(const XMFLOAT4X4& world,
        const XMFLOAT4X4& inverse_world, bool negative_scale, float local_radius_scale,
        const Physics::Triangle* triangles, std::size_t triangle_count,
        const XMFLOAT3& start, const XMFLOAT3& end, float radius,
        Physics::SphereCastHit& hit) const
    {
        if (triangles == nullptr || triangle_count == 0) return false;

        // ---- クエリをこの Collider のローカル空間へ移す ----
        const XMMATRIX inverse = XMLoadFloat4x4(&inverse_world);
        XMFLOAT3 local_start{};
        XMFLOAT3 local_end{};
        XMStoreFloat3(&local_start, XMVector3TransformCoord(XMLoadFloat3(&start), inverse));
        XMStoreFloat3(&local_end, XMVector3TransformCoord(XMLoadFloat3(&end), inverse));

        // 半径もローカルへ。非一様拡縮では真球にならないため、
        // 最も縮む軸で割って安全側（大きめ）に取る。
        Physics::SphereCastQuery query{};
        query.start = local_start;
        query.end = local_end;
        query.radius = radius * local_radius_scale;
        // 法線の向き判定はワールドで行うので、ローカルでは絞らない。
        query.minimum_normal_y = -1.0f;
        query.maximum_normal_y = 1.0f;

        Physics::SphereCastHit local_hit{};
        if (!Physics::CastSphereAgainstTriangles(query, triangles, triangle_count, local_hit))
        {
            return false;
        }

        // ---- 結果をワールドへ戻す ----
        const XMMATRIX world_matrix = XMLoadFloat4x4(&world);
        XMFLOAT3 world_center{};
        XMStoreFloat3(&world_center,
            XMVector3TransformCoord(XMLoadFloat3(&local_hit.center), world_matrix));
        XMFLOAT3 world_position{};
        XMStoreFloat3(&world_position,
            XMVector3TransformCoord(XMLoadFloat3(&local_hit.position), world_matrix));

        // 法線は逆行列の転置で変換する。位置と同じ行列では正しくならない。
        const XMMATRIX normal_matrix = XMMatrixTranspose(inverse);
        XMVECTOR world_normal = XMVector3TransformNormal(
            XMLoadFloat3(&local_hit.normal), normal_matrix);
        world_normal = XMVector3Normalize(world_normal);

        // 負の拡縮では面の裏表が反転するので法線を戻す。
        if (negative_scale) world_normal = XMVectorNegate(world_normal);

        hit = local_hit;
        hit.center = world_center;
        hit.position = world_position;
        XMStoreFloat3(&hit.normal, world_normal);
        return true;
    }

    bool SceneCollisionWorld::SweepSingleCollider(const Components::ColliderComponent& collider,
        const XMFLOAT3& start, const XMFLOAT3& end, float radius,
        Physics::SphereCastHit& hit) const
    {
        switch (collider.Shape())
        {
        case Components::ColliderShape::Mesh:
        {
            const auto& mesh = static_cast<const Components::MeshColliderComponent&>(collider);
            if (!mesh.ReadyForQuery()) return false;

            const auto& cooked = mesh.Cooked();
            const XMMATRIX inverse = XMLoadFloat4x4(&mesh.InverseWorldMatrix());
            XMFLOAT3 local_start{};
            XMFLOAT3 local_end{};
            XMStoreFloat3(&local_start, XMVector3TransformCoord(XMLoadFloat3(&start), inverse));
            XMStoreFloat3(&local_end, XMVector3TransformCoord(XMLoadFloat3(&end), inverse));

            const float local_radius = radius * mesh.LocalRadiusScale();
            const XMFLOAT3 local_min{
                std::min(local_start.x, local_end.x) - local_radius,
                std::min(local_start.y, local_end.y) - local_radius,
                std::min(local_start.z, local_end.z) - local_radius };
            const XMFLOAT3 local_max{
                std::max(local_start.x, local_end.x) + local_radius,
                std::max(local_start.y, local_end.y) + local_radius,
                std::max(local_start.z, local_end.z) + local_radius };

            cooked->CollectTriangles(local_min, local_max, scratch_indices_);
            if (scratch_indices_.empty()) return false;

            // 絞り込んだ三角形だけを詰め直して判定へ渡す。
            scratch_triangles_.clear();
            scratch_triangles_.reserve(scratch_indices_.size());
            const Physics::Triangle* source = cooked->Triangles();
            for (const std::uint32_t triangle_index : scratch_indices_)
            {
                scratch_triangles_.push_back(source[triangle_index]);
            }

            return SweepLocalTriangles(mesh.WorldMatrix(), mesh.InverseWorldMatrix(),
                mesh.NegativeScale(), mesh.LocalRadiusScale(),
                scratch_triangles_.data(), scratch_triangles_.size(),
                start, end, radius, hit);
        }

        case Components::ColliderShape::Box:
        {
            const auto& box = static_cast<const Components::BoxColliderComponent&>(collider);
            const XMFLOAT3 half = box.WorldHalfExtents();
            if (half.x <= 0.0f || half.y <= 0.0f || half.z <= 0.0f) return false;

            // 箱は 12 枚の三角形へ展開して、Mesh と同じ経路で解く。
            // 回転した箱を軸並行として扱わないので、見た目と当たりがずれない。
            Physics::Triangle triangles[Physics::box_triangle_count]{};
            Physics::BuildBoxTriangles(half, triangles);

            // 拡縮は半辺長へ既に反映済みなので、ここでは位置と回転だけの行列を作る。
            const Core::GameObject* owner = box.Owner();
            const XMFLOAT3 center = box.WorldCenter();
            XMMATRIX world = XMMatrixTranslation(center.x, center.y, center.z);
            if (owner != nullptr)
            {
                const XMFLOAT3 euler = owner->GetTransform().LocalRotationEuler();
                world = XMMatrixRotationRollPitchYaw(euler.x, euler.y, euler.z) * world;
            }

            XMFLOAT4X4 world_matrix{};
            XMFLOAT4X4 inverse_matrix{};
            XMStoreFloat4x4(&world_matrix, world);
            XMVECTOR determinant{};
            XMStoreFloat4x4(&inverse_matrix, XMMatrixInverse(&determinant, world));

            // 回転と平行移動しか入っていないので、半径は等倍のままでよい。
            return SweepLocalTriangles(world_matrix, inverse_matrix, false, 1.0f,
                triangles, Physics::box_triangle_count, start, end, radius, hit);
        }

        case Components::ColliderShape::Capsule:
        {
            const auto& capsule =
                static_cast<const Components::CapsuleColliderComponent&>(collider);
            XMFLOAT3 segment_start{};
            XMFLOAT3 segment_end{};
            capsule.WorldSegment(segment_start, segment_end);
            return Physics::SweepSphereAgainstCapsule(start, end, radius,
                segment_start, segment_end, capsule.EffectiveRadius(), hit);
        }

        case Components::ColliderShape::Sphere:
        {
            const auto& sphere =
                static_cast<const Components::SphereColliderComponent&>(collider);
            const XMFLOAT3 center = sphere.WorldCenter();
            // 線分の長さ 0 のカプセル = 球。同じ解析解を使い回す。
            return Physics::SweepSphereAgainstCapsule(start, end, radius,
                center, center, sphere.EffectiveRadius(), hit);
        }
        }
        return false;
    }

    bool SceneCollisionWorld::SweepSceneColliders(const XMFLOAT3& start, const XMFLOAT3& end,
        float radius, float minimum_normal_y, float maximum_normal_y,
        int layer, int mask, SphereSweepHit& hit) const
    {
        hit = SphereSweepHit{};
        if (scene_ == nullptr || mode_ == BackendMode::LegacyOnly) return false;

        // クエリのワールド AABB。Broad Phase の粗い絞り込みに使う。
        const XMFLOAT3 query_min{
            std::min(start.x, end.x) - radius,
            std::min(start.y, end.y) - radius,
            std::min(start.z, end.z) - radius };
        const XMFLOAT3 query_max{
            std::max(start.x, end.x) + radius,
            std::max(start.y, end.y) + radius,
            std::max(start.z, end.z) + radius };

        bool found = false;
        float best_fraction = 2.0f;

        // 登録表だけを走査する。Scene 全体は見ない。
        for (const Registration& entry : entries_)
        {
            if (!entry.active || !entry.bounds_valid) continue;

            // Trigger は押し戻しへ使わない。通り抜ける。
            if (entry.trigger) continue;

            // Layer / Mask。双方向で一致したときだけ衝突とみなす。
            if (!Layers::Interact(layer, mask, entry.layer, entry.mask)) continue;

            // ワールド AABB で早期に外す。ここまでは実体を引き直さない。
            if (!Physics::BoundsOverlap(query_min, query_max,
                entry.bounds_min, entry.bounds_max))
            {
                continue;
            }

            // ここで初めて実体を引き直す。生ポインタは保持していない。
            const Components::ColliderComponent* collider = Resolve(entry);
            if (collider == nullptr || !collider->BlocksMovement()) continue;

            Physics::SphereCastHit shape_hit{};
            if (!SweepSingleCollider(*collider, start, end, radius, shape_hit)) continue;

            // 面の向きでの絞り込みはワールド空間で行う。
            // 床と壁の区別は「ワールドでの上向き成分」で決まるため。
            if (shape_hit.normal.y < minimum_normal_y ||
                shape_hit.normal.y > maximum_normal_y)
            {
                continue;
            }

            if (shape_hit.fraction >= best_fraction) continue;

            best_fraction = shape_hit.fraction;
            hit.center = shape_hit.center;
            hit.normal = shape_hit.normal;
            hit.fraction = shape_hit.fraction;
            hit.source.backend = CollisionBackend::SceneCollider;
            hit.source.object = entry.object;
            hit.source.collider = entry.collider;
            hit.valid = true;
            found = true;
        }
        return found;
    }

    // -----------------------------------------------------------------------
    // IPhysicsQueryService
    // -----------------------------------------------------------------------

    bool SceneCollisionWorld::CollisionAvailable() const
    {
        if (mode_ != BackendMode::LegacyOnly && blocking_collider_count_ > 0) return true;
        if (ShouldConsultLegacy() && legacy_->CollisionAvailable()) return true;
        return false;
    }

    bool SceneCollisionWorld::SweepSphere(const XMFLOAT3& start, const XMFLOAT3& end,
        float radius, float maximum_normal_y, SphereSweepHit& hit) const
    {
        return SweepSphereFiltered(start, end, radius, maximum_normal_y,
            Layers::Default, Layers::all_layers_mask, hit);
    }

    bool SceneCollisionWorld::SweepSphereFiltered(const XMFLOAT3& start, const XMFLOAT3& end,
        float radius, float maximum_normal_y, int layer, int mask, SphereSweepHit& hit) const
    {
        hit = SphereSweepHit{};

        SphereSweepHit scene_hit{};
        const bool scene_found = SweepSceneColliders(start, end, radius,
            -1.0f, maximum_normal_y, layer, mask, scene_hit);

        SphereSweepHit legacy_hit{};
        bool legacy_found = false;
        if (ShouldConsultLegacy())
        {
            legacy_consulted_ = true;
            legacy_found = legacy_->SweepSphere(start, end, radius, maximum_normal_y, legacy_hit);
            if (legacy_found) legacy_hit.source.backend = CollisionBackend::LegacyStage;
        }

        // 両方から返った場合は近い方を採る。
        // 同じ地形が両方から返ることはない（移行済みなら Legacy を見ないため）。
        if (scene_found && legacy_found)
        {
            hit = scene_hit.fraction <= legacy_hit.fraction ? scene_hit : legacy_hit;
        }
        else if (scene_found) { hit = scene_hit; }
        else if (legacy_found) { hit = legacy_hit; }
        else { return false; }

        last_sweep_source_ = hit.source;
        return true;
    }

    bool SceneCollisionWorld::QueryGround(const XMFLOAT3& origin, float radius,
        float up_offset, float down_distance, float walkable_normal_y, GroundHit& hit) const
    {
        return QueryGroundFiltered(origin, radius, up_offset, down_distance,
            walkable_normal_y, Layers::Default, Layers::all_layers_mask, hit);
    }

    bool SceneCollisionWorld::QueryGroundFiltered(const XMFLOAT3& origin, float radius,
        float up_offset, float down_distance, float walkable_normal_y,
        int layer, int mask, GroundHit& hit) const
    {
        hit = GroundHit{};

        // 上へ持ち上げてから真下へ球を落とす。旧経路と同じ考え方。
        const XMFLOAT3 start{ origin.x, origin.y + up_offset, origin.z };
        const XMFLOAT3 end{ origin.x, origin.y + up_offset - down_distance, origin.z };

        SphereSweepHit scene_hit{};
        const bool scene_found = SweepSceneColliders(start, end, radius,
            walkable_normal_y, 1.0f, layer, mask, scene_hit);

        GroundHit legacy_hit{};
        bool legacy_found = false;
        if (ShouldConsultLegacy())
        {
            legacy_consulted_ = true;
            legacy_found = legacy_->QueryGround(origin, radius, up_offset,
                down_distance, walkable_normal_y, legacy_hit);
            if (legacy_found) legacy_hit.source.backend = CollisionBackend::LegacyStage;
        }

        const auto take_scene = [&]()
        {
            // 球中心から半径ぶん下が接地点。
            hit.position = XMFLOAT3{
                scene_hit.center.x,
                scene_hit.center.y - radius,
                scene_hit.center.z };
            hit.normal = scene_hit.normal;
            hit.source = scene_hit.source;
            hit.valid = true;
        };

        if (scene_found && legacy_found)
        {
            // 高い方（=先に足が着く方）を採る。
            const float scene_height = scene_hit.center.y - radius;
            if (scene_height >= legacy_hit.position.y) take_scene();
            else hit = legacy_hit;
        }
        else if (scene_found) { take_scene(); }
        else if (legacy_found) { hit = legacy_hit; }
        else { return false; }

        last_ground_source_ = hit.source;
        return true;
    }

    // -----------------------------------------------------------------------
    // Trigger
    // -----------------------------------------------------------------------

    bool SceneCollisionWorld::Overlaps(const Components::ColliderComponent& trigger,
        const Components::ColliderComponent& other) const
    {
        // 相手を「動かない球（または線分）」とみなし、
        // 移動量 0 のスイープを掛けて「開始時点で重なっているか」を見る。
        //
        // Trigger 側が Mesh / Box の場合はローカル空間の三角形へ落ちるので、
        // 相手の形は球で近似する。Trigger の入り口判定としては十分で、
        // 「Trigger の中に入ったのに反応しない」ことは起きない
        // （近似は必ず本来より大きい側へ倒してある）。
        XMFLOAT3 probe_center{ 0.0f, 0.0f, 0.0f };
        float probe_radius = 0.0f;

        switch (other.Shape())
        {
        case Components::ColliderShape::Sphere:
        {
            const auto& sphere = static_cast<const Components::SphereColliderComponent&>(other);
            probe_center = sphere.WorldCenter();
            probe_radius = sphere.EffectiveRadius();
            break;
        }
        case Components::ColliderShape::Capsule:
        {
            const auto& capsule =
                static_cast<const Components::CapsuleColliderComponent&>(other);
            XMFLOAT3 segment_start{};
            XMFLOAT3 segment_end{};
            capsule.WorldSegment(segment_start, segment_end);

            // Trigger 側も解析形状なら、近似せずカプセル同士として解く。
            if (trigger.Shape() == Components::ColliderShape::Sphere)
            {
                const auto& sphere =
                    static_cast<const Components::SphereColliderComponent&>(trigger);
                const XMFLOAT3 center = sphere.WorldCenter();
                Physics::SphereCastHit hit{};
                return Physics::SweepSphereAgainstCapsule(center, center,
                    sphere.EffectiveRadius(), segment_start, segment_end,
                    capsule.EffectiveRadius(), hit);
            }

            // Mesh / Box / Capsule の Trigger には、線分の中点を球として当てる。
            probe_center = XMFLOAT3{
                (segment_start.x + segment_end.x) * 0.5f,
                (segment_start.y + segment_end.y) * 0.5f,
                (segment_start.z + segment_end.z) * 0.5f };
            probe_radius = capsule.EffectiveRadius() +
                0.5f * std::sqrt(
                    (segment_end.x - segment_start.x) * (segment_end.x - segment_start.x) +
                    (segment_end.y - segment_start.y) * (segment_end.y - segment_start.y) +
                    (segment_end.z - segment_start.z) * (segment_end.z - segment_start.z));
            break;
        }
        case Components::ColliderShape::Box:
        {
            const auto& box = static_cast<const Components::BoxColliderComponent&>(other);
            const XMFLOAT3 half = box.WorldHalfExtents();
            probe_center = box.WorldCenter();
            // 外接球で近似する。取りこぼすより多めに拾う方を選ぶ。
            probe_radius = std::sqrt(half.x * half.x + half.y * half.y + half.z * half.z);
            break;
        }
        case Components::ColliderShape::Mesh:
        {
            XMFLOAT3 minimum{};
            XMFLOAT3 maximum{};
            if (!other.ComputeWorldBounds(minimum, maximum)) return false;
            probe_center = XMFLOAT3{
                (minimum.x + maximum.x) * 0.5f,
                (minimum.y + maximum.y) * 0.5f,
                (minimum.z + maximum.z) * 0.5f };
            probe_radius = 0.5f * std::sqrt(
                (maximum.x - minimum.x) * (maximum.x - minimum.x) +
                (maximum.y - minimum.y) * (maximum.y - minimum.y) +
                (maximum.z - minimum.z) * (maximum.z - minimum.z));
            break;
        }
        }

        if (probe_radius <= 0.0f) return false;

        Physics::SphereCastHit hit{};
        // 移動量 0 のスイープ。当たったなら開始時点で重なっている。
        return SweepSingleCollider(trigger, probe_center, probe_center, probe_radius, hit);
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
