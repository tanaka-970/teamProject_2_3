// Editor の中核処理のうち「Workspace 状態・メニュー・全体描画の前半」を持つ。
//
//   framework_editor.cpp                    … Workspace 切替、値、メニュー（このファイル）
//   framework_editor_docking.cpp            … DockSpace 構築と全体パネルのオーケストレーション
//   framework_editor_scene.cpp              … Scene View 前半（Prompt / Toolbar）
//   framework_editor_scene_view.cpp          … Scene View 後半（View / Search / Hierarchy）
//   framework_editor_scripting.cpp           … Editor ログと C# Catalog / Build / Reload
//   framework_editor_panels.cpp              … Project / Console / Workspace パネル
//   framework_editor_diagnostics.cpp         … Runtime Mode と操作キャラクター診断
//
// 関数本体は分割前のまま移動し、Workspace の既存分岐と呼び出し順は変更しない。
#include "framework.h"
#include "../../RePlayEngine/Editor/Style/EditorStyle.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <string>

void framework::apply_toon_preset(int preset)
{
    toon_preset_index = preset;
    switch (preset)
    {
    case 1: // Cool Ink
        toon.material.shadow_tint = { 0.30f, 0.42f, 0.70f, 0.85f };
        toon.material.rim_color = { 0.70f, 0.95f, 1.00f, 0.95f };
        toon.material.specular_tint = { 0.85f, 0.95f, 1.00f, 0.65f };
        toon.material.toon_params = { 4.0f, 0.48f, 1.20f, 0.0f };
        toon.material.specular_params = { 48.0f, 0.70f, 0.55f, 0.25f };
        toon.outline.outline_color = { 0.01f, 0.02f, 0.05f, 1.0f };
        toon.outline.outline_params = { 0.024f, 0.022f, 0.0f, 0.0f };
        break;
    case 2: // High Contrast
        toon.material.shadow_tint = { 0.12f, 0.10f, 0.16f, 1.0f };
        toon.material.rim_color = { 1.0f, 0.95f, 0.55f, 1.0f };
        toon.material.specular_tint = { 1.0f, 1.0f, 1.0f, 1.0f };
        toon.material.toon_params = { 2.4f, 0.42f, 1.6f, 0.0f };
        toon.material.specular_params = { 72.0f, 0.78f, 1.0f, 0.10f };
        toon.outline.outline_color = { 0.0f, 0.0f, 0.0f, 1.0f };
        toon.outline.outline_params = { 0.030f, 0.030f, 0.0f, 0.0f };
        break;
    case 3: // Soft Anime
        toon.material.shadow_tint = { 0.62f, 0.52f, 0.70f, 0.45f };
        toon.material.rim_color = { 1.0f, 0.78f, 0.72f, 0.45f };
        toon.material.specular_tint = { 1.0f, 0.92f, 0.86f, 0.35f };
        toon.material.toon_params = { 5.0f, 0.62f, 0.55f, 0.0f };
        toon.material.specular_params = { 24.0f, 0.55f, 0.35f, 0.35f };
        toon.outline.outline_color = { 0.12f, 0.07f, 0.10f, 1.0f };
        toon.outline.outline_params = { 0.014f, 0.012f, 0.0f, 0.0f };
        break;
    default: // Warm Cel
        toon.material.shadow_tint = { 0.55f, 0.40f, 0.65f, 0.65f };
        toon.material.rim_color = { 1.00f, 0.85f, 0.60f, 0.75f };
        toon.material.specular_tint = { 1.00f, 1.00f, 0.95f, 0.80f };
        toon.material.toon_params = { 3.0f, 0.55f, 1.0f, 0.0f };
        toon.material.specular_params = { 32.0f, 0.60f, 0.8f, 0.4f };
        toon.outline.outline_color = { 0.05f, 0.05f, 0.08f, 1.0f };
        toon.outline.outline_params = { 0.020f, 0.020f, 0.0f, 0.0f };
        break;
    }
}
void framework::reset_editor_values()
{
    auto& post = post_process.GetSettings();
    camera_position = { 0.0f, 4.0f, -10.0f, 1.0f };
    light_direction = { 0.300f, 0.000f, 0.500f, 0.0f };
    translation = { 0.0f, 0.0f, 0.0f };
    scaling = { 0.01f, 0.01f, 0.01f };
    rotation = { 81.0f, 8.5f, 180.0f };
    material_color = { 1, 1, 1, 1 };
    background_color = { 46.0f / 255.0f, 56.0f / 255.0f, 61.0f / 255.0f, 1.0f };
    draw_background_image = false;
    use_pbr_skin = enable_toon_shader = enable_unlit_shader = true;
    enable_outline_shader = enable_pbr_shadow_shader = true;
    enable_luminance_shader = enable_final_pass_shader = true;
    enable_bloom_shader = enable_fxaa_shader = true;
    enable_vignette_shader = enable_static_meshes = false;
    enable_scene_game = enable_stage_shader = true;
    enable_particles = enable_trail = false;
    enable_deferred = true;
    shading_per_stage = SHADING_MODEL_PBR;
    stage_texture_wrap = true;
    stage_texture_contrast = 1.20f;
    // カメラと旧ステージの初期化だけ。Scene 内の GameObject には触れない。
    // 「シーン初期化」で操作キャラクターが消えたり作り直されたりしない。
    if (game_scene) game_scene->Gameplay().ResetGameplay();
    render_graph.SetOutput(0);
    luminance_threshold = 1.0f;
    pbr.light.directional_color = { 1, 1, 1, 3.598f };
    pbr.light.ibl_params = { 1.372f, 1.021f, 0.791f, 1.188f };
    pbr.light.shadow_params = { 0.741f, 0.00092f, 1.500f, 1.0f };
    pbr.stage_material.options = { 0, 0, 1, 0 };
    pbr.stage_material.base_tint = { 1, 1, 1, 1 };
    post.exposure = 0.619f;
    post.bloom_intensity = 0.25f;
    post.vignette_strength = 0.138f;
    post.fxaa_enable = 1.0f;
    // 旧 Player 用の skinned[0] スロットは撤去したので、
    // ここで初期化するのは静的メッシュとステージのぶんだけ。
    // キャラクターの描画方式は Renderer Component のプロパティが持つ。
    shading_per_static[0] = SHADING_MODEL_PBR;
    pixelate_grid_per_static[0] = stage_pixelate_grid = 6.0f;
    pixelate_strength_per_static[0] = stage_pixelate_strength = 1.0f;
    outline_per_static[0] = false;
    shader_layers_static[0].Clear();
    stage_shader_layers.Clear();
    character_profiles_static[0] = ReplayEngine::Rendering::CharacterMaterialProfile{};
    stage_character_profile = ReplayEngine::Rendering::CharacterMaterialProfile{};
    shader_preset_status.clear();
    lights.data.light_counts = { 0, 0, 0, 0 };
    apply_toon_preset(0);
}

