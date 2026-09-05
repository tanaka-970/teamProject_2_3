// Landscape editor のうち「Scene View の Raycast・Hover・編集操作」だけを持つ。
//
// Toolbar と編集状態のリセットは framework_landscape_editor.cpp に置く。

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
#include <memory>
#include <string>
#include <utility>

#include "framework_landscape_editorInternal.h"

using namespace framework_landscape_editor_detail;

bool framework::handle_landscape_viewport_edit()
{
#ifdef USE_IMGUI
    // Stroke の終了だけは Viewport の外へ出ても受け取る。
    if (landscape_stroke_transaction && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const bool subdivide_stroke = !landscape_editor_tool.StrokeActive();
        std::unique_ptr<ReplayEngine::Landscape::LandscapeUndoCommand> command;
        if (!subdivide_stroke) command = landscape_editor_tool.EndStroke();

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

        if (subdivide_stroke)
        {
            if (landscape_subdivide_stroke_changed) object_editor_context.CommitEdit();
            else object_editor_context.CancelEdit();
        }
        else
        {
            if (command != nullptr) object_editor_context.CommitLandscapeEdit(
                landscape_stroke_object, std::move(command));
        }
        landscape_stroke_transaction = false;
        landscape_subdivide_stroke_changed = false;
        landscape_stroke_object = ReplayEngine::Core::ObjectID::Invalid();
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
    if (!landscape_stroke_transaction &&
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

    // ブラシ操作はクリック開始からリリースまで一つの取り消し履歴にまとめる。
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && has_hit &&
        !landscape_stroke_transaction)
    {
        landscape_brush_mode = (std::max)(0, (std::min)(landscape_brush_mode, 5));
        const auto brush_mode = static_cast<ReplayEngine::Landscape::LandscapeBrushMode>(
            landscape_brush_mode);
        if (brush_mode == ReplayEngine::Landscape::LandscapeBrushMode::Subdivide)
        {
            object_editor_context.BeginEdit(u8"地形をブラシで細かくする");
            landscape_stroke_transaction = true;
            landscape_subdivide_stroke_changed = false;
        }
        else
        {
            landscape_stroke_transaction = landscape_editor_tool.BeginStroke(
                landscape->Data(), brush_mode, landscape_brush);
        }
        if (!landscape_stroke_transaction)
        {
            landscape_stroke_object = ReplayEngine::Core::ObjectID::Invalid();
        }
        else
        {
            landscape_stroke_object = object->ID();
            if (auto* collider = object->GetComponent<
                ReplayEngine::Components::LandscapeColliderComponent>())
            {
                // ブラシ中は古い衝突形状を維持し、リリース後に一度だけ再構築する。
                collider->BeginInteractiveEdit();
            }
        }
    }

    if (landscape_stroke_transaction && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        if (has_hit)
        {
            {
                REPLAY_PROFILE_SCOPE("Landscape/Brush");
                if (landscape_editor_tool.StrokeActive())
                {
                    const float dt = (std::max)(1.0f / 240.0f,
                        (std::min)(ImGui::GetIO().DeltaTime, 1.0f / 15.0f));
                    landscape_editor_tool.ApplySample(hit.position, dt);
                }
                else landscape_subdivide_stroke_changed =
                    ReplayEngine::Landscape::LandscapeEditorTool::ApplySubdivideSample(
                        landscape->Data(), hit.position, hit.face_index, landscape_brush) ||
                    landscape_subdivide_stroke_changed;
            }
        }
        return true;
    }
    return false;
#else
    return false;
#endif
}
