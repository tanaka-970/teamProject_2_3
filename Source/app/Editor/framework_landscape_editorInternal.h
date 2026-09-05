#pragma once

// Landscape editor の分割実装だけが共有する内部 helper。
// 外部の Editor コードから include するものではない。

#include "framework.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeColliderComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/PrimitiveMeshRendererComponent.h"
#include "../../RePlayEngine/Object/GameObject/GameObject.h"

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace framework_landscape_editor_detail
{
    constexpr std::size_t no_face = static_cast<std::size_t>(-1);
    constexpr std::uint32_t no_vertex = static_cast<std::uint32_t>(-1);

    // RePlayEngine currently uses ImGui 1.80 WIP. BeginDisabled / EndDisabled
    // were added later, so keep disabled controls compatible with the project's
    // existing ImGui version by using the same pattern as PropertyDrawer.

    struct ImDrawClipScope
    {
        ImDrawClipScope(ImDrawList* draw_list, const ImVec2& minimum, const ImVec2& maximum)
            : draw(draw_list)
        {
            if (draw != nullptr) draw->PushClipRect(minimum, maximum, true);
        }
        ~ImDrawClipScope()
        {
            if (draw != nullptr) draw->PopClipRect();
        }
        ImDrawClipScope(const ImDrawClipScope&) = delete;
        ImDrawClipScope& operator=(const ImDrawClipScope&) = delete;
        ImDrawList* draw = nullptr;
    };

    struct DisabledScope
    {
        explicit DisabledScope(bool disabled) : active(disabled)
        {
            if (!active) return;
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
        }

        ~DisabledScope()
        {
            if (!active) return;
            ImGui::PopStyleVar();
            ImGui::PopItemFlag();
        }

        DisabledScope(const DisabledScope&) = delete;
        DisabledScope& operator=(const DisabledScope&) = delete;

        bool active = false;
    };

    inline float PointSegmentDistanceSq(const DirectX::XMFLOAT3& point,
        const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b)
    {
        const float ab_x = b.x - a.x;
        const float ab_y = b.y - a.y;
        const float ab_z = b.z - a.z;
        const float ap_x = point.x - a.x;
        const float ap_y = point.y - a.y;
        const float ap_z = point.z - a.z;
        const float length_sq = ab_x * ab_x + ab_y * ab_y + ab_z * ab_z;
        float t = length_sq > 1.0e-12f
            ? (ap_x * ab_x + ap_y * ab_y + ap_z * ab_z) / length_sq : 0.0f;
        t = (std::max)(0.0f, (std::min)(1.0f, t));
        const float dx = point.x - (a.x + ab_x * t);
        const float dy = point.y - (a.y + ab_y * t);
        const float dz = point.z - (a.z + ab_z * t);
        return dx * dx + dy * dy + dz * dz;
    }

    inline bool ClosestFaceEdge(const ReplayEngine::Landscape::LandscapeData& data,
        std::size_t face_index, const DirectX::XMFLOAT3& hit_position,
        std::uint32_t& out_a, std::uint32_t& out_b)
    {
        out_a = no_vertex;
        out_b = no_vertex;
        const std::size_t offset = face_index * 3;
        if (offset + 2 >= data.Indices().size()) return false;
        const std::uint32_t face[3] = {
            data.Indices()[offset], data.Indices()[offset + 1], data.Indices()[offset + 2] };
        float best = (std::numeric_limits<float>::max)();
        for (int edge = 0; edge < 3; ++edge)
        {
            const std::uint32_t a = face[edge];
            const std::uint32_t b = face[(edge + 1) % 3];
            if (a >= data.Vertices().size() || b >= data.Vertices().size()) continue;
            const float distance = PointSegmentDistanceSq(hit_position,
                data.Vertices()[a].position, data.Vertices()[b].position);
            if (distance >= best) continue;
            best = distance;
            out_a = (std::min)(a, b);
            out_b = (std::max)(a, b);
        }
        return out_a != no_vertex && out_b != no_vertex;
    }

    inline ReplayEngine::Components::LandscapeComponent* SelectedLandscape(
        ReplayEngine::Editor::EditorContext& context,
        ReplayEngine::Scene::Scene& scene,
        ReplayEngine::Core::GameObject*& object)
    {
        object = context.Selection().ResolvePrimary(scene);
        if (object == nullptr || object->PendingDestroy() || !object->ActiveInHierarchy())
            return nullptr;
        return object->GetComponent<ReplayEngine::Components::LandscapeComponent>();
    }

    inline bool ToLocalRay(const ReplayEngine::Core::Transform& transform,
        const ReplayEngine::Editor::EditorViewportCamera::Ray& world_ray,
        DirectX::XMFLOAT3& local_origin, DirectX::XMFLOAT3& local_direction)
    {
        using namespace DirectX;
        const XMFLOAT4X4 world_values = transform.WorldMatrixFloat4x4();
        const XMMATRIX world = XMLoadFloat4x4(&world_values);
        XMVECTOR determinant{};
        const XMMATRIX inverse = XMMatrixInverse(&determinant, world);
        if (std::fabs(XMVectorGetX(determinant)) <= 1.0e-8f) return false;

        const XMVECTOR origin = XMVector3TransformCoord(XMLoadFloat3(&world_ray.origin), inverse);
        XMVECTOR direction = XMVector3TransformNormal(XMLoadFloat3(&world_ray.direction), inverse);
        const float length = XMVectorGetX(XMVector3Length(direction));
        if (!std::isfinite(length) || length <= 1.0e-6f) return false;
        direction = XMVectorScale(direction, 1.0f / length);
        XMStoreFloat3(&local_origin, origin);
        XMStoreFloat3(&local_direction, direction);
        return true;
    }

    inline bool ProjectToScene(const DirectX::XMFLOAT3& local,
        const ReplayEngine::Core::Transform& transform,
        const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& projection,
        float width, float height, float min_x, float min_y,
        ImVec2& out, float* depth = nullptr)
    {
        using namespace DirectX;
        const XMFLOAT4X4 world_values = transform.WorldMatrixFloat4x4();
        const XMMATRIX world = XMLoadFloat4x4(&world_values);
        const XMVECTOR projected = XMVector3Project(XMLoadFloat3(&local),
            0.0f, 0.0f, width, height, 0.0f, 1.0f, projection, view, world);
        XMFLOAT3 screen{};
        XMStoreFloat3(&screen, projected);
        if (!std::isfinite(screen.x) || !std::isfinite(screen.y) ||
            screen.z < 0.0f || screen.z > 1.0f) return false;
        out = { min_x + screen.x, min_y + screen.y };
        if (depth != nullptr) *depth = screen.z;
        return true;
    }

    inline DirectX::XMFLOAT3 Add(const DirectX::XMFLOAT3& a,
        const DirectX::XMFLOAT3& b) noexcept
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    inline DirectX::XMFLOAT3 Scale(const DirectX::XMFLOAT3& value, float scale) noexcept
    {
        return { value.x * scale, value.y * scale, value.z * scale };
    }

    inline bool SampleLandscapeSurfaceAtXZ(
        const ReplayEngine::Landscape::LandscapeData& data,
        float x, float z, float fallback_y, DirectX::XMFLOAT3& out)
    {
        const DirectX::XMFLOAT3 bounds_min = data.BoundsMin();
        const DirectX::XMFLOAT3 bounds_max = data.BoundsMax();
        const float height = (std::max)(1.0f, bounds_max.y - bounds_min.y);
        const float margin = (std::max)(4.0f, height + 2.0f);
        const DirectX::XMFLOAT3 origin{ x, bounds_max.y + margin, z };
        const DirectX::XMFLOAT3 direction{ 0.0f, -1.0f, 0.0f };

        ReplayEngine::Landscape::LandscapeRayHit hit{};
        if (data.Raycast(origin, direction, height + margin * 2.0f, hit))
        {
            out = hit.position;
            return true;
        }

        out = { x, fallback_y, z };
        return false;
    }

    inline bool DrawProjectedLine(ImDrawList* draw,
        const ReplayEngine::Core::Transform& transform,
        const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& projection,
        float width, float height, float min_x, float min_y,
        const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b,
        ImU32 color, float thickness)
    {
        ImVec2 screen_a{};
        ImVec2 screen_b{};
        if (!ProjectToScene(a, transform, view, projection, width, height,
            min_x, min_y, screen_a)) return false;
        if (!ProjectToScene(b, transform, view, projection, width, height,
            min_x, min_y, screen_b)) return false;

        draw->AddLine(screen_a, screen_b, IM_COL32(12, 18, 14, 180),
            thickness + 2.0f);
        draw->AddLine(screen_a, screen_b, color, thickness);
        return true;
    }

    struct TerrainRingCache
    {
        struct Ring
        {
            float radius = -1.0f;
            std::vector<DirectX::XMFLOAT3> points;
            std::vector<std::uint8_t> valid;
        };

        bool Matches(const ReplayEngine::Landscape::LandscapeData& value,
            const DirectX::XMFLOAT3& value_center, float value_radius,
            int value_preview_mode) const noexcept
        {
            return data == &value && revision == value.Revision() &&
                center.x == value_center.x && center.y == value_center.y &&
                center.z == value_center.z && radius == value_radius &&
                preview_mode == value_preview_mode;
        }

        void Prepare(const ReplayEngine::Landscape::LandscapeData& value,
            const DirectX::XMFLOAT3& value_center, float value_radius,
            int value_preview_mode)
        {
            if (Matches(value, value_center, value_radius, value_preview_mode)) return;
            data = &value;
            revision = value.Revision();
            center = value_center;
            radius = value_radius;
            preview_mode = value_preview_mode;
            for (Ring& ring : rings)
            {
                ring.radius = -1.0f;
                ring.points.clear();
                ring.valid.clear();
            }
        }

        const ReplayEngine::Landscape::LandscapeData* data = nullptr;
        std::uint64_t revision = 0;
        DirectX::XMFLOAT3 center{};
        float radius = 0.0f;
        int preview_mode = -1;
        Ring rings[4];
    };

    inline void DrawTerrainRing(ImDrawList* draw,
        const ReplayEngine::Landscape::LandscapeData& data,
        const ReplayEngine::Core::Transform& transform,
        const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& projection,
        float width, float height, float min_x, float min_y,
        const DirectX::XMFLOAT3& center, float radius,
        ImU32 color, float thickness, TerrainRingCache& cache,
        std::size_t cache_index)
    {
        if (radius <= 0.0f || cache_index >= 4u) return;

        const int segments = data.FaceCount() > 10000 ? 36 : 64;
        TerrainRingCache::Ring& ring = cache.rings[cache_index];
        if (ring.radius != radius || ring.points.size() != static_cast<std::size_t>(segments + 1))
        {
            ring.radius = radius;
            ring.points.resize(static_cast<std::size_t>(segments + 1));
            ring.valid.resize(ring.points.size());
            for (int index = 0; index <= segments; ++index)
            {
                const float angle = DirectX::XM_2PI *
                    static_cast<float>(index) / static_cast<float>(segments);
                ring.valid[static_cast<std::size_t>(index)] = SampleLandscapeSurfaceAtXZ(data,
                    center.x + std::cos(angle) * radius,
                    center.z + std::sin(angle) * radius,
                    center.y, ring.points[static_cast<std::size_t>(index)]) ? 1u : 0u;
                ring.points[static_cast<std::size_t>(index)].y += 0.04f;
            }
        }

        DirectX::XMFLOAT3 previous{};
        bool previous_valid = false;
        for (int index = 0; index <= segments; ++index)
        {
            const DirectX::XMFLOAT3& point = ring.points[static_cast<std::size_t>(index)];
            const bool valid = ring.valid[static_cast<std::size_t>(index)] != 0;

            if (index > 0 && previous_valid && valid)
            {
                DrawProjectedLine(draw, transform, view, projection, width, height,
                    min_x, min_y, previous, point, color, thickness);
            }
            previous = point;
            previous_valid = valid;
        }
    }

    inline void DrawTerrainGridInBrush(ImDrawList* draw,
        const ReplayEngine::Landscape::LandscapeData& data,
        const ReplayEngine::Core::Transform& transform,
        const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& projection,
        float width, float height, float min_x, float min_y,
        const DirectX::XMFLOAT3& center, float radius)
    {
        if (radius <= 0.0f) return;

        const float base_spacing = (std::max)(data.CellSize(), radius * 0.25f);
        const float spacing = (std::max)(0.5f, base_spacing);
        const int steps = (std::min)(12,
            static_cast<int>(std::ceil(radius / spacing)));
        const ImU32 color = IM_COL32(90, 240, 180, 115);

        for (int index = -steps; index <= steps; ++index)
        {
            const float offset = static_cast<float>(index) * spacing;
            if (std::fabs(offset) > radius) continue;
            const float chord = std::sqrt((std::max)(0.0f,
                radius * radius - offset * offset));

            DirectX::XMFLOAT3 a{}, b{};
            SampleLandscapeSurfaceAtXZ(data, center.x - chord, center.z + offset,
                center.y, a);
            SampleLandscapeSurfaceAtXZ(data, center.x + chord, center.z + offset,
                center.y, b);
            a.y += 0.035f;
            b.y += 0.035f;
            DrawProjectedLine(draw, transform, view, projection, width, height,
                min_x, min_y, a, b, color, 1.0f);

            SampleLandscapeSurfaceAtXZ(data, center.x + offset, center.z - chord,
                center.y, a);
            SampleLandscapeSurfaceAtXZ(data, center.x + offset, center.z + chord,
                center.y, b);
            a.y += 0.035f;
            b.y += 0.035f;
            DrawProjectedLine(draw, transform, view, projection, width, height,
                min_x, min_y, a, b, color, 1.0f);
        }
    }

    inline void DrawBrushFaceInfluence(ImDrawList* draw,
        const ReplayEngine::Landscape::LandscapeData& data,
        const ReplayEngine::Core::Transform& transform,
        const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& projection,
        float width, float height, float min_x, float min_y,
        const DirectX::XMFLOAT3& center, float radius)
    {
        if (radius <= 0.0f || data.FaceCount() > 30000) return;
        const float radius_sq = radius * radius;

        for (std::size_t face = 0; face < data.FaceCount(); ++face)
        {
            const DirectX::XMFLOAT3 face_center = data.FaceCenter(face);
            const float dx = face_center.x - center.x;
            const float dz = face_center.z - center.z;
            const float distance_sq = dx * dx + dz * dz;
            if (distance_sq > radius_sq) continue;

            const float t = 1.0f - std::sqrt(distance_sq) / radius;
            const DirectX::XMFLOAT3 normal = data.FaceNormal(face);
            const float slope = 1.0f - (std::max)(0.0f,
                (std::min)(1.0f, std::fabs(normal.y)));
            const int alpha = static_cast<int>(24.0f + t * 52.0f + slope * 36.0f);
            const ImU32 color = IM_COL32(255, 210, 80,
                (std::min)(96, (std::max)(20, alpha)));

            const std::size_t offset = face * 3;
            if (offset + 2 >= data.Indices().size()) continue;
            ImVec2 screen[3]{};
            bool valid = true;
            for (int i = 0; i < 3; ++i)
            {
                const std::uint32_t vertex = data.Indices()[offset + i];
                if (vertex >= data.Vertices().size())
                {
                    valid = false;
                    break;
                }
                DirectX::XMFLOAT3 p = data.Vertices()[vertex].position;
                p = Add(p, Scale(normal, 0.025f));
                if (!ProjectToScene(p, transform, view, projection,
                    width, height, min_x, min_y, screen[i]))
                {
                    valid = false;
                    break;
                }
            }
            if (valid) draw->AddTriangleFilled(screen[0], screen[1], screen[2], color);
        }
    }

}
