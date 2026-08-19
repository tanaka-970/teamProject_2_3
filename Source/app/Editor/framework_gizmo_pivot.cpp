#include "framework.h"

#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Components/Core/PivotComponent.h"
#include "../../RePlayEngine/Components/Physics/MeshColliderComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeColliderComponent.h"
#include "../../RePlayEngine/Physics/CookedMeshCollision.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    DirectX::XMFLOAT3 TransformPoint(const DirectX::XMFLOAT3& point, DirectX::FXMMATRIX matrix)
    {
        DirectX::XMFLOAT3 result;
        DirectX::XMStoreFloat3(&result, DirectX::XMVector3TransformCoord(
            DirectX::XMLoadFloat3(&point), matrix));
        return result;
    }

    float DistanceSquared(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) noexcept
    {
        const float x = a.x - b.x;
        const float y = a.y - b.y;
        const float z = a.z - b.z;
        return x * x + y * y + z * z;
    }

    DirectX::XMFLOAT3 ClosestPointOnSegment(const DirectX::XMFLOAT3& point,
        const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) noexcept
    {
        const DirectX::XMVECTOR p = DirectX::XMLoadFloat3(&point);
        const DirectX::XMVECTOR av = DirectX::XMLoadFloat3(&a);
        const DirectX::XMVECTOR ab = DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&b), av);
        const float denominator = DirectX::XMVectorGetX(DirectX::XMVector3Dot(ab, ab));
        float amount = denominator > 0.0000001f
            ? DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMVectorSubtract(p, av), ab)) / denominator
            : 0.0f;
        amount = (std::max)(0.0f, (std::min)(amount, 1.0f));
        DirectX::XMFLOAT3 result;
        DirectX::XMStoreFloat3(&result, DirectX::XMVectorMultiplyAdd(
            DirectX::XMVectorReplicate(amount), ab, av));
        return result;
    }

    // Ericson の領域判定と同じ形で、三角形上の最短点を返す。
    // Surface Snap が「面の平面」ではなく実際の三角形内へ吸着するために必要。
    DirectX::XMFLOAT3 ClosestPointOnTriangle(const DirectX::XMFLOAT3& point,
        const ReplayEngine::Physics::Triangle& triangle) noexcept
    {
        using namespace DirectX;
        const XMVECTOR p = XMLoadFloat3(&point);
        const XMVECTOR a = XMLoadFloat3(&triangle.vertices[0]);
        const XMVECTOR b = XMLoadFloat3(&triangle.vertices[1]);
        const XMVECTOR c = XMLoadFloat3(&triangle.vertices[2]);
        const XMVECTOR ab = XMVectorSubtract(b, a);
        const XMVECTOR ac = XMVectorSubtract(c, a);
        const XMVECTOR ap = XMVectorSubtract(p, a);
        const float d1 = XMVectorGetX(XMVector3Dot(ab, ap));
        const float d2 = XMVectorGetX(XMVector3Dot(ac, ap));
        if (d1 <= 0.0f && d2 <= 0.0f) return triangle.vertices[0];

        const XMVECTOR bp = XMVectorSubtract(p, b);
        const float d3 = XMVectorGetX(XMVector3Dot(ab, bp));
        const float d4 = XMVectorGetX(XMVector3Dot(ac, bp));
        if (d3 >= 0.0f && d4 <= d3) return triangle.vertices[1];

        const float vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
        {
            const float v = d1 / (d1 - d3);
            XMFLOAT3 result;
            XMStoreFloat3(&result, XMVectorMultiplyAdd(XMVectorReplicate(v), ab, a));
            return result;
        }

        const XMVECTOR cp = XMVectorSubtract(p, c);
        const float d5 = XMVectorGetX(XMVector3Dot(ab, cp));
        const float d6 = XMVectorGetX(XMVector3Dot(ac, cp));
        if (d6 >= 0.0f && d5 <= d6) return triangle.vertices[2];

        const float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
        {
            const float w = d2 / (d2 - d6);
            XMFLOAT3 result;
            XMStoreFloat3(&result, XMVectorMultiplyAdd(XMVectorReplicate(w), ac, a));
            return result;
        }

        const float va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
        {
            const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
            XMFLOAT3 result;
            XMStoreFloat3(&result, XMVectorMultiplyAdd(
                XMVectorReplicate(w), XMVectorSubtract(c, b), b));
            return result;
        }

        const float denominator = 1.0f / (va + vb + vc);
        const float v = vb * denominator;
        const float w = vc * denominator;
        XMFLOAT3 result;
        XMStoreFloat3(&result, XMVectorAdd(a, XMVectorAdd(
            XMVectorScale(ab, v), XMVectorScale(ac, w))));
        return result;
    }

    const ReplayEngine::Physics::CookedMeshCollisionData* ObjectCookedMesh(
        ReplayEngine::Core::GameObject& object) noexcept
    {
        if (auto* mesh = object.GetComponent<ReplayEngine::Components::MeshColliderComponent>())
        {
            if (mesh->Cooked() && mesh->Cooked()->Valid()) return mesh->Cooked().get();
        }
        if (auto* landscape = object.GetComponent<ReplayEngine::Components::LandscapeColliderComponent>())
        {
            if (landscape->Cooked() && landscape->Cooked()->Valid()) return landscape->Cooked().get();
        }
        return nullptr;
    }
}

