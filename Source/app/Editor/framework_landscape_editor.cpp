// Landscape editor の責務を 3 つのファイルへ分けている:
//   framework_landscape_editor.cpp           … Toolbar と編集状態のリセット（このファイル）
//   framework_landscape_editor_viewport.cpp … Scene View の Raycast・Hover・編集操作
//   framework_landscape_editorInternal.h     … 分割後の Landscape helper 共通部
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
#include <vector>

#include "framework_landscape_editorInternal.h"

using namespace framework_landscape_editor_detail;

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
            u8"地形の描画機能が無いため表示されません。");
        if (object_editor_context.CanEdit())
        {
            ImGui::SameLine();
            if (ImGui::SmallButton(u8"描画機能を追加"))
            {
                object_editor_context.BeginEdit(u8"地形の描画機能を追加");
                if (object->AddComponent<ReplayEngine::Components::LandscapeRendererComponent>() != nullptr)
                {
                    object_editor_context.CommitEdit();
                    object_editor_context.SetStatus(u8"地形の描画機能を追加しました");
                }
                else object_editor_context.CancelEdit();
            }
            ReplayEngine::Editor::EditorHelp::Item("button.landscape.add_renderer");
        }
    }
    if (landscape_collider == nullptr)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.30f, 1.0f),
            u8"地形の衝突判定機能が無いため当たり判定はありません。");
        if (object_editor_context.CanEdit())
        {
            ImGui::SameLine();
            if (ImGui::SmallButton(u8"衝突判定を追加"))
            {
                object_editor_context.BeginEdit(u8"地形の衝突判定を追加");
                if (object->AddComponent<ReplayEngine::Components::LandscapeColliderComponent>() != nullptr)
                {
                    object_editor_context.CommitEdit();
                    object_editor_context.SetStatus(u8"地形の衝突判定を追加しました");
                }
                else object_editor_context.CancelEdit();
            }
            ReplayEngine::Editor::EditorHelp::Item("button.landscape.add_collider");
        }
    }
    if (primitive_renderer != nullptr && primitive_renderer->visible)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.60f, 0.22f, 1.0f),
            u8"基本形状と地形が同時表示されています。見た目が重なる可能性があります。");
        if (object_editor_context.CanEdit())
        {
            ImGui::SameLine();
            if (ImGui::SmallButton(u8"基本形状を非表示"))
            {
                object_editor_context.BeginEdit(u8"基本形状を非表示");
                primitive_renderer->visible = false;
                primitive_renderer->OnPropertyChanged("visible");
                object_editor_context.CommitEdit();
                object_editor_context.SetStatus(u8"基本形状を非表示にしました。地形はそのままです");
            }
            ReplayEngine::Editor::EditorHelp::Item("button.landscape.disable_primitive");
        }
    }

    ImGui::Checkbox(u8"地形を編集", &landscape_edit_enabled);
    if (!landscape_edit_enabled)
    {
        ImGui::SameLine();
        ImGui::TextDisabled(u8"地形を通常のオブジェクトとして選択中");
        ImGui::PopID();
        return;
    }

    const char* edit_modes[] = { u8"スカルプト", u8"トポロジー" };
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130.0f);
    ImGui::Combo(u8"##LandscapeMode", &landscape_edit_mode, edit_modes, IM_ARRAYSIZE(edit_modes));

    auto& data = landscape->Data();
    ImGui::SameLine();
    ImGui::TextDisabled(u8"頂点:%zu  面:%zu", data.VertexCount(), data.FaceCount());

    if (landscape_edit_mode == 0)
    {
        const char* brush_modes[] = {
            u8"盛り上げる", u8"掘り下げる", u8"なめらかにする", u8"平らにする", u8"でこぼこにする"
        };
        ImGui::SetNextItemWidth(110.0f);
        ImGui::Combo(u8"ブラシ", &landscape_brush_mode,
            brush_modes, IM_ARRAYSIZE(brush_modes));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::DragFloat(u8"ブラシの半径", &landscape_brush.radius, 0.1f, 0.1f, 256.0f, "%.2f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::DragFloat(u8"強さ", &landscape_brush.strength, 0.05f, 0.0f, 100.0f, "%.2f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        ImGui::DragFloat(u8"減衰（ふちのなだらかさ）", &landscape_brush.falloff, 0.02f, 0.0f, 1.0f, "%.2f");
        ImGui::SameLine();
        const char* preview_modes[] = {
            u8"外周のみ", u8"減衰も表示", u8"格子", u8"等高線", u8"格子と等高線"
        };
        landscape_brush_preview_mode = (std::max)(0,
            (std::min)(landscape_brush_preview_mode,
                static_cast<int>(IM_ARRAYSIZE(preview_modes)) - 1));
        ImGui::SetNextItemWidth(130.0f);
        ImGui::Combo(u8"ブラシの見え方", &landscape_brush_preview_mode,
            preview_modes, IM_ARRAYSIZE(preview_modes));

        int direction = static_cast<int>(landscape_brush.direction);
        const char* directions[] = { u8"真上へ（ローカル Y）", u8"面の向きへ（法線）" };
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
            ImGui::DragFloat(u8"平らにする高さ", &landscape_brush.flatten_height, 0.05f);
        }
        if (landscape_brush_mode == static_cast<int>(ReplayEngine::Landscape::LandscapeBrushMode::Noise))
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            ImGui::DragFloat(u8"でこぼこの細かさ", &landscape_brush.noise_scale,
                0.01f, 0.001f, 100.0f, "%.3f");
        }

        constexpr std::size_t maximum_brush_subdivide_faces = 256;
        const std::size_t subdivision_capacity = (std::min)({
            maximum_brush_subdivide_faces,
            (ReplayEngine::Landscape::LandscapeData::maximum_vertices - data.VertexCount()) / 3,
            (ReplayEngine::Landscape::LandscapeData::maximum_indices - data.Indices().size()) / 9 });
        const bool can_subdivide_brush = object_editor_context.CanEdit() &&
            !landscape_editor_tool.StrokeActive() && landscape_brush_hover_valid &&
            landscape_brush_hover_object == object->ID() && subdivision_capacity > 0;
        {
            DisabledScope disabled(!can_subdivide_brush);
            if (ImGui::Button(u8"ブラシ範囲を細かくする") && can_subdivide_brush)
            {
                std::vector<std::pair<float, std::size_t>> faces;
                const float radius_sq = landscape_brush.radius * landscape_brush.radius;
                for (std::size_t face = 0; face < data.FaceCount(); ++face)
                {
                    float nearest_sq = (std::numeric_limits<float>::max)();
                    const std::size_t offset = face * 3;
                    for (int corner = 0; corner < 3; ++corner)
                    {
                        const auto& position = data.Vertices()[data.Indices()[offset + corner]].position;
                        const float dx = position.x - landscape_brush_hover_position.x;
                        const float dy = position.y - landscape_brush_hover_position.y;
                        const float dz = position.z - landscape_brush_hover_position.z;
                        const float distance_sq = landscape_brush.direction ==
                            ReplayEngine::Landscape::LandscapeSculptDirection::LocalY
                            ? dx * dx + dz * dz : dx * dx + dy * dy + dz * dz;
                        nearest_sq = (std::min)(nearest_sq, distance_sq);
                    }
                    if (face == landscape_brush_hover_face) nearest_sq = -1.0f;
                    if (nearest_sq <= radius_sq) faces.emplace_back(nearest_sq, face);
                }

                const std::size_t found_faces = faces.size();
                if (faces.size() > subdivision_capacity)
                {
                    std::nth_element(faces.begin(), faces.begin() + subdivision_capacity, faces.end());
                    faces.resize(subdivision_capacity);
                }
                std::sort(faces.begin(), faces.end(), [](const auto& left, const auto& right)
                { return left.second < right.second; });

                if (faces.empty())
                {
                    object_editor_context.SetStatus(u8"ブラシ範囲に分割できる面がありません");
                }
                else
                {
                    object_editor_context.BeginEdit(u8"地形のブラシ範囲を分割");
                    data.BeginTopologyBatch();
                    bool succeeded = true;
                    for (const auto& face : faces)
                        if (!data.SubdivideFace(face.second)) { succeeded = false; break; }
                    data.EndTopologyBatch();
                    if (succeeded)
                    {
                        object_editor_context.CommitEdit();
                        std::string status = u8"ブラシ範囲の面を " +
                            std::to_string(faces.size()) + u8" 枚分割しました";
                        if (found_faces > faces.size()) status += u8"（1回の上限まで）";
                        object_editor_context.SetStatus(status);
                        landscape_brush_hover_valid = false;
                    }
                    else
                    {
                        object_editor_context.CancelEdit();
                        object_editor_context.SetStatus(u8"ブラシ範囲の分割に失敗しました");
                    }
                }
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled(u8"1回につき最大256面。カーソルを地形に合わせて押します");
        ImGui::TextDisabled(u8"左ドラッグ: 編集 | Ctrl/Alt+左クリック: 通常選択 | Esc: 地形編集を終了 | 値は地形内の座標");
    }
    else
    {
        const char* selection_modes[] = { u8"面", u8"辺をつなぐ" };
        ImGui::SetNextItemWidth(130.0f);
        ImGui::Combo(u8"選択", &landscape_topology_selection_mode,
            selection_modes, IM_ARRAYSIZE(selection_modes));

        if (landscape_topology_selection_mode == 0)
        {
            const bool face_selected = landscape_selected_face < data.FaceCount();
            if (face_selected)
                ImGui::TextDisabled(u8"選択中の面: %zu", landscape_selected_face);
            else
                ImGui::TextDisabled(u8"シーン画面で面をクリックして選択");

            const auto edit = [&](const char* label, const char* history_label,
                const char* help_key, auto&& operation)
            {
                const bool enabled = face_selected && object_editor_context.CanEdit();
                DisabledScope disabled(!enabled);
                const bool clicked = ImGui::Button(label);
                ReplayEngine::Editor::EditorHelp::Item(help_key);
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
                    object_editor_context.SetStatus(std::string(history_label) + u8" に失敗しました");
                }
            };

            edit(u8"面を分割", u8"地形の面を分割",
                "button.landscape.operation.Landscape Face を分割",
                [&] { return data.SubdivideFace(landscape_selected_face); });
            ImGui::SameLine();
            ImGui::SetNextItemWidth(85.0f);
            ImGui::DragFloat("##ExtrudeDistance", &landscape_extrude_distance, 0.05f, -100.0f, 100.0f, "%.2f");
            ImGui::SameLine();
            edit(u8"面を押し出す", u8"地形の面を押し出し",
                "button.landscape.operation.Landscape Face を押し出し", [&] {
                return data.ExtrudeFace(landscape_selected_face, landscape_extrude_distance);
            });
            ImGui::SameLine();
            ImGui::SetNextItemWidth(75.0f);
            ImGui::DragFloat("##InsetAmount", &landscape_inset_amount, 0.01f, 0.01f, 0.95f, "%.2f");
            ImGui::SameLine();
            edit(u8"面の内側を分ける", u8"地形の面の内側を分ける",
                "button.landscape.operation.Landscape Face をInset", [&] {
                return data.InsetFace(landscape_selected_face, landscape_inset_amount);
            });
            ImGui::SameLine();
            edit(u8"面を消して穴を開ける", u8"地形に穴を開ける",
                "button.landscape.operation.Landscape に穴を開ける", [&] {
                return data.DeleteFace(landscape_selected_face);
            });

            ImGui::SetNextItemWidth(90.0f);
            ImGui::DragFloat(u8"トンネルの深さ", &landscape_tunnel_depth, 0.1f, 0.1f, 500.0f, "%.1f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            ImGui::DragInt(u8"分割", &landscape_tunnel_segments, 0.1f, 1, 64);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            ImGui::DragFloat(u8"奥の広さ", &landscape_tunnel_end_scale, 0.01f, 0.05f, 4.0f, "%.2f");
            ImGui::SameLine();
            edit(u8"洞窟・トンネルを作る", u8"地形にトンネルを作る",
                "button.landscape.operation.Landscape Tunnel を生成", [&] {
                return data.CreateTunnelFromFace(landscape_selected_face,
                    landscape_tunnel_depth, landscape_tunnel_segments,
                    landscape_tunnel_end_scale);
            });
            ImGui::TextDisabled(u8"穴開け・押し出し・トンネルでは、張り出しや同じ位置の床と天井も作れます");
        }
        else
        {
            const bool first = landscape_bridge_a0 != no_vertex && landscape_bridge_a1 != no_vertex;
            const bool second = landscape_bridge_b0 != no_vertex && landscape_bridge_b1 != no_vertex;
            if (first)
                ImGui::TextDisabled(u8"1本目の辺: %u-%u", landscape_bridge_a0, landscape_bridge_a1);
            else
                ImGui::TextDisabled(u8"シーン画面で1本目の辺を選択");
            if (second)
                ImGui::TextDisabled(u8"2本目の辺: %u-%u", landscape_bridge_b0, landscape_bridge_b1);
            else if (first)
                ImGui::TextDisabled(u8"次に離れた2本目の辺を選択");

            if (ImGui::Button(u8"辺の選択を消す"))
            {
                landscape_bridge_a0 = landscape_bridge_a1 = no_vertex;
                landscape_bridge_b0 = landscape_bridge_b1 = no_vertex;
            }
            ReplayEngine::Editor::EditorHelp::Item("button.landscape.clear_edges");
            ImGui::SameLine();
            const bool can_bridge = first && second && object_editor_context.CanEdit();
            DisabledScope disabled(!can_bridge);
            const bool bridge_clicked = ImGui::Button(u8"2本の辺をつなぐ");
            ReplayEngine::Editor::EditorHelp::Item("button.landscape.bridge_edges");
            if (bridge_clicked && can_bridge)
            {
                object_editor_context.BeginEdit(u8"地形の辺をつなぐ");
                if (data.BridgeEdges(landscape_bridge_a0, landscape_bridge_a1,
                    landscape_bridge_b0, landscape_bridge_b1))
                {
                    object_editor_context.CommitEdit();
                    landscape_bridge_a0 = landscape_bridge_a1 = no_vertex;
                    landscape_bridge_b0 = landscape_bridge_b1 = no_vertex;
                    object_editor_context.SetStatus(u8"地形の辺をつなぎました");
                }
                else
                {
                    object_editor_context.CancelEdit();
                    object_editor_context.SetStatus(u8"辺をつなげませんでした。頂点を共有しない2本の辺を選んでください");
                }
            }
            ImGui::TextDisabled(u8"離れた2本の辺を2枚の三角形でつなぎ、洞窟の入口・アーチ・継ぎ目を作ります");
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
    std::unique_ptr<ReplayEngine::Landscape::LandscapeUndoCommand> command;
    if (landscape_editor_tool.StrokeActive())
    {
        if (rollback_stroke) landscape_editor_tool.CancelStroke();
        else command = landscape_editor_tool.EndStroke();
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
        if (!rollback_stroke && command != nullptr) object_editor_context.CommitLandscapeEdit(
            landscape_stroke_object, std::move(command));
        landscape_stroke_transaction = false;
        landscape_stroke_object = ReplayEngine::Core::ObjectID::Invalid();
    }
    landscape_edit_enabled = false;
    landscape_brush_hover_valid = false;
    landscape_brush_hover_face = no_face;
    landscape_brush_hover_object = ReplayEngine::Core::ObjectID::Invalid();
    landscape_selected_face = no_face;
    landscape_bridge_a0 = landscape_bridge_a1 = no_vertex;
    landscape_bridge_b0 = landscape_bridge_b1 = no_vertex;
#else
    (void)rollback_stroke;
#endif
}
