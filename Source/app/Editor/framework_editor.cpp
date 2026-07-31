#include "framework.h"
#include "../../RePlayEngine/Scene/Legacy/LegacySceneDocumentSerializer.h"
#include "../../RePlayEngine/Components/Gameplay/CharacterMotorComponent.h"
#include "../../RePlayEngine/Components/Gameplay/PlayerControllerComponent.h"
#include "../../RePlayEngine/Components/Gameplay/PlayerInputComponent.h"
#include "shader.h"
#include "texture.h"
#include "skinned_mesh.h"

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
    const std::string preserved_scene_name = editor_scene_document.SceneName();
    auto& post = post_process.GetSettings();
    camera_position = { 0.0f, 4.0f, -10.0f, 1.0f };
    light_direction = { 0.300f, 0.000f, 0.500f, 0.0f };
    translation = { 0.0f, 0.0f, 0.0f };
    scaling = { 0.01f, 0.01f, 0.01f };
    rotation = { 81.0f, 8.5f, 180.0f };
    material_color = { 1, 1, 1, 1 };
    background_color = { 46.0f / 255.0f, 56.0f / 255.0f, 61.0f / 255.0f, 1.0f };
    draw_background_image = false;
    animate_model = true;
    use_pbr_skin = enable_toon_shader = enable_unlit_shader = true;
    enable_outline_shader = enable_pbr_shadow_shader = true;
    enable_luminance_shader = enable_final_pass_shader = true;
    enable_bloom_shader = enable_fxaa_shader = true;
    enable_vignette_shader = enable_static_meshes = false;
    enable_scene_game = enable_stage_shader = true;
    enable_stage_render = false;
    stage_asset_placed = false;
    active_stage_placement_id = 0;
    editor_scene_document.Clear();
    editor_scene_document.SetSceneName(preserved_scene_name);
    runtime_scene_document.Clear();
    scene_undo_stack.Clear();
    selected_scene_entity_id = 0;
    selected_scene_entity_ids.clear();
    copied_scene_entities.clear();
    enable_particles = enable_trail = false;
    enable_deferred = true;
    shading_per_stage = SHADING_MODEL_PBR;
    stage_texture_wrap = true;
    stage_texture_contrast = 1.20f;
    if (game_scene) game_scene->Gameplay().ResetGameplay();
    animation_clip_index = game_scene && game_scene->Gameplay().GetPlayer().clip_idle >= 0
        ? game_scene->Gameplay().GetPlayer().clip_idle : 0;
    animation_tick = 0.0f;
    animation_speed = 1.0f;
    animation_loop = true;
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
    shading_model_override = shading_per_skinned[0] = shading_per_static[0] = SHADING_MODEL_PBR;
    outline_per_skinned[0] = outline_per_static[0] = false;
    shader_layers_skinned[0].Clear();
    shader_layers_static[0].Clear();
    stage_shader_layers.Clear();
    character_profiles_skinned[0] = ReplayEngine::Rendering::CharacterMaterialProfile{};
    character_profiles_static[0] = ReplayEngine::Rendering::CharacterMaterialProfile{};
    stage_character_profile = ReplayEngine::Rendering::CharacterMaterialProfile{};
    shader_preset_status.clear();
    lights.data.light_counts.x = 1;
    lights.data.point_lights[0].position = { 3, 4, -24, 18 };
    lights.data.point_lights[0].color = { 0.55f, 0.75f, 1, 2 };
    apply_toon_preset(0);
}

void framework::configure_editor_style()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 2.0f;
    style.ChildRounding = 2.0f;
    style.FrameRounding = 2.0f;
    style.PopupRounding = 2.0f;
    style.TabRounding = 2.0f;
    style.WindowBorderSize = 1.0f;
    style.FramePadding = { 7.0f, 5.0f };
    style.ItemSpacing = { 8.0f, 6.0f };
    style.Colors[ImGuiCol_WindowBg] = { 0.075f, 0.082f, 0.094f, 1.0f };
    style.Colors[ImGuiCol_TitleBg] = { 0.055f, 0.061f, 0.071f, 1.0f };
    style.Colors[ImGuiCol_TitleBgActive] = { 0.075f, 0.20f, 0.34f, 1.0f };
    style.Colors[ImGuiCol_Header] = { 0.10f, 0.30f, 0.48f, 0.78f };
    style.Colors[ImGuiCol_HeaderHovered] = { 0.13f, 0.39f, 0.62f, 0.86f };
    style.Colors[ImGuiCol_Button] = { 0.10f, 0.27f, 0.42f, 1.0f };
    style.Colors[ImGuiCol_ButtonHovered] = { 0.13f, 0.39f, 0.62f, 1.0f };
    style.Colors[ImGuiCol_TabActive] = { 0.10f, 0.31f, 0.50f, 1.0f };
}

