// SceneCollisionWorld のうち「スイープと問い合わせ」だけを持つ。
//
// 形状ごとの分岐は SweepSingleCollider の 1 か所だけ。
// 新しい形状を足すときはそこへ case を 1 つ増やす。

#include "SceneCollisionWorld.h"

#include "../Runtime/Scene.h"
#include "../../Components/Physics/BoxColliderComponent.h"
#include "../../Components/Physics/CapsuleColliderComponent.h"
#include "../../Components/Physics/MeshColliderComponent.h"
#include "../../Components/Physics/SphereColliderComponent.h"
#include "../../Components/Landscape/LandscapeColliderComponent.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Physics/CollisionLayers.h"
#include "../../Physics/ShapeSweep.h"

#include <DirectXCollision.h>

#include <algorithm>
#include <cmath>
#include <unordered_set>

using namespace DirectX;

namespace ReplayEngine::Scene
{
    namespace Layers = Physics::CollisionLayers;

    namespace
    {
        XMFLOAT3 Add(const XMFLOAT3& a, const XMFLOAT3& b) noexcept
        {
            return { a.x + b.x, a.y + b.y, a.z + b.z };
        }

        XMFLOAT3 Scale(const XMFLOAT3& value, float factor) noexcept
        {
            return { value.x * factor, value.y * factor, value.z * factor };
        }

        XMFLOAT3 Normalize(const XMFLOAT3& value) noexcept
        {
            const float length = std::sqrt(value.x * value.x + value.y * value.y +
                value.z * value.z);
            return length > 1.0e-6f
                ? XMFLOAT3{ value.x / length, value.y / length, value.z / length }
                : XMFLOAT3{};
        }

        XMFLOAT4 NormalizeRotation(const XMFLOAT4& value) noexcept
        {
            XMFLOAT4 result{};
            XMVECTOR quaternion = XMLoadFloat4(&value);
            if (XMVectorGetX(XMVector4LengthSq(quaternion)) <= 1.0e-8f)
                quaternion = XMQuaternionIdentity();
            else quaternion = XMQuaternionNormalize(quaternion);
            XMStoreFloat4(&result, quaternion);
            return result;
        }

        PhysicsQueryHit MakeQueryHit(const SceneCollisionWorld::Registration& entry,
            const XMFLOAT3& point, const XMFLOAT3& normal, float distance,
            float fraction) noexcept
        {
            PhysicsQueryHit hit;
            hit.point = point;
            hit.normal = normal;
            hit.distance = distance;
            hit.fraction = fraction;
            hit.source.backend = CollisionBackend::SceneCollider;
            hit.source.object = entry.object;
            hit.source.collider = entry.collider;
            hit.valid = true;
            return hit;
        }
    }

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

