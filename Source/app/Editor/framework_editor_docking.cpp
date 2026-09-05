// Editor のうち「DockSpace 構築と全体パネルのオーケストレーション」を持つ。
// draw_editor の関数本体は変更せず、責務単位でこの翻訳単位へ移動している。
#include "framework.h"
#include "../../RePlayEngine/Editor/Style/EditorStyle.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <string>

void framework::draw_editor()
{
    editor_session_active = true;

    // 検索欄の状態はここで毎フレーム倒し、実際に描かれたときだけ立て直す。
    //
    // 立てたまま欄が描かれなくなると二度と更新されず、
    // EditorCameraController の ui_text_input_active が立ちっぱなしになって
    // ズーム / Pan / Orbit / Fly がすべて効かなくなる。
    // 欄が消える経路は 2 つあり、どちらも実際に起きる:
    //   ・ツールバーの幅が足りず検索欄自体を描かない
    //   ・メニューバーが出ずツールバーごと呼ばれない
    // draw_scene_view_panel() が scene_view_hovered でやっているのと同じ作法。
    search_input_active = false;
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_MenuBar;
    ImGui::Begin("RePlay Editor DockSpace", nullptr, host_flags);
    ImGui::PopStyleVar(3);

    if (ImGui::BeginMenuBar())
    {
        draw_editor_main_menu();
        ImGui::Separator();
        draw_editor_toolbar();
        draw_runtime_mode_banner();
        ImGui::EndMenuBar();
    }

    const ImGuiID dockspace_id = ImGui::GetID("RePlayEditorDockSpaceJP2");
    // Scene View は central node の矩形へ透明ウィンドウとして重ねる。
    // ここに別パネルを Dock すると同じ座標で描かれて文字が読めなくなるため、
    // central node は全 Workspace で Scene View 専用に空ける。
    const ImGuiDockNodeFlags dockspace_flags =
        ImGuiDockNodeFlags_PassthruCentralNode |
        ImGuiDockNodeFlags_NoDockingInCentralNode;
    ImGui::DockSpace(dockspace_id, ImVec2(0, 0), dockspace_flags);
    if (!editor_layout_checked || editor_layout_dirty)
    {
        editor_layout_checked = true;
        const bool rebuild = editor_layout_dirty;
        editor_layout_dirty = false;
        ImGuiDockNode* root = ImGui::DockBuilderGetNode(dockspace_id);
        if (rebuild || !root || !root->IsSplitNode())
        {
            ImGui::DockBuilderRemoveNode(dockspace_id);
            ImGui::DockBuilderAddNode(dockspace_id,
                ImGuiDockNodeFlags_DockSpace | dockspace_flags);
            ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);
            ImGuiID center = dockspace_id;
            float left_ratio = 0.20f;
            float right_ratio = 0.27f;
            float bottom_ratio = 0.27f;
            if (active_editor_workspace == editor_workspace::placement) right_ratio = 0.36f;
            if (active_editor_workspace == editor_workspace::modeling) right_ratio = 0.33f;
            if (active_editor_workspace == editor_workspace::animation) bottom_ratio = 0.36f;
            if (active_editor_workspace == editor_workspace::rendering) left_ratio = 0.16f;
            if (active_editor_workspace == editor_workspace::ui)
            {
                left_ratio = 0.18f;
                right_ratio = 0.30f;
                bottom_ratio = 0.20f;
            }
            if (active_editor_workspace == editor_workspace::motion)
            {
                left_ratio = 0.16f;
                right_ratio = 0.26f;
                bottom_ratio = 0.40f;
            }
            if (active_editor_workspace == editor_workspace::shader_adjustment)
            {
                left_ratio = 0.16f;
                right_ratio = 0.40f;
                bottom_ratio = 0.20f;
            }
            ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, left_ratio, nullptr, &center);
            ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, right_ratio, nullptr, &center);
            ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, bottom_ratio, nullptr, &center);
            if (active_editor_workspace == editor_workspace::ui)
            {
                ImGui::DockBuilderDockWindow("UI 階層", left);
                ImGui::DockBuilderDockWindow("UI インスペクター", right);
                ImGui::DockBuilderDockWindow("プロジェクト", bottom);
                ImGui::DockBuilderDockWindow("コンソール", bottom);
            }
            else if (active_editor_workspace == editor_workspace::motion)
            {
                ImGui::DockBuilderDockWindow("Motion レイヤー", left);
                ImGui::DockBuilderDockWindow(u8"Motion リグ", left);
                ImGui::DockBuilderDockWindow("階層", left);
                ImGui::DockBuilderDockWindow("Motion インスペクター", right);
                ImGui::DockBuilderDockWindow(u8"イージングカーブ###EasingCurveEditor", right);
                // Motion Workspace でも central node には Dock しない。
                // 3D 表示は draw_scene_view_panel() がこの空き領域へ配置する。
                ImGui::DockBuilderDockWindow("タイムライン", bottom);
                ImGui::DockBuilderDockWindow("グラフエディター", bottom);
                ImGui::DockBuilderDockWindow("Motion プレビュー", bottom);
                ImGui::DockBuilderDockWindow("プロジェクト", bottom);
                ImGui::DockBuilderDockWindow("コンソール", bottom);
            }
            else
            {
                ImGui::DockBuilderDockWindow("階層", left);
                ImGui::DockBuilderDockWindow("インスペクター", right);
                ImGui::DockBuilderDockWindow("プロジェクト", bottom);
                ImGui::DockBuilderDockWindow("コンソール", bottom);
                ImGui::DockBuilderDockWindow("ワークスペース", bottom);
                ImGui::DockBuilderDockWindow("Validation & Diagnostics", bottom);
            }
            ImGui::DockBuilderFinish(dockspace_id);
            if (ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dockspace_id))
            {
                if (central->Windows.Size > 0)
                {
                    push_editor_log("Warning",
                        "Editor layout: central node に Dock されたウィンドウがあります。Scene View と重なる可能性があります。");
                }
            }
            editor_layout_saved_version = editor_layout_version;
            save_editor_session();
        }
    }
    if (ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dockspace_id))
    {
        scene_view_overlay_position = central->Pos;
        scene_view_overlay_size = central->Size;
        scene_view_overlay_valid = central->Size.x > 1.0f && central->Size.y > 1.0f;
    }
    else
    {
        scene_view_overlay_valid = false;
    }
    ImGui::End();

    draw_object_scene_recovery_prompt();
    draw_unsaved_object_scene_prompt();
    draw_export_game_dialog();
    draw_sprite_atlas_editor();
    draw_easing_editor();

    if (active_editor_workspace == editor_workspace::ui)
    {
        draw_scene_view_panel();
        draw_ui_hierarchy();
        draw_ui_inspector();
        if (show_project_panel) draw_project_panel();
        if (show_console_panel) draw_console_panel();
        draw_search_results();
        draw_ui_focus_style_manager();
        if (active_editor_view == editor_view::scene) handle_viewport_selection();
        draw_collider_debug_overlay();
        draw_dx12_debug_panel();
        return;
    }

    if (active_editor_workspace == editor_workspace::motion)
    {
        // Motionショートカットは文字入力中に発火させない。
        const ImGuiIO& motion_io = ImGui::GetIO();
        if (!motion_io.WantTextInput)
        {
            if (!motion_io.KeyCtrl && !motion_io.KeyAlt &&
                ImGui::IsKeyPressed('S', false))
                add_motion_key_at_preview_time();
            if (!motion_io.KeyCtrl && !motion_io.KeyShift && !motion_io.KeyAlt &&
                ImGui::IsKeyPressed(VK_DELETE, false))
                delete_motion_keys();
            if (motion_io.KeyCtrl && !motion_io.KeyShift && !motion_io.KeyAlt &&
                ImGui::IsKeyPressed('D', false))
                duplicate_motion_keys();
            if (motion_io.KeyCtrl && !motion_io.KeyShift && !motion_io.KeyAlt &&
                ImGui::IsKeyPressed('C', false))
                copy_motion_keys();
            if (motion_io.KeyCtrl && !motion_io.KeyShift && !motion_io.KeyAlt &&
                ImGui::IsKeyPressed('V', false))
                paste_motion_keys();
            if (!motion_io.KeyCtrl && !motion_io.KeyShift && !motion_io.KeyAlt &&
                ImGui::IsKeyPressed(VK_SPACE, false))
                toggle_motion_preview_playback();
            if (!motion_io.KeyCtrl && !motion_io.KeyShift && !motion_io.KeyAlt &&
                ImGui::IsKeyPressed(VK_HOME, false))
                seek_motion_preview_time(0.0f);
            if (!motion_io.KeyCtrl && !motion_io.KeyShift && !motion_io.KeyAlt &&
                ImGui::IsKeyPressed(VK_END, false))
                seek_motion_preview_time(motion_editor_loaded
                    ? motion_editor_asset.duration : motion_editor_composition.duration);
            if (!motion_io.KeyCtrl && !motion_io.KeyShift && !motion_io.KeyAlt &&
                ImGui::IsKeyPressed(VK_NEXT, false))
                step_motion_preview_frames(1);
            if (!motion_io.KeyCtrl && !motion_io.KeyShift && !motion_io.KeyAlt &&
                ImGui::IsKeyPressed(VK_PRIOR, false))
                step_motion_preview_frames(-1);
            if (!motion_io.KeyCtrl && !motion_io.KeyShift && !motion_io.KeyAlt &&
                ImGui::IsKeyPressed(VK_F9, false))
            {
                const ReplayEngine::Assets::AssetRecord* preset_record =
                    motion_selected_easing_curve.IsAssigned()
                    ? asset_database.FindByGuid(motion_selected_easing_curve.guid) : nullptr;
                const ReplayEngine::Motion::EasingCurveAsset* preset_curve =
                    preset_record != nullptr &&
                    preset_record->kind == ReplayEngine::Assets::AssetKind::EasingCurve
                    ? ReplayEngine::Motion::EasingCurveAsset::Resolve(
                        &asset_database, motion_selected_easing_curve)
                    : nullptr;
                if (preset_curve != nullptr)
                {
                    apply_motion_easing_to_selection(
                        ReplayEngine::Motion::MotionEasing::PresetCurve,
                        &motion_selected_easing_curve);
                }
                else
                {
                    apply_motion_easing_to_selection(
                        ReplayEngine::Motion::MotionEasing::EaseInOutCubic);
                }
            }
        }
        draw_scene_view_panel();
        if (show_hierarchy_panel) draw_scene_hierarchy();
        draw_motion_layers();
        draw_motion_preview();
        draw_motion_inspector();
        draw_motion_timeline();
        draw_motion_graph_editor();
        draw_motion_rig();
        if (show_project_panel) draw_project_panel();
        if (show_console_panel) draw_console_panel();
        draw_search_results();
        draw_ui_focus_style_manager();
        if (active_editor_view == editor_view::scene) handle_viewport_selection();
        draw_collider_debug_overlay();
        draw_dx12_debug_panel();
        return;
    }

    draw_scene_view_panel();
    if (show_hierarchy_panel) draw_scene_hierarchy();
    if (show_inspector_panel) draw_inspector();
    if (show_project_panel) draw_project_panel();
    if (show_console_panel) draw_console_panel();
    if (show_workspace_panel) draw_workspace_panel();
    draw_scene_notes_panel();
    draw_scene_flow_panel();
    draw_editor_camera_preset_manager();
    draw_ui_focus_style_manager();
    draw_shader_catalog_panel();
    {
        shader_composer_editor.Draw(content_root_path(), shader_library, asset_database);
    }
    draw_golden_panel();
    if (show_validation_panel)
        object_validation_panel.Draw(object_editor_context, &asset_database,
            &object_collision_world, object_render_items.Size());
    draw_collision_diagnostics_panel();
    draw_dx12_debug_panel();
    draw_search_results();
    if (active_editor_view == editor_view::scene) handle_viewport_selection();

    // Collider の可視化は最後に描く。
    // 背景の描画リストへ積むので、パネルの下に隠れず、
    // かつパネルの上へも被らない。
    draw_collider_debug_overlay();
}
