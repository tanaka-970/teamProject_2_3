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
#include "../../RePlayEngine/Object/Registry/ComponentRegistry.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

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

namespace
{
    constexpr const char* builtin_editor_style_id = "builtin-replay-default";

    ReplayEngine::Editor::EditorStylePreset MakeDefaultEditorStylePreset()
    {
        ReplayEngine::Editor::EditorStylePreset preset;
        preset.id = builtin_editor_style_id;
        preset.name = "RePlay Default";
        preset.scope = ReplayEngine::Editor::EditorStylePresetScope::Personal;
        preset.source_path.clear();
        return preset;
    }
}

ReplayEngine::Editor::EditorStylePreset framework::capture_editor_style_preset() const
{
    ReplayEngine::Editor::EditorStylePreset preset;
    preset.button_scale = ui_button_scale;
    preset.font_scale = ui_font_scale;
    preset.text_color = ImVec4(ui_text_color[0], ui_text_color[1], ui_text_color[2], 1.0f);
    preset.tokens = ReplayEngine::Editor::EditorStyle::Tokens();
    for (const std::string& category : ReplayEngine::Core::ComponentRegistry::Categories())
        preset.category_colors[category] =
            ReplayEngine::Editor::EditorStyle::ComponentCategoryColor(category);
    return preset;
}

ReplayEngine::Editor::EditorStyleEditHistory::Snapshot
    framework::capture_editor_style_snapshot() const
{
    const auto preset = capture_editor_style_preset();
    ReplayEngine::Editor::EditorStyleEditHistory::Snapshot snapshot;
    snapshot.button_scale = preset.button_scale;
    snapshot.font_scale = preset.font_scale;
    snapshot.text_color = preset.text_color;
    snapshot.tokens = preset.tokens;
    snapshot.style_overridden = ui_style_overridden;
    if (active_editor_style_preset_index >= 0 &&
        active_editor_style_preset_index < static_cast<int>(editor_style_presets.size()))
    {
        snapshot.active_preset_id = editor_style_presets[
            static_cast<std::size_t>(active_editor_style_preset_index)].id;
    }
    snapshot.category_colors = preset.category_colors;
    return snapshot;
}

void framework::apply_editor_style_snapshot(
    const ReplayEngine::Editor::EditorStyleEditHistory::Snapshot& snapshot)
{
    if (snapshot.category_colors.empty())
        ReplayEngine::Editor::EditorStyle::ResetComponentCategoryColors();
    else
        ReplayEngine::Editor::EditorStyle::ReplaceComponentCategoryColors(
            snapshot.category_colors);
    ui_button_scale = snapshot.button_scale;
    ui_font_scale = snapshot.font_scale;
    ui_text_color[0] = snapshot.text_color.x;
    ui_text_color[1] = snapshot.text_color.y;
    ui_text_color[2] = snapshot.text_color.z;
    ReplayEngine::Editor::EditorStyle::SetTokens(snapshot.tokens);
    ui_style_overridden = snapshot.style_overridden;
    if (!snapshot.active_preset_id.empty())
    {
        const auto found = std::find_if(editor_style_presets.begin(), editor_style_presets.end(),
            [&snapshot](const auto& preset) { return preset.id == snapshot.active_preset_id; });
        if (found != editor_style_presets.end())
            active_editor_style_preset_index = static_cast<int>(
                std::distance(editor_style_presets.begin(), found));
    }
}