DirectX::XMFLOAT3 framework::resolve_object_pivot_world(ReplayEngine::Core::GameObject& object,
    ReplayEngine::Scene::Scene& scene) const
{
    using ReplayEngine::Components::PivotComponent;
    const DirectX::XMFLOAT3 origin = object.GetTransform().WorldPosition();
    const PivotComponent* pivot = object.GetComponent<PivotComponent>();
    if (pivot == nullptr || pivot->ModeValue() == PivotComponent::Mode::SelfOrigin) return origin;

    if (pivot->ModeValue() == PivotComponent::Mode::WorldPoint) return pivot->local_point;
    if (pivot->ModeValue() == PivotComponent::Mode::TargetObject)
    {
        if (pivot->target.IsAssigned())
        {
            if (ReplayEngine::Core::GameObject* target = scene.FindGameObjectByID(pivot->target.object))
                return target->GetTransform().WorldPosition();
        }
        return origin;
    }

    DirectX::XMFLOAT3 local = pivot->local_point;
    if (pivot->ModeValue() == PivotComponent::Mode::BoundsCenter ||
        pivot->ModeValue() == PivotComponent::Mode::BoundsFace)
    {
        const ReplayEngine::Physics::CookedMeshCollisionData* cooked = ObjectCookedMesh(object);
        if (cooked == nullptr) return origin;
        const DirectX::XMFLOAT3 minimum = cooked->LocalBoundsMin();
        const DirectX::XMFLOAT3 maximum = cooked->LocalBoundsMax();
        local = { (minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f,
            (minimum.z + maximum.z) * 0.5f };
        if (pivot->ModeValue() == PivotComponent::Mode::BoundsFace)
        {
            const DirectX::XMFLOAT3 direction = pivot->local_point;
            const float ax = std::abs(direction.x);
            const float ay = std::abs(direction.y);
            const float az = std::abs(direction.z);
            if (ax >= ay && ax >= az) local.x = direction.x < 0.0f ? minimum.x : maximum.x;
            else if (ay >= az) local.y = direction.y < 0.0f ? minimum.y : maximum.y;
            else local.z = direction.z < 0.0f ? minimum.z : maximum.z;
        }
    }
    return TransformPoint(local, object.GetTransform().WorldMatrix());
}

bool framework::snap_primary_pivot_to_mesh(int mode)
{
    using ReplayEngine::Components::PivotComponent;
    ReplayEngine::Scene::Scene& scene = active_object_scene();
    ReplayEngine::Core::GameObject* object = object_editor_context.Selection().ResolvePrimary(scene);
    if (object == nullptr || !object_editor_context.CanEdit()) return false;
    PivotComponent* pivot = object->GetComponent<PivotComponent>();
    const ReplayEngine::Physics::CookedMeshCollisionData* cooked = ObjectCookedMesh(*object);
    if (pivot == nullptr || cooked == nullptr || cooked->TriangleCount() == 0) return false;

    DirectX::XMFLOAT3 query = pivot->local_point;
    if (pivot->ModeValue() == PivotComponent::Mode::WorldPoint)
    {
        DirectX::XMVECTOR determinant{};
        const DirectX::XMMATRIX inverse = DirectX::XMMatrixInverse(&determinant,
            object->GetTransform().WorldMatrix());
        query = TransformPoint(pivot->local_point, inverse);
    }
    else if (pivot->ModeValue() == PivotComponent::Mode::SelfOrigin ||
        pivot->ModeValue() == PivotComponent::Mode::BoundsCenter ||
        pivot->ModeValue() == PivotComponent::Mode::TargetObject)
    {
        const DirectX::XMFLOAT3 minimum = cooked->LocalBoundsMin();
        const DirectX::XMFLOAT3 maximum = cooked->LocalBoundsMax();
        query = { (minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f,
            (minimum.z + maximum.z) * 0.5f };
    }

    DirectX::XMFLOAT3 best{};
    float best_distance = (std::numeric_limits<float>::max)();
    for (std::size_t index = 0; index < cooked->TriangleCount(); ++index)
    {
        const ReplayEngine::Physics::Triangle& triangle = cooked->Triangles()[index];
        if (mode == 1)
        {
            for (const DirectX::XMFLOAT3& vertex : triangle.vertices)
            {
                const float distance = DistanceSquared(query, vertex);
                if (distance < best_distance) { best_distance = distance; best = vertex; }
            }
        }
        else if (mode == 2)
        {
            for (int edge = 0; edge < 3; ++edge)
            {
                const DirectX::XMFLOAT3 candidate = ClosestPointOnSegment(query,
                    triangle.vertices[edge], triangle.vertices[(edge + 1) % 3]);
                const float distance = DistanceSquared(query, candidate);
                if (distance < best_distance) { best_distance = distance; best = candidate; }
            }
        }
        else
        {
            const DirectX::XMFLOAT3 candidate = ClosestPointOnTriangle(query, triangle);
            const float distance = DistanceSquared(query, candidate);
            if (distance < best_distance) { best_distance = distance; best = candidate; }
        }
    }
    if (!std::isfinite(best_distance)) return false;

    object_editor_context.BeginEdit(u8"PivotをメッシュへSnap");
    if (pivot->ModeValue() == PivotComponent::Mode::WorldPoint)
        pivot->local_point = TransformPoint(best, object->GetTransform().WorldMatrix());
    else
    {
        pivot->SetMode(PivotComponent::Mode::CustomLocal);
        pivot->local_point = best;
    }
    object_editor_context.CommitEdit();
    object_editor_context.SetStatus(mode == 1 ? u8"Pivotを頂点へSnapしました" :
        mode == 2 ? u8"Pivotを辺へSnapしました" : u8"Pivotを面へSnapしました");
    return true;
}
