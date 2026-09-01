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
#include <string>

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
            ReplayEngine::Editor::EditorHelp::Item("button.landscape.add_renderer");
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
            ReplayEngine::Editor::EditorHelp::Item("button.landscape.add_collider");
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
            ReplayEngine::Editor::EditorHelp::Item("button.landscape.disable_primitive");
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
                const std::string help_key = std::string("button.landscape.operation.") +
                    history_label;
                ReplayEngine::Editor::EditorHelp::Item(help_key.c_str());
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
            ReplayEngine::Editor::EditorHelp::Item("button.landscape.clear_edges");
            ImGui::SameLine();
            const bool can_bridge = first && second && object_editor_context.CanEdit();
            DisabledScope disabled(!can_bridge);
            const bool bridge_clicked = ImGui::Button("Bridge");
            ReplayEngine::Editor::EditorHelp::Item("button.landscape.bridge_edges");
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