void framework::ensure_editor_style_presets_loaded()
{
    if (editor_style_presets_loaded) return;
    editor_style_presets_loaded = true;

    std::string load_error;
    const std::vector<ReplayEngine::Editor::EditorStylePreset> loaded =
        ReplayEngine::Editor::EditorStylePresetStore::LoadAll(load_error);
    editor_style_presets.clear();
    editor_style_presets.push_back(MakeDefaultEditorStylePreset());
    for (const auto& preset : loaded)
    {
        if (preset.id == builtin_editor_style_id) continue;
        editor_style_presets.push_back(preset);
    }

    std::string active_id;
    std::string active_error;
    const bool active_loaded =
        ReplayEngine::Editor::EditorStylePresetStore::LoadActivePresetId(
            active_id, active_error);
    active_editor_style_preset_index = 0;
    editor_style_active_selection_loaded = false;
    if (active_loaded)
    {
        for (std::size_t index = 0; index < editor_style_presets.size(); ++index)
        {
            if (editor_style_presets[index].id == active_id)
            {
                active_editor_style_preset_index = static_cast<int>(index);
                editor_style_active_selection_loaded = true;
                break;
            }
        }
    }

    const auto& preset = editor_style_presets[
        static_cast<std::size_t>(active_editor_style_preset_index)];
    if (preset.category_colors.empty())
        ReplayEngine::Editor::EditorStyle::ResetComponentCategoryColors();
    else
        ReplayEngine::Editor::EditorStyle::ReplaceComponentCategoryColors(
            preset.category_colors);
    ReplayEngine::Editor::EditorStyle::SetTokens(preset.tokens);
    ui_button_scale = preset.button_scale;
    ui_font_scale = preset.font_scale;
    ui_text_color[0] = preset.text_color.x;
    ui_text_color[1] = preset.text_color.y;
    ui_text_color[2] = preset.text_color.z;
    ui_style_overridden = preset.id != builtin_editor_style_id;
    (void)load_error;
    (void)active_error;
}

bool framework::switch_editor_style_preset(const std::string& id)
{
    ensure_editor_style_presets_loaded();
    const auto found = std::find_if(editor_style_presets.begin(), editor_style_presets.end(),
        [&id](const auto& preset) { return preset.id == id; });
    if (found == editor_style_presets.end()) return false;

    const auto before = capture_editor_style_snapshot();
    editor_style_history.Begin(before, "見た目プリセットを切り替え");
    if (found->category_colors.empty())
        ReplayEngine::Editor::EditorStyle::ResetComponentCategoryColors();
    else
        ReplayEngine::Editor::EditorStyle::ReplaceComponentCategoryColors(
            found->category_colors);
    ReplayEngine::Editor::EditorStyle::SetTokens(found->tokens);
    ui_button_scale = found->button_scale;
    ui_font_scale = found->font_scale;
    ui_text_color[0] = found->text_color.x;
    ui_text_color[1] = found->text_color.y;
    ui_text_color[2] = found->text_color.z;
    ui_style_overridden = found->id != builtin_editor_style_id;
    active_editor_style_preset_index = static_cast<int>(
        std::distance(editor_style_presets.begin(), found));
    editor_style_history.Commit(capture_editor_style_snapshot());
    std::string error;
    ReplayEngine::Editor::EditorStylePresetStore::SaveActivePresetId(
        found->id, error);
    configure_editor_style();
    return true;
}

bool framework::make_active_editor_style_preset_personal_copy()
{
    ensure_editor_style_presets_loaded();
    if (active_editor_style_preset_index < 0 ||
        active_editor_style_preset_index >= static_cast<int>(editor_style_presets.size()))
        return false;
    const auto source = editor_style_presets[
        static_cast<std::size_t>(active_editor_style_preset_index)];
    if (source.Editable() && source.id != builtin_editor_style_id) return true;

    auto copy = ReplayEngine::Editor::EditorStylePresetStore::DuplicateAsPersonal(
        source, source.name + " - Personal");
    const auto current = capture_editor_style_preset();
    copy.button_scale = current.button_scale;
    copy.font_scale = current.font_scale;
    copy.text_color = current.text_color;
    copy.tokens = current.tokens;
    copy.category_colors = current.category_colors;
    std::string error;
    if (!ReplayEngine::Editor::EditorStylePresetStore::Save(copy, error)) return false;
    editor_style_presets.push_back(std::move(copy));
    active_editor_style_preset_index = static_cast<int>(editor_style_presets.size()) - 1;
    ReplayEngine::Editor::EditorStylePresetStore::SaveActivePresetId(
        editor_style_presets.back().id, error);
    return true;
}

bool framework::save_active_editor_style_preset()
{
    ensure_editor_style_presets_loaded();
    if (active_editor_style_preset_index < 0 ||
        active_editor_style_preset_index >= static_cast<int>(editor_style_presets.size()))
        return false;
    const auto& active_preset = editor_style_presets[static_cast<std::size_t>(
        active_editor_style_preset_index)];
    if (!active_preset.Editable() || active_preset.id == builtin_editor_style_id)
    {
        if (!make_active_editor_style_preset_personal_copy()) return false;
    }

    ReplayEngine::Editor::EditorStylePreset& active = editor_style_presets[static_cast<std::size_t>(
        active_editor_style_preset_index)];
    const auto current = capture_editor_style_preset();
    active.button_scale = current.button_scale;
    active.font_scale = current.font_scale;
    active.text_color = current.text_color;
    active.tokens = current.tokens;
    active.category_colors = current.category_colors;
    std::string error;
    if (!ReplayEngine::Editor::EditorStylePresetStore::Save(active, error)) return false;
    ReplayEngine::Editor::EditorStylePresetStore::SaveActivePresetId(active.id, error);
    return true;
}