void framework::configure_editor_style()
{
    const float dpi = hwnd != nullptr
        ? static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f : 1.0f;
    ReplayEngine::Editor::EditorStyle::Apply(dpi);
    ImGui::GetIO().FontGlobalScale = dpi;

    // Window メニュー →「UI の見た目」で変えた分を上から重ねる。
    //
    // EditorStyle::Apply の中で決め打ちせずここへ置くのは、
    // Apply が「配色と余白の基準」を作る役で、
    // 個人の見やすさの調整はその上に乗る別物だから。
    if (!ui_style_overridden) return;

    ImGuiStyle& style = ImGui::GetStyle();
    style.FramePadding.x *= ui_button_scale;
    style.FramePadding.y *= ui_button_scale;
    style.ItemSpacing.x  *= ui_button_scale;
    style.ItemSpacing.y  *= ui_button_scale;
    style.Colors[ImGuiCol_Text] =
        ImVec4(ui_text_color[0], ui_text_color[1], ui_text_color[2], 1.0f);

    ImGui::GetIO().FontGlobalScale = dpi * ui_font_scale;
}

void framework::remember_active_editor_view()
{
    const std::size_t workspace_index = static_cast<std::size_t>(active_editor_workspace);
    if (workspace_index < editor_view_by_workspace.size())
        editor_view_by_workspace[workspace_index] = active_editor_view;
}

void framework::apply_remembered_editor_view(editor_workspace workspace)
{
    const std::size_t workspace_index = static_cast<std::size_t>(workspace);
    active_editor_view = workspace_index < editor_view_by_workspace.size()
        ? editor_view_by_workspace[workspace_index] : editor_view::scene;
    editor_view_tab_sync_pending = true;
}

