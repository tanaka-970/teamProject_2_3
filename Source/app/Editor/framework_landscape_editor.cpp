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

namespace
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

    float PointSegmentDistanceSq(const DirectX::XMFLOAT3& point,
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

    bool ClosestFaceEdge(const ReplayEngine::Landscape::LandscapeData& data,
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

    ReplayEngine::Components::LandscapeComponent* SelectedLandscape(
        ReplayEngine::Editor::EditorContext& context,
        ReplayEngine::Scene::Scene& scene,
        ReplayEngine::Core::GameObject*& object)
    {
        object = context.Selection().ResolvePrimary(scene);
        if (object == nullptr || object->PendingDestroy() || !object->ActiveInHierarchy())
            return nullptr;
        return object->GetComponent<ReplayEngine::Components::LandscapeComponent>();
    }

    bool ToLocalRay(const ReplayEngine::Core::Transform& transform,
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

    bool ProjectToScene(const DirectX::XMFLOAT3& local,
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

    DirectX::XMFLOAT3 Add(const DirectX::XMFLOAT3& a,
        const DirectX::XMFLOAT3& b) noexcept
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    DirectX::XMFLOAT3 Scale(const DirectX::XMFLOAT3& value, float scale) noexcept
    {
        return { value.x * scale, value.y * scale, value.z * scale };
    }

    bool SampleLandscapeSurfaceAtXZ(
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

    bool DrawProjectedLine(ImDrawList* draw,
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

    void DrawTerrainRing(ImDrawList* draw,
        const ReplayEngine::Landscape::LandscapeData& data,
        const ReplayEngine::Core::Transform& transform,
        const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& projection,
        float width, float height, float min_x, float min_y,
        const DirectX::XMFLOAT3& center, float radius,
        ImU32 color, float thickness)
    {
        if (radius <= 0.0f) return;

        const int segments = data.FaceCount() > 10000 ? 36 : 64;
        DirectX::XMFLOAT3 previous{};
        bool previous_valid = false;
        for (int index = 0; index <= segments; ++index)
        {
            const float angle = DirectX::XM_2PI *
                static_cast<float>(index) / static_cast<float>(segments);
            DirectX::XMFLOAT3 point{};
            const bool valid = SampleLandscapeSurfaceAtXZ(data,
                center.x + std::cos(angle) * radius,
                center.z + std::sin(angle) * radius,
                center.y, point);
            point.y += 0.04f;

            if (index > 0 && previous_valid && valid)
            {
                DrawProjectedLine(draw, transform, view, projection, width, height,
                    min_x, min_y, previous, point, color, thickness);
            }
            previous = point;
            previous_valid = valid;
        }
    }

    void DrawTerrainGridInBrush(ImDrawList* draw,
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

    void DrawBrushFaceInfluence(ImDrawList* draw,
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

void framework::draw_landscape_editor_toolbar()
{
#ifdef USE_IMGUI
    if (active_editor_view != editor_view::scene || object_scene_play_mode) return;

    ReplayEngine::Scene::Scene& scene = active_object_scene();
    ReplayEngine::Core::GameObject* object = nullptr;
    auto* landscape = SelectedLandscape(object_editor_context, scene, object);
    if (landscape == nullptr) return;

    ImGui::Separator();
    ImGui::PushID("LandscapeEditorToolbar");

    // Landscape は普通の GameObject + Component のまま保ちつつ、
    // 見た目/衝突/Primitive 重複の状態を操作した場所で即座に説明・修復できるようにする。
    auto* landscape_renderer = object->GetComponent<ReplayEngine::Components::LandscapeRendererComponent>();
    auto* landscape_collider = object->GetComponent<ReplayEngine::Components::LandscapeColliderComponent>();
    auto* primitive_renderer = object->GetComponent<ReplayEngine::Components::PrimitiveMeshRendererComponent>();
    if (landscape_renderer == nullptr)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.32f, 1.0f),
            u8"Landscape Renderer が無いため地形は表示されません。");
        if (object_editor_context.CanEdit())
        {
            ImGui::SameLine();
            if (ImGui::SmallButton(u8"Rendererを追加"))
            {
                object_editor_context.BeginEdit("Landscape Renderer を追加");
                if (object->AddComponent<ReplayEngine::Components::LandscapeRendererComponent>() != nullptr)
                {
                    object_editor_context.CommitEdit();
                    object_editor_context.SetStatus("Landscape Renderer を追加しました");
                }
                else object_editor_context.CancelEdit();
            }
        }
    }
    if (landscape_collider == nullptr)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.30f, 1.0f),
            u8"Landscape Collider が無いため地形に衝突判定はありません。");
        if (object_editor_context.CanEdit())
        {
            ImGui::SameLine();
            if (ImGui::SmallButton(u8"Colliderを追加"))
            {
                object_editor_context.BeginEdit("Landscape Collider を追加");
                if (object->AddComponent<ReplayEngine::Components::LandscapeColliderComponent>() != nullptr)
                {
                    object_editor_context.CommitEdit();
                    object_editor_context.SetStatus("Landscape Collider を追加しました");
                }
                else object_editor_context.CancelEdit();
            }
        }
    }
    if (primitive_renderer != nullptr && primitive_renderer->visible)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.60f, 0.22f, 1.0f),
            u8"Primitive と Landscape が同時表示されています。見た目が重なる可能性があります。");
        if (object_editor_context.CanEdit())
        {
            ImGui::SameLine();
            if (ImGui::SmallButton(u8"Primitive表示をOFF"))
            {
                object_editor_context.BeginEdit("Primitive 表示を無効化");
                primitive_renderer->visible = false;
                primitive_renderer->OnPropertyChanged("visible");
                object_editor_context.CommitEdit();
                object_editor_context.SetStatus("Primitive 表示をOFFにしました。Landscape Component はそのままです");
            }
        }
    }

    ImGui::Checkbox(u8"Landscape 編集", &landscape_edit_enabled);
    if (!landscape_edit_enabled)
    {
        ImGui::SameLine();
        ImGui::TextDisabled(u8"Ground を通常の GameObject として選択中");
        ImGui::PopID();
        return;
    }

    const char* edit_modes[] = { u8"スカルプト", u8"トポロジー" };
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130.0f);
    ImGui::Combo(u8"##LandscapeMode", &landscape_edit_mode, edit_modes, IM_ARRAYSIZE(edit_modes));

    auto& data = landscape->Data();
    ImGui::SameLine();
    ImGui::TextDisabled("V:%zu  F:%zu", data.VertexCount(), data.FaceCount());

    if (landscape_edit_mode == 0)
    {
        const char* brush_modes[] = {
            "Raise", "Lower", "Smooth", "Flatten", "Noise"
        };
        ImGui::SetNextItemWidth(110.0f);
        ImGui::Combo(u8"ブラシ", &landscape_brush_mode,
            brush_modes, IM_ARRAYSIZE(brush_modes));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::DragFloat(u8"ローカル半径", &landscape_brush.radius, 0.1f, 0.1f, 256.0f, "%.2f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::DragFloat(u8"強さ", &landscape_brush.strength, 0.05f, 0.0f, 100.0f, "%.2f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        ImGui::DragFloat(u8"Falloff", &landscape_brush.falloff, 0.02f, 0.0f, 1.0f, "%.2f");
        ImGui::SameLine();
        const char* preview_modes[] = {
            "Ring", "Falloff", "Grid", "Contour", "Grid + Contour"
        };
        landscape_brush_preview_mode = (std::max)(0,
            (std::min)(landscape_brush_preview_mode,
                static_cast<int>(IM_ARRAYSIZE(preview_modes)) - 1));
        ImGui::SetNextItemWidth(130.0f);
        ImGui::Combo("Preview", &landscape_brush_preview_mode,
            preview_modes, IM_ARRAYSIZE(preview_modes));

        int direction = static_cast<int>(landscape_brush.direction);
        const char* directions[] = { "Local Y", "Vertex Normal" };
        ImGui::SetNextItemWidth(130.0f);
        if (ImGui::Combo(u8"方向", &direction, directions, IM_ARRAYSIZE(directions)))
        {
            landscape_brush.direction = static_cast<ReplayEngine::Landscape::LandscapeSculptDirection>(
                (std::max)(0, (std::min)(direction, 1)));
        }
        if (landscape_brush_mode == static_cast<int>(ReplayEngine::Landscape::LandscapeBrushMode::Flatten))
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            ImGui::DragFloat(u8"ローカル高さ", &landscape_brush.flatten_height, 0.05f);
        }
        if (landscape_brush_mode == static_cast<int>(ReplayEngine::Landscape::LandscapeBrushMode::Noise))
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            ImGui::DragFloat(u8"Noise Scale", &landscape_brush.noise_scale,
                0.01f, 0.001f, 100.0f, "%.3f");
        }
        ImGui::TextDisabled(u8"左ドラッグ: 編集 | Ctrl/Alt+左クリック: 通常選択 | Esc: Landscape編集終了 | 値はLocal空間");
    }
    else
    {
        const char* selection_modes[] = { "Face", "Edge / Bridge" };
        ImGui::SetNextItemWidth(130.0f);
        ImGui::Combo(u8"選択", &landscape_topology_selection_mode,
            selection_modes, IM_ARRAYSIZE(selection_modes));

        if (landscape_topology_selection_mode == 0)
        {
            const bool face_selected = landscape_selected_face < data.FaceCount();
            if (face_selected)
                ImGui::TextDisabled(u8"選択 Face: %zu", landscape_selected_face);
            else
                ImGui::TextDisabled(u8"Scene View で Face をクリックして選択");

            const auto edit = [&](const char* label, const char* history_label, auto&& operation)
            {
                const bool enabled = face_selected && object_editor_context.CanEdit();
                DisabledScope disabled(!enabled);
                const bool clicked = ImGui::Button(label);
                if (!clicked || !enabled) return;

                object_editor_context.BeginEdit(history_label);
                if (operation())
                {
                    object_editor_context.CommitEdit();
                    if (landscape_selected_face >= data.FaceCount()) landscape_selected_face = no_face;
                }
                else
                {
                    object_editor_context.CancelEdit();
                    object_editor_context.SetStatus(std::string(history_label) + " に失敗しました");
                }
            };

            edit("Subdivide", "Landscape Face を分割", [&] { return data.SubdivideFace(landscape_selected_face); });
            ImGui::SameLine();
            ImGui::SetNextItemWidth(85.0f);
            ImGui::DragFloat("##ExtrudeDistance", &landscape_extrude_distance, 0.05f, -100.0f, 100.0f, "%.2f");
            ImGui::SameLine();
            edit("Extrude", "Landscape Face を押し出し", [&] {
                return data.ExtrudeFace(landscape_selected_face, landscape_extrude_distance);
            });
            ImGui::SameLine();
            ImGui::SetNextItemWidth(75.0f);
            ImGui::DragFloat("##InsetAmount", &landscape_inset_amount, 0.01f, 0.01f, 0.95f, "%.2f");
            ImGui::SameLine();
            edit("Inset", "Landscape Face をInset", [&] {
                return data.InsetFace(landscape_selected_face, landscape_inset_amount);
            });
            ImGui::SameLine();
            edit("Cut Hole", "Landscape に穴を開ける", [&] {
                return data.DeleteFace(landscape_selected_face);
            });

            ImGui::SetNextItemWidth(90.0f);
            ImGui::DragFloat(u8"Tunnel 深さ", &landscape_tunnel_depth, 0.1f, 0.1f, 500.0f, "%.1f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            ImGui::DragInt(u8"分割", &landscape_tunnel_segments, 0.1f, 1, 64);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            ImGui::DragFloat(u8"終端Scale", &landscape_tunnel_end_scale, 0.01f, 0.05f, 4.0f, "%.2f");
            ImGui::SameLine();
            edit("Cave / Tunnel", "Landscape Tunnel を生成", [&] {
                return data.CreateTunnelFromFace(landscape_selected_face,
                    landscape_tunnel_depth, landscape_tunnel_segments,
                    landscape_tunnel_end_scale);
            });
            ImGui::TextDisabled(u8"Cut Hole / Extrude / Tunnel は Height Field 制約なし。張り出し・床と天井を同一XZに保持可能");
        }
        else
        {
            const bool first = landscape_bridge_a0 != no_vertex && landscape_bridge_a1 != no_vertex;
            const bool second = landscape_bridge_b0 != no_vertex && landscape_bridge_b1 != no_vertex;
            if (first)
                ImGui::TextDisabled("Edge A: %u-%u", landscape_bridge_a0, landscape_bridge_a1);
            else
                ImGui::TextDisabled(u8"Scene View で 1 本目の Edge を選択");
            if (second)
                ImGui::TextDisabled("Edge B: %u-%u", landscape_bridge_b0, landscape_bridge_b1);
            else if (first)
                ImGui::TextDisabled(u8"次に離れた 2 本目の Edge を選択");

            if (ImGui::Button(u8"Edge 選択をクリア"))
            {
                landscape_bridge_a0 = landscape_bridge_a1 = no_vertex;
                landscape_bridge_b0 = landscape_bridge_b1 = no_vertex;
            }
            ImGui::SameLine();
            const bool can_bridge = first && second && object_editor_context.CanEdit();
            DisabledScope disabled(!can_bridge);
            const bool bridge_clicked = ImGui::Button("Bridge");
            if (bridge_clicked && can_bridge)
            {
                object_editor_context.BeginEdit("Landscape Edge をBridge");
                if (data.BridgeEdges(landscape_bridge_a0, landscape_bridge_a1,
                    landscape_bridge_b0, landscape_bridge_b1))
                {
                    object_editor_context.CommitEdit();
                    landscape_bridge_a0 = landscape_bridge_a1 = no_vertex;
                    landscape_bridge_b0 = landscape_bridge_b1 = no_vertex;
                    object_editor_context.SetStatus("Landscape Edge をBridgeしました");
                }
                else
                {
                    object_editor_context.CancelEdit();
                    object_editor_context.SetStatus("Bridge に失敗しました。共有頂点のない2本のEdgeを選んでください");
                }
            }
            ImGui::TextDisabled(u8"Bridge は離れた2本のEdgeを2三角形で接続。洞窟の開口・アーチ・継ぎ目作成に利用");
        }
    }
    ImGui::PopID();
#else
    return;
#endif
}

