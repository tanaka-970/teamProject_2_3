#include "framework.h"
// Prefab は v7 の SceneData 方式へ移行済み。
#include "../../RePlayEngine/Scene/Serialization/PrefabSerializer.h"

#include <commdlg.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>

namespace
{
    constexpr int EditorSessionVersion = 3;

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

    // Landscape Tool は左ドラッグを Sculpt / Face 選択へ使う。
    // Stroke の mouse-up は Viewport 外でも拾う必要があるため Hover 判定より先。
    if (handle_landscape_viewport_edit()) return;
    if (!scene_view_hovered) return;

    const bool suppress_drag_selection =
        landscape_edit_enabled && active_editor_view == editor_view::scene &&
        !object_scene_play_mode;
    if (suppress_drag_selection) viewport_drag_selecting = false;

    // 編集カメラがマウスを掴んでいるフレームは選択処理を動かさない。
    // カメラ操作とギズモ操作・矩形選択が同時に走らないようにする。
    if (editor_camera_consumed_input) return;

    draw_scene_grid_overlay();
    // GizmoハンドルがHover/Drag中ならPickingへ入力を渡さない。
    if (draw_object_transform_gizmo()) return;

    POINT client_origin{ 0, 0 };
    ClientToScreen(hwnd, &client_origin);
    using namespace DirectX;

    // Scene View の描画と同じ行列を使う。
    // 別々に組み立てると、見えている位置と拾える位置がずれる。
    const XMMATRIX view = viewport_view_matrix();
    const XMMATRIX projection = viewport_projection_matrix();

    const ImVec2 mouse = ImGui::GetMousePos();
    const float mouse_x = mouse.x - static_cast<float>(client_origin.x);
    const float mouse_y = mouse.y - static_cast<float>(client_origin.y);
    const bool inside_viewport = mouse_x >= 0.0f && mouse_y >= 0.0f &&
        mouse_x < static_cast<float>(client_width) &&
        mouse_y < static_cast<float>(client_height);

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
            const DirectX::XMFLOAT3 world = object->GetTransform().WorldPosition();
            const XMVECTOR projected = XMVector3Project(XMLoadFloat3(&world),
                0.0f, 0.0f, static_cast<float>(client_width), static_cast<float>(client_height),
                0.0f, 1.0f, projection, view, XMMatrixIdentity());
            XMFLOAT3 screen{};
            XMStoreFloat3(&screen, projected);
            const float screen_x = screen.x + static_cast<float>(client_origin.x);
            const float screen_y = screen.y + static_cast<float>(client_origin.y);
            if (screen.z >= 0.0f && screen.z <= 1.0f &&
                screen_x >= go_minimum_x && screen_x <= go_maximum_x &&
                screen_y >= go_minimum_y && screen_y <= go_maximum_y)
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

    // 単一クリックでは画面座標をワールド空間のピッキングレイへ変換する。
    const XMVECTOR near_point = XMVector3Unproject(
        XMVectorSet(mouse_x, mouse_y, 0.0f, 1.0f), 0.0f, 0.0f,
        static_cast<float>(client_width), static_cast<float>(client_height),
        0.0f, 1.0f, projection, view, XMMatrixIdentity());
    const XMVECTOR far_point = XMVector3Unproject(
        XMVectorSet(mouse_x, mouse_y, 1.0f, 1.0f), 0.0f, 0.0f,
        static_cast<float>(client_width), static_cast<float>(client_height),
        0.0f, 1.0f, projection, view, XMMatrixIdentity());
    XMFLOAT3 origin{};
    XMFLOAT3 direction{};
    XMStoreFloat3(&origin, near_point);
    XMStoreFloat3(&direction, XMVector3Normalize(far_point - near_point));

    const ReplayEngine::Core::ObjectID picked_object =
        ReplayEngine::Editor::ViewportPicker::Pick(
            active_object_scene(), origin, direction);
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
    if (!editor_session_active) return;

    std::error_code directory_error;
    std::filesystem::create_directories(EditorSessionFolder(), directory_error);
    if (directory_error) return;

    std::ofstream state(EditorSessionStatePath(), std::ios::trunc);
    if (!state) return;
    state << "REPLAY_EDITOR_SESSION " << EditorSessionVersion << '\n';
    state << "OBJECT_SCENE_PATH " << std::quoted(object_scene_path.generic_string()) << '\n';
    for (const std::filesystem::path& path : recent_scene_paths)
        state << "RECENT_SCENE " << std::quoted(path.generic_u8string()) << '\n';
    state << "WORKSPACE " << static_cast<int>(active_editor_workspace) << '\n';
    state << "VIEW " << static_cast<int>(active_editor_view) << '\n';
}

void framework::restore_editor_session()
{
    std::ifstream state(EditorSessionStatePath());
    if (!state) return;

    std::string signature;
    int version = 0;
    if (!(state >> signature >> version) || signature != "REPLAY_EDITOR_SESSION" ||
        version < 2 || version > EditorSessionVersion) return;

    std::string scene_path;
    int workspace = static_cast<int>(editor_workspace::general);
    int view = static_cast<int>(editor_view::scene);
    std::vector<std::filesystem::path> restored_recent_scenes;
    std::string key;
    while (state >> key)
    {
        if (key == "OBJECT_SCENE_PATH") state >> std::quoted(scene_path);
        else if (key == "RECENT_SCENE")
        {
            std::string recent_path;
            state >> std::quoted(recent_path);
            if (!recent_path.empty()) restored_recent_scenes.emplace_back(
                std::filesystem::u8path(recent_path));
        }
        else if (key == "WORKSPACE") state >> workspace;
        else if (key == "VIEW") state >> view;
        else
        {
            std::string ignored;
            std::getline(state, ignored);
        }
    }

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

    const int last_workspace = static_cast<int>(editor_workspace::shader_adjustment);
    workspace = std::clamp(workspace, 0, last_workspace);
    view = std::clamp(view, 0, static_cast<int>(editor_view::game));
    active_editor_workspace = static_cast<editor_workspace>(workspace);
    active_editor_view = static_cast<editor_view>(view);
    selected_editor_object = editor_selection::world;
    edit_mode_active = true;
    editor_mode = true;
    editor_session_active = true;
    editor_layout_checked = false;
    editor_layout_dirty = false;
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