void framework::set_editor_workspace(editor_workspace workspace)
{
    if (active_editor_workspace == workspace) return;
    const editor_workspace previous_workspace = active_editor_workspace;
    remember_active_editor_view();
    if (previous_workspace == editor_workspace::motion) stop_motion_preview();
    active_editor_workspace = workspace;
    apply_remembered_editor_view(active_editor_workspace);
    editor_layout_dirty = true;
    switch (active_editor_workspace)
    {
    case editor_workspace::placement:
        selected_editor_object = editor_selection::game_object;
        break;
    case editor_workspace::modeling:
        selected_editor_object = editor_selection::game_object;
        break;
    case editor_workspace::animation:
        // アニメーションは AnimatorComponent が持つ。
        // 選択中の GameObject をそのまま見せる（固定のプレイヤー項目は無い）。
        selected_editor_object = editor_selection::game_object;
        break;
    case editor_workspace::rendering: selected_editor_object = editor_selection::rendering; break;
    case editor_workspace::shader_adjustment:
        if (selected_editor_object != editor_selection::rendering)
            selected_editor_object = editor_selection::rendering;
        break;
    case editor_workspace::ui:
        selected_editor_object = editor_selection::game_object;
        show_ui_hierarchy_panel = true;
        show_ui_preview_panel = false;
        show_ui_inspector_panel = true;
        break;
    case editor_workspace::motion:
        selected_editor_object = editor_selection::game_object;
        show_motion_layers_panel = true;
        show_motion_preview_panel = true;
        show_motion_inspector_panel = true;
        show_motion_timeline_panel = true;
        show_motion_graph_panel = true;
        break;
    default: selected_editor_object = editor_selection::world; break;
    }
}

void framework::set_edit_mode(bool enabled)
{
    if (edit_mode_active == enabled) return;
    edit_mode_active = enabled;
    if (!enabled)
    {
        if (ImGui::GetCurrentContext())
        {
            ImGui::ClearActiveID();
            ImGui::SetWindowFocus(static_cast<const char*>(nullptr));
            ImGui::CaptureKeyboardFromApp(false);
        }
        SetFocus(hwnd);
    }
}

