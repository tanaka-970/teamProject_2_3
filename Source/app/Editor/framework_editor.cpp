#include "framework.h"
#include "../../RePlayEngine/Components/Core/PivotComponent.h"
#include "../../RePlayEngine/Components/Gameplay/CharacterMotorComponent.h"
#include "../../RePlayEngine/Components/Gameplay/PlayerControllerComponent.h"
#include "../../RePlayEngine/Components/Gameplay/PlayerInputComponent.h"
#include "../../RePlayEngine/Editor/Style/EditorStyle.h"
#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Scripting/CSharp/CSharpProject.h"
#include "../../RePlayEngine/Scripting/CSharp/CSharpScriptBackend.h"
#include "../../RePlayEngine/Scripting/Core/ScriptComponent.h"
#include "../../RePlayEngine/Scripting/Core/ScriptRuntime.h"
#include "../../RePlayEngine/Scene/Serialization/SceneData.h"
#include "../../RePlayEngine/Scene/Serialization/SceneSerializer.h"
#include "shader.h"
#include "texture.h"
#include "skinned_mesh.h"

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cctype>
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
        show_ui_preview_panel = true;
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
        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
            request_object_scene_action(object_scene_action::exit_application);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit"))
    {
        const bool motion_workspace = active_editor_workspace == editor_workspace::motion;
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false,
            motion_workspace ? motion_edit_history.CanUndo()
                : (object_editor_context.CanEdit() &&
                    object_editor_context.History().CanUndo())))
        {
            if (motion_workspace) undo_motion_edit();
            else object_editor_context.Undo();
        }
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false,
            motion_workspace ? motion_edit_history.CanRedo()
                : (object_editor_context.CanEdit() &&
                    object_editor_context.History().CanRedo())))
        {
            if (motion_workspace) redo_motion_edit();
            else object_editor_context.Redo();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false,
            object_editor_context.CanEdit() && !object_editor_context.Selection().Empty()))
            object_hierarchy_panel.DuplicateSelection(object_editor_context);
        if (ImGui::MenuItem("Delete", "Delete", false,
            object_editor_context.CanEdit() && !object_editor_context.Selection().Empty()))
            object_hierarchy_panel.DestroySelection(object_editor_context);
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

    if (ImGui::BeginMenu("Window"))
    {
        ImGui::MenuItem("Scene / Game View", nullptr, &show_scene_view);
        ImGui::MenuItem("Hierarchy", nullptr, &show_hierarchy_panel);
        ImGui::MenuItem("Inspector", nullptr, &show_inspector_panel);
        ImGui::MenuItem("Project / Assets", nullptr, &show_project_panel);
        ImGui::MenuItem("Console", nullptr, &show_console_panel);
        ImGui::MenuItem("Workspace", nullptr, &show_workspace_panel);
        ImGui::MenuItem("Validation / Diagnostics", nullptr, &show_validation_panel);
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
            ImGui::MenuItem("Canvas プレビュー", nullptr, &show_ui_preview_panel);
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
        if (ImGui::MenuItem("Reset Layout")) editor_layout_dirty = true;

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

void framework::draw_object_scene_recovery_prompt()
{
    if (!object_recovery_available) return;
    if (!object_recovery_prompt_opened)
    {
        ImGui::OpenPopup("Scene Recovery");
        object_recovery_prompt_opened = true;
    }

    namespace Serialization = ReplayEngine::Scene::Serialization;
    if (!ImGui::BeginPopupModal("Scene Recovery", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    ImGui::TextUnformatted("A newer autosave was found.");
    ImGui::TextWrapped("Scene: %s", object_scene_path.string().c_str());
    ImGui::TextWrapped("Autosave: %s", object_recovery_path.string().c_str());

    Serialization::SceneData preview;
    std::string preview_error;
    const bool readable = Serialization::SceneSerializer::LoadFromFile(
        preview, object_recovery_path, preview_error);
    if (readable)
        ImGui::Text("Version %d / %zu GameObjects", preview.version, preview.objects.size());
    else
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.30f, 1.0f), "%s", preview_error.c_str());

    if (ImGui::Button("Recover", ImVec2(110.0f, 0.0f)) && readable)
    {
        if (object_scene_play_mode) exit_object_play_mode();
        detach_collision_world();
        Serialization::SceneLoadReport report;
        Serialization::ApplySceneData(preview, object_scene, report);
        object_editor_context.ResetSceneState();
        object_scene.Start();
        object_editor_context.AttachScene(&object_scene);
        object_editor_context.SetScenePath(object_scene_path);
        object_editor_context.MarkDirty();
        attach_collision_world(object_scene);
        object_editor_context.SetStatus("Autosaveを復旧しました。Saveで本Sceneへ反映してください");
        object_recovery_available = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Keep for later", ImVec2(110.0f, 0.0f)))
    {
        object_recovery_available = false;
        object_editor_context.SetStatus("Autosaveを保持しました");
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard", ImVec2(110.0f, 0.0f)))
    {
        std::error_code error;
        std::filesystem::remove(object_recovery_path, error);
        object_recovery_available = false;
        object_editor_context.SetStatus(error ? "Autosaveを削除できませんでした" : "Autosaveを破棄しました");
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void framework::draw_unsaved_object_scene_prompt()
{
    if (object_scene_unsaved_prompt_requested)
    {
        ImGui::OpenPopup(u8"未保存のシーン");
        object_scene_unsaved_prompt_requested = false;
        object_unsaved_prompt_open = true;
    }

    if (!ImGui::BeginPopupModal(u8"未保存のシーン", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize))
    {
        // 開いていたものが閉じられた場合だけ後始末する。
        //
        // Esc で閉じると、どのボタンも押されないまま予約だけが残る。
        // 残したままにすると、次に別の操作を要求したときに
        // 古い予約（たとえばアプリ終了）が実行されてしまう。
        // 閉じた = 選ばなかった、として取り消す。
        if (object_unsaved_prompt_open)
        {
            object_unsaved_prompt_open = false;
            pending_object_scene_action = object_scene_action::none;
            pending_object_scene_path.clear();
        }
        return;
    }

    // 終了なのか、別のシーンへ移るだけなのかでボタンの意味が変わる。
    // どちらも「続行」と書くと、押した結果がアプリ終了なのか分からない。
    const bool exiting = pending_object_scene_action == object_scene_action::exit_application;
    const char* save_label = exiting ? u8"保存して終了" : u8"保存して続行";
    const char* discard_label = exiting ? u8"破棄して終了" : u8"破棄して続行";

    ImGui::TextUnformatted(u8"現在のシーンには未保存の変更があります。");
    ImGui::TextWrapped(u8"%s", object_editor_context.DisplayTitle().c_str());
    ImGui::Spacing();
    ImGui::TextUnformatted(exiting
        ? u8"保存してから終了しますか？"
        : u8"保存してから続行しますか？");

    if (!object_scene_save_failure.empty())
    {
        // 保存に失敗したまま黙って閉じない。閉じると
        // 「保存できていないのに終了した」ように見える。
        // ただし理由を出さないと、押しても無反応にしか見えない。
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
            u8"保存できませんでした");
        ImGui::TextWrapped(u8"%s", object_scene_save_failure.c_str());
        ImGui::TextDisabled(u8"別名で保存するか、破棄して終了を選んでください。");
        ImGui::Spacing();
    }

    if (ImGui::Button(save_label, ImVec2(140.0f, 0.0f)))
    {
        if (save_object_scene(false))
        {
            object_scene_save_failure.clear();
            // 終了を確定させてから実行する。
            // 実行後に Dirty が立て直されても、もう確認へは戻らない。
            if (exiting) object_exit_confirmed = true;
            object_unsaved_prompt_open = false;
            ImGui::CloseCurrentPopup();
            execute_pending_object_scene_action();
        }
        else
        {
            // 失敗の理由をこのダイアログへ出す。
            // ステータス行はプロジェクトタブにしか出ず、
            // ここからは見えないため気付けなかった。
            object_scene_save_failure = object_editor_context.Status();
            if (object_scene_save_failure.empty())
            {
                object_scene_save_failure = u8"理由は不明です。";
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"別名で保存", ImVec2(120.0f, 0.0f)))
    {
        if (save_object_scene(true))
        {
            object_scene_save_failure.clear();
            if (exiting) object_exit_confirmed = true;
            object_unsaved_prompt_open = false;
            ImGui::CloseCurrentPopup();
            execute_pending_object_scene_action();
        }
        else
        {
            object_scene_save_failure = object_editor_context.Status();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(discard_label, ImVec2(140.0f, 0.0f)))
    {
        // ここは「変更を捨てる」が仕事。編集内容は保存しない。
        object_scene_save_failure.clear();
        discard_object_scene_autosave();
        object_editor_context.ClearDirty();
        if (exiting) object_exit_confirmed = true;
        object_unsaved_prompt_open = false;
        ImGui::CloseCurrentPopup();
        execute_pending_object_scene_action();
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"キャンセル", ImVec2(100.0f, 0.0f)))
    {
        object_scene_save_failure.clear();
        pending_object_scene_action = object_scene_action::none;
        pending_object_scene_path.clear();
        object_unsaved_prompt_open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void framework::draw_editor_toolbar()
{
    // New / Open / Save / Undo / Redo はツールバーへ置かない。
    //
    // File / Edit メニューと Ctrl+N/O/S/Z/Y に同じものがあり、
    // ツールバーに並べても幅を食うだけで得るものが無い。
    // ここへ残すのは「今どの状態か」が見た目に要るものだけにする。
    //   ギズモの操作種別 … 今どのモードかが分からないと操作できない
    //   実行 / 停止      … 今 Play 中かどうかが一目で要る
    //
    // 未保存かどうかはウィンドウタイトルとシーン名の * で分かる。
    // モードごとに Scene View のギズモの形が変わる。
    //   Move   … 軸線 + 先端の丸
    //   Rotate … 軸まわりの円
    //   Scale  … 軸線 + 先端の四角
    //
    // 選択中のモードはボタンの色でも示す。
    // 形だけだと Scene View を見ていないと分からず、
    // ツールバーを見ても «今どれか» が読み取れなかった。
    const auto gizmo_mode_button = [&](const char* label,
        ReplayEngine::Editor::GizmoOperation mode, const char* tooltip)
    {
        const bool active = transform_gizmo.Operation() == mode;
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button,
                ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
        }
        if (ImGui::Button(label)) transform_gizmo.SetOperation(mode);
        if (active) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    };

    gizmo_mode_button("Move", ReplayEngine::Editor::GizmoOperation::Translate,
        u8"移動（既定: Shift+W）\n"
        u8"軸線の先端が丸。軸をドラッグするとその方向へ動く。\n"
        u8"ドラッグ中に Esc で取り消し。");
    ImGui::SameLine();
    gizmo_mode_button("Rotate", ReplayEngine::Editor::GizmoOperation::Rotate,
        u8"回転（既定: Shift+E）\n"
        u8"軸まわりの円。円周を掴んで、円に沿って引くと回る。\n"
        u8"ドラッグ中に Esc で取り消し。");
    ImGui::SameLine();
    gizmo_mode_button("Scale", ReplayEngine::Editor::GizmoOperation::Scale,
        u8"拡縮（既定: Shift+R）\n"
        u8"軸線の先端が四角。軸をドラッグするとその軸だけ伸縮する。\n"
        u8"ドラッグ中に Esc で取り消し。");
    ImGui::SameLine();
    bool snap = transform_gizmo.SnapEnabled();
    if (ImGui::Checkbox("Snap", &snap)) transform_gizmo.SetSnapEnabled(snap);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            u8"ドラッグ量を一定の刻みに丸める。\n"
            u8"移動・回転・拡縮のどのモードにも効く。");
    }
    ImGui::SameLine();
    if (ImGui::Button(gizmo_local_space ? "Local" : "World"))
        gizmo_local_space = !gizmo_local_space;
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            u8"ギズモの軸の向きを切り替える。\n"
            u8"World … ワールド座標の軸に固定する。\n"
            u8"Local … 選択しているオブジェクトの回転に追従する。\n"
            u8"傾いた物を «その物にとっての前» へ動かしたいときは Local。");
    }

    ReplayEngine::Core::GameObject* pivot_object =
        object_editor_context.Selection().ResolvePrimary(active_object_scene());
    const bool has_pivot = pivot_object != nullptr &&
        pivot_object->GetComponent<ReplayEngine::Components::PivotComponent>() != nullptr;
    ImGui::SameLine();
    if (ImGui::Button(pivot_edit_mode ? u8"Pivot:ON" : u8"Pivot"))
        pivot_edit_mode = has_pivot ? !pivot_edit_mode : false;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(has_pivot
            ? u8"Pivot 編集補助。Transform は動かさず基準点だけを編集する。"
            : u8"選択オブジェクトへ Pivot Component を追加すると使える。");
    if (pivot_edit_mode && has_pivot)
    {
        ImGui::SameLine();
        if (ImGui::Button(u8"面Snap")) snap_primary_pivot_to_mesh(0);
        ImGui::SameLine();
        if (ImGui::Button(u8"頂点Snap")) snap_primary_pivot_to_mesh(1);
        ImGui::SameLine();
        if (ImGui::Button(u8"辺Snap")) snap_primary_pivot_to_mesh(2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(u8"CookedMeshCollision の実三角形へ正確に吸着する。");
    }

    ImGui::SameLine();
    bool auxiliary_views = editor_auxiliary_views;
    if (ImGui::Checkbox(u8"補助View", &auxiliary_views))
        editor_auxiliary_views = auxiliary_views;
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            u8"Scene View の右側へ Front / Side / Top を重ねて表示する。\n"
            u8"メイン View は全面のままなので Picking / Gizmo の座標は変わらない。");
    }

    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();

    // 実行ボタンは色と大きさで他から切り離す。
    //
    // 以前は同じ見た目のボタンが 8 個並ぶ中に "Play" が紛れており、
    // しかもメニューバーにも同名の "Play" があった。
    // どちらを押せばよいか画面から判断できず、実際に迷子になった。
    const ImVec2 transport_size(96.0f, 0.0f);
    if (!object_scene_play_mode)
    {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.16f, 0.62f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.74f, 0.36f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.12f, 0.50f, 0.22f, 1.0f));
        if (ImGui::Button(u8"▶ 実行 (F5)", transport_size)) enter_object_play_mode();
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(u8"ゲームを実行します。\n"
                u8"実行用のコピーが動くので、編集中のシーンは変わりません。\n"
                u8"C# スクリプトの Update はここから先でしか動きません。");
        }
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.70f, 0.52f, 0.14f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.84f, 0.63f, 0.20f, 1.0f));
        if (ImGui::Button(object_scene_paused ? u8"▶ 再開" : u8"❚❚ 一時停止", transport_size))
            object_scene_paused = !object_scene_paused;
        ImGui::PopStyleColor(2);

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.72f, 0.22f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.86f, 0.30f, 0.26f, 1.0f));
        if (ImGui::Button(u8"■ 停止", transport_size)) exit_object_play_mode();
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(u8"編集モードへ戻ります。\n"
                u8"実行中に動いた位置や生成した物はすべて破棄されます。");
        }
    }

    if (ImGui::GetContentRegionAvail().x > 280.0f)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        if (focus_search_requested)
        {
            ImGui::SetKeyboardFocusHere();
            focus_search_requested = false;
        }
        ImGui::InputTextWithHint("##FeatureSearch", "Search...", editor_search_text,
            IM_ARRAYSIZE(editor_search_text));
        search_input_active = ImGui::IsItemActive();
    }
}

