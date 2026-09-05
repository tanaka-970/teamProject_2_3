#include "framework.h"

#include "imgui/ImGuizmo.h"
// Prefab は v7 の SceneData 方式へ移行済み。
#include "../../RePlayEngine/Scene/Serialization/PrefabSerializer.h"
#include "../../RePlayEngine/Editor/Viewport/EditorSelectionBounds.h"
#include "../../RePlayEngine/Editor/Style/EditorStyle.h"

#include <commdlg.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <limits>
#include <sstream>

namespace
{
    constexpr int EditorSessionVersion = 4;

    std::filesystem::path EditorSessionFolder()
    {
        return std::filesystem::path("Saved") / "EditorSession";
    }

    std::filesystem::path EditorSessionStatePath()
    {
        return EditorSessionFolder() / "session.ini";
    }

    std::filesystem::path BrowseSceneFile(HWND owner, bool save,
        const wchar_t* title, const wchar_t* extension, const wchar_t* filter)
    {
        wchar_t filename[32768]{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = owner;
        dialog.lpstrFile = filename;
        dialog.nMaxFile = static_cast<DWORD>(_countof(filename));
        dialog.lpstrFilter = filter;
        dialog.lpstrDefExt = extension;
        dialog.lpstrTitle = title;
        dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
            (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        const BOOL accepted = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
        return accepted ? std::filesystem::path(filename) : std::filesystem::path{};
    }

    std::string SafePrefabName(std::string name)
    {
        for (char& character : name)
        {
            if (character == '<' || character == '>' || character == ':' ||
                character == '"' || character == '/' || character == '\\' ||
                character == '|' || character == '?' || character == '*') character = '_';
        }
        return name.empty() ? "Prefab" : name;
    }

}

void framework::handle_viewport_selection()
{
    if (!edit_mode_active || !game_scene) return;
    // UIワークスペースではScene ViewもUI専用の直接編集面として扱う。
    // UI枠のドラッグを3Dの矩形選択が同時に拾い、選択解除するのを防ぐ。
    if (active_editor_workspace == editor_workspace::ui)
    {
        viewport_drag_selecting = false;
        return;
    }
    if (ui_scene_view_input_consumed)
    {
        viewport_drag_selecting = false;
        return;
    }

    // Landscape Tool は左ドラッグを Sculpt / Face 選択へ使う。
    // Stroke の mouse-up は Viewport 外でも拾う必要があるため Hover 判定より先。
    if (handle_landscape_viewport_edit()) return;
    // AI range handles consume only the selected handle drag; normal selection/Gizmo remains unchanged.
    if (handle_ai_navigation_debug_edit())
    {
        viewport_drag_selecting = false;
        return;
    }
    if (handle_normal_adjust_gizmo())
    {
        viewport_drag_selecting = false;
        return;
    }
    // 掴んでいる間は窓の外へ出ても離さない。ここで返すとドラッグが途中で止まる。
    if (!scene_view_hovered && !ImGuizmo::IsUsing()) return;

    const bool suppress_drag_selection =
        landscape_edit_enabled && active_editor_view == editor_view::scene &&
        !object_scene_play_mode && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt;
    if (suppress_drag_selection) viewport_drag_selecting = false;

    // 編集カメラがマウスを掴んでいるフレームは選択処理を動かさない。
    // カメラ操作とギズモ操作・矩形選択が同時に走らないようにする。
    if (editor_camera_consumed_input) return;

    draw_scene_grid_overlay();
    // GizmoハンドルがHover/Drag中ならPickingへ入力を渡さない。
    if (draw_bone_transform_gizmo()) return;
    if (draw_object_transform_gizmo()) return;

    // リグを出しているときは、骨のクリックを GameObject の選択より先に見る。
    // 骨を掴めたらそこで消費し、選択が入れ替わらないようにする。
    if ((show_rig_debug_draw || show_motion_rig_panel) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !object_rig_debug_bones.empty())
    {
        const DirectX::XMMATRIX rig_view_projection =
            viewport_view_matrix() * viewport_projection_matrix();
        const ImVec2 rig_origin = ImGui::GetMainViewport()->Pos;
        const ImVec2 rig_size = ImGui::GetMainViewport()->Size;
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        // 掴める距離。関節の見た目より少し広くしないと当てにくい。
        const float pick_radius = (std::max)(6.0f, rig_joint_radius * 3.0f);
        float best_distance = pick_radius;
        const std::string* best_name = nullptr;
        std::uint64_t best_owner = 0;
        for (const auto& rig : object_rig_debug_bones)
        {
            for (const rig_debug_bone& bone : rig.second)
            {
                ImVec2 screen{};
                if (!project_world_to_screen(rig_view_projection, bone.world,
                    rig_origin, rig_size, screen))
                    continue;
                const float dx = screen.x - mouse.x;
                const float dy = screen.y - mouse.y;
                const float distance = std::sqrt(dx * dx + dy * dy);
                if (distance >= best_distance) continue;
                best_distance = distance;
                best_name = &bone.name;
                best_owner = rig.first;
            }
        }
        if (best_name != nullptr)
        {
            select_rig_bone(*best_name, ImGui::GetIO().KeyCtrl);
            object_editor_context.Selection().Select(
                ReplayEngine::Core::ObjectID{ best_owner });
            viewport_drag_selecting = false;
            return;
        }
    }

    using namespace DirectX;
    const XMMATRIX view = viewport_view_matrix();
    const XMMATRIX projection = viewport_projection_matrix();
    const float scene_width = scene_view_max_x - scene_view_min_x;
    const float scene_height = scene_view_max_y - scene_view_min_y;
    POINT client_origin{ 0, 0 };
    ClientToScreen(hwnd, &client_origin);
    const float render_width = (std::max)(1.0f, static_cast<float>(client_width));
    const float render_height = (std::max)(1.0f, static_cast<float>(client_height));

    const ImVec2 mouse = ImGui::GetMousePos();
    const float mouse_x = mouse.x - scene_view_min_x;
    const float mouse_y = mouse.y - scene_view_min_y;
    const bool inside_viewport = scene_width > 1.0f && scene_height > 1.0f &&
        mouse_x >= 0.0f && mouse_y >= 0.0f && mouse_x < scene_width && mouse_y < scene_height;

    if (!suppress_drag_selection && !viewport_drag_selecting && scene_view_hovered &&
        inside_viewport && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        viewport_drag_selecting = true;
        viewport_drag_start = { static_cast<LONG>(mouse.x), static_cast<LONG>(mouse.y) };
    }
    if (!viewport_drag_selecting) return;

    const ImVec2 drag_start{ static_cast<float>(viewport_drag_start.x),
        static_cast<float>(viewport_drag_start.y) };
    const float drag_x = mouse.x - drag_start.x;
    const float drag_y = mouse.y - drag_start.y;
    const float drag_distance_squared = drag_x * drag_x + drag_y * drag_y;
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && drag_distance_squared > 16.0f)
    {
        const ImVec2 minimum{ (std::min)(drag_start.x, mouse.x), (std::min)(drag_start.y, mouse.y) };
        const ImVec2 maximum{ (std::max)(drag_start.x, mouse.x), (std::max)(drag_start.y, mouse.y) };
        ImDrawList* draw_list = ImGui::GetForegroundDrawList();
        draw_list->AddRectFilled(minimum, maximum, IM_COL32(45, 130, 210, 38));
        draw_list->AddRect(minimum, maximum, IM_COL32(80, 175, 255, 235), 0.0f, 0, 1.5f);
    }
    if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left)) return;

    viewport_drag_selecting = false;
    const bool additive = ImGui::GetIO().KeyShift || ImGui::GetIO().KeyCtrl;
    // 4ピクセルを超えた操作をクリックではなく矩形選択として扱う。
    if (drag_distance_squared > 16.0f)
    {
        ReplayEngine::Scene::Scene& object_scene_view = active_object_scene();
        if (!additive) object_editor_context.Selection().Clear();
        bool selected_game_object = false;
        const float go_minimum_x = (std::min)(drag_start.x, mouse.x);
        const float go_maximum_x = (std::max)(drag_start.x, mouse.x);
        const float go_minimum_y = (std::min)(drag_start.y, mouse.y);
        const float go_maximum_y = (std::max)(drag_start.y, mouse.y);
        for (std::size_t index = 0; index < object_scene_view.GameObjectCount(); ++index)
        {
            const ReplayEngine::Core::GameObject* object = object_scene_view.GameObjectAt(index);
            if (object == nullptr || object->PendingDestroy() || !object->ActiveInHierarchy()) continue;
            const auto bounds = ReplayEngine::Editor::EditorSelectionBounds::Compute(*object);
            if (!bounds.valid) continue;
            const XMFLOAT3 corners[8] = {
                {bounds.minimum.x,bounds.minimum.y,bounds.minimum.z},{bounds.maximum.x,bounds.minimum.y,bounds.minimum.z},
                {bounds.minimum.x,bounds.maximum.y,bounds.minimum.z},{bounds.maximum.x,bounds.maximum.y,bounds.minimum.z},
                {bounds.minimum.x,bounds.minimum.y,bounds.maximum.z},{bounds.maximum.x,bounds.minimum.y,bounds.maximum.z},
                {bounds.minimum.x,bounds.maximum.y,bounds.maximum.z},{bounds.maximum.x,bounds.maximum.y,bounds.maximum.z} };
            float min_x=(std::numeric_limits<float>::max)(), min_y=(std::numeric_limits<float>::max)();
            float max_x=-(std::numeric_limits<float>::max)(), max_y=-(std::numeric_limits<float>::max)();
            bool any=false;
            for (const XMFLOAT3& corner : corners)
            {
                const XMVECTOR projected=XMVector3Project(XMLoadFloat3(&corner),0,0,render_width,render_height,0,1,projection,view,XMMatrixIdentity());
                XMFLOAT3 screen{}; XMStoreFloat3(&screen,projected);
                if (!std::isfinite(screen.x)||!std::isfinite(screen.y)||screen.z<0||screen.z>1) continue;
                any=true; const float sx=static_cast<float>(client_origin.x)+screen.x, sy=static_cast<float>(client_origin.y)+screen.y;
                min_x=(std::min)(min_x,sx); min_y=(std::min)(min_y,sy); max_x=(std::max)(max_x,sx); max_y=(std::max)(max_y,sy);
            }
            if (any && max_x>=go_minimum_x && min_x<=go_maximum_x && max_y>=go_minimum_y && min_y<=go_maximum_y)
            {
                object_editor_context.Selection().Select(object->ID(), true);
                selected_game_object = true;
            }
        }
        if (selected_game_object)
        {
            selected_editor_object = editor_selection::game_object;
            object_editor_context.SetStatus(std::to_string(
                object_editor_context.Selection().Count()) + "個を選択しました");
            return;
        }

        selected_editor_object = editor_selection::world;
        object_editor_context.SetStatus("選択を解除しました");
        return;
    }

    // 単一クリックも Landscape / D&D と同じ共通 ray を使う。
    // Scene View local -> client viewport の変換を一か所へ集約し、描画とのズレを防ぐ。
    const auto pick_ray = viewport_picking_ray(mouse_x, mouse_y);
    const ReplayEngine::Core::ObjectID picked_object =
        ReplayEngine::Editor::ViewportPicker::Pick(
            active_object_scene(), pick_ray.origin, pick_ray.direction);
    if (picked_object.Valid())
    {
        object_editor_context.Selection().Select(picked_object, additive);
        selected_editor_object = editor_selection::game_object;
        return;
    }
    if (!additive)
    {
        object_editor_context.Selection().Clear();
        selected_editor_object = editor_selection::world;
    }
}