void framework::set_editor_workspace(editor_workspace workspace)
{
    if (active_editor_workspace == workspace) return;
    if (active_editor_workspace == editor_workspace::placement && !stage_asset_placed)
    {
        enable_stage_render = false;
    }
    active_editor_workspace = workspace;
    editor_layout_dirty = true;
    switch (active_editor_workspace)
    {
    case editor_workspace::placement:
        selected_editor_object = editor_selection::stage;
        enable_stage_render = skinned_meshes[1] != nullptr || stage_gltf_model != nullptr;
        break;
    case editor_workspace::modeling:  selected_editor_object = editor_selection::stage; break;
    case editor_workspace::animation: selected_editor_object = editor_selection::player; break;
    case editor_workspace::rendering: selected_editor_object = editor_selection::rendering; break;
    case editor_workspace::shader_adjustment:
        if (selected_editor_object != editor_selection::player &&
            selected_editor_object != editor_selection::stage &&
            selected_editor_object != editor_selection::rendering)
            selected_editor_object = editor_selection::player;
        break;
    default: selected_editor_object = editor_selection::world; break;
    }
}

void framework::set_edit_mode(bool enabled)
{
    if (edit_mode_active == enabled) return;
    if (!enabled)
        runtime_scene_document = editor_scene_document;
    else
        runtime_scene_document.Clear();
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

void framework::draw_editor_toolbar()
{
    const char* workspaces[] = {
        "基本", "配置", "モデリング", "アニメーション", "レンダリング", "シェーダー調整"
    };
    int workspace = static_cast<int>(active_editor_workspace);
    ImGui::SetNextItemWidth(130.0f);
    if (ImGui::Combo("##Workspace", &workspace, workspaces, IM_ARRAYSIZE(workspaces)))
    {
        set_editor_workspace(static_cast<editor_workspace>(workspace));
    }
    ImGui::SameLine();
    draw_scene_document_toolbar();
    ImGui::SameLine();
    if (ImGui::Button("シーン初期化")) reset_editor_values();
    ImGui::SameLine();
    if (ImGui::Button(edit_mode_active ? "編集モード: ON  F3" : "プレイモード  F3"))
    {
        set_edit_mode(!edit_mode_active);
    }
    ImGui::SameLine();
    if (ImGui::Button(is_fullscreen() ? "ウィンドウ表示  F11" : "全画面表示  F11")) toggle_fullscreen();
    ImGui::SameLine();
    if (ImGui::Button("ゲーム表示  F1")) editor_hide_requested = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    if (focus_search_requested)
    {
        ImGui::SetKeyboardFocusHere();
        focus_search_requested = false;
    }
    ImGui::InputTextWithHint(
        "##FeatureSearch", "機能を検索...", editor_search_text, IM_ARRAYSIZE(editor_search_text));
    search_input_active = ImGui::IsItemActive();
    if (ImGui::IsItemActivated()) set_edit_mode(true);
    if (search_input_active && ImGui::IsKeyPressed(VK_ESCAPE))
    {
        editor_search_text[0] = '\0';
        ImGui::ClearActiveID();
        search_input_active = false;
    }
    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();
    ImGui::Text("%ux%u  |  %.1f FPS  |  %.2f ms", client_width, client_height,
        ImGui::GetIO().Framerate, 1000.0f / (ImGui::GetIO().Framerate > 0.0f ? ImGui::GetIO().Framerate : 1.0f));
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
    result("プレイヤーを編集", "player プレイヤー モデル", editor_selection::player);
    result("ステージを編集", "stage ステージ 地形", editor_selection::stage);
    result("平行光源を編集", "directional light ライト 光源 pbr ibl shadow", editor_selection::directional_light);
    result("点光源を編集", "point light 点光源 ライト", editor_selection::point_lights);
    result("描画設定を開く", "render rendering 描画 deferred shader", editor_selection::rendering);
    result("ポスト処理を開く", "post process ポスト bloom fxaa", editor_selection::post_process);
    if (matches("edit play mode 編集 プレイ") && ImGui::Selectable("編集／プレイモードを切り替える"))
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
        // 移行後は旧 Player の固定項目を出さない。
        // Player は下の GameObject ツリーへ通常の GameObject として現れる。
        // 同じ Player を 2 か所で編集できる状態を残さないため。
        if (!object_player_active)
        {
            item("Legacy 旧プレイヤー（未変換）", editor_selection::player);
        }
        item(stage_asset_placed ? "ステージ（配置済み）" : "ステージ素材（未配置）",
            editor_selection::stage);
        if (ImGui::TreeNodeEx("ライト", ImGuiTreeNodeFlags_DefaultOpen))
        {
            item("平行光源", editor_selection::directional_light);
            item("点光源", editor_selection::point_lights);
            ImGui::TreePop();
        }
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
        // 移行状態で判定する。Component の有無では判定しない。
        // 一度変換したら二度と変換ボタンを出さない。
        const auto& migration = active_object_scene().Services().PlayerMigration();
        if (migration.Migrated())
        {
            ImGui::TextDisabled("旧 Player は変換済みです（Player ObjectID: %s）",
                migration.MigratedObject().ToString().c_str());
        }
        else if (!object_scene_play_mode)
        {
            if (ImGui::Button("旧 Player を GameObject へ変換"))
            {
                convert_legacy_player_to_gameobject();
            }
        }
        ImGui::Separator();

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

void framework::draw_project_panel()
{
    ImGui::Begin("プロジェクト");
    if (ImGui::CollapsingHeader("シーン", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("現在: %s  [%s]", editor_scene_document.SceneName().c_str(),
            editor_scene_document.SceneIdentifier().c_str());
        if (ImGui::Button("新しいシーン...")) ImGui::OpenPopup("NewScenePopup");
        if (ImGui::BeginPopupModal("NewScenePopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            static char new_scene_name[256] = "新しいシーン";
            ImGui::InputText("シーン名", new_scene_name, IM_ARRAYSIZE(new_scene_name));
            if (ImGui::Button("作成", { 100.0f, 0.0f }))
            {
                create_scene_document(new_scene_name);
                strncpy_s(new_scene_name, "新しいシーン", _TRUNCATE);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("キャンセル", { 100.0f, 0.0f })) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        std::error_code scene_error;
        const std::filesystem::path scene_folder = std::filesystem::path("resources") / "Scenes";
        if (std::filesystem::exists(scene_folder, scene_error))
        {
            std::vector<std::filesystem::path> scene_paths;
            for (const auto& entry : std::filesystem::directory_iterator(scene_folder, scene_error))
            {
                // ここが列挙するのは旧ステージ配置記録 (.replaystage)。
                // 新しい GameObject シーン (.replayscene) は
                // load_object_scene / save_object_scene が扱うため、ここへは出さない。
                if (scene_error || !entry.is_regular_file() ||
                    entry.path().extension() !=
                        ReplayEngine::Scene::Legacy::LegacySceneDocumentSerializer::file_extension)
                    continue;
                scene_paths.push_back(entry.path());
            }
            std::sort(scene_paths.begin(), scene_paths.end());
            for (const auto& scene_path : scene_paths)
            {
                const bool current = scene_path.lexically_normal() == current_scene_path.lexically_normal();
                ImGui::PushID(scene_path.generic_u8string().c_str());
                if (ImGui::Selectable(scene_path.stem().u8string().c_str(), current))
                {
                    save_scene_document(false);
                    current_scene_path = scene_path;
                    load_scene_document(false);
                }
                ImGui::PopID();
            }
        }
        ImGui::Separator();
    }
    if (ImGui::Button("モデルファイルを取り込む...")) browse_stage_asset();
    ImGui::SameLine();
    if (ImGui::Button("Prefabを配置...")) load_prefab();
    ImGui::SameLine();
    ImGui::TextDisabled("FBXキャッシュ / GLB / glTF");
    if (async_stage_load_active)
    {
        ImGui::ProgressBar(async_asset_manager.Progress(), ImVec2(-1.0f, 0.0f), "モデルを確認中");
    }
    if (!selected_stage_asset_path.empty())
    {
        ImGui::TextWrapped("選択中: %s", selected_stage_asset_path.c_str());
        ImGui::TextWrapped("状態: %s", stage_asset_status.c_str());
        if (!selected_stage_cache_path.empty())
            ImGui::TextWrapped("キャッシュ: %s", selected_stage_cache_path.c_str());
    }
    ImGui::Separator();
    ImGui::Text("登録アセット: %zu", asset_database.Records().size());
    for (const auto& asset : asset_database.Records())
    {
        ImGui::BulletText("%s  [%s]", asset.display_name.c_str(), asset.guid.substr(0, 8).c_str());
        ImGui::Indent();
        ImGui::TextDisabled("%s", asset.source_path.generic_u8string().c_str());
        ImGui::Unindent();
    }
    ImGui::Separator();
    ImGui::Text("シーン配置記録: %zu", editor_scene_document.Entities().size());
    for (const auto& entity : editor_scene_document.Entities())
    {
        ImGui::BulletText("%s  [%s]", entity.name.c_str(), entity.identifier.c_str());
        if (entity.model_renderer)
        {
            ImGui::Indent();
            ImGui::TextDisabled("Asset: %s  GUID: %.8s",
                entity.model_renderer->asset_name.empty() ? "（旧データ）" :
                    entity.model_renderer->asset_name.c_str(),
                entity.model_renderer->asset_guid.c_str());
            ImGui::Unindent();
        }
    }
    ImGui::Separator();
    ImGui::TextDisabled("現在のシーンが読み込んでいる素材");
    ImGui::BulletText("プレイヤー / AllAnimation1.cereal");
    ImGui::BulletText("ステージ素材 / ファイルから選択");
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
    ImGui::TextColored({ 0.45f, 0.85f, 0.55f, 1.0f }, "[正常] RePlayランタイム動作中");
    ImGui::Text("現在のモード: %s", edit_mode_active ? "編集（ゲーム停止）" : "プレイ（WASD有効）");
    ImGui::Text("画面サイズ: %u x %u", client_width, client_height);
    ImGui::TextUnformatted("描画方式: Deferred（固定）");
    const char* outputs[] = { "Final", "HDR Scene", "Bloom", "Deferred Lit",
        "GBuffer Base Color", "GBuffer Normal", "GBuffer Material", "Depth" };
    ImGui::Text("出力: %s", outputs[render_graph.OutputIndex()]);
    ImGui::TextDisabled("Ctrl+S: 保存  Ctrl+Z/Y: 元に戻す/やり直す  Ctrl+D: 複製");
    ImGui::TextDisabled("F1: エディタ表示  F2: 出力  F3: 編集/プレイ  F11: 全画面");
    ImGui::End();
}

void framework::draw_workspace_panel()
{
    ImGui::Begin("ワークスペース");
    switch (active_editor_workspace)
    {
    case editor_workspace::placement:
        ImGui::TextUnformatted("オブジェクト配置Workspace");
        ImGui::TextDisabled("モデルやPrefabを選び、Transformを調整して複数のEntityとして配置します。");
        draw_stage_placement_controls();
        break;
    case editor_workspace::modeling:
        ImGui::TextUnformatted("モデリングWorkspace");
        ImGui::TextDisabled("形状編集用のテーブルです。配置操作は配置Workspaceへ分離しています。");
        if (ImGui::Button("ステージ素材を選択")) selected_editor_object = editor_selection::stage;
        ImGui::SameLine();
        if (ImGui::Button("配置Workspaceへ")) set_editor_workspace(editor_workspace::placement);
        break;
    case editor_workspace::animation:
        ImGui::TextUnformatted("アニメーションWorkspace");
        ImGui::TextDisabled("今後、タイムラインとアニメーション編集をここへ登録します。");
        if (ImGui::Button("プレイヤーを選択")) selected_editor_object = editor_selection::player;
        ImGui::SameLine();
        ImGui::Checkbox("プレビュー", &animate_model);
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
        draw_editor_toolbar();
        draw_runtime_mode_banner();
        ImGui::EndMenuBar();
    }

    const ImGuiID dockspace_id = ImGui::GetID("RePlayEditorDockSpaceJP2");
    ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
    if (!editor_layout_checked || editor_layout_dirty)
    {
        editor_layout_checked = true;
        const bool rebuild = editor_layout_dirty;
        editor_layout_dirty = false;
        ImGuiDockNode* root = ImGui::DockBuilderGetNode(dockspace_id);
        if (rebuild || !root || !root->IsSplitNode())
        {
            ImGui::DockBuilderRemoveNode(dockspace_id);
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);
            ImGuiID center = dockspace_id;
            float left_ratio = 0.20f;
            float right_ratio = 0.27f;
            float bottom_ratio = 0.27f;
            if (active_editor_workspace == editor_workspace::placement) right_ratio = 0.36f;
            if (active_editor_workspace == editor_workspace::modeling) right_ratio = 0.33f;
            if (active_editor_workspace == editor_workspace::animation) bottom_ratio = 0.36f;
            if (active_editor_workspace == editor_workspace::rendering) left_ratio = 0.16f;
            if (active_editor_workspace == editor_workspace::shader_adjustment)
            {
                left_ratio = 0.16f;
                right_ratio = 0.40f;
                bottom_ratio = 0.20f;
            }
            ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, left_ratio, nullptr, &center);
            ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, right_ratio, nullptr, &center);
            ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, bottom_ratio, nullptr, &center);
            ImGui::DockBuilderDockWindow("階層", left);
            ImGui::DockBuilderDockWindow("インスペクター", right);
            ImGui::DockBuilderDockWindow("プロジェクト", bottom);
            ImGui::DockBuilderDockWindow("コンソール", bottom);
            ImGui::DockBuilderDockWindow("ワークスペース", bottom);
            ImGui::DockBuilderFinish(dockspace_id);
        }
    }
    ImGui::End();

    draw_scene_hierarchy();
    draw_inspector();
    draw_project_panel();
    draw_console_panel();
    draw_workspace_panel();
    draw_search_results();
    handle_viewport_selection();
}


// ---------------------------------------------------------------------------
// 実行モード表示と Player 診断
// ---------------------------------------------------------------------------
//
// 「動かない原因が入力なのか Edit Mode なのか分からない」状態を解消するための表示。

void framework::draw_runtime_mode_banner()
{
    ImGui::Separator();

    if (object_scene_play_mode)
    {
        ImGui::TextColored(ImVec4(0.4f, 0.95f, 0.5f, 1.0f), "PLAY MODE");
        ImGui::TextDisabled("実行シーンで動作中 / 入力有効");
    }
    else if (object_runtime_active())
    {
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "RUNNING");
        ImGui::TextDisabled("編集シーンをそのまま実行中 / 入力有効");
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "EDIT MODE");
        ImGui::TextDisabled("編集中 / 物理と入力は停止 (F3 で切替、F5 で Play)");
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

void framework::draw_player_diagnostics()
{
#ifdef _DEBUG
    // Debug ビルドでのみ表示する。Release へ診断処理を残さない。
    if (!ImGui::CollapsingHeader("Player Runtime Diagnostics")) return;

    namespace Components = ReplayEngine::Components;
    const ReplayEngine::Scene::Scene& scene = active_object_scene();

    ImGui::Text("Mode: %s", object_scene_play_mode ? "Play"
        : (object_runtime_active() ? "Running" : "Edit"));
    ImGui::Text("Runtime active: %s", object_runtime_active() ? "true" : "false");
    ImGui::Text("Legacy migrated: %s", scene.Services().PlayerMigration().Migrated() ? "true" : "false");
    ImGui::Text("Legacy Player Update: %s",
        (game_scene && game_scene->Gameplay().LegacyPlayerActive()) ? "true" : "false");
    ImGui::Text("Legacy Player Render: %s", object_player_active ? "false" : "true");
    ImGui::Text("Fixed accumulator: %.4f", object_fixed_accumulator);
    ImGui::Text("Render items: %zu", object_render_items.Size());

    const ReplayEngine::Core::ObjectID controlled = scene.Services().ControlledObject();
    const ReplayEngine::Core::GameObject* target =
        controlled.Valid() ? scene.FindGameObjectByID(controlled) : nullptr;

    if (target == nullptr)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "Controlled Object: なし");
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
    ImGui::Text("Collision available: %s",
        object_collision_bridge.CollisionAvailable() ? "true" : "false");
#endif
}
