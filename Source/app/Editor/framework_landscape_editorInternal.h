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
        const ReplayEngine::Landscape::LandscapeData* data = nullptr;
        std::uint64_t revision = 0;
        ReplayEngine::Landscape::LandscapeRayHit hit{};
        ReplayEngine::Landscape::LandscapeData::SurfaceRegion region;
        DirectX::XMFLOAT3 tangent{}, bitangent{};
        float radius = 0;
        void Prepare(const ReplayEngine::Landscape::LandscapeData& value,
            const ReplayEngine::Landscape::LandscapeRayHit& picked, float value_radius, int)
        {
            if (data == &value && revision == value.Revision() && radius == value_radius &&
                hit.face_index == picked.face_index && hit.position.x == picked.position.x &&
                hit.position.y == picked.position.y && hit.position.z == picked.position.z) return;
            data = &value; revision = value.Revision(); hit = picked; radius = value_radius;
            value.QuerySurface(hit.position, radius, hit.face_index, region);
            const auto normal = DirectX::XMLoadFloat3(&hit.normal);
            const auto axis = std::fabs(hit.normal.y) < 0.9f
                ? DirectX::XMVectorSet(0,1,0,0) : DirectX::XMVectorSet(1,0,0,0);
            const auto t = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(normal, axis));
            DirectX::XMStoreFloat3(&tangent, t);
            DirectX::XMStoreFloat3(&bitangent, DirectX::XMVector3Cross(normal, t));
        }
        bool Sample(float x, float y, DirectX::XMFLOAT3& point) const
        {
            const auto target = Add(hit.position, Add(Scale(tangent,x), Scale(bitangent,y)));
            ReplayEngine::Landscape::LandscapeRayHit projected;
            if (!data->ProjectSurface(target, hit.normal, radius, region, projected)) return false;
            point = Add(projected.position, Scale(projected.normal, 0.025f));
            return true;
        }
    };

    inline void DrawTerrainRing(ImDrawList* draw,
        const ReplayEngine::Landscape::LandscapeData& data,
        const ReplayEngine::Core::Transform& transform,
        const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& projection,
        float width, float height, float min_x, float min_y,
        const DirectX::XMFLOAT3& center, float radius,
        ImU32 color, float thickness, TerrainRingCache& cache, std::size_t)
    {
        ImVec2 center_screen{}, edge_screen{};
        float pixels = 48.0f;
        const auto edge = Add(center, Scale(cache.tangent, radius));
        if (ProjectToScene(center,transform,view,projection,width,height,min_x,min_y,center_screen) &&
            ProjectToScene(edge,transform,view,projection,width,height,min_x,min_y,edge_screen))
            pixels = std::hypot(edge_screen.x-center_screen.x, edge_screen.y-center_screen.y);
        const int segments = (std::max)(16,(std::min)(48,static_cast<int>(pixels * 0.5f)));
        DirectX::XMFLOAT3 previous{};
        bool previous_valid = false;
        for (int i=0; i<=segments; ++i)
        {
            const float angle = DirectX::XM_2PI * i / segments;
            DirectX::XMFLOAT3 point{};
            float radial = radius;
            bool valid = false;
            for (int iteration=0; iteration<3; ++iteration)
            {
                valid = cache.Sample(std::cos(angle)*radial, std::sin(angle)*radial, point);
                if (!valid) break;
                const float distance = std::sqrt((point.x-center.x)*(point.x-center.x) +
                    (point.y-center.y)*(point.y-center.y)+(point.z-center.z)*(point.z-center.z));
                if (distance <= radius * 1.01f) break;
                radial *= radius / distance;
            }
            if (i>0 && previous_valid && valid)
                DrawProjectedLine(draw,transform,view,projection,width,height,min_x,min_y,
                    previous,point,color,thickness);
            previous=point; previous_valid=valid;
        }
    }

    inline void DrawTerrainGridInBrush(ImDrawList* draw,
        const ReplayEngine::Landscape::LandscapeData& data,
        const ReplayEngine::Core::Transform& transform,
        const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& projection,
        float width, float height, float min_x, float min_y,
        const DirectX::XMFLOAT3&, float, const TerrainRingCache& cache)
    {
        for (const auto face : cache.region.faces)
            for (int edge=0;edge<3;++edge)
            {
                const auto a=data.VertexPosition(data.Indices()[face*3+edge]);
                const auto b=data.VertexPosition(data.Indices()[face*3+(edge+1)%3]);
                const auto normal=data.FaceNormal(face);
                DrawProjectedLine(draw,transform,view,projection,width,height,min_x,min_y,
                    Add(a,Scale(normal,0.025f)),Add(b,Scale(normal,0.025f)),IM_COL32(90,240,180,115),1.0f);
            }
    }

    inline void DrawBrushFaceInfluence(ImDrawList* draw,
        const ReplayEngine::Landscape::LandscapeData& data,
        const ReplayEngine::Core::Transform& transform,
        const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& projection,
        float width, float height, float min_x, float min_y,
        const DirectX::XMFLOAT3&, float, const TerrainRingCache& cache)
    {
        for (const auto face : cache.region.faces)
        {
            ImVec2 screen[3]{};
            bool valid=true;
            const auto normal=data.FaceNormal(face);
            for(int i=0;i<3;++i)
            {
                const auto p=Add(data.VertexPosition(data.Indices()[face*3+i]),Scale(normal,0.025f));
                valid=ProjectToScene(p,transform,view,projection,width,height,min_x,min_y,screen[i]) && valid;
            }
            if(valid) draw->AddTriangleFilled(screen[0],screen[1],screen[2],IM_COL32(255,210,80,40));
        }
    }
}