void framework::save_editor_session()
{
    if (standalone_game_mode) return;
    if (!editor_session_active) return;
    remember_active_editor_view();

    std::error_code directory_error;
    std::filesystem::create_directories(EditorSessionFolder(), directory_error);
    if (directory_error) return;

    std::ofstream state(EditorSessionStatePath(), std::ios::trunc);
    if (!state) return;
    state << "REPLAY_EDITOR_SESSION " << EditorSessionVersion << '\n';
    state << "LAYOUT_VERSION " << editor_layout_saved_version << '\n';
    state << "OBJECT_SCENE_PATH " << std::quoted(object_scene_path.generic_string()) << '\n';
    for (const std::filesystem::path& path : recent_scene_paths)
        state << "RECENT_SCENE " << std::quoted(path.generic_u8string()) << '\n';
    state << "WORKSPACE " << static_cast<int>(active_editor_workspace) << '\n';
    state << "VIEW " << static_cast<int>(active_editor_view) << '\n';
    for (std::size_t index = 0; index < editor_view_by_workspace.size(); ++index)
        state << "WORKSPACE_VIEW " << index << ' ' <<
            static_cast<int>(editor_view_by_workspace[index]) << '\n';
    state << "UI_STYLE " << (ui_style_overridden ? 1 : 0) << ' '
        << ui_button_scale << ' ' << ui_font_scale << ' '
        << ui_text_color[0] << ' ' << ui_text_color[1] << ' ' << ui_text_color[2] << '\n';
    for (const auto& entry : ReplayEngine::Editor::EditorStyle::ComponentCategoryColors())
        state << "COMPONENT_CATEGORY_COLOR " << std::quoted(entry.first) << ' '
            << entry.second.x << ' ' << entry.second.y << ' ' << entry.second.z << '\n';
}