void framework::reset_landscape_editor_state(bool rollback_stroke)
{
#ifdef USE_IMGUI
    bool had_command = false;
    if (landscape_editor_tool.StrokeActive())
    {
        if (rollback_stroke) landscape_editor_tool.CancelStroke();
        else had_command = landscape_editor_tool.EndStroke() != nullptr;
    }

    // Sculpt 中に止めた Collider cook を、選択変更/Scene切替/Play開始でも必ず解除する。
    // LandscapeEditorTool が保持する非所有 data pointer も Scene を跨いで残さない。
    ReplayEngine::Scene::Scene& scene = active_object_scene();
    for (std::size_t index = 0; index < scene.GameObjectCount(); ++index)
    {
        auto* object = scene.GameObjectAt(index);
        if (object == nullptr || object->PendingDestroy()) continue;
        if (auto* collider = object->GetComponent<ReplayEngine::Components::LandscapeColliderComponent>())
            collider->EndInteractiveEdit();
    }
    if (landscape_stroke_transaction)
    {
        if (!rollback_stroke && had_command) object_editor_context.CommitEdit();
        else object_editor_context.CancelEdit();
        landscape_stroke_transaction = false;
    }
    landscape_edit_enabled = false;
    landscape_selected_face = no_face;
    landscape_bridge_a0 = landscape_bridge_a1 = no_vertex;
    landscape_bridge_b0 = landscape_bridge_b1 = no_vertex;
#else
    (void)rollback_stroke;
#endif
}