void framework::draw_scene_view_panel()
{
    scene_view_hovered = false;
    scene_view_focused = false;
    if (!show_scene_view) return;

    if (scene_view_overlay_valid)
    {
        ImGui::SetNextWindowPos(scene_view_overlay_position, ImGuiCond_Always);
        ImGui::SetNextWindowSize(scene_view_overlay_size, ImGuiCond_Always);
    }
    ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    const ImGuiWindowFlags scene_view_flags = ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;
    if (!ImGui::Begin("Scene View", &show_scene_view, scene_view_flags))
    {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("SceneGameTabs"))
    {
        const ImGuiTabItemFlags scene_flags =
            editor_view_tab_sync_pending && active_editor_view == editor_view::scene
                ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        const ImGuiTabItemFlags game_flags =
            editor_view_tab_sync_pending && active_editor_view == editor_view::game
                ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem("Scene", nullptr, scene_flags))
        {
            active_editor_view = editor_view::scene;
            remember_active_editor_view();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Game", nullptr, game_flags))
        {
            active_editor_view = editor_view::game;
            remember_active_editor_view();
            ImGui::EndTabItem();
        }
        editor_view_tab_sync_pending = false;
        ImGui::EndTabBar();
    }

    if (active_editor_view == editor_view::scene)
    {
        const char* modes[] = { "Shaded", "Unlit", "Wireframe", "Shaded Wireframe", "Collision" };
        ImGui::SetNextItemWidth(150.0f);
        ImGui::Combo("##SceneDrawMode", &scene_view_draw_mode, modes, IM_ARRAYSIZE(modes));
        ImGui::SameLine();
        ImGui::Checkbox("Collider", &show_collider_debug_draw);
        ImGui::SameLine();
        ImGui::Checkbox("Grid", &show_scene_grid);
        ImGui::SameLine();
        ImGui::TextDisabled("Perspective | %s | %s | %s",
            gizmo_local_space ? "Local" : "World",
            transform_gizmo.SnapEnabled() ? "Snap" : "Free",
            object_scene_play_mode ? (object_scene_paused ? "Paused" : "Playing") : "Editing");
        ensure_editor_camera_presets_loaded();
        ImGui::TextDisabled(u8"Camera preset: %s | カメラ > プリセット管理 で操作を自由設定",
            active_editor_camera_preset().name.c_str());
        // Landscape を選択しているときだけ専用 Tool を出す。
        // Component 自体に ImGui / Editor 状態を持たせない。
        draw_landscape_editor_toolbar();
        if (landscape_edit_enabled)
        {
            const char* landscape_tool = landscape_edit_mode == 0
                ? u8"Landscape / Sculpt"
                : (landscape_topology_selection_mode == 0
                    ? u8"Landscape / Topology Face"
                    : u8"Landscape / Topology Edge");
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
                u8"ACTIVE TOOL: %s  (Escで終了 / Ctrl・Alt+左クリックで通常選択)", landscape_tool);
        }
        else
        {
            ImGui::TextDisabled("ACTIVE TOOL: Transform / Scene Selection");
        }
    }
    else
    {
        ImGui::TextDisabled("Runtime Camera | Free Aspect");
        if (!object_scene_play_mode)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
                "PlayでRuntime Sceneをプレビュー");
        }
    }

    const ImVec2 minimum = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 1.0f) size.x = 1.0f;
    if (size.y < 1.0f) size.y = 1.0f;
    ImGui::InvisibleButton("##SceneViewportSurface", size);
    const ImVec2 maximum{ minimum.x + size.x, minimum.y + size.y };
    scene_view_min_x = minimum.x;
    scene_view_min_y = minimum.y;
    scene_view_max_x = maximum.x;
    scene_view_max_y = maximum.y;
    scene_view_hovered = ImGui::IsItemHovered();
    scene_view_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // 右クリックの Play From Here / Checkpoint / Scene Memo。
    // InvisibleButton の直後に置くことで ContextItem の対象を確実に Viewport にする。
    draw_play_from_here_context_menu();

    if (active_editor_view == editor_view::scene && ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("REPLAY_ASSET_GUID"))
        {
            if (payload->Data != nullptr && payload->DataSize > 1)
            {
                const std::string guid(static_cast<const char*>(payload->Data));
                if (const auto* asset = asset_database.FindByGuid(guid))
                {
                    DirectX::XMFLOAT3 drop_position{};
                    const bool has_drop_position = scene_view_mouse_world_point(drop_position, nullptr);
                    ReplayEngine::Core::ObjectID drop_target = ReplayEngine::Core::ObjectID::Invalid();
                    const ImVec2 mouse = ImGui::GetMousePos();
                    const float local_x = mouse.x - scene_view_min_x;
                    const float local_y = mouse.y - scene_view_min_y;
                    if (local_x >= 0.0f && local_y >= 0.0f)
                    {
                        const auto ray = viewport_picking_ray(local_x, local_y);
                        drop_target = ReplayEngine::Editor::ViewportPicker::Pick(
                            object_scene, ray.origin, ray.direction);
                    }
                    place_asset_in_object_scene(*asset, asset_drop_add_collider,
                        has_drop_position ? &drop_position : nullptr, drop_target);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    draw_scene_note_overlay();
    ImGui::End();
}

void framework::draw_search_results()
{
    if (editor_search_text[0] == '\0') return;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({ viewport->Pos.x + 410.0f, viewport->Pos.y + 38.0f }, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.98f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking;
    ImGui::Begin("##SearchResultsOverlay", nullptr, flags);
    ImGui::TextDisabled("機能検索結果");
    ImGui::Separator();

    std::string query = editor_search_text;
    std::transform(query.begin(), query.end(), query.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto matches = [&query](const char* keywords)
    {
        std::string text = keywords;
        std::transform(text.begin(), text.end(), text.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text.find(query) != std::string::npos;
    };
    const auto result = [this, &matches](const char* label, const char* keywords,
        editor_selection selection)
    {
        if (matches(keywords) && ImGui::Selectable(label))
        {
            selected_editor_object = selection;
            editor_search_text[0] = '\0';
            search_input_active = false;
        }
    };
    result("ワールドを編集", "world ワールド 背景", editor_selection::world);
    result("カメラを編集", "camera カメラ 視点", editor_selection::camera);
    if (matches("player プレイヤー 操作 キャラクター character") &&
        ImGui::Selectable("操作対象 GameObject を選択"))
    {
        ReplayEngine::Scene::Scene& scene = active_object_scene();
        const ReplayEngine::Core::ObjectID controlled = scene.Services().ControlledObject();
        if (controlled.Valid() && scene.FindGameObjectByID(controlled) != nullptr)
        {
            object_editor_context.Selection().Select(controlled, false);
            selected_editor_object = editor_selection::game_object;
        }
        else object_editor_context.SetStatus("操作対象 GameObject が設定されていません");
        editor_search_text[0] = '\0';
        search_input_active = false;
    }
    result("描画設定を開く", "render rendering 描画 deferred shader", editor_selection::rendering);
    result("ポスト処理を開く", "post process ポスト bloom fxaa", editor_selection::post_process);
    if (matches("input capture editor 入力 キャプチャ") && ImGui::Selectable("Editor入力キャプチャを切り替える"))
    {
        set_edit_mode(!edit_mode_active);
        editor_search_text[0] = '\0';
    }
    if (matches("workspace モデリング modeling") && ImGui::Selectable("モデリングWorkspaceへ"))
    {
        set_editor_workspace(editor_workspace::modeling);
        editor_search_text[0] = '\0';
    }
    if (matches("workspace 配置 placement place") && ImGui::Selectable("配置Workspaceへ"))
    {
        set_editor_workspace(editor_workspace::placement);
        editor_search_text[0] = '\0';
    }
    if (matches("workspace アニメーション animation") && ImGui::Selectable("アニメーションWorkspaceへ"))
    {
        set_editor_workspace(editor_workspace::animation);
        editor_search_text[0] = '\0';
    }
    if (matches("workspace レンダリング rendering") && ImGui::Selectable("レンダリングWorkspaceへ"))
    {
        set_editor_workspace(editor_workspace::rendering);
        editor_search_text[0] = '\0';
    }
    if (matches("workspace ui canvas userinterface ユーザーインターフェイス") &&
        ImGui::Selectable("UI Workspaceへ"))
    {
        set_editor_workspace(editor_workspace::ui);
        editor_search_text[0] = '\0';
    }
    if (matches("workspace motion timeline keyframe モーション タイムライン") &&
        ImGui::Selectable("Motion Workspaceへ"))
    {
        set_editor_workspace(editor_workspace::motion);
        editor_search_text[0] = '\0';
    }
    if (matches("shader material preset シェーダー 材質 プリセット") &&
        ImGui::Selectable("シェーダー調整テーブルへ"))
    {
        set_editor_workspace(editor_workspace::shader_adjustment);
        editor_search_text[0] = '\0';
    }
    if (matches("fullscreen 全画面 フルスクリーン") && ImGui::Selectable("全画面表示を切り替える"))
    {
        toggle_fullscreen();
        editor_search_text[0] = '\0';
    }
    ImGui::End();
}

void framework::draw_scene_hierarchy()
{
    ImGui::Begin("階層");
    const auto item = [this](const char* label, editor_selection value)
    {
        if (ImGui::Selectable(label, selected_editor_object == value)) selected_editor_object = value;
    };
    if (ImGui::TreeNodeEx("ゲームシーン", ImGuiTreeNodeFlags_DefaultOpen))
    {
        item("ワールド", editor_selection::world);
        item("メインカメラ", editor_selection::camera);
        // 「プレイヤー」という固定項目は無い。
        // 操作キャラクターは下の GameObject ツリーへ通常の GameObject として現れ、
        // 名前も自由に変えられる。同じ対象を 2 か所から編集できる状態は作らない。
        item("描画設定", editor_selection::rendering);
        item("ポスト処理", editor_selection::post_process);
        ImGui::TreePop();
    }

    ImGui::Separator();

    // GameObject / Component 基盤のツリー。
    // 表示・選択・作成・削除・複製・親子変更は HierarchyPanel が担当する。
    // framework 側は「選択が変わったらインスペクターの表示先を切り替える」だけ。
    if (ImGui::TreeNodeEx("GameObject", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // 操作対象が設定されていない Scene であることを伝えるだけ。
        // ここで何かを生成したり、代わりの GameObject を選んだりはしない。
        if (object_missing_controlled_target)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                "このシーンには操作対象が設定されていません");
            ImGui::TextDisabled(
                "GameObject を選び、インスペクターの「操作対象に設定」で指定してください");
            ImGui::Separator();
        }

        object_editor_context.SetPlayMode(object_scene_play_mode);
        object_editor_context.AttachScene(&active_object_scene());

        const ReplayEngine::Core::ObjectID before = object_editor_context.Selection().Primary();
        object_hierarchy_panel.DrawContents(object_editor_context);
        const ReplayEngine::Core::ObjectID after = object_editor_context.Selection().Primary();

        if (after.Valid() && after != before)
        {
            selected_editor_object = editor_selection::game_object;
        }
        ImGui::TreePop();
    }

    ImGui::End();
}

void framework::push_editor_log(std::string severity, std::string message,
    std::filesystem::path file, int line, int column)
{
    // 画面のログはコピーしないと外へ出せない。
    // 同じ内容をファイルへも落としておく。
    //
    // 起動ごとに切り詰める（append ではなく trunc を初回だけ）。
    // 追記し続けると前回の実行と混ざり、どれが今回のものか分からなくなる。
    {
        static bool truncated = false;
        std::error_code folder_error;
        const std::filesystem::path folder =
            std::filesystem::path("Saved") / "Diagnostics";
        std::filesystem::create_directories(folder, folder_error);

        std::ofstream sink(folder / "editor_log.txt",
            std::ios::binary | (truncated ? std::ios::app : std::ios::trunc));
        truncated = true;
        if (sink)
        {
            sink << '[' << severity << "] " << message;
            if (!file.empty())
            {
                sink << "  (" << file.generic_u8string();
                if (line > 0) sink << ':' << line;
                if (column > 0) sink << ':' << column;
                sink << ')';
            }
            sink << '\n';
        }
    }

    editor_log_entry entry;
    entry.severity = std::move(severity);
    entry.message = std::move(message);
    entry.file = std::move(file);
    entry.line = line;
    entry.column = column;
    editor_log_entries.push_back(std::move(entry));
    if (editor_log_entries.size() > 500)
    {
        editor_log_entries.erase(editor_log_entries.begin());
        if (selected_editor_log_index > 0) --selected_editor_log_index;
    }
}

void framework::snapshot_csharp_script_write_times()
{
    namespace CSharp = ReplayEngine::Scripting::CSharp;

    csharp_source_write_times.clear();
    for (const CSharp::CSharpBehaviourInfo& info :
        CSharp::CSharpProject::DiscoverBehaviours(std::filesystem::current_path()))
    {
        std::error_code error;
        const std::filesystem::file_time_type time =
            std::filesystem::last_write_time(info.source_path, error);
        if (error) continue;
        csharp_source_write_times[info.source_path.generic_u8string()] = time;
    }
    csharp_scripts_dirty = false;
}

void framework::poll_csharp_script_changes(float elapsed_time)
{
    namespace CSharp = ReplayEngine::Scripting::CSharp;

    csharp_scan_accumulator += elapsed_time;
    if (csharp_scan_accumulator < 1.0f) return;
    csharp_scan_accumulator = 0.0f;

    // この回で新しく変化を見つけたか。
    // 見つけた直後は再ビルドしない。Visual Studio は複数ファイルを
    // 続けて保存するので、検出のたびに走らせるとビルドが重なる。
    // 「変化が落ち着いた次の回」で 1 度だけ走らせる。
    bool changed_this_scan = false;

    for (const CSharp::CSharpBehaviourInfo& info :
        CSharp::CSharpProject::DiscoverBehaviours(std::filesystem::current_path()))
    {
        std::error_code error;
        const std::filesystem::file_time_type time =
            std::filesystem::last_write_time(info.source_path, error);
        if (error) continue;

        const std::string key = info.source_path.generic_u8string();
        const auto found = csharp_source_write_times.find(key);
        if (found == csharp_source_write_times.end())
        {
            csharp_source_write_times[key] = time;
            csharp_scripts_dirty = true;
            changed_this_scan = true;
            push_editor_log("Info", "C# source detected: " + key, info.source_path);
            continue;
        }

        if (found->second != time)
        {
            found->second = time;
            csharp_scripts_dirty = true;
            changed_this_scan = true;
            push_editor_log("Info", "C# source changed: " + key, info.source_path);
        }
    }

    // 保存を検出したら自動で再コンパイルする。
    //
    // 手で「Build && Reload C#」を押す運用だと、押し忘れたまま
    // 「直したのに動かない」と悩む時間が生まれる。実際にそれで詰まった。
    //
    // コンパイルに失敗しても直前に成功した Assembly が維持されるので、
    // 自動で走らせても編集中のシーンは壊れない。
    if (csharp_scripts_dirty && csharp_auto_reload && !changed_this_scan)
    {
        csharp_scripts_dirty = false;
        push_editor_log("Info", "C# の変更を検出したので自動で再コンパイルします");
        build_and_reload_csharp_scripts();
    }
}

// C# を一括で作り直す。
// Catalog の更新と Assembly の再コンパイルを 1 回でやる。
bool framework::rebuild_all_csharp()
{
    push_editor_log("Info", "===== C# 一括更新 開始 =====");

    // 先に Catalog。新しく増えた .cs をここで拾う。
    const bool catalog_ok = refresh_csharp_scripts();

    // 次に Assembly。Catalog に載った型が実際に生成できる状態になる。
    const bool build_ok = build_and_reload_csharp_scripts();

    // 変更検出の基準を今の状態へ揃える。
    // これをしないと、直後の巡回でもう一度自動再コンパイルが走る。
    snapshot_csharp_script_write_times();
    csharp_scripts_dirty = false;

    push_editor_log(catalog_ok && build_ok ? "Info" : "Error",
        std::string("===== C# 一括更新 終了 (Catalog=") +
        (catalog_ok ? "OK" : "NG") + " / Build=" + (build_ok ? "OK" : "NG") + ") =====");

    return catalog_ok && build_ok;
}

bool framework::refresh_csharp_scripts()
{
    namespace CSharp = ReplayEngine::Scripting::CSharp;
    namespace Scripting = ReplayEngine::Scripting;

    initialize_runtime_services();
    if (!object_script_runtime)
    {
        push_editor_log("Error", "ScriptRuntime is not initialized.");
        return false;
    }

    std::string error;
    if (!CSharp::CSharpProject::RefreshCatalog(std::filesystem::current_path(),
        asset_database, object_script_runtime->Catalog(), error))
    {
        editor_command_result = "C# Catalog 更新失敗: " + error;
        push_editor_log("Error", editor_command_result);
        return false;
    }

    for (const Scripting::ScriptTypeDescriptor& descriptor :
        object_script_runtime->Catalog().All())
    {
        if (descriptor.language != Scripting::ScriptLanguage::CSharp) continue;
        object_script_runtime->RequestSchemaReload(descriptor.type_id);
    }

    // Schema の差し替えをここで 1 回通す。
    // ApplyPendingSchemaSwaps は Play セッションに登録済みの Component へしか
    // 配らない（内部で world_ が無ければ抜ける）ので、編集 Scene の
    // Component は下の resolve_editor_script_schemas で自分で引き直す。
    object_script_runtime->ApplyPendingSchemaSwaps(0.0f);

    // 編集 Scene の ScriptComponent を再解決する。
    //
    // これが無いと、Catalog を更新しても編集 Scene の Component は
    // Unresolved のまま残る。Scene を読み込んだ時点では Catalog がまだ
    // 空なので、起動直後は必ずこの状態になっていた。
    const std::size_t resolved = resolve_editor_script_schemas();

    snapshot_csharp_script_write_times();
    editor_command_result = "C# Catalog を更新しました（編集 Scene の Script " +
        std::to_string(resolved) + " 件を再解決）";
    push_editor_log("Info", editor_command_result);
    return true;
}

// 編集 Scene の ScriptComponent へ Schema を引き直させる。
// 戻り値は Schema を持てた Component の数。
std::size_t framework::resolve_editor_script_schemas()
{
    std::size_t resolved = 0;

    // 再帰で階層を降りる。ラムダ再帰を使わず素直に書く。
    struct Walker
    {
        static void Visit(ReplayEngine::Core::GameObject& object, std::size_t& count)
        {
            for (std::size_t index = 0; index < object.ComponentCount(); ++index)
            {
                ReplayEngine::Core::Component* component = object.ComponentAt(index);
                if (component == nullptr) continue;
                auto* script =
                    dynamic_cast<ReplayEngine::Scripting::ScriptComponent*>(component);
                if (script == nullptr) continue;
                if (script->ResolveSchema()) ++count;
            }
            for (ReplayEngine::Core::GameObject* child : object.Children())
            {
                if (child != nullptr) Visit(*child, count);
            }
        }
    };

    for (ReplayEngine::Core::GameObject* root : object_scene.RootGameObjects())
    {
        if (root != nullptr) Walker::Visit(*root, resolved);
    }
    return resolved;
}

bool framework::build_and_reload_csharp_scripts()
{
    namespace CSharp = ReplayEngine::Scripting::CSharp;
    namespace Scripting = ReplayEngine::Scripting;

    initialize_runtime_services();
    if (!object_script_runtime)
    {
        push_editor_log("Error", "ScriptRuntime is not initialized.");
        return false;
    }

    auto* backend = dynamic_cast<CSharp::CSharpScriptBackend*>(
        object_script_runtime->Backend(Scripting::ScriptLanguage::CSharp));
    if (backend == nullptr)
    {
        editor_command_result = "C# Backend が接続されていません";
        push_editor_log("Error", editor_command_result);
        return false;
    }

    CSharp::CSharpBuildResult build;
    const bool reloaded = backend->CompileAndReload(&build);
    for (const CSharp::CSharpDiagnostic& diagnostic : build.diagnostics)
    {
        std::string severity = "Info";
        if (diagnostic.severity == CSharp::CSharpDiagnostic::Severity::Warning)
            severity = "Warning";
        else if (diagnostic.severity == CSharp::CSharpDiagnostic::Severity::Error)
            severity = "Error";

        push_editor_log(severity,
            diagnostic.code + ": " + diagnostic.message,
            diagnostic.file, diagnostic.line, diagnostic.column);
    }

    if (!reloaded)
    {
        editor_command_result =
            "C# Compile/Reload 失敗。直前に成功した Assembly は維持しています。";
        if (build.diagnostics.empty() && !build.output_text.empty())
        {
            push_editor_log("Error", build.output_text);
        }
        return false;
    }

    refresh_csharp_scripts();
    editor_command_result = "C# Compile/Reload 成功: " +
        build.output_assembly.generic_u8string();
    push_editor_log("Info", editor_command_result, build.output_assembly);
    csharp_scripts_dirty = false;
    return true;
}

bool framework::create_csharp_behaviour_asset()
{
    namespace CSharp = ReplayEngine::Scripting::CSharp;

    CSharp::CSharpBehaviourInfo info;
    std::string error;
    if (!CSharp::CSharpProject::CreateBehaviour(std::filesystem::current_path(),
        new_csharp_behaviour_name, new_csharp_namespace, info, error))
    {
        editor_command_result = "C# Behaviour 作成失敗: " + error;
        push_editor_log("Error", editor_command_result);
        return false;
    }

    const ReplayEngine::Assets::AssetRecord& record =
        asset_database.Register(info.source_path, ReplayEngine::Assets::AssetKind::Script);
    selected_asset_guid = record.guid;
    if (!asset_database.Save(error))
    {
        push_editor_log("Warning", "C# script asset registration could not be saved: " + error,
            info.source_path);
    }

    refresh_csharp_scripts();

    std::string open_error;
    if (!CSharp::CSharpProject::OpenVisualStudio(info.source_path, 1, open_error))
    {
        push_editor_log("Warning", open_error, info.source_path);
    }

    editor_command_result = "C# Behaviour を作成しました: " +
        info.source_path.generic_u8string();
    push_editor_log("Info", editor_command_result, info.source_path, 1);
    return true;
}

bool framework::open_selected_csharp_asset(int line)
{
    namespace CSharp = ReplayEngine::Scripting::CSharp;

    const ReplayEngine::Assets::AssetRecord* selected_asset =
        selected_asset_guid.empty() ? nullptr : asset_database.FindByGuid(selected_asset_guid);
    if (selected_asset == nullptr ||
        selected_asset->kind != ReplayEngine::Assets::AssetKind::Script ||
        selected_asset->source_path.extension() != ".cs")
    {
        editor_command_result = "C# script asset が選択されていません";
        push_editor_log("Warning", editor_command_result);
        return false;
    }

    std::string error;
    if (!CSharp::CSharpProject::OpenVisualStudio(selected_asset->source_path, line, error))
    {
        editor_command_result = error;
        push_editor_log("Error", error, selected_asset->source_path, line);
        return false;
    }

    editor_command_result = "Visual Studio で開きました: " +
        selected_asset->source_path.generic_u8string();
    push_editor_log("Info", editor_command_result, selected_asset->source_path, line);
    return true;
}

void framework::draw_project_panel()
{
    ImGui::Begin("プロジェクト");
    // GameObject Scene (.replayscene) is the only authoring format.
    if (ImGui::CollapsingHeader("GameObject シーン", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("現在: %s", active_object_scene().Name().c_str());
        ImGui::TextDisabled("%s", object_scene_path.generic_u8string().c_str());
        draw_new_object_scene_controls();
        ImGui::SameLine();
        if (ImGui::Button("Prefabとして保存...")) save_selected_prefab(true);
        ImGui::TextDisabled("%s", object_editor_context.Status().c_str());
        ImGui::Separator();
    }

    if (ImGui::CollapsingHeader("C# Scripts", ImGuiTreeNodeFlags_DefaultOpen))
    {
        namespace CSharp = ReplayEngine::Scripting::CSharp;

        ImGui::TextDisabled(u8"C# Script の作成は Project Browser の右クリック > Create に統一しました。");

        // 一括更新をいちばん目立つ位置へ置く。
        // Catalog 更新と再コンパイルを分けて覚えるのは negligible な区別で、
        // 実際には「とりあえず全部更新したい」がほとんど。
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f, 0.48f, 0.72f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.60f, 0.86f, 1.0f));
        if (ImGui::Button(u8"C# をすべて更新", ImVec2(150.0f, 0.0f))) rebuild_all_csharp();
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(u8"Catalog の更新と Assembly の再コンパイルを"
                u8"まとめて行います。\n迷ったらこれを押してください。");
        }

        ImGui::SameLine();
        ImGui::Checkbox(u8"自動更新", &csharp_auto_reload);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(u8".cs を保存すると自動で再コンパイルします。\n"
                u8"失敗しても直前に成功した Assembly を使い続けるので、\n"
                u8"編集中の状態は壊れません。");
        }

        ImGui::SameLine();
        if (ImGui::Button("Open Selected .cs")) open_selected_csharp_asset();

        if (csharp_scripts_dirty)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.74f, 0.28f, 1.0f),
                csharp_auto_reload ? u8"変更検出 → まもなく更新"
                                   : u8"変更検出済み（自動更新は無効）");
        }

        // 個別操作は畳んでおく。普段は使わない。
        if (ImGui::TreeNode(u8"個別に実行"))
        {
            if (ImGui::Button("Refresh C# Catalog")) refresh_csharp_scripts();
            ImGui::SameLine();
            if (ImGui::Button("Build && Reload C#")) build_and_reload_csharp_scripts();
            ImGui::TreePop();
        }
        ImGui::TextDisabled("%s",
            CSharp::CSharpProject::GameScriptsProjectPath(
                std::filesystem::current_path()).generic_u8string().c_str());
        ImGui::TextDisabled("%s",
            CSharp::CSharpProject::GameScriptsSolutionPath(
                std::filesystem::current_path()).generic_u8string().c_str());
        ImGui::Separator();
    }

    if (ImGui::Button("モデルファイルを取り込む...")) browse_model_asset();
    ImGui::SameLine();
    if (ImGui::Button("Prefabを配置...")) load_prefab();
    ImGui::SameLine();
    ImGui::TextDisabled("FBXキャッシュ / GLB / glTF");
    if (async_stage_load_active)
    {
        ImGui::ProgressBar(async_asset_manager.Progress(), ImVec2(-1.0f, 0.0f), "モデルを確認中");
    }
    if (!selected_model_asset_path.empty())
    {
        ImGui::TextWrapped("選択中: %s", selected_model_asset_path.c_str());
        ImGui::TextWrapped("状態: %s", model_asset_status.c_str());
        if (!selected_model_cache_path.empty())
            ImGui::TextWrapped("キャッシュ: %s", selected_model_cache_path.c_str());
    }
    ImGui::Separator();
    ImGui::TextDisabled("Asset 作成は Project Browser の右クリック > Create から行います");
    ImGui::Separator();
    ImGui::Text("Assets: %zu", asset_database.Records().size());

    // Project ブラウザ本体 (Source/app/Editor/framework_project_browser.cpp)。
    // フォルダツリー + そのフォルダの中身。作成・改名・D&D はそちら側。
    draw_project_browser();

    ImGui::Checkbox("SceneへModel配置時にMesh Colliderも追加", &asset_drop_add_collider);
    ImGui::SameLine();
    const auto* selected_asset = selected_asset_guid.empty()
        ? nullptr : asset_database.FindByGuid(selected_asset_guid);
    if (selected_asset != nullptr && ImGui::Button("選択AssetをSceneへ配置"))
        place_asset_in_object_scene(*selected_asset, asset_drop_add_collider);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Colliderは左の設定が有効な場合だけ明示的に追加します");
    if (selected_asset != nullptr &&
        (selected_asset->kind == ReplayEngine::Assets::AssetKind::Script ||
            selected_asset->source_path.extension() == ".cs"))
    {
        ImGui::SameLine();
        if (ImGui::Button("Visual Studioで開く")) open_selected_csharp_asset();
    }
    if (selected_asset != nullptr &&
        selected_asset->kind == ReplayEngine::Assets::AssetKind::Shader)
    {
        ImGui::SameLine();
        if (ImGui::Button("ShaderをVisual Studioで開く"))
        {
            std::string open_error;
            if (!ReplayEngine::Scripting::CSharp::CSharpProject::OpenVisualStudio(
                selected_asset->source_path, 1, open_error))
            {
                push_editor_log("Warning", open_error, selected_asset->source_path);
            }
        }
    }
    draw_material_asset_editor();
    ImGui::Separator();
    ImGui::TextDisabled("現在のシーンが読み込んでいる素材");
    ImGui::BulletText("キャラクター / Renderer Component の AssetGUID から解決");
    ImGui::BulletText("Stage / Model / Prefab / Material Asset");
    ImGui::BulletText("IBL / 拡散・鏡面・BRDF LUT");
    ImGui::BulletText("シェーダー / PBR・Toon・Deferred・PostProcess");
    ImGui::End();
}