void framework::restore_editor_session()
{
    if (standalone_game_mode || object_boot_from_startup_scene) return;

    editor_layout_checked = false;
    editor_layout_dirty = true;
    editor_layout_saved_version = 0;

    std::ifstream state(EditorSessionStatePath());
    if (!state) return;

    std::string signature;
    int version = 0;
    if (!(state >> signature >> version) || signature != "REPLAY_EDITOR_SESSION" ||
        version < 2 || version > EditorSessionVersion)
    {
        push_editor_log("Warning",
            "Editor session を読み取れません。既定値で起動します",
            EditorSessionStatePath());
        return;
    }

    ensure_editor_style_presets_loaded();
    const bool restore_legacy_style = !editor_style_active_selection_loaded;
    if (restore_legacy_style)
        ReplayEngine::Editor::EditorStyle::ResetComponentCategoryColors();
    editor_style_history.Clear();

    std::string scene_path;
    int workspace = static_cast<int>(editor_workspace::general);
    int view = static_cast<int>(editor_view::scene);
    int restored_layout_version = 0;
    bool layout_version_read = false;
    bool layout_version_invalid = false;
    std::vector<std::filesystem::path> restored_recent_scenes;
    std::string key;
    while (state >> key)
    {
        if (key == "LAYOUT_VERSION")
        {
            std::string value_line;
            std::getline(state, value_line);
            std::istringstream parser(value_line);
            int parsed_version = 0;
            char trailing = '\0';
            if ((parser >> parsed_version) && !(parser >> trailing))
            {
                restored_layout_version = parsed_version;
                layout_version_read = true;
            }
            else
            {
                layout_version_invalid = true;
            }
        }
        else if (key == "OBJECT_SCENE_PATH") state >> std::quoted(scene_path);
        else if (key == "RECENT_SCENE")
        {
            std::string recent_path;
            state >> std::quoted(recent_path);
            if (!recent_path.empty()) restored_recent_scenes.emplace_back(
                std::filesystem::u8path(recent_path));
        }
        else if (key == "WORKSPACE") state >> workspace;
        else if (key == "VIEW") state >> view;
        else if (key == "WORKSPACE_VIEW")
        {
            int saved_workspace = -1;
            int saved_view = static_cast<int>(editor_view::scene);
            state >> saved_workspace >> saved_view;
            if (saved_workspace >= 0 &&
                saved_workspace < static_cast<int>(editor_view_by_workspace.size()))
            {
                saved_view = std::clamp(saved_view, 0,
                    static_cast<int>(editor_view::game));
                editor_view_by_workspace[
                    static_cast<std::size_t>(saved_workspace)] =
                    static_cast<editor_view>(saved_view);
            }
        }
        else if (key == "COMPONENT_CATEGORY_COLOR")
        {
            std::string category;
            float red = 0.0f;
            float green = 0.0f;
            float blue = 0.0f;
            if (state >> std::quoted(category) >> red >> green >> blue &&
                !category.empty() && restore_legacy_style)
            {
                ReplayEngine::Editor::EditorStyle::SetComponentCategoryColor(
                    category, ImVec4(red, green, blue, 1.0f));
            }
        }
        else if (key == "UI_STYLE")
        {
            int overridden = 0;
            float button_scale = 1.0f;
            float font_scale = 1.0f;
            float red = 1.0f;
            float green = 1.0f;
            float blue = 1.0f;
            if (state >> overridden >> button_scale >> font_scale >> red >> green >> blue &&
                restore_legacy_style)
            {
                ui_style_overridden = overridden != 0;
                ui_button_scale = std::clamp(button_scale, 0.6f, 3.0f);
                ui_font_scale = std::clamp(font_scale, 0.7f, 2.5f);
                ui_text_color[0] = std::clamp(red, 0.0f, 1.0f);
                ui_text_color[1] = std::clamp(green, 0.0f, 1.0f);
                ui_text_color[2] = std::clamp(blue, 0.0f, 1.0f);
            }
        }
        else
        {
            std::string ignored;
            std::getline(state, ignored);
        }
    }

    if (restore_legacy_style) configure_editor_style();

    recent_scene_paths.clear();
    for (auto iterator = restored_recent_scenes.rbegin();
        iterator != restored_recent_scenes.rend(); ++iterator)
        add_recent_object_scene(*iterator);

    if (!scene_path.empty())
    {
        const std::filesystem::path restored(scene_path);
        std::error_code error;
        if (std::filesystem::exists(restored, error) && !error &&
            restored.lexically_normal() != object_scene_path.lexically_normal())
        {
            const auto previous = object_scene_path;
            object_scene_path = restored;
            if (!load_object_scene(false)) object_scene_path = previous;
        }
    }
    add_recent_object_scene(object_scene_path);

    const int last_workspace = static_cast<int>(editor_workspace::motion);
    workspace = std::clamp(workspace, 0, last_workspace);
    view = std::clamp(view, 0, static_cast<int>(editor_view::game));
    active_editor_workspace = static_cast<editor_workspace>(workspace);
    active_editor_view = static_cast<editor_view>(view);
    remember_active_editor_view();
    editor_view_tab_sync_pending = true;
    selected_editor_object = editor_selection::world;
    edit_mode_active = true;
    editor_mode = true;
    editor_session_active = true;
    editor_layout_saved_version =
        (!layout_version_invalid && layout_version_read) ? restored_layout_version : 0;
    editor_layout_dirty = layout_version_invalid || !layout_version_read ||
        editor_layout_saved_version != editor_layout_version;
    if (layout_version_invalid)
    {
        push_editor_log("Warning",
            "Editor layout version を読み取れません。既定レイアウトを再構築します",
            EditorSessionStatePath());
    }
    object_editor_context.SetStatus("前回の編集セッションを復元しました");
}