        Physics::SphereCastQuery query{};
        query.start = local_start;
        query.end = local_end;
        // 半径もローカルへ。非一様拡縮では真球にならないため、
        // 最も縮む軸で割って安全側（大きめ）に取る。
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
        case Components::ColliderShape::Landscape:
        {
            const auto& landscape =
                static_cast<const Components::LandscapeColliderComponent&>(collider);
            if (!landscape.ReadyForQuery() || landscape.Cooked() == nullptr) return false;

            // Landscape も任意 topology になったので、8k/数万 triangle を毎 query
            // 総当たりしない。MeshCollider と同じ local-space XZ grid で候補を絞る。
            const auto& cooked = landscape.Cooked();
            const XMMATRIX inverse = XMLoadFloat4x4(&landscape.InverseWorldMatrix());
            XMFLOAT3 local_start{};
            XMFLOAT3 local_end{};
            XMStoreFloat3(&local_start, XMVector3TransformCoord(XMLoadFloat3(&start), inverse));
            XMStoreFloat3(&local_end, XMVector3TransformCoord(XMLoadFloat3(&end), inverse));

            const float local_radius = radius * landscape.LocalRadiusScale();
            const XMFLOAT3 local_min{
                (std::min)(local_start.x, local_end.x) - local_radius,
                (std::min)(local_start.y, local_end.y) - local_radius,
                (std::min)(local_start.z, local_end.z) - local_radius };
            const XMFLOAT3 local_max{
                (std::max)(local_start.x, local_end.x) + local_radius,
                (std::max)(local_start.y, local_end.y) + local_radius,
                (std::max)(local_start.z, local_end.z) + local_radius };

            cooked->CollectTriangles(local_min, local_max, scratch_indices_);
            if (scratch_indices_.empty()) return false;
            scratch_triangles_.clear();
            scratch_triangles_.reserve(scratch_indices_.size());
            const Physics::Triangle* source = cooked->Triangles();
            for (const std::uint32_t triangle_index : scratch_indices_)
            {
                scratch_triangles_.push_back(source[triangle_index]);
            }

            return SweepLocalTriangles(landscape.WorldMatrix(), landscape.InverseWorldMatrix(),
                landscape.NegativeScale(), landscape.LocalRadiusScale(),
                scratch_triangles_.data(), scratch_triangles_.size(),
                start, end, radius, hit);
        }

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
                (std::min)(local_start.x, local_end.x) - local_radius,
                (std::min)(local_start.y, local_end.y) - local_radius,
                (std::min)(local_start.z, local_end.z) - local_radius };
            const XMFLOAT3 local_max{
                (std::max)(local_start.x, local_end.x) + local_radius,
                (std::max)(local_start.y, local_end.y) + local_radius,
                (std::max)(local_start.z, local_end.z) + local_radius };

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
                const XMFLOAT4 quaternion = owner->GetTransform().WorldRotationQuaternion();
                world = XMMatrixRotationQuaternion(XMLoadFloat4(&quaternion)) * world;
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
        const CollisionQueryFilter& filter, SphereSweepHit& hit) const
    {
        hit = SphereSweepHit{};
        if (scene_ == nullptr) return false;

        // クエリのワールド AABB。Broad Phase の粗い絞り込みに使う。
        const XMFLOAT3 query_min{
            (std::min)(start.x, end.x) - radius,
            (std::min)(start.y, end.y) - radius,
            (std::min)(start.z, end.z) - radius };
        const XMFLOAT3 query_max{
            (std::max)(start.x, end.x) + radius,
            (std::max)(start.y, end.y) + radius,
            (std::max)(start.z, end.z) + radius };

        bool found = false;
        float best_fraction = 2.0f;

        // 登録表だけを走査する。Scene 全体は見ない。
        for (const Registration& entry : entries_)
        {
            if (!entry.active || !entry.bounds_valid) continue;

            // Trigger は押し戻しへ使わない。通り抜ける。
            if (entry.trigger) continue;

            // 自分自身の Collider は無視する。
            // これを外すと、Motor が自分の Collider に当たったと判断して
            // 毎フレーム宙へ持ち上がる（実際に起きた不具合）。
            if (filter.ignore_object.Valid() && entry.object == filter.ignore_object) continue;

            // Layer / Mask。双方向で一致したときだけ衝突とみなす。
            if (!Layers::Interact(filter.layer, filter.mask, entry.layer, entry.mask)) continue;

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
            hit.position = shape_hit.position;
            hit.normal = shape_hit.normal;
            hit.fraction = shape_hit.fraction;
            hit.source.backend = CollisionBackend::SceneCollider;
            hit.source.object = entry.object;
            hit.source.collider = entry.collider;
            hit.started_overlapping = shape_hit.started_overlapping;
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
        return blocking_collider_count_ > 0;
    }

    bool SceneCollisionWorld::SweepSphere(const XMFLOAT3& start, const XMFLOAT3& end,
        float radius, float maximum_normal_y, SphereSweepHit& hit) const
    {
        CollisionQueryFilter filter;
        filter.layer = Layers::Default;
        filter.mask = Layers::all_layers_mask;
        return SweepSphereFiltered(start, end, radius, maximum_normal_y, filter, hit);
    }

    bool SceneCollisionWorld::SweepSphereFiltered(const XMFLOAT3& start, const XMFLOAT3& end,
        float radius, float maximum_normal_y, const CollisionQueryFilter& filter,
        SphereSweepHit& hit) const
    {
        hit = SphereSweepHit{};

        SphereSweepHit scene_hit{};
        const bool scene_found = SweepSceneColliders(start, end, radius,
            -1.0f, maximum_normal_y, filter, scene_hit);

        if (!scene_found) return false;
        hit = scene_hit;

        last_sweep_source_ = hit.source;
        return true;
    }

    bool SceneCollisionWorld::Raycast(const XMFLOAT3& origin,
        const XMFLOAT3& direction, float max_distance, RaycastHit& hit) const
    {
        CollisionQueryFilter filter;
        filter.layer = Layers::Default;
        filter.mask = Layers::all_layers_mask;
        return RaycastFiltered(origin, direction, max_distance, filter, hit);
    }

    bool SceneCollisionWorld::RaycastFiltered(const XMFLOAT3& origin,
        const XMFLOAT3& direction, float max_distance,
        const CollisionQueryFilter& filter, RaycastHit& hit) const
    {
        hit = RaycastHit{};
        if (scene_ == nullptr || max_distance <= 0.0f) return false;

        const float length = std::sqrt(direction.x * direction.x +
            direction.y * direction.y + direction.z * direction.z);
        if (length <= 1.0e-6f) return false;

        const XMFLOAT3 normalized{
            direction.x / length, direction.y / length, direction.z / length };
        const XMFLOAT3 end{
            origin.x + normalized.x * max_distance,
            origin.y + normalized.y * max_distance,
            origin.z + normalized.z * max_distance };

        // radius=0 の SphereSweep は線分 Ray と同値。
        // 形状ごとの判定を Raycast 用に二重実装せず、既存の
        // Mesh/Box/Capsule/Sphere の正確な経路をそのまま使う。
        SphereSweepHit sweep{};
        if (!SweepSceneColliders(origin, end, 0.0f, -1.0f, 1.0f, filter, sweep))
            return false;

        hit.point = sweep.center;
        hit.normal = sweep.normal;
        hit.distance = (std::max)(0.0f, (std::min)(1.0f, sweep.fraction)) * max_distance;
        hit.source = sweep.source;
        hit.valid = true;
        last_ray_source_ = hit.source;
        return true;
    }

    bool SceneCollisionWorld::RaycastAllFiltered(const XMFLOAT3& origin,
        const XMFLOAT3& direction, float max_distance,
        const CollisionQueryFilter& filter, std::vector<PhysicsQueryHit>& hits) const
    {
        hits.clear();
        if (scene_ == nullptr || max_distance <= 0.0f) return false;
        const XMFLOAT3 normalized = Normalize(direction);
        if (normalized.x == 0.0f && normalized.y == 0.0f && normalized.z == 0.0f)
            return false;
        const XMFLOAT3 end = Add(origin, Scale(normalized, max_distance));

        for (const Registration& entry : entries_)
        {
            if (!entry.active || !entry.bounds_valid || entry.trigger) continue;
            if (filter.ignore_object.Valid() && entry.object == filter.ignore_object) continue;
            if (!Layers::Interact(filter.layer, filter.mask, entry.layer, entry.mask)) continue;
            const Components::ColliderComponent* collider = Resolve(entry);
            if (collider == nullptr || !collider->BlocksMovement()) continue;

            Physics::SphereCastHit shape_hit{};
            if (!SweepSingleCollider(*collider, origin, end, 0.0f, shape_hit)) continue;
            const float fraction = (std::max)(0.0f, (std::min)(1.0f, shape_hit.fraction));
            hits.push_back(MakeQueryHit(entry, shape_hit.position, shape_hit.normal,
                fraction * max_distance, fraction));
        }
        std::sort(hits.begin(), hits.end(), [](const PhysicsQueryHit& a,
            const PhysicsQueryHit& b) { return a.distance < b.distance; });
        return !hits.empty();
    }

    bool SceneCollisionWorld::SphereCastAll(const XMFLOAT3& origin,
        const XMFLOAT3& direction, float radius, float max_distance,
        const CollisionQueryFilter& filter, std::vector<PhysicsQueryHit>& hits) const
    {
        hits.clear();
        if (scene_ == nullptr || radius < 0.0f || max_distance < 0.0f) return false;
        const XMFLOAT3 normalized = Normalize(direction);
        if (max_distance > 0.0f && normalized.x == 0.0f &&
            normalized.y == 0.0f && normalized.z == 0.0f) return false;
        const XMFLOAT3 end = Add(origin, Scale(normalized, max_distance));

        for (const Registration& entry : entries_)
        {
            if (!entry.active || !entry.bounds_valid || entry.trigger) continue;
            if (filter.ignore_object.Valid() && entry.object == filter.ignore_object) continue;
            if (!Layers::Interact(filter.layer, filter.mask, entry.layer, entry.mask)) continue;
            const Components::ColliderComponent* collider = Resolve(entry);
            if (collider == nullptr || !collider->BlocksMovement()) continue;

            Physics::SphereCastHit shape_hit{};
            if (!SweepSingleCollider(*collider, origin, end, radius, shape_hit)) continue;
            const float fraction = (std::max)(0.0f, (std::min)(1.0f, shape_hit.fraction));
            const XMFLOAT3 point = Add(shape_hit.center, Scale(shape_hit.normal, -radius));
            hits.push_back(MakeQueryHit(entry, point, shape_hit.normal,
                fraction * max_distance, fraction));
        }
        std::sort(hits.begin(), hits.end(), [](const PhysicsQueryHit& a,
            const PhysicsQueryHit& b) { return a.distance < b.distance; });
        return !hits.empty();
    }

    bool SceneCollisionWorld::OverlapSphere(const XMFLOAT3& center, float radius,
        const CollisionQueryFilter& filter, std::vector<PhysicsQueryHit>& hits) const
    {
        hits.clear();
        if (scene_ == nullptr || radius < 0.0f) return false;
        for (const Registration& entry : entries_)
        {
            if (!entry.active || !entry.bounds_valid) continue;
            if (filter.ignore_object.Valid() && entry.object == filter.ignore_object) continue;
            if (!Layers::Interact(filter.layer, filter.mask, entry.layer, entry.mask)) continue;
            const Components::ColliderComponent* collider = Resolve(entry);
            if (collider == nullptr) continue;
            Physics::SphereCastHit shape_hit{};
            if (!SweepSingleCollider(*collider, center, center, radius, shape_hit)) continue;
            hits.push_back(MakeQueryHit(entry, shape_hit.position, shape_hit.normal, 0.0f, 0.0f));
        }
        return !hits.empty();
    }

    bool SceneCollisionWorld::OverlapCapsule(const XMFLOAT3& point_a,
        const XMFLOAT3& point_b, float radius, const CollisionQueryFilter& filter,
        std::vector<PhysicsQueryHit>& hits) const
    {
        hits.clear();
        if (scene_ == nullptr || radius < 0.0f) return false;
        for (const Registration& entry : entries_)
        {
            if (!entry.active || !entry.bounds_valid) continue;
            if (filter.ignore_object.Valid() && entry.object == filter.ignore_object) continue;
            if (!Layers::Interact(filter.layer, filter.mask, entry.layer, entry.mask)) continue;
            const Components::ColliderComponent* collider = Resolve(entry);
            if (collider == nullptr) continue;
            Physics::SphereCastHit shape_hit{};
            // Capsule は線分上を移動する球の体積と同値。
            if (!SweepSingleCollider(*collider, point_a, point_b, radius, shape_hit)) continue;
            hits.push_back(MakeQueryHit(entry, shape_hit.position, shape_hit.normal, 0.0f, 0.0f));
        }
        return !hits.empty();
    }

    bool SceneCollisionWorld::OverlapBox(const XMFLOAT3& center,
        const XMFLOAT3& half_extents, const XMFLOAT4& rotation,
        const CollisionQueryFilter& filter, std::vector<PhysicsQueryHit>& hits) const
    {
        hits.clear();
        if (scene_ == nullptr || half_extents.x <= 0.0f || half_extents.y <= 0.0f ||
            half_extents.z <= 0.0f) return false;

        BoundingOrientedBox query(center, half_extents, NormalizeRotation(rotation));
        BoundingBox query_bounds{};
        XMFLOAT3 query_corners[BoundingOrientedBox::CORNER_COUNT]{};
        query.GetCorners(query_corners);
        BoundingBox::CreateFromPoints(query_bounds, BoundingOrientedBox::CORNER_COUNT,
            query_corners, sizeof(XMFLOAT3));

        for (const Registration& entry : entries_)
        {
            if (!entry.active || !entry.bounds_valid) continue;
            if (filter.ignore_object.Valid() && entry.object == filter.ignore_object) continue;
            if (!Layers::Interact(filter.layer, filter.mask, entry.layer, entry.mask)) continue;
            BoundingBox target_bounds{};
            BoundingBox::CreateFromPoints(target_bounds, XMLoadFloat3(&entry.bounds_min),
                XMLoadFloat3(&entry.bounds_max));
            if (!query_bounds.Intersects(target_bounds)) continue;

            const Components::ColliderComponent* collider = Resolve(entry);
            if (collider == nullptr) continue;
            bool overlap = false;
            if (collider->Shape() == Components::ColliderShape::Sphere)
            {
                const auto& sphere = static_cast<const Components::SphereColliderComponent&>(*collider);
                overlap = query.Intersects(BoundingSphere(sphere.WorldCenter(),
                    sphere.EffectiveRadius()));
            }
            else if (collider->Shape() == Components::ColliderShape::Box)
            {
                const auto& box = static_cast<const Components::BoxColliderComponent&>(*collider);
                XMFLOAT4 orientation{ 0.0f, 0.0f, 0.0f, 1.0f };
                if (box.Owner() != nullptr)
                    orientation = box.Owner()->GetTransform().WorldRotationQuaternion();
                overlap = query.Intersects(BoundingOrientedBox(box.WorldCenter(),
                    box.WorldHalfExtents(), NormalizeRotation(orientation)));
            }
            else
            {
                // Capsule / Mesh / Landscape は正確な world bounds を持つ。
                // OBB 側との交差は bounds を使い、false negative を出さない。
                overlap = query.Intersects(target_bounds);
            }
            if (!overlap) continue;

            const XMFLOAT3 target_center{ (entry.bounds_min.x + entry.bounds_max.x) * 0.5f,
                (entry.bounds_min.y + entry.bounds_max.y) * 0.5f,
                (entry.bounds_min.z + entry.bounds_max.z) * 0.5f };
            const XMFLOAT3 normal = Normalize(XMFLOAT3{ target_center.x - center.x,
                target_center.y - center.y, target_center.z - center.z });
            hits.push_back(MakeQueryHit(entry, target_center, normal, 0.0f, 0.0f));
        }
        return !hits.empty();
    }

    bool SceneCollisionWorld::BoxCastAll(const XMFLOAT3& center,
        const XMFLOAT3& half_extents, const XMFLOAT4& rotation,
        const XMFLOAT3& direction, float max_distance,
        const CollisionQueryFilter& filter, std::vector<PhysicsQueryHit>& hits) const
    {
        hits.clear();
        if (max_distance < 0.0f) return false;
        const XMFLOAT3 normalized = Normalize(direction);
        if (max_distance > 0.0f && normalized.x == 0.0f &&
            normalized.y == 0.0f && normalized.z == 0.0f) return false;
        const float feature = (std::max)(0.02f, (std::min)(half_extents.x,
            (std::min)(half_extents.y, half_extents.z)) * 0.5f);
        const int steps = std::clamp(
            static_cast<int>(std::ceil(max_distance / feature)), 1, 512);
        std::unordered_set<ColliderID> seen;
        for (int step = 0; step <= steps; ++step)
        {
            const float fraction = static_cast<float>(step) / static_cast<float>(steps);
            const float distance = max_distance * fraction;
            std::vector<PhysicsQueryHit> overlaps;
            OverlapBox(Add(center, Scale(normalized, distance)), half_extents,
                rotation, filter, overlaps);
            for (PhysicsQueryHit hit : overlaps)
            {
                if (!seen.insert(hit.source.collider).second) continue;
                hit.distance = distance;
                hit.fraction = fraction;
                hits.push_back(hit);
            }
        }
        std::sort(hits.begin(), hits.end(), [](const PhysicsQueryHit& a,
            const PhysicsQueryHit& b) { return a.distance < b.distance; });
        return !hits.empty();
    }

    bool SceneCollisionWorld::CapsuleCastAll(const XMFLOAT3& point_a,
        const XMFLOAT3& point_b, float radius, const XMFLOAT3& direction,
        float max_distance, const CollisionQueryFilter& filter,
        std::vector<PhysicsQueryHit>& hits) const
    {
        hits.clear();
        if (radius < 0.0f || max_distance < 0.0f) return false;
        const XMFLOAT3 normalized = Normalize(direction);
        if (max_distance > 0.0f && normalized.x == 0.0f &&
            normalized.y == 0.0f && normalized.z == 0.0f) return false;
        const float feature = (std::max)(0.02f, radius * 0.5f);
        const int steps = std::clamp(
            static_cast<int>(std::ceil(max_distance / feature)), 1, 512);
        std::unordered_set<ColliderID> seen;
        for (int step = 0; step <= steps; ++step)
        {
            const float fraction = static_cast<float>(step) / static_cast<float>(steps);
            const float distance = max_distance * fraction;
            const XMFLOAT3 offset = Scale(normalized, distance);
            std::vector<PhysicsQueryHit> overlaps;
            OverlapCapsule(Add(point_a, offset), Add(point_b, offset), radius,
                filter, overlaps);
            for (PhysicsQueryHit hit : overlaps)
            {
                if (!seen.insert(hit.source.collider).second) continue;
                hit.distance = distance;
                hit.fraction = fraction;
                hits.push_back(hit);
            }
        }
        std::sort(hits.begin(), hits.end(), [](const PhysicsQueryHit& a,
            const PhysicsQueryHit& b) { return a.distance < b.distance; });
        return !hits.empty();
    }

    bool SceneCollisionWorld::QueryGround(const XMFLOAT3& origin, float radius,
        float up_offset, float down_distance, float walkable_normal_y, GroundHit& hit) const
    {
        CollisionQueryFilter filter;
        filter.layer = Layers::Default;
        filter.mask = Layers::all_layers_mask;
        return QueryGroundFiltered(origin, radius, up_offset, down_distance,
            walkable_normal_y, filter, hit);
    }

    bool SceneCollisionWorld::QueryGroundFiltered(const XMFLOAT3& origin, float radius,
        float up_offset, float down_distance, float walkable_normal_y,
        const CollisionQueryFilter& filter, GroundHit& hit) const
    {
        hit = GroundHit{};

        // 上へ持ち上げてから真下へ球を落とす。旧経路と同じ考え方。
        const XMFLOAT3 start{ origin.x, origin.y + up_offset, origin.z };
        const XMFLOAT3 end{ origin.x, origin.y + up_offset - down_distance, origin.z };

        SphereSweepHit scene_hit{};
        const bool scene_found = SweepSceneColliders(start, end, radius,
            walkable_normal_y, 1.0f, filter, scene_hit);

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

        if (!scene_found) return false;
        take_scene();

        last_ground_source_ = hit.source;
        return true;
    }
}