void framework::draw_console_panel()
{
    ImGui::Begin("コンソール");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint("##EditorCommand", "> コマンドを入力...",
        editor_command_text, IM_ARRAYSIZE(editor_command_text), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        execute_editor_command(editor_command_text);
        editor_command_text[0] = '\0';
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::TextWrapped("%s", editor_command_result.c_str());
    ImGui::Separator();
    if (ImGui::Button("Clear Logs"))
    {
        editor_log_entries.clear();
        selected_editor_log_index = -1;
    }
    ImGui::SameLine();
    ImGui::Text("Editor Logs: %zu", editor_log_entries.size());
    if (ImGui::BeginChild("EditorLogEntries", ImVec2(0.0f, 150.0f), true))
    {
        for (int index = 0; index < static_cast<int>(editor_log_entries.size()); ++index)
        {
            const editor_log_entry& entry = editor_log_entries[index];
            ImVec4 color{ 0.78f, 0.82f, 0.90f, 1.0f };
            if (entry.severity == "Error") color = { 1.0f, 0.43f, 0.38f, 1.0f };
            else if (entry.severity == "Warning") color = { 1.0f, 0.74f, 0.28f, 1.0f };
            else if (entry.severity == "Info") color = { 0.56f, 0.84f, 1.0f, 1.0f };

            std::string label = "[" + entry.severity + "] " + entry.message;
            if (!entry.file.empty())
            {
                label += " (" + entry.file.filename().generic_u8string();
                if (entry.line > 0) label += ":" + std::to_string(entry.line);
                label += ")";
            }

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            const bool selected = selected_editor_log_index == index;
            if (ImGui::Selectable(label.c_str(), selected))
            {
                selected_editor_log_index = index;
                if (!entry.file.empty())
                {
                    std::string error;
                    ReplayEngine::Scripting::CSharp::CSharpProject::OpenVisualStudio(
                        entry.file, entry.line, error);
                    if (!error.empty()) editor_command_result = error;
                }
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && !entry.file.empty())
            {
                ImGui::SetTooltip("%s", entry.file.generic_u8string().c_str());
            }
        }
    }
    ImGui::EndChild();
    ImGui::Separator();
    ImGui::TextColored({ 0.45f, 0.85f, 0.55f, 1.0f }, "[正常] RePlayランタイム動作中");
    ImGui::Text("Editor入力: %s", edit_mode_active ? "編集操作" : "Game View入力キャプチャ");
    ImGui::Text("画面サイズ: %u x %u", client_width, client_height);
    ImGui::TextUnformatted("描画方式: Deferred（固定）");
    const char* outputs[] = { "Final", "HDR Scene", "Bloom", "Deferred Lit",
        "GBuffer Base Color", "GBuffer Normal", "GBuffer Material", "Depth" };
    ImGui::Text("出力: %s", outputs[render_graph.OutputIndex()]);
    ImGui::TextDisabled("Ctrl+S: 保存  Ctrl+Z/Y: 元に戻す/やり直す  Ctrl+D: 複製");
    ImGui::TextDisabled("F1: エディタ表示  F2: 名前変更  Ctrl+F2: 出力  F3: 入力キャプチャ  F5: 実行  F11: 全画面");
    ImGui::End();
}

void framework::draw_workspace_panel()
{
    ImGui::Begin("ワークスペース");
    switch (active_editor_workspace)
    {
    case editor_workspace::placement:
        ImGui::TextUnformatted("オブジェクト配置Workspace");
        ImGui::TextDisabled("ProjectのAsset BrowserからModelやPrefabをScene Viewへ配置します。");
        break;
    case editor_workspace::modeling:
        ImGui::TextUnformatted("モデリングWorkspace");
        ImGui::TextDisabled("形状編集用のテーブルです。配置操作は配置Workspaceへ分離しています。");
        if (ImGui::Button("選択GameObjectを編集")) selected_editor_object = editor_selection::game_object;
        ImGui::SameLine();
        if (ImGui::Button("配置Workspaceへ")) set_editor_workspace(editor_workspace::placement);
        break;
    case editor_workspace::animation:
        ImGui::TextUnformatted("アニメーションWorkspace");
        ImGui::TextDisabled("今後、タイムラインとアニメーション編集をここへ登録します。");
        ImGui::TextDisabled(
            "クリップの割り当てと再生は、対象 GameObject の Animator で編集します。");
        break;
    case editor_workspace::rendering:
        ImGui::TextUnformatted("レンダリングWorkspace");
        ImGui::TextDisabled("今後、RenderGraph・シェーダー・Profilerをここへ登録します。");
        if (ImGui::Button("描画設定を開く")) selected_editor_object = editor_selection::rendering;
        ImGui::SameLine();
        ImGui::TextUnformatted("Renderer: Deferred（固定）");
        break;
    case editor_workspace::shader_adjustment:
        ImGui::TextUnformatted("シェーダー調整Workspace");
        ImGui::TextDisabled("右上の専用テーブルで材質、合成順、プリセット、画面効果を編集します。");
        ImGui::TextUnformatted("方式: 型付きパラメータ + 順序付き追加パス");
        ImGui::TextDisabled("シェーダーグラフを使わず、安全な範囲で表現を組み合わせます。");
        break;
    case editor_workspace::ui:
        ImGui::TextUnformatted("UI Workspace");
        ImGui::TextDisabled("Canvas、RectTransform、UI Component を編集します。");
        if (ImGui::Button("UI 階層を開く")) show_ui_hierarchy_panel = true;
        ImGui::SameLine();
        if (ImGui::Button("Canvas プレビューを開く")) show_ui_preview_panel = true;
        break;
    case editor_workspace::motion:
        ImGui::TextUnformatted("Motion Workspace");
        ImGui::TextDisabled("Motion Asset、キー、プレビューを編集します。");
        if (ImGui::Button("Motion レイヤーを開く")) show_motion_layers_panel = true;
        ImGui::SameLine();
        if (ImGui::Button("タイムラインを開く")) show_motion_timeline_panel = true;
        break;
    default:
        ImGui::TextUnformatted("基本Workspace");
        ImGui::TextDisabled("シーン編集と実行状態の確認を行います。");
        if (ImGui::Button("ワールドを選択")) selected_editor_object = editor_selection::world;
        break;
    }
    ImGui::End();
}


void framework::draw_editor()
{
    editor_session_active = true;
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
            ImGuiID ui_preview = 0;
            if (active_editor_workspace == editor_workspace::ui)
            {
                // UI Workspace は central node をさらに左右へ割る。
                // 左側だけ Canvas プレビューに使い、残った central node は Scene View 専用に空ける。
                ui_preview = ImGui::DockBuilderSplitNode(center,
                    ImGuiDir_Left, 0.50f, nullptr, &center);
            }
            if (active_editor_workspace == editor_workspace::ui)
            {
                ImGui::DockBuilderDockWindow("UI 階層", left);
                ImGui::DockBuilderDockWindow("UI インスペクター", right);
                ImGui::DockBuilderDockWindow("Canvas プレビュー", ui_preview);
                ImGui::DockBuilderDockWindow("プロジェクト", bottom);
                ImGui::DockBuilderDockWindow("コンソール", bottom);
            }
            else if (active_editor_workspace == editor_workspace::motion)
            {
                ImGui::DockBuilderDockWindow("Motion レイヤー", left);
                ImGui::DockBuilderDockWindow("階層", left);
                ImGui::DockBuilderDockWindow("Motion インスペクター", right);
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

    if (active_editor_workspace == editor_workspace::ui)
    {
        draw_scene_view_panel();
        draw_ui_hierarchy();
        draw_ui_preview();
        draw_ui_inspector();
        if (show_project_panel) draw_project_panel();
        if (show_console_panel) draw_console_panel();
        draw_search_results();
        if (active_editor_view == editor_view::scene) handle_viewport_selection();
        draw_collider_debug_overlay();
        return;
    }

    if (active_editor_workspace == editor_workspace::motion)
    {
        draw_scene_view_panel();
        if (show_hierarchy_panel) draw_scene_hierarchy();
        draw_motion_layers();
        draw_motion_preview();
        draw_motion_inspector();
        draw_motion_timeline();
        draw_motion_graph_editor();
        if (show_project_panel) draw_project_panel();
        if (show_console_panel) draw_console_panel();
        draw_search_results();
        if (active_editor_view == editor_view::scene) handle_viewport_selection();
        draw_collider_debug_overlay();
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
    draw_shader_catalog_panel();
    {
        std::error_code composer_root_error;
        const std::filesystem::path composer_root = std::filesystem::current_path(composer_root_error);
        if (!composer_root_error)
            shader_composer_editor.Draw(composer_root, shader_library, asset_database);
    }
    draw_golden_panel();
    if (show_validation_panel)
        object_validation_panel.Draw(object_editor_context, &asset_database,
            &object_collision_world, object_render_items.Size());
    draw_collision_diagnostics_panel();
    draw_search_results();
    if (active_editor_view == editor_view::scene) handle_viewport_selection();

    // Collider の可視化は最後に描く。
    // 背景の描画リストへ積むので、パネルの下に隠れず、
    // かつパネルの上へも被らない。
    draw_collider_debug_overlay();
}


// ---------------------------------------------------------------------------
// 実行モード表示と操作キャラクターの診断
// ---------------------------------------------------------------------------
//
// 「動かない原因が入力なのか Edit Mode なのか分からない」状態を解消するための表示。

void framework::draw_runtime_mode_banner()
{
    ImGui::Separator();

    if (object_scene_play_mode)
    {
        const ImVec4 color = object_scene_paused
            ? ImVec4(1.0f, 0.75f, 0.25f, 1.0f)
            : ImVec4(0.4f, 0.95f, 0.5f, 1.0f);
        ImGui::TextColored(color, object_scene_paused
            ? u8"❚❚ 一時停止中" : u8"▶ 実行中");
        ImGui::TextDisabled(object_scene_paused
            ? u8"実行シーンを一時停止中"
            : u8"実行シーンで動作中 / 入力有効 / C# の Update が回っています");
    }
    else if (object_runtime_active())
    {
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), u8"▶ 実行中（編集シーン）");
        ImGui::TextDisabled(u8"編集シーンをそのまま実行中 / 入力有効");
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), u8"■ 編集中（停止）");
        ImGui::TextDisabled(u8"物理・入力・C# スクリプトはすべて停止中");
        ImGui::TextDisabled(u8"動かすには上の緑の「▶ 実行」ボタン、または F5");
    }