void framework::draw_editor_main_menu()
{
    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New Empty Scene")) create_object_scene(u8"新しいシーン", false);
        if (ImGui::MenuItem("New Default Scene")) create_object_scene(u8"新しいシーン", true);
        if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) load_object_scene(true);
        if (ImGui::BeginMenu("Recent Scenes"))
        {
            if (recent_scene_paths.empty()) ImGui::TextDisabled("履歴はありません");
            for (std::size_t index = 0; index < recent_scene_paths.size(); ++index)
            {
                const std::filesystem::path& path = recent_scene_paths[index];
                std::error_code error;
                const bool exists = std::filesystem::exists(path, error) && !error;
                std::string label = path.filename().u8string();
                if (!exists) label += " [Missing]";
                label += "##RecentScene" + std::to_string(index);
                if (ImGui::MenuItem(label.c_str(), nullptr, false, exists))
                    request_object_scene_action(object_scene_action::open_path, path);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", path.generic_u8string().c_str());
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (active_editor_workspace == editor_workspace::motion && motion_editor_loaded)
        {
            if (ImGui::MenuItem("Save Motion", "Ctrl+S")) save_current_motion_asset();
        }
        else if (ImGui::MenuItem("Save", "Ctrl+S")) save_object_scene(false);
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) save_object_scene(true);
        if (ImGui::MenuItem(u8"ゲームを書き出す...")) open_export_game_dialog();
        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
            request_object_scene_action(object_scene_action::exit_application);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit"))
    {
        const bool atlas_context = sprite_atlas_editor_loaded && sprite_atlas_editor_keyboard_focus;
        const bool motion_workspace = active_editor_workspace == editor_workspace::motion;
        const bool external_context = !atlas_context && !motion_workspace &&
            (project_browser_focused || selected_editor_object == editor_selection::asset ||
                selected_editor_object == editor_selection::world);
        const bool scene_context = !atlas_context && !motion_workspace && !external_context;
        const bool scene_edit_blocked = scene_context && !object_editor_context.CanEdit();
        const bool can_undo = atlas_context ? sprite_atlas_history_cursor > 0
            : motion_workspace ? motion_edit_history.CanUndo()
            : external_context ? external_file_history.CanUndo()
            : object_editor_context.History().CanUndo();
        if (scene_edit_blocked) ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
            ImGui::GetStyle().Alpha * 0.5f);
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, can_undo))
        {
            if (atlas_context) undo_sprite_atlas_edit();
            else if (motion_workspace) undo_motion_edit();
            else if (external_context) undo_external_file_edit();
            else object_editor_context.Undo();
        }
        if (scene_edit_blocked)
        {
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("実行中は元に戻せません。Shift+F5 で停止してください。");
            ImGui::PopStyleVar();
        }
        const bool can_redo = atlas_context ? sprite_atlas_history_cursor < sprite_atlas_history.size()
            : motion_workspace ? motion_edit_history.CanRedo()
            : external_context ? external_file_history.CanRedo()
            : object_editor_context.History().CanRedo();
        if (scene_edit_blocked) ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
            ImGui::GetStyle().Alpha * 0.5f);
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, can_redo))
        {
            if (atlas_context) redo_sprite_atlas_edit();
            else if (motion_workspace) redo_motion_edit();
            else if (external_context) redo_external_file_edit();
            else object_editor_context.Redo();
        }
        if (scene_edit_blocked) ImGui::PopStyleVar();
        ImGui::EndMenu();
    }
    // GameObject / Component / Assets を 1 つへまとめる。
    //
    // それぞれ 1〜3 項目しか無いのにメニューバーを 3 つ占有しており、
    // 目的の項目がどこにあるか分からなかった。「作る・置く」で 1 つにする。
    if (ImGui::BeginMenu(u8"作成"))
    {
        if (ImGui::MenuItem(u8"空の GameObject", nullptr, false,
            object_editor_context.CanEdit()))
        {
            object_hierarchy_panel.CreateEmpty(object_editor_context);
            if (object_editor_context.Selection().Primary().Valid())
                selected_editor_object = editor_selection::game_object;
        }
        if (ImGui::MenuItem(u8"選択中を操作対象にする", nullptr, false,
            object_editor_context.CanEdit() && !object_editor_context.Selection().Empty()))
        {
            object_editor_context.BeginEdit("操作対象を変更");
            object_scene.Services().SetControlledObject(object_editor_context.Selection().Primary());
            object_editor_context.CommitEdit();
        }

        ImGui::Separator();
        if (ImGui::MenuItem(u8"モデルを取り込む...")) browse_model_asset();
        if (ImGui::MenuItem(u8"Prefab を置く...")) load_prefab();

        ImGui::Separator();
        if (ImGui::MenuItem(u8"インスペクターを開く", nullptr, false,
            !object_editor_context.Selection().Empty()))
        {
            show_inspector_panel = true;
            selected_editor_object = editor_selection::game_object;
        }
        if (ImGui::MenuItem(u8"プロジェクトを開く")) show_project_panel = true;
        ImGui::EndMenu();
    }
    // 操作方法・速度・user preset は上部の Camera メニューへ集約する。
    if (ImGui::BeginMenu(u8"カメラ"))
    {
        draw_editor_camera_top_menu();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("UI"))
    {
        if (ImGui::MenuItem(u8"フォーカス表示管理..."))
            show_ui_focus_style_manager = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Window"))
    {
        ImGui::MenuItem("Scene / Game View", nullptr, &show_scene_view);
        ImGui::MenuItem("Hierarchy", nullptr, &show_hierarchy_panel);
        ImGui::MenuItem("Inspector", nullptr, &show_inspector_panel);
        ImGui::MenuItem("Project / Assets", nullptr, &show_project_panel);
        ImGui::MenuItem(u8"イージングカーブ", nullptr, &show_easing_editor_panel);
        ImGui::MenuItem("Console", nullptr, &show_console_panel);
        ImGui::MenuItem("Workspace", nullptr, &show_workspace_panel);
        ImGui::MenuItem("Validation / Diagnostics", nullptr, &show_validation_panel);
        ImGui::MenuItem("DX12 Debug", nullptr, &show_dx12_debug_panel);
        if (ImGui::MenuItem(u8"UI フォーカス表示..."))
            show_ui_focus_style_manager = true;
        ImGui::Separator();
        // Workspace の往復。
        //
        // UI / Motion へ行く項目だけがあって「戻る」が無いと、
        // 一度移動したユーザーが Scene へ帰れなくなる。
        // 現在いる Workspace には印を付け、どこにいるかを分かるようにする。
        if (ImGui::MenuItem(u8"Scene Workspaceへ", nullptr,
            active_editor_workspace == editor_workspace::general))
        {
            set_editor_workspace(editor_workspace::general);
        }
        if (ImGui::MenuItem("UI Workspaceへ", nullptr,
            active_editor_workspace == editor_workspace::ui))
        {
            set_editor_workspace(editor_workspace::ui);
        }
        if (ImGui::MenuItem("Motion Workspaceへ", nullptr,
            active_editor_workspace == editor_workspace::motion))
        {
            set_editor_workspace(editor_workspace::motion);
        }
        if (active_editor_workspace == editor_workspace::ui)
        {
            ImGui::MenuItem("UI 階層", nullptr, &show_ui_hierarchy_panel);
            ImGui::MenuItem("UI インスペクター", nullptr, &show_ui_inspector_panel);
        }
        if (active_editor_workspace == editor_workspace::motion)
        {
            ImGui::MenuItem("Motion レイヤー", nullptr, &show_motion_layers_panel);
            ImGui::MenuItem("Motion プレビュー", nullptr, &show_motion_preview_panel);
            ImGui::MenuItem("Motion インスペクター", nullptr, &show_motion_inspector_panel);
            ImGui::MenuItem("タイムライン", nullptr, &show_motion_timeline_panel);
            ImGui::MenuItem("グラフエディター", nullptr, &show_motion_graph_panel);
        }
        ImGui::Separator();
        ImGui::MenuItem(u8"シーンメモ", nullptr, &show_scene_notes_panel);
        ImGui::MenuItem("Scene Flow", nullptr, &show_scene_flow_panel);
        ImGui::MenuItem(u8"カメラ操作プリセット", nullptr, &show_camera_preset_manager);
        ImGui::MenuItem("Collision Diagnostics", nullptr, &show_collision_diagnostics);
        ImGui::Separator();
        // シェーダ資産の一覧。
        // .hlsl の #pragma がそのまま項目になることを確かめる窓。
        ImGui::MenuItem(u8"シェーダ一覧", nullptr, &show_shader_catalog_panel);
        if (ImGui::MenuItem("Shader Composer", nullptr, false, shader_composer_editor.HasAsset()))
            shader_composer_editor.Show();
        // 見た目が変わっていないことを機械で確かめる窓。
        // 描画やシェーダを触る前に基準を撮っておくこと。
        ImGui::MenuItem(u8"スクリーンショット回帰", nullptr, &show_golden_panel);
        ImGui::Separator();
        if (ImGui::MenuItem(u8"レイアウトを初期化")) editor_layout_dirty = true;

        // 見た目の調整。人によって画面サイズも見やすい大きさも違うので、
        // 固定値で決め打ちせずここで変えられるようにする。
        ImGui::Separator();
        if (ImGui::BeginMenu(u8"UI の見た目"))
        {
            bool style_changed = false;

            ImGui::TextDisabled(u8"大きさ");
            ImGui::SetNextItemWidth(180.0f);
            style_changed |= ImGui::SliderFloat(
                u8"ボタンの余白", &ui_button_scale, 0.6f, 3.0f, "x%.2f");
            ImGui::SetNextItemWidth(180.0f);
            style_changed |= ImGui::SliderFloat(
                u8"文字の大きさ", &ui_font_scale, 0.7f, 2.5f, "x%.2f");

            ImGui::Separator();
            ImGui::TextDisabled(u8"文字色");
            ImGui::SetNextItemWidth(200.0f);
            style_changed |= ImGui::ColorEdit3(u8"##UITextColor", ui_text_color);

            ImGui::Separator();
            if (ImGui::MenuItem(u8"大きめにする"))
            {
                ui_button_scale = 1.8f;
                ui_font_scale = 1.3f;
                style_changed = true;
            }
            if (ImGui::MenuItem(u8"既定へ戻す"))
            {
                ui_button_scale = 1.0f;
                ui_font_scale = 1.0f;
                ui_text_color[0] = 1.0f;
                ui_text_color[1] = 1.0f;
                ui_text_color[2] = 1.0f;
                style_changed = true;
            }

            // 変えた瞬間に反映する。
            // configure_editor_style は毎回 EditorStyle::Apply から作り直すので、
            // 何度呼んでも倍率が積み重なることはない。
            if (style_changed)
            {
                ui_style_overridden = true;
                configure_editor_style();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    // メニュー名を "Play" にしない。
    // ツールバーの実行ボタンと同名だと、どちらを押せばよいか区別できない。
    // 実際にそれで迷子になった。
    if (ImGui::BeginMenu(u8"実行"))
    {
        if (ImGui::MenuItem(u8"▶ 実行", "F5", false, !object_scene_play_mode))
            enter_object_play_mode();
        if (ImGui::MenuItem(object_scene_paused ? u8"▶ 再開" : u8"❚❚ 一時停止", nullptr,
            false, object_scene_play_mode)) object_scene_paused = !object_scene_paused;
        if (ImGui::MenuItem(u8"■ 停止", "Shift+F5", false, object_scene_play_mode))
            exit_object_play_mode();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help"))
    {
        ImGui::TextDisabled("RePlayEngine Editor / C++17 / Direct3D 11");
        ImGui::Separator();
        ImGui::TextWrapped("SceneをGameObjectとComponentの組み合わせで制作します。");
        ImGui::EndMenu();
    }
}
