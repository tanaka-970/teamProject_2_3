#include "raycast.h"

#include "stage.h"
#include "skinned_mesh.h"
#include "../../RePlayEngine/Physics/SphereCast.h"

#include <cmath>
#include <limits>
#include <algorithm>
#include <vector>

namespace
{
    bool IntersectTriangle(const DirectX::XMVECTOR& origin,
                           const DirectX::XMVECTOR& direction,
                           const DirectX::XMVECTOR& v0,
                           const DirectX::XMVECTOR& v1,
                           const DirectX::XMVECTOR& v2,
                           float max_distance,
                           float min_normal_y,
                           GameRaycast::RaycastHit& out_hit)
    {
        using namespace DirectX;

        // Moller-Trumbore法でレイと三角形の交点を直接求める。
        const XMVECTOR e1 = XMVectorSubtract(v1, v0);
        const XMVECTOR e2 = XMVectorSubtract(v2, v0);
        const XMVECTOR p = XMVector3Cross(direction, e2);
        const float det = XMVectorGetX(XMVector3Dot(e1, p));
        if (std::fabs(det) < 1.0e-6f)
        {
            return false;
        }

        const float inv_det = 1.0f / det;
        const XMVECTOR tvec = XMVectorSubtract(origin, v0);
        const float u = XMVectorGetX(XMVector3Dot(tvec, p)) * inv_det;
        if (u < 0.0f || u > 1.0f)
        {
            return false;
        }

        const XMVECTOR q = XMVector3Cross(tvec, e1);
        const float v = XMVectorGetX(XMVector3Dot(direction, q)) * inv_det;
        if (v < 0.0f || (u + v) > 1.0f)
        {
            return false;
        }

        const float distance = XMVectorGetX(XMVector3Dot(e2, q)) * inv_det;
        if (distance < 0.0f || distance > max_distance)
        {
            return false;
        }

        // 面の頂点順に関係なく、法線をレイの進行方向と逆向きにそろえる。
        XMVECTOR normal = XMVector3Normalize(XMVector3Cross(e1, e2));
        if (XMVectorGetX(XMVector3Dot(normal, direction)) > 0.0f)
        {
            normal = XMVectorNegate(normal);
        }

        XMFLOAT3 normal_f{};
        XMStoreFloat3(&normal_f, normal);
        if (normal_f.y < min_normal_y)
        {
            return false;
        }

        out_hit.distance = distance;
        XMStoreFloat3(&out_hit.position, XMVectorAdd(origin, XMVectorScale(direction, distance)));
        out_hit.normal = normal_f;
        return true;
    }
}

namespace GameRaycast
{
    bool RaycastStage(const Stage& stage,
                      const DirectX::XMFLOAT3& origin,
                      const DirectX::XMFLOAT3& direction,
                      float max_distance,
                      RaycastHit& hit,
                      float min_normal_y)
    {
        using namespace DirectX;

        const auto& collision_mesh = stage.GetCollisionMesh();
        if (!collision_mesh.Enabled() || !collision_mesh.Valid() || max_distance <= 0.0f)
        {
            return false;
        }

        XMVECTOR ray_origin = XMLoadFloat3(&origin);
        XMVECTOR ray_dir = XMLoadFloat3(&direction);
        const float dir_len = XMVectorGetX(XMVector3Length(ray_dir));
        if (dir_len <= 1.0e-6f)
        {
            return false;
        }
        ray_dir = XMVectorScale(ray_dir, 1.0f / dir_len);

        bool found = false;
        RaycastHit closest{};
        closest.distance = (std::numeric_limits<float>::max)();
        XMFLOAT3 end{};
        XMStoreFloat3(&end, ray_origin + ray_dir * max_distance);
        const XMFLOAT3 bounds_min{
            (std::min)(origin.x, end.x), (std::min)(origin.y, end.y), (std::min)(origin.z, end.z) };
        const XMFLOAT3 bounds_max{
            (std::max)(origin.x, end.x), (std::max)(origin.y, end.y), (std::max)(origin.z, end.z) };
        // レイ区間のAABBで空間分割を絞り、全三角形との総当たりを避ける。
        std::vector<std::uint32_t> candidates;
        collision_mesh.CollectTriangles(bounds_min, bounds_max, candidates);
        for (std::uint32_t triangle_index : candidates)
        {
            const auto& triangle = collision_mesh.TriangleAt(triangle_index);
            RaycastHit candidate{};
            if (IntersectTriangle(ray_origin, ray_dir,
                XMLoadFloat3(&triangle.vertices[0]), XMLoadFloat3(&triangle.vertices[1]),
                XMLoadFloat3(&triangle.vertices[2]), max_distance, min_normal_y, candidate) &&
                candidate.distance < closest.distance)
            {
                closest = candidate;
                found = true;
            }
        }

        if (found)
        {
            hit = closest;
        }
        return found;
    }