#ifdef _DEBUG
    // ウィンドウがアクティブでないと GetAsyncKeyState が拾えないことがある。
    if (::GetForegroundWindow() != hwnd)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f),
            "ゲーム画面をクリックすると入力を受け取ります");
    }
#endif
}

void framework::draw_controlled_character_diagnostics()
{
#ifdef _DEBUG
    // Debug ビルドでのみ表示する。Release へ診断処理を残さない。
    if (!ImGui::CollapsingHeader("Controlled Character Diagnostics")) return;

    namespace Components = ReplayEngine::Components;
    const ReplayEngine::Scene::Scene& scene = active_object_scene();

    ImGui::Text("Mode: %s", object_scene_play_mode ? "Play"
        : (object_runtime_active() ? "Running" : "Edit"));
    ImGui::Text("Runtime active: %s", object_runtime_active() ? "true" : "false");
    ImGui::Text("Fixed accumulator: %.4f", object_fixed_accumulator);
    ImGui::Text("Render items: %zu", object_render_items.Size());

    const ReplayEngine::Core::ObjectID controlled = scene.Services().ControlledObject();
    const ReplayEngine::Core::GameObject* target =
        controlled.Valid() ? scene.FindGameObjectByID(controlled) : nullptr;

    if (target == nullptr)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
            "このシーンには操作対象が設定されていません");
        ImGui::TextDisabled("インスペクターの「操作対象に設定」で指定してください");
        return;
    }

    ImGui::Text("Controlled Object: %s (ObjectID %s)",
        target->Name().c_str(), controlled.ToString().c_str());

    if (const auto* input = target->GetComponent<Components::PlayerInputComponent>())
    {
        ImGui::Text("Input enabled: %s", input->ActiveInHierarchy() ? "true" : "false");
        ImGui::Text("Input X/Y: %.2f / %.2f", input->MoveX(), input->MoveY());
        ImGui::Text("Jump latched: %s", input->JumpLatched() ? "true" : "false");
    }
    else ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "Player Input: なし");

    if (const auto* controller = target->GetComponent<Components::PlayerControllerComponent>())
    {
        ImGui::Text("Controller enabled: %s", controller->ActiveInHierarchy() ? "true" : "false");
        if (!controller->HasRequiredComponents())
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "  %s",
                controller->MissingRequirementText());
    }
    else ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "Player Controller: なし");

    if (const auto* motor = target->GetComponent<Components::CharacterMotorComponent>())
    {
        ImGui::Text("Motor enabled: %s", motor->ActiveInHierarchy() ? "true" : "false");
        const auto& velocity = motor->Velocity();
        ImGui::Text("Velocity: %.2f / %.2f / %.2f", velocity.x, velocity.y, velocity.z);
        ImGui::Text("Grounded: %s", motor->Grounded() ? "true" : "false");
        ImGui::Text("Vertical physics: %s", motor->vertical_physics ? "true" : "false");
    }
    else ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "Character Motor: なし");

    const auto position = target->GetTransform().WorldPosition();
    ImGui::Text("Position: %.2f / %.2f / %.2f", position.x, position.y, position.z);

    // ---- 衝突の出所 -------------------------------------------------------
    //
    // 「今どちらのバックエンドで当たっているか」が分からないと、
    // MeshCollider を置いても効いているのか判断できない。
    ImGui::Separator();
    ImGui::Text("Collision available: %s",
        object_collision_world.CollisionAvailable() ? "true" : "false");
    ImGui::TextUnformatted("Backend mode: Scene Colliders Only");
    ImGui::Text("Active colliders: %zu (blocking %zu / trigger %zu / mesh %zu)",
        object_collision_world.ActiveColliderCount(),
        object_collision_world.BlockingColliderCount(),
        object_collision_world.TriggerColliderCount(),
        object_collision_world.MeshColliderCount());
    const auto& ground_source = object_collision_world.LastGroundSource();
    const auto& sweep_source = object_collision_world.LastSweepSource();
    ImGui::Text("Ground hit from: %s (Object %s / Collider %u)",
        ReplayEngine::Scene::ToString(ground_source.backend),
        ground_source.object.ToString().c_str(), ground_source.collider);
    ImGui::Text("Wall hit from: %s (Object %s / Collider %u)",
        ReplayEngine::Scene::ToString(sweep_source.backend),
        sweep_source.object.ToString().c_str(), sweep_source.collider);

    if (ImGui::Button("衝突の診断ウィンドウを開く")) show_collision_diagnostics = true;
    ImGui::SameLine();
    ImGui::Checkbox("Collider を描画", &show_collider_debug_draw);
#endif
}
