#include "ColliderDebugDraw.h"

#include "../../Components/Gameplay/CharacterMotorComponent.h"
#include "../../Components/Physics/BoxColliderComponent.h"
#include "../../Components/Physics/CapsuleColliderComponent.h"
#include "../../Components/Physics/MeshColliderComponent.h"
#include "../../Components/Physics/SphereColliderComponent.h"
#include "../../Components/Landscape/LandscapeColliderComponent.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Scene/Services/SceneCollisionWorld.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace ReplayEngine::Editor
{
    namespace
    {
        void AppendLine(const XMFLOAT3& start, const XMFLOAT3& end,
            std::uint32_t color, std::vector<DebugLine>& out)
        {
            DebugLine line;
            line.start = start;
            line.end = end;
            line.color = color;
            out.push_back(line);
        }

        // 1 つの平面上へ円を描く。axis_a / axis_b は互いに直交した単位ベクトル。
        void AppendCircle(const XMFLOAT3& center, const XMFLOAT3& axis_a,
            const XMFLOAT3& axis_b, float radius, int segments,
            std::uint32_t color, std::vector<DebugLine>& out)
        {
            const int steps = (std::max)(4, segments);
            XMFLOAT3 previous{};
            for (int index = 0; index <= steps; ++index)
            {
                const float angle = XM_2PI * static_cast<float>(index) /
                    static_cast<float>(steps);
                const float cosine = std::cos(angle) * radius;
                const float sine = std::sin(angle) * radius;

                const XMFLOAT3 point{
                    center.x + axis_a.x * cosine + axis_b.x * sine,
                    center.y + axis_a.y * cosine + axis_b.y * sine,
                    center.z + axis_a.z * cosine + axis_b.z * sine };

                if (index > 0) AppendLine(previous, point, color, out);
                previous = point;
            }
        }

        // 半円。カプセルの端の丸みに使う。
        //   from … 円弧の始点方向、to … 円弧の頂点方向。どちらも単位ベクトル。
        void AppendHalfCircle(const XMFLOAT3& center, const XMFLOAT3& from,
            const XMFLOAT3& to, float radius, int segments,
            std::uint32_t color, std::vector<DebugLine>& out)
        {
            const int steps = (std::max)(2, segments / 2);
            XMFLOAT3 previous{};
            for (int index = 0; index <= steps; ++index)
            {
                const float angle = XM_PI * static_cast<float>(index) /
                    static_cast<float>(steps);
                const float cosine = std::cos(angle) * radius;
                const float sine = std::sin(angle) * radius;

                const XMFLOAT3 point{
                    center.x + from.x * cosine + to.x * sine,
                    center.y + from.y * cosine + to.y * sine,
                    center.z + from.z * cosine + to.z * sine };

                if (index > 0) AppendLine(previous, point, color, out);
                previous = point;
            }
        }

        XMFLOAT3 Normalized(const XMFLOAT3& value, const XMFLOAT3& fallback) noexcept
        {
            const float length_squared =
                value.x * value.x + value.y * value.y + value.z * value.z;
            if (length_squared <= 1.0e-12f) return fallback;
            const float inverse = 1.0f / std::sqrt(length_squared);
            return XMFLOAT3{ value.x * inverse, value.y * inverse, value.z * inverse };
        }

        // v と直交する単位ベクトルを 1 本作る。
        XMFLOAT3 AnyPerpendicular(const XMFLOAT3& value) noexcept
        {
            // 最も小さい成分の軸を選ぶと、外積が退化しない。
            const XMFLOAT3 helper = (std::fabs(value.y) < 0.9f)
                ? XMFLOAT3{ 0.0f, 1.0f, 0.0f } : XMFLOAT3{ 1.0f, 0.0f, 0.0f };
            XMFLOAT3 result{};
            XMStoreFloat3(&result, XMVector3Normalize(XMVector3Cross(
                XMLoadFloat3(&value), XMLoadFloat3(&helper))));
            return result;
        }
    }

    void ColliderDebugDraw::AppendAxisAlignedBox(const XMFLOAT3& minimum,
        const XMFLOAT3& maximum, std::uint32_t color, std::vector<DebugLine>& out)
    {
        const XMFLOAT3 corners[8]{
            { minimum.x, minimum.y, minimum.z }, { maximum.x, minimum.y, minimum.z },
            { maximum.x, maximum.y, minimum.z }, { minimum.x, maximum.y, minimum.z },
            { minimum.x, minimum.y, maximum.z }, { maximum.x, minimum.y, maximum.z },
            { maximum.x, maximum.y, maximum.z }, { minimum.x, maximum.y, maximum.z },
        };
        static const int edges[12][2]{
            { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
            { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
            { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
        };
        for (const auto& edge : edges)
        {
            AppendLine(corners[edge[0]], corners[edge[1]], color, out);
        }
    }

    void ColliderDebugDraw::AppendBox(const XMFLOAT3& center, const XMFLOAT3& half_extents,
        const XMFLOAT4X4& rotation, std::uint32_t color, std::vector<DebugLine>& out)
    {
        const XMMATRIX matrix = XMLoadFloat4x4(&rotation);

        XMFLOAT3 corners[8]{};
        for (int index = 0; index < 8; ++index)
        {
            const XMFLOAT3 local{
                (index & 1) ? half_extents.x : -half_extents.x,
                (index & 2) ? half_extents.y : -half_extents.y,
                (index & 4) ? half_extents.z : -half_extents.z };

            XMFLOAT3 rotated{};
            XMStoreFloat3(&rotated,
                XMVector3TransformNormal(XMLoadFloat3(&local), matrix));
            corners[index] = XMFLOAT3{
                center.x + rotated.x, center.y + rotated.y, center.z + rotated.z };
        }

        // 添字はビット (x, y, z) の組み合わせ。1 ビットだけ違う頂点同士が辺になる。
        static const int edges[12][2]{
            { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 },
            { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 },
            { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
        };
        for (const auto& edge : edges)
        {
            AppendLine(corners[edge[0]], corners[edge[1]], color, out);
        }
    }

    void ColliderDebugDraw::AppendSphere(const XMFLOAT3& center, float radius,
        int segments, std::uint32_t color, std::vector<DebugLine>& out)
    {
        if (radius <= 0.0f) return;

        // 3 平面の円で球を表す。線の本数を抑えつつ形が分かる。
        AppendCircle(center, XMFLOAT3{ 1, 0, 0 }, XMFLOAT3{ 0, 1, 0 },
            radius, segments, color, out);
        AppendCircle(center, XMFLOAT3{ 0, 1, 0 }, XMFLOAT3{ 0, 0, 1 },
            radius, segments, color, out);
        AppendCircle(center, XMFLOAT3{ 1, 0, 0 }, XMFLOAT3{ 0, 0, 1 },
            radius, segments, color, out);
    }

    void ColliderDebugDraw::AppendCapsule(const XMFLOAT3& segment_start,
        const XMFLOAT3& segment_end, float radius, int segments,
        std::uint32_t color, std::vector<DebugLine>& out)
    {
        if (radius <= 0.0f) return;

        const XMFLOAT3 axis = Normalized(XMFLOAT3{
            segment_end.x - segment_start.x,
            segment_end.y - segment_start.y,
            segment_end.z - segment_start.z }, XMFLOAT3{ 0.0f, 1.0f, 0.0f });

        const XMFLOAT3 side_a = AnyPerpendicular(axis);
        XMFLOAT3 side_b{};
        XMStoreFloat3(&side_b, XMVector3Normalize(XMVector3Cross(
            XMLoadFloat3(&axis), XMLoadFloat3(&side_a))));

        // 両端の円。
        AppendCircle(segment_start, side_a, side_b, radius, segments, color, out);
        AppendCircle(segment_end, side_a, side_b, radius, segments, color, out);

        // 円柱部分の 4 本の縦線。
        const XMFLOAT3 offsets[4]{
            { side_a.x * radius, side_a.y * radius, side_a.z * radius },
            { -side_a.x * radius, -side_a.y * radius, -side_a.z * radius },
            { side_b.x * radius, side_b.y * radius, side_b.z * radius },
            { -side_b.x * radius, -side_b.y * radius, -side_b.z * radius },
        };
        for (const XMFLOAT3& offset : offsets)
        {
            AppendLine(
                XMFLOAT3{ segment_start.x + offset.x, segment_start.y + offset.y,
                          segment_start.z + offset.z },
                XMFLOAT3{ segment_end.x + offset.x, segment_end.y + offset.y,
                          segment_end.z + offset.z },
                color, out);
        }

        // 端の丸み。軸の外向きへ半円を 2 枚ずつ。
        const XMFLOAT3 down{ -axis.x, -axis.y, -axis.z };
        AppendHalfCircle(segment_start, side_a, down, radius, segments, color, out);
        AppendHalfCircle(segment_start, side_b, down, radius, segments, color, out);
        AppendHalfCircle(segment_end, side_a, axis, radius, segments, color, out);
        AppendHalfCircle(segment_end, side_b, axis, radius, segments, color, out);
    }

    void ColliderDebugDraw::Build(const Scene::SceneCollisionWorld& world,
        const Options& options, std::vector<DebugLine>& out)
    {
        out.clear();

        const Scene::Scene* scene = world.AttachedScene();
        if (scene == nullptr) return;

        for (const auto& entry : world.Registrations())
        {
            Core::GameObject* object = scene->FindGameObjectByID(entry.object);
            if (object == nullptr || object->PendingDestroy()) continue;

            const auto* collider = Components::FindColliderByID(*object, entry.collider);
            if (collider == nullptr || !collider->debug_draw) continue;

            // Character Motor の移動用に選ばれている Collider かどうか。
            bool is_primary = false;
            if (const auto* motor = object->GetComponent<Components::CharacterMotorComponent>())
            {
                is_primary = motor->primary_collider_key == collider->collider_key;
            }

            // 色で状態を表す。優先順位は「無効 > 移動用 > Trigger > 通常」。
            std::uint32_t color = ColliderDebugColors::blocking;
            if (!collider->ActiveInHierarchy()) color = ColliderDebugColors::disabled;
            else if (is_primary)                color = ColliderDebugColors::primary;
            else if (collider->is_trigger)      color = ColliderDebugColors::trigger;

            if (options.draw_bounds && entry.bounds_valid)
            {
                AppendAxisAlignedBox(entry.bounds_min, entry.bounds_max,
                    ColliderDebugColors::bounds, out);
            }

            // 中心オフセットの位置に小さな十字を出す。
            // 「どこを中心として当たっているか」が Transform と別なので見せる。
            {
                const XMFLOAT3 center = collider->WorldCenter();
                constexpr float marker = 0.08f;
                AppendLine({ center.x - marker, center.y, center.z },
                    { center.x + marker, center.y, center.z }, color, out);
                AppendLine({ center.x, center.y - marker, center.z },
                    { center.x, center.y + marker, center.z }, color, out);
                AppendLine({ center.x, center.y, center.z - marker },
                    { center.x, center.y, center.z + marker }, color, out);
            }

            if (!options.draw_shapes) continue;

            switch (collider->Shape())
            {
            case Components::ColliderShape::Sphere:
            {
                const auto& sphere =
                    static_cast<const Components::SphereColliderComponent&>(*collider);
                AppendSphere(sphere.WorldCenter(), sphere.EffectiveRadius(),
                    options.circle_segments, color, out);
                break;
            }
            case Components::ColliderShape::Capsule:
            {
                const auto& capsule =
                    static_cast<const Components::CapsuleColliderComponent&>(*collider);
                XMFLOAT3 segment_start{};
                XMFLOAT3 segment_end{};
                capsule.WorldSegment(segment_start, segment_end);
                AppendCapsule(segment_start, segment_end, capsule.EffectiveRadius(),
                    options.circle_segments, color, out);
                break;
            }
            case Components::ColliderShape::Box:
            {
                const auto& box =
                    static_cast<const Components::BoxColliderComponent&>(*collider);
                const XMFLOAT3 euler = object->GetTransform().LocalRotationEuler();
                XMFLOAT4X4 rotation{};
                XMStoreFloat4x4(&rotation,
                    XMMatrixRotationRollPitchYaw(euler.x, euler.y, euler.z));
                AppendBox(box.WorldCenter(), box.WorldHalfExtents(), rotation, color, out);
                break;
            }
            case Components::ColliderShape::Landscape:
            {
                const auto& landscape =
                    static_cast<const Components::LandscapeColliderComponent&>(*collider);
                if (!landscape.ReadyForQuery()) break;
                if (!options.draw_mesh_wireframe || !landscape.debug_draw_wireframe) break;

                const XMMATRIX matrix = XMLoadFloat4x4(&landscape.WorldMatrix());
                const auto& triangles = landscape.Triangles();
                for (const Physics::Triangle& triangle : triangles)
                {
                    XMFLOAT3 world_vertices[3]{};
                    for (int vertex = 0; vertex < 3; ++vertex)
                    {
                        XMStoreFloat3(&world_vertices[vertex], XMVector3TransformCoord(
                            XMLoadFloat3(&triangle.vertices[vertex]), matrix));
                    }
                    AppendLine(world_vertices[0], world_vertices[1], color, out);
                    AppendLine(world_vertices[1], world_vertices[2], color, out);
                    AppendLine(world_vertices[2], world_vertices[0], color, out);
                }
                break;
            }
            case Components::ColliderShape::Mesh:
            {
                const auto& mesh =
                    static_cast<const Components::MeshColliderComponent&>(*collider);

                // Cook に失敗している Mesh は赤い境界ボックスで知らせる。
                // 「置いたのに当たらない」原因が画面から分かるようにする。
                if (!mesh.ReadyForQuery())
                {
                    XMFLOAT3 minimum{};
                    XMFLOAT3 maximum{};
                    if (mesh.ComputeWorldBounds(minimum, maximum))
                    {
                        AppendAxisAlignedBox(minimum, maximum,
                            ColliderDebugColors::cook_failed, out);
                    }
                    break;
                }

                // 既定は境界ボックスのみ。三角形は本数が多いので、
                // Collider ごとに明示的に有効にしたときだけ描く。
                if (!options.draw_mesh_wireframe || !mesh.debug_draw_wireframe) break;

                const auto& cooked = mesh.Cooked();
                const XMMATRIX matrix = XMLoadFloat4x4(&mesh.WorldMatrix());
                const Physics::Triangle* triangles = cooked->Triangles();
                for (std::size_t index = 0; index < cooked->TriangleCount(); ++index)
                {
                    XMFLOAT3 world_vertices[3]{};
                    for (int vertex = 0; vertex < 3; ++vertex)
                    {
                        XMStoreFloat3(&world_vertices[vertex], XMVector3TransformCoord(
                            XMLoadFloat3(&triangles[index].vertices[vertex]), matrix));
                    }
                    AppendLine(world_vertices[0], world_vertices[1], color, out);
                    AppendLine(world_vertices[1], world_vertices[2], color, out);
                    AppendLine(world_vertices[2], world_vertices[0], color, out);
                }
                break;
            }
            }
        }
    }
}