    bool RaycastStageDown(const Stage& stage,
                          const DirectX::XMFLOAT3& position,
                          float up_offset,
                          float down_distance,
                          RaycastHit& hit,
                          float min_normal_y)
    {
        DirectX::XMFLOAT3 origin{ position.x, position.y + up_offset, position.z };
        return RaycastStage(stage, origin, { 0.0f, -1.0f, 0.0f }, down_distance, hit, min_normal_y);
    }

    bool SphereCastStage(const Stage& stage,
                         const DirectX::XMFLOAT3& start,
                         const DirectX::XMFLOAT3& end,
                         float radius,
                         SphereCastHit& hit,
                         float minimum_normal_y,
                         float maximum_normal_y)
    {
        using namespace DirectX;
        using ReplayEngine::Physics::SphereCastQuery;
        using ReplayEngine::Physics::Triangle;

        const auto& collision_mesh = stage.GetCollisionMesh();
        if (!collision_mesh.Enabled() || !collision_mesh.Valid() || radius <= 0.0f) return false;

        SphereCastQuery query{};
        query.start = start;
        query.end = end;
        query.radius = radius;
        query.minimum_normal_y = minimum_normal_y;
        query.maximum_normal_y = maximum_normal_y;

        bool found = false;
        ReplayEngine::Physics::SphereCastHit closest{};
        closest.distance = (std::numeric_limits<float>::max)();
        const XMFLOAT3 bounds_min{
            (std::min)(start.x, end.x) - radius,
            (std::min)(start.y, end.y) - radius,
            (std::min)(start.z, end.z) - radius };
        const XMFLOAT3 bounds_max{
            (std::max)(start.x, end.x) + radius,
            (std::max)(start.y, end.y) + radius,
            (std::max)(start.z, end.z) + radius };
        // 半径分だけ広げた掃引AABBを使い、接触し得る三角形だけを検査する。
        std::vector<std::uint32_t> candidates;
        collision_mesh.CollectTriangles(bounds_min, bounds_max, candidates);
        for (std::uint32_t triangle_index : candidates)
        {
            ReplayEngine::Physics::SphereCastHit candidate{};
            if (ReplayEngine::Physics::CastSphereAgainstTriangle(query,
                collision_mesh.TriangleAt(triangle_index), candidate) &&
                candidate.distance < closest.distance)
            {
                candidate.triangle_index = triangle_index;
                closest = candidate;
                found = true;
            }
        }

        if (!found) return false;
        hit.position = closest.position;
        hit.normal = closest.normal;
        hit.center = closest.center;
        hit.distance = closest.distance;
        hit.fraction = closest.fraction;
        hit.started_overlapping = closest.started_overlapping;
        return true;
    }

    bool SphereCastStageDown(const Stage& stage,
                             const DirectX::XMFLOAT3& position,
                             float radius,
                             float up_offset,
                             float down_distance,
                             SphereCastHit& hit,
                             float minimum_normal_y)
    {
        const DirectX::XMFLOAT3 start{
            position.x, position.y + up_offset + radius, position.z };
        const DirectX::XMFLOAT3 end{
            position.x, position.y - down_distance + radius, position.z };
        return SphereCastStage(stage, start, end, radius, hit,
            minimum_normal_y, 1.0f);
    }
}