bool framework::handle_landscape_viewport_edit()
{
#ifdef USE_IMGUI
    // Stroke の終了だけは Viewport の外へ出ても受け取る。
    if (landscape_editor_tool.StrokeActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const auto command = landscape_editor_tool.EndStroke();

        // ドラッグ中に止めていた Collision Cook を解除する。
        // Scene 内の一時フラグを全件解除しておけば、選択が途中で変わっても残らない。
        ReplayEngine::Scene::Scene& release_scene = active_object_scene();
        for (std::size_t index = 0; index < release_scene.GameObjectCount(); ++index)
        {
            auto* release_object = release_scene.GameObjectAt(index);
            if (release_object == nullptr || release_object->PendingDestroy()) continue;
            if (auto* release_collider = release_object->GetComponent<
                ReplayEngine::Components::LandscapeColliderComponent>())
                release_collider->EndInteractiveEdit();
        }

        if (landscape_stroke_transaction)
        {
            if (command != nullptr) object_editor_context.CommitEdit();
            else object_editor_context.CancelEdit();
            landscape_stroke_transaction = false;
        }
        return true;
    }

    if (!landscape_edit_enabled || active_editor_view != editor_view::scene ||
        object_scene_play_mode || !object_editor_context.CanEdit()) return false;

    ReplayEngine::Scene::Scene& scene = active_object_scene();
    ReplayEngine::Core::GameObject* object = nullptr;
    auto* landscape = SelectedLandscape(object_editor_context, scene, object);
    if (landscape == nullptr)
    {
        reset_landscape_editor_state(true);
        object_editor_context.SetStatus("Landscape 編集を終了しました（Landscape の選択が外れました）");
        return false;
    }
    if (ImGui::IsKeyPressed(VK_ESCAPE))
    {
        reset_landscape_editor_state(true);
        object_editor_context.SetStatus("Landscape 編集を終了しました");
        return true;
    }
    if (!landscape_editor_tool.StrokeActive() &&
        (ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyAlt)) return false;
    if (!scene_view_hovered || editor_camera_consumed_input) return false;

    // Scene View is only the interactive/clip rect. The 3D scene underneath is rendered to
    // the full client viewport, so ray creation and world->screen projection must use that
    // full viewport or the brush/edit point drifts farther away toward the edges.
    const float scene_width = scene_view_max_x - scene_view_min_x;
    const float scene_height = scene_view_max_y - scene_view_min_y;
    if (scene_width <= 1.0f || scene_height <= 1.0f) return false;
    const ImVec2 mouse = ImGui::GetMousePos();
    const float local_x = mouse.x - scene_view_min_x;
    const float local_y = mouse.y - scene_view_min_y;
    if (local_x < 0.0f || local_y < 0.0f || local_x > scene_width || local_y > scene_height)
        return false;

    const auto world_ray = viewport_picking_ray(local_x, local_y);
    DirectX::XMFLOAT3 ray_origin{};
    DirectX::XMFLOAT3 ray_direction{};
    if (!ToLocalRay(object->GetTransform(), world_ray, ray_origin, ray_direction)) return false;

    ReplayEngine::Landscape::LandscapeRayHit hit{};
    const bool has_hit = landscape->Data().Raycast(ray_origin, ray_direction,
        1000000.0f, hit);
    const auto& data = landscape->Data();
    const auto view = viewport_view_matrix();
    const auto projection = viewport_projection_matrix();
    POINT client_origin{ 0, 0 };
    ClientToScreen(hwnd, &client_origin);
    const float width = (std::max)(1.0f, static_cast<float>(client_width));
    const float height = (std::max)(1.0f, static_cast<float>(client_height));
    const float projection_min_x = static_cast<float>(client_origin.x);
    const float projection_min_y = static_cast<float>(client_origin.y);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImDrawClipScope landscape_clip(draw,
        ImVec2(scene_view_min_x, scene_view_min_y),
        ImVec2(scene_view_max_x, scene_view_max_y));

    std::uint32_t hover_edge_a = no_vertex;
    std::uint32_t hover_edge_b = no_vertex;
    if (has_hit && landscape_edit_mode == 1 && landscape_topology_selection_mode == 1)
    {
        ClosestFaceEdge(data, hit.face_index, hit.position, hover_edge_a, hover_edge_b);
    }

    // Hover feedback: Sculpt はブラシ円。Topology は Face / Edge 選択を明示する。
    if (has_hit)
    {
        ImVec2 center{};
        if (ProjectToScene(hit.position, object->GetTransform(), view, projection,
            width, height, projection_min_x, projection_min_y, center))
        {
            if (landscape_edit_mode == 0)
            {
                const auto& transform = object->GetTransform();
                const int preview_mode = (std::max)(0,
                    (std::min)(landscape_brush_preview_mode, 4));

                if (preview_mode != 0)
                {
                    DrawBrushFaceInfluence(draw, data, transform, view, projection,
                        width, height, projection_min_x, projection_min_y,
                        hit.position, landscape_brush.radius);
                }

                if (preview_mode == 2 || preview_mode == 4)
                {
                    DrawTerrainGridInBrush(draw, data, transform, view, projection,
                        width, height, projection_min_x, projection_min_y,
                        hit.position, landscape_brush.radius);
                }

                if (preview_mode == 3 || preview_mode == 4)
                {
                    DrawTerrainRing(draw, data, transform, view, projection,
                        width, height, projection_min_x, projection_min_y,
                        hit.position, landscape_brush.radius * 0.25f,
                        IM_COL32(255, 230, 130, 95), 1.0f);
                    DrawTerrainRing(draw, data, transform, view, projection,
                        width, height, projection_min_x, projection_min_y,
                        hit.position, landscape_brush.radius * 0.5f,
                        IM_COL32(255, 220, 100, 140), 1.0f);
                    DrawTerrainRing(draw, data, transform, view, projection,
                        width, height, projection_min_x, projection_min_y,
                        hit.position, landscape_brush.radius * 0.75f,
                        IM_COL32(255, 205, 75, 175), 1.0f);
                }
                else if (preview_mode == 1)
                {
                    DrawTerrainRing(draw, data, transform, view, projection,
                        width, height, projection_min_x, projection_min_y,
                        hit.position, landscape_brush.radius * 0.5f,
                        IM_COL32(255, 230, 135, 135), 1.0f);
                }

                DrawTerrainRing(draw, data, transform, view, projection,
                    width, height, projection_min_x, projection_min_y,
                    hit.position, landscape_brush.radius,
                    IM_COL32(255, 190, 55, 245), 2.0f);

                // 穴/崖/洞窟では XZ terrain ring が途切れても、ブラシ自体は消さない。
                // hit center と同じ投影で screen-space fallback を重ねる。
                ImVec2 radius_point{};
                DirectX::XMFLOAT3 radius_local = hit.position;
                radius_local.x += landscape_brush.radius;
                float radius_pixels = 12.0f;
                if (ProjectToScene(radius_local, transform, view, projection, width, height,
                    projection_min_x, projection_min_y, radius_point))
                {
                    const float dx = radius_point.x - center.x;
                    const float dy = radius_point.y - center.y;
                    radius_pixels = std::sqrt(dx * dx + dy * dy);
                }
                radius_pixels = (std::max)(5.0f, (std::min)(radius_pixels, 4096.0f));
                draw->AddCircle(center, radius_pixels, IM_COL32(255, 210, 80, 150), 64, 1.25f);
                draw->AddCircleFilled(center, 3.0f, IM_COL32(255, 238, 180, 255));

                constexpr float cross = 0.35f;
                DirectX::XMFLOAT3 x0{}, x1{}, z0{}, z1{};
                SampleLandscapeSurfaceAtXZ(data, hit.position.x - cross,
                    hit.position.z, hit.position.y, x0);
                SampleLandscapeSurfaceAtXZ(data, hit.position.x + cross,
                    hit.position.z, hit.position.y, x1);
                SampleLandscapeSurfaceAtXZ(data, hit.position.x,
                    hit.position.z - cross, hit.position.y, z0);
                SampleLandscapeSurfaceAtXZ(data, hit.position.x,
                    hit.position.z + cross, hit.position.y, z1);
                x0.y += 0.06f; x1.y += 0.06f; z0.y += 0.06f; z1.y += 0.06f;
                DrawProjectedLine(draw, transform, view, projection, width, height,
                    projection_min_x, projection_min_y, x0, x1,
                    IM_COL32(255, 235, 160, 255), 2.0f);
                DrawProjectedLine(draw, transform, view, projection, width, height,
                    projection_min_x, projection_min_y, z0, z1,
                    IM_COL32(255, 235, 160, 255), 2.0f);

                char radius_text[32]{};
                std::snprintf(radius_text, sizeof(radius_text),
                    "R %.1f local", landscape_brush.radius);
                draw->AddText({ center.x + 8.0f, center.y + 8.0f },
                    IM_COL32(255, 238, 180, 245), radius_text);
            }
            else if (landscape_topology_selection_mode == 0)
            {
                const std::size_t offset = hit.face_index * 3;
                if (offset + 2 < data.Indices().size())
                {
                    ImVec2 screen[3]{};
                    bool valid = true;
                    for (int i = 0; i < 3; ++i)
                    {
                        const std::uint32_t vertex = data.Indices()[offset + i];
                        if (vertex >= data.Vertices().size() ||
                            !ProjectToScene(data.Vertices()[vertex].position,
                                object->GetTransform(), view, projection, width, height,
                                projection_min_x, projection_min_y, screen[i]))
                        {
                            valid = false;
                            break;
                        }
                    }
                    if (valid)
                    {
                        const ImU32 color = hit.face_index == landscape_selected_face
                            ? IM_COL32(255, 210, 55, 255)
                            : IM_COL32(80, 210, 255, 235);
                        draw->AddTriangle(screen[0], screen[1], screen[2], color, 2.0f);
                    }
                }
            }
            else if (hover_edge_a != no_vertex && hover_edge_b != no_vertex &&
                hover_edge_a < data.Vertices().size() && hover_edge_b < data.Vertices().size())
            {
                ImVec2 a{}, b{};
                if (ProjectToScene(data.Vertices()[hover_edge_a].position,
                    object->GetTransform(), view, projection, width, height,
                    projection_min_x, projection_min_y, a) &&
                    ProjectToScene(data.Vertices()[hover_edge_b].position,
                        object->GetTransform(), view, projection, width, height,
                        projection_min_x, projection_min_y, b))
                {
                    draw->AddLine(a, b, IM_COL32(80, 220, 255, 255), 4.0f);
                }
            }
        }
    }

    // Edge Bridge の選択は hover face が変わっても常時見えるよう persistent highlight。
    if (landscape_edit_mode == 1 && landscape_topology_selection_mode == 1)
    {
        const auto draw_selected_edge = [&](std::uint32_t a_index, std::uint32_t b_index,
            ImU32 color)
        {
            if (a_index == no_vertex || b_index == no_vertex ||
                a_index >= data.Vertices().size() || b_index >= data.Vertices().size()) return;
            ImVec2 a{}, b{};
            if (!ProjectToScene(data.Vertices()[a_index].position,
                object->GetTransform(), view, projection, width, height,
                projection_min_x, projection_min_y, a) ||
                !ProjectToScene(data.Vertices()[b_index].position,
                    object->GetTransform(), view, projection, width, height,
                    projection_min_x, projection_min_y, b)) return;
            draw->AddLine(a, b, color, 5.0f);
            draw->AddCircleFilled(a, 3.5f, color);
            draw->AddCircleFilled(b, 3.5f, color);
        };
        draw_selected_edge(landscape_bridge_a0, landscape_bridge_a1,
            IM_COL32(255, 205, 55, 255));
        draw_selected_edge(landscape_bridge_b0, landscape_bridge_b1,
            IM_COL32(255, 115, 80, 255));
    }

    if (landscape_edit_mode == 1)
    {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            if (landscape_topology_selection_mode == 0)
            {
                landscape_selected_face = has_hit ? hit.face_index : no_face;
                if (has_hit) object_editor_context.SetStatus("Landscape Face を選択しました");
            }
            else if (hover_edge_a != no_vertex && hover_edge_b != no_vertex)
            {
                // 2本揃った後の次クリックは新しい1本目として選び直す。
                const bool first_valid = landscape_bridge_a0 != no_vertex && landscape_bridge_a1 != no_vertex;
                const bool second_valid = landscape_bridge_b0 != no_vertex && landscape_bridge_b1 != no_vertex;
                if (!first_valid || second_valid)
                {
                    landscape_bridge_a0 = hover_edge_a;
                    landscape_bridge_a1 = hover_edge_b;
                    landscape_bridge_b0 = landscape_bridge_b1 = no_vertex;
                    object_editor_context.SetStatus("Landscape Bridge: 1本目のEdgeを選択しました");
                }
                else if (landscape_bridge_a0 == hover_edge_a && landscape_bridge_a1 == hover_edge_b)
                {
                    object_editor_context.SetStatus("同じEdgeです。離れた2本目のEdgeを選択してください");
                }
                else
                {
                    landscape_bridge_b0 = hover_edge_a;
                    landscape_bridge_b1 = hover_edge_b;
                    object_editor_context.SetStatus("Landscape Bridge: 2本目のEdgeを選択しました");
                }
            }
            else if (!has_hit)
            {
                landscape_selected_face = no_face;
            }
            return true;
        }
        return false;
    }

    // Sculpt はクリック開始からリリースまで 1 Undo transaction にまとめる。
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && has_hit &&
        !landscape_editor_tool.StrokeActive())
    {
        landscape_brush_mode = (std::max)(0, (std::min)(landscape_brush_mode, 4));
        object_editor_context.BeginEdit("Landscape Sculpt");
        landscape_stroke_transaction = landscape_editor_tool.BeginStroke(
            landscape->Data(),
            static_cast<ReplayEngine::Landscape::LandscapeBrushMode>(landscape_brush_mode),
            landscape_brush);
        if (!landscape_stroke_transaction)
        {
            object_editor_context.CancelEdit();
        }
        else if (auto* collider = object->GetComponent<
            ReplayEngine::Components::LandscapeColliderComponent>())
        {
            // Sculpt 中は古い collision を維持し、release 後に 1 回だけ recook。
            collider->BeginInteractiveEdit();
        }
    }

    if (landscape_editor_tool.StrokeActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        if (has_hit)
        {
            const float dt = (std::max)(1.0f / 240.0f,
                (std::min)(ImGui::GetIO().DeltaTime, 1.0f / 15.0f));
            landscape_editor_tool.ApplySample(hit.position, dt);
        }
        return true;
    }
    return false;
#else
    return false;
#endif
}