// Prefab は v7 の SceneData 方式へ移行済み。
// 対象はGameObject / Component基盤のScene。
// 保存内容は Component 構成とプロパティを含む部分木で、
// ComponentRegistry と PropertyRegistry を通るため型ごとの分岐は存在しない。
void framework::save_selected_prefab(bool choose_path)
{
    namespace Serialization = ReplayEngine::Scene::Serialization;

    if (object_scene_play_mode)
    {
        // Play 中の値（移動後の位置・減った HP・速度）を Prefab へ焼き込まない。
        object_editor_context.SetStatus("実行中は Prefab を保存できません");
        return;
    }

    ReplayEngine::Scene::Scene& scene = object_scene;
    const ReplayEngine::Core::GameObject* target =
        object_editor_context.Selection().ResolvePrimary(scene);
    if (target == nullptr)
    {
        object_editor_context.SetStatus("Prefab にする GameObject が選択されていません");
        return;
    }

    // 既定のファイル名は GameObject 名。ただしこれは「初期値」でしかない。
    // 保存後に Prefab 名を変えても、参照は AssetGUID なので壊れない。
    // 操作対象の判定に名前を使う場所は 1 か所も無い。
    std::filesystem::path path = std::filesystem::path("resources/Prefabs") /
        (SafePrefabName(target->Name()) + Serialization::PrefabSerializer::file_extension);

    if (choose_path)
    {
        std::error_code folder_error;
        std::filesystem::create_directories(path.parent_path(), folder_error);

        const auto selected = BrowseSceneFile(hwnd, true, L"Prefabとして保存", L"replayprefab",
            L"RePlay Prefab (*.replayprefab)\0*.replayprefab\0\0");
        if (selected.empty()) return;
        path = selected;
    }

    std::string error;
    if (!Serialization::PrefabSerializer::Save(scene, target->ID(), path, error))
    {
        object_editor_context.SetStatus("Prefab 保存失敗: " + error);
        return;
    }

    // AssetDatabase へ登録して AssetGUID を発行する。
    // これで Project Settings の Default Controlled Character Prefab から
    // GUID で指せるようになる。
    const auto& record = asset_database.Register(path,
        ReplayEngine::Assets::AssetKind::Scene);
    std::string database_error;
    asset_database.Save(database_error);

    last_saved_prefab_guid = record.guid;
    object_editor_context.BeginEdit("Prefab instanceへLink");
    if (Serialization::PrefabSerializer::LinkInstance(
        scene, target->ID(), record.guid, error))
    {
        object_editor_context.CommitEdit();
    }
    else
    {
        object_editor_context.CancelEdit();
        object_editor_context.SetStatus("Prefabは保存しましたがinstanceへLinkできません: " + error);
        return;
    }
    object_editor_context.SetStatus("Prefab を保存しました: " + path.generic_string());
}