bool framework::undo_editor_style()
{
    auto snapshot = capture_editor_style_snapshot();
    std::string label;
    if (!editor_style_history.Undo(snapshot, label)) return false;
    apply_editor_style_snapshot(snapshot);
    std::string error;
    if (active_editor_style_preset_index >= 0 &&
        active_editor_style_preset_index < static_cast<int>(editor_style_presets.size()))
        ReplayEngine::Editor::EditorStylePresetStore::SaveActivePresetId(
            editor_style_presets[static_cast<std::size_t>(active_editor_style_preset_index)].id,
            error);
    configure_editor_style();
    return true;
}

bool framework::redo_editor_style()
{
    auto snapshot = capture_editor_style_snapshot();
    std::string label;
    if (!editor_style_history.Redo(snapshot, label)) return false;
    apply_editor_style_snapshot(snapshot);
    std::string error;
    if (active_editor_style_preset_index >= 0 &&
        active_editor_style_preset_index < static_cast<int>(editor_style_presets.size()))
        ReplayEngine::Editor::EditorStylePresetStore::SaveActivePresetId(
            editor_style_presets[static_cast<std::size_t>(active_editor_style_preset_index)].id,
            error);
    configure_editor_style();
    return true;
}

void framework::configure_editor_style()
{
    ensure_editor_style_presets_loaded();
    const float dpi = hwnd != nullptr
        ? static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f : 1.0f;
    ReplayEngine::Editor::EditorStyle::Apply(dpi);
    ImGui::GetIO().FontGlobalScale = dpi * ui_font_scale *
        (ReplayEngine::Editor::EditorStyle::Tokens().font_size / 15.0f);

    ImGuiStyle& style = ImGui::GetStyle();
    style.FramePadding.x *= ui_button_scale;
    style.FramePadding.y *= ui_button_scale;
    style.ItemSpacing.x  *= ui_button_scale;
    style.ItemSpacing.y  *= ui_button_scale;
    style.Colors[ImGuiCol_Text] =
        ImVec4(ui_text_color[0], ui_text_color[1], ui_text_color[2], 1.0f);

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
    if (editor_style_history.InTransaction() && !ImGui::IsAnyItemActive())
    {
        editor_style_history.Commit(capture_editor_style_snapshot());
        save_active_editor_style_preset();
    }
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
                ReplayEngine::Editor::EditorHelp::Item(
                    "button.scene.recent_scene", path.generic_u8string().c_str());
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
        const bool material_context = !project_browser_focused && !atlas_context &&
            !motion_workspace && material_editor_loaded &&
            selected_editor_object == editor_selection::asset;
        const bool scene_flow_context = !project_browser_focused &&
            scene_flow_editor_loaded && show_scene_flow_panel;
        const bool external_context = !atlas_context && !motion_workspace &&
            !scene_flow_context &&
            !material_context &&
            (project_browser_focused || selected_editor_object == editor_selection::asset ||
                selected_editor_object == editor_selection::world ||
                selected_editor_object == editor_selection::rendering);
        const bool scene_context = !atlas_context && !motion_workspace &&
            !material_context && !external_context;
        const bool scene_edit_blocked = scene_context && !object_editor_context.CanEdit();
        const bool can_undo = atlas_context ? sprite_atlas_history_cursor > 0
            : motion_workspace ? (motion_composition_loaded
                ? composition_edit_history.CanUndo() : motion_edit_history.CanUndo())
            : scene_flow_context ? scene_flow_edit_history.CanUndo()
            : material_context ? material_editor_history.CanUndo()
            : external_context ? external_file_history.CanUndo()
            : object_editor_context.History().CanUndo();
        if (scene_edit_blocked) ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
            ImGui::GetStyle().Alpha * 0.5f);
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, can_undo))
        {
            if (atlas_context) undo_sprite_atlas_edit();
            else if (motion_workspace) undo_motion_edit();
            else if (scene_flow_context) undo_scene_flow_edit();
            else if (material_context) undo_material_editor();
            else if (external_context) undo_external_file_edit();
            else object_editor_context.Undo();
        }
        if (scene_edit_blocked)
        {
            ReplayEngine::Editor::EditorHelp::Item("button.edit.undo_blocked",
                u8"実行中は元に戻せません。Shift+F5 で停止してください。");
            ImGui::PopStyleVar();
        }
        const bool can_redo = atlas_context ? sprite_atlas_history_cursor < sprite_atlas_history.size()
            : motion_workspace ? (motion_composition_loaded
                ? composition_edit_history.CanRedo() : motion_edit_history.CanRedo())
            : scene_flow_context ? scene_flow_edit_history.CanRedo()
            : material_context ? material_editor_history.CanRedo()
            : external_context ? external_file_history.CanRedo()
            : object_editor_context.History().CanRedo();
        if (scene_edit_blocked) ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
            ImGui::GetStyle().Alpha * 0.5f);
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, can_redo))
        {
            if (atlas_context) redo_sprite_atlas_edit();
            else if (motion_workspace) redo_motion_edit();
            else if (scene_flow_context) redo_scene_flow_edit();
            else if (material_context) redo_material_editor();
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
            bool style_save_requested = false;
            ensure_editor_style_presets_loaded();

            ImGui::TextDisabled(u8"見た目プリセット");
            const int preset_count = static_cast<int>(editor_style_presets.size());
            if (active_editor_style_preset_index < 0 ||
                active_editor_style_preset_index >= preset_count)
                active_editor_style_preset_index = 0;
            const std::string active_style_id = editor_style_presets[
                static_cast<std::size_t>(active_editor_style_preset_index)].id;
            const auto& initial_style_preset = editor_style_presets[
                static_cast<std::size_t>(active_editor_style_preset_index)];
            const std::string active_style_label = initial_style_preset.name +
                (initial_style_preset.Editable() ? u8"  [個人]" : u8"  [共有]");
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::BeginCombo("##EditorStylePreset", active_style_label.c_str()))
            {
                for (const auto& preset : editor_style_presets)
                {
                    const std::string label = preset.name +
                        (preset.Editable() ? u8"  [個人]" : u8"  [共有]");
                    const bool selected = preset.id == active_style_id;
                    if (ImGui::Selectable(label.c_str(), selected))
                        switch_editor_style_preset(preset.id);
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            const auto& current_style_preset = editor_style_presets[
                static_cast<std::size_t>(active_editor_style_preset_index)];
            if (editor_style_name_buffer_id != current_style_preset.id)
            {
                std::snprintf(editor_style_name_buffer.data(),
                    editor_style_name_buffer.size(), "%s", current_style_preset.name.c_str());
                editor_style_name_buffer_id = current_style_preset.id;
            }
            ImGui::SetNextItemWidth(260.0f);
            ImGui::InputText("##EditorStylePresetName", editor_style_name_buffer.data(),
                editor_style_name_buffer.size());
            ImGui::SameLine();
            if (ImGui::Button(u8"名前を付けて保存") &&
                editor_style_name_buffer[0] != '\0')
            {
                auto created = capture_editor_style_preset();
                created.id = ReplayEngine::Editor::EditorStylePresetStore::MakeUniqueId();
                created.name = editor_style_name_buffer.data();
                created.scope = ReplayEngine::Editor::EditorStylePresetScope::Personal;
                std::string error;
                if (ReplayEngine::Editor::EditorStylePresetStore::Save(created, error))
                {
                    editor_style_presets.push_back(std::move(created));
                    active_editor_style_preset_index =
                        static_cast<int>(editor_style_presets.size()) - 1;
                    ReplayEngine::Editor::EditorStylePresetStore::SaveActivePresetId(
                        editor_style_presets.back().id, error);
                    editor_style_name_buffer_id = editor_style_presets.back().id;
                }
                else object_editor_context.SetStatus(error);
            }
            ImGui::SameLine();
            if (ImGui::Button(u8"個人コピー"))
            {
                if (make_active_editor_style_preset_personal_copy())
                {
                    editor_style_name_buffer_id.clear();
                    style_changed = true;
                    style_save_requested = true;
                }
            }

            const bool active_style_editable = editor_style_presets[
                static_cast<std::size_t>(active_editor_style_preset_index)].Editable();
            ImGui::SameLine();
            if (active_style_editable && ImGui::Button(u8"チーム共有コピー"))
            {
                const auto source = editor_style_presets[
                    static_cast<std::size_t>(active_editor_style_preset_index)];
                ReplayEngine::Editor::EditorStylePreset published;
                std::string error;
                if (ReplayEngine::Editor::EditorStylePresetStore::PublishSharedCopy(
                    source, source.name + " Shared", published, error))
                    editor_style_presets.push_back(std::move(published));
                else object_editor_context.SetStatus(error);
            }
            ImGui::SameLine();
            if (active_style_editable && active_editor_style_preset_index != 0 &&
                ImGui::Button(u8"削除"))
            {
                const auto deleting = editor_style_presets[
                    static_cast<std::size_t>(active_editor_style_preset_index)];
                std::string error;
                if (ReplayEngine::Editor::EditorStylePresetStore::DeletePersonal(
                    deleting, error))
                {
                    editor_style_presets.erase(editor_style_presets.begin() +
                        active_editor_style_preset_index);
                    active_editor_style_preset_index = 0;
                    switch_editor_style_preset(builtin_editor_style_id);
                    editor_style_name_buffer_id.clear();
                }
                else object_editor_context.SetStatus(error);
            }

            ImGui::TextDisabled(u8"大きさ");
            static float button_scale_edit = 1.0f;
            static bool button_scale_editing = false;
            if (!button_scale_editing) button_scale_edit = ui_button_scale;
            ImGui::SetNextItemWidth(180.0f);
            const auto button_scale_before = capture_editor_style_snapshot();
            const bool button_scale_changed = ImGui::SliderFloat(
                u8"ボタンの余白", &button_scale_edit, 0.6f, 3.0f, "x%.2f");
            if (button_scale_changed && !editor_style_history.InTransaction())
                editor_style_history.Begin(button_scale_before, "ボタンの余白を変更");
            button_scale_editing |= button_scale_changed;
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                ui_button_scale = button_scale_edit;
                style_changed = true;
                if (editor_style_history.InTransaction())
                    editor_style_history.Commit(capture_editor_style_snapshot());
                style_save_requested = true;
                button_scale_editing = false;
            }
            static float font_scale_edit = 1.0f;
            static bool font_scale_editing = false;
            if (!font_scale_editing) font_scale_edit = ui_font_scale;
            ImGui::SetNextItemWidth(180.0f);
            const auto font_scale_before = capture_editor_style_snapshot();
            const bool font_scale_changed = ImGui::SliderFloat(
                u8"文字の大きさ", &font_scale_edit, 0.7f, 2.5f, "x%.2f");
            if (font_scale_changed && !editor_style_history.InTransaction())
                editor_style_history.Begin(font_scale_before, "文字の大きさを変更");
            font_scale_editing |= font_scale_changed;
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                ui_font_scale = font_scale_edit;
                style_changed = true;
                if (editor_style_history.InTransaction())
                    editor_style_history.Commit(capture_editor_style_snapshot());
                style_save_requested = true;
                font_scale_editing = false;
            }

            // マテリアルスロットのテクスチャ見本。文字高に対する倍率で持つ。
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::SliderFloat(u8"テクスチャ見本の大きさ",
                &ui_texture_preview_scale, 1.5f, 12.0f, "x%.1f"))
            {
                style_changed = true;
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) style_save_requested = true;

            ImGui::Separator();
            ImGui::TextDisabled(u8"文字色");
            ImGui::SetNextItemWidth(200.0f);
            const auto text_color_before = capture_editor_style_snapshot();
            const bool text_color_changed = ImGui::ColorEdit3(u8"##UITextColor", ui_text_color);
            if (text_color_changed && !editor_style_history.InTransaction())
                editor_style_history.Begin(text_color_before, "文字色を変更");
            style_changed |= text_color_changed;
            if (text_color_changed)
            {
                auto tokens = ReplayEngine::Editor::EditorStyle::Tokens();
                tokens.text = ImVec4(ui_text_color[0], ui_text_color[1], ui_text_color[2], 1.0f);
                ReplayEngine::Editor::EditorStyle::SetTokens(tokens);
            }
            if (ImGui::IsItemDeactivatedAfterEdit() &&
                editor_style_history.InTransaction())
            {
                editor_style_history.Commit(capture_editor_style_snapshot());
                style_save_requested = true;
            }

            ImGui::Separator();
            ImGui::TextDisabled(u8"Component カテゴリ色");
            ImGui::TextDisabled(u8"見出しと仕切りのアクセント色を設定できます");
            if (ImGui::TreeNodeEx(u8"カテゴリごとの色", 0))
            {
                for (const std::string& category :
                    ReplayEngine::Core::ComponentRegistry::Categories())
                {
                    ImGui::PushID(category.c_str());
                    ImVec4 color = ReplayEngine::Editor::EditorStyle::ComponentCategoryColor(category);
                    ImGui::SetNextItemWidth(200.0f);
                    const auto category_color_before = capture_editor_style_snapshot();
                    const bool color_changed = ImGui::ColorEdit3(category.c_str(), &color.x,
                        ImGuiColorEditFlags_NoAlpha);
                    if (color_changed && !editor_style_history.InTransaction())
                        editor_style_history.Begin(category_color_before,
                            "Component カテゴリ色を変更");
                    if (color_changed)
                    {
                        ReplayEngine::Editor::EditorStyle::SetComponentCategoryColor(
                            category, color);
                        style_changed = true;
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit() &&
                        editor_style_history.InTransaction())
                    {
                        editor_style_history.Commit(capture_editor_style_snapshot());
                        style_save_requested = true;
                    }
                    ImGui::PopID();
                }
                if (ImGui::SmallButton(u8"カテゴリ色を既定へ戻す"))
                {
                    editor_style_history.Begin(capture_editor_style_snapshot(),
                        "Component カテゴリ色を既定へ戻す");
                    ReplayEngine::Editor::EditorStyle::ResetComponentCategoryColors();
                    editor_style_history.Commit(capture_editor_style_snapshot());
                    style_changed = true;
                    style_save_requested = true;
                }
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx(u8"詳細", 0))
            {
                ImGui::TextDisabled(u8"変更は個人プリセットへ保存されます");
                auto tokens = ReplayEngine::Editor::EditorStyle::Tokens();
                auto draw_token_color = [&](const char* label, ImVec4& value)
                {
                    const auto before = capture_editor_style_snapshot();
                    const bool changed = ImGui::ColorEdit3(label, &value.x,
                        ImGuiColorEditFlags_NoAlpha);
                    if (changed && !editor_style_history.InTransaction())
                        editor_style_history.Begin(before, "見た目の色を変更");
                    if (changed)
                    {
                        ReplayEngine::Editor::EditorStyle::SetTokens(tokens);
                        style_changed = true;
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit() && editor_style_history.InTransaction())
                    {
                        editor_style_history.Commit(capture_editor_style_snapshot());
                        style_save_requested = true;
                    }
                };
                auto draw_token_value = [&](const char* label, float& value,
                    float minimum, float maximum, const char* format)
                {
                    const auto before = capture_editor_style_snapshot();
                    const bool changed = ImGui::SliderFloat(label, &value, minimum, maximum, format);
                    if (changed && !editor_style_history.InTransaction())
                        editor_style_history.Begin(before, "見た目の大きさを変更");
                    if (changed)
                    {
                        ReplayEngine::Editor::EditorStyle::SetTokens(tokens);
                        style_changed = true;
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit() && editor_style_history.InTransaction())
                    {
                        editor_style_history.Commit(capture_editor_style_snapshot());
                        style_save_requested = true;
                    }
                };
                if (ImGui::TreeNodeEx(u8"色", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    draw_token_color(u8"全体の背景", tokens.main_background);
                    draw_token_color(u8"パネルの背景", tokens.panel_background);
                    draw_token_color(u8"見出しの背景", tokens.header_background);
                    draw_token_color(u8"ツールバーの背景", tokens.toolbar_background);
                    draw_token_color(u8"枠線", tokens.border);
                    draw_token_color(u8"補助文字", tokens.secondary_text);
                    draw_token_color(u8"無効な文字", tokens.disabled_text);
                    draw_token_color(u8"強調", tokens.accent);
                    draw_token_color(u8"選択中", tokens.selection);
                    draw_token_color(u8"ホバー", tokens.hover);
                    draw_token_color(u8"操作中", tokens.active);
                    draw_token_color(u8"成功", tokens.success);
                    draw_token_color(u8"警告", tokens.warning);
                    draw_token_color(u8"エラー", tokens.error);
                    draw_token_color(u8"見つからない項目", tokens.missing);
                    draw_token_color(u8"Prefab", tokens.prefab);
                    draw_token_color(u8"通常の当たり判定", tokens.normal_collider);
                    draw_token_color(u8"トリガーの当たり判定", tokens.trigger_collider);
                    draw_token_color(u8"主な当たり判定", tokens.primary_collider);
                    ImGui::TreePop();
                }
                if (ImGui::TreeNodeEx(u8"余白と大きさ", 0))
                {
                    draw_token_value(u8"ウィンドウの余白", tokens.padding, 0.0f, 40.0f, "%.0f");
                    draw_token_value(u8"項目の間隔", tokens.item_spacing, 0.0f, 30.0f, "%.0f");
                    draw_token_value(u8"パネルの間隔", tokens.panel_spacing, 0.0f, 40.0f, "%.0f");
                    draw_token_value(u8"見出しの高さ", tokens.header_height, 12.0f, 80.0f, "%.0f");
                    draw_token_value(u8"ツールバーの高さ", tokens.toolbar_height, 12.0f, 80.0f, "%.0f");
                    draw_token_value(u8"入力欄の高さ", tokens.input_height, 12.0f, 80.0f, "%.0f");
                    draw_token_value(u8"基準の文字サイズ", tokens.font_size, 8.0f, 40.0f, "%.0f");
                    ImGui::TreePop();
                }
                if (ImGui::TreeNodeEx(u8"枠", 0))
                {
                    draw_token_value(u8"枠線の太さ", tokens.border_thickness, 0.0f, 4.0f, "%.1f");
                    draw_token_value(u8"角の丸み", tokens.corner_radius, 0.0f, 20.0f, "%.0f");
                    ImGui::TreePop();
                }
                if (ImGui::SmallButton(u8"詳細を既定へ戻す"))
                {
                    editor_style_history.Begin(capture_editor_style_snapshot(), "詳細を既定へ戻す");
                    ReplayEngine::Editor::EditorStyle::ResetTokens();
                    const auto& defaults = ReplayEngine::Editor::EditorStyle::Tokens();
                    ui_text_color[0] = defaults.text.x;
                    ui_text_color[1] = defaults.text.y;
                    ui_text_color[2] = defaults.text.z;
                    editor_style_history.Commit(capture_editor_style_snapshot());
                    style_changed = true;
                    style_save_requested = true;
                }
                ImGui::TreePop();
            }

            ImGui::Separator();
            if (ImGui::MenuItem(u8"カテゴリ色を元に戻す", nullptr, false,
                editor_style_history.CanUndo()))
            {
                undo_editor_style();
                style_changed = true;
                style_save_requested = true;
            }
            if (ImGui::MenuItem(u8"カテゴリ色をやり直す", nullptr, false,
                editor_style_history.CanRedo()))
            {
                redo_editor_style();
                style_changed = true;
                style_save_requested = true;
            }
            if (ImGui::MenuItem(u8"大きめにする"))
            {
                editor_style_history.Begin(capture_editor_style_snapshot(),
                    "見た目を大きめにする");
                ui_button_scale = 1.8f;
                ui_font_scale = 1.3f;
                editor_style_history.Commit(capture_editor_style_snapshot());
                style_changed = true;
                style_save_requested = true;
            }
            if (ImGui::MenuItem(u8"既定へ戻す"))
            {
                switch_editor_style_preset(builtin_editor_style_id);
                editor_style_name_buffer_id.clear();
            }

            // 確定した見た目の変更を反映する。
            if (style_changed)
            {
                ui_style_overridden = true;
                configure_editor_style();
            }
            if (style_save_requested) save_active_editor_style_preset();
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    // メニュー名を "Play" にしない。
    // ツールバーの実行ボタンと同名だと、どちらを押せばよいか区別できない。
    // 実際にそれで迷子になった。
    if (ImGui::BeginMenu(u8"実行"))
    {
        if (ImGui::MenuItem(u8"▲ 実行", "F5", false,
            !object_scene_play_mode && !object_editor_play_loading))
            enter_object_play_mode();
        if (ImGui::MenuItem(object_scene_paused ? u8"▲ 再開" : u8"││ 一時停止", nullptr,
            false, object_scene_play_mode)) object_scene_paused = !object_scene_paused;
        if (ImGui::MenuItem(u8"■ 停止", "Shift+F5", false,
            object_scene_play_mode || object_editor_play_loading))
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