void framework::load_prefab()
{
    namespace Serialization = ReplayEngine::Scene::Serialization;

    if (object_scene_play_mode)
    {
        object_editor_context.SetStatus("実行中は Prefab を配置できません");
        return;
    }

    const auto path = BrowseSceneFile(hwnd, false, L"Prefabを配置", L"replayprefab",
        L"RePlay Prefab (*.replayprefab)\0*.replayprefab\0\0");
    if (path.empty()) return;

    const auto& record = asset_database.Register(path,
        ReplayEngine::Assets::AssetKind::Scene);
    std::string database_error;
    asset_database.Save(database_error);

    // 配置は 1 操作として Undo できるようにする。
    object_editor_context.BeginEdit("Prefab を配置");

    std::string error;
    Serialization::SceneLoadReport report;
    const ReplayEngine::Core::ObjectID id =
        Serialization::PrefabSerializer::Instantiate(
            object_scene, path, error, &report, record.guid);

    if (!id.Valid())
    {
        object_editor_context.CancelEdit();
        object_editor_context.SetStatus("Prefab 読込失敗: " + error);
        return;
    }

    object_editor_context.CommitEdit();
    object_editor_context.Selection().Select(id, false);
    selected_editor_object = editor_selection::game_object;

    std::string status = "Prefab を配置しました";
    if (!report.Clean())
    {
        status += "（警告 " + std::to_string(report.warnings.size()) + " 件）";
        for (const std::string& warning : report.warnings)
            OutputDebugStringA(("[Prefab] " + warning + "\n").c_str());
    }
    object_editor_context.SetStatus(status);
}

