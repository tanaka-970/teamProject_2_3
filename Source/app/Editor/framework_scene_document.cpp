#include "framework.h"
#include "gltf_model.h"
#include "skinned_mesh.h"
// 旧ステージ配置記録（SceneDocument）専用のシリアライザ。
// GameObject / Component の保存は v7 の Scene/Serialization/SceneSerializer が担当する。
#include "../../RePlayEngine/Scene/Legacy/LegacySceneDocumentSerializer.h"
// Prefab は v7 の SceneData 方式へ移行済み。
#include "../../RePlayEngine/Scene/Serialization/PrefabSerializer.h"

#include <commdlg.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>

namespace
{
    constexpr int EditorSessionVersion = 1;

    std::filesystem::path EditorSessionFolder()
    {
        return std::filesystem::path("Saved") / "EditorSession";
    }

    std::filesystem::path EditorSessionStatePath()
    {
        return EditorSessionFolder() / "session.ini";
    }

    std::filesystem::path EditorSessionScenePath()
    {
        return EditorSessionFolder() / "LastSession.replayscene";
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

    std::vector<ReplayEngine::Physics::Triangle> TransformTriangles(
        const std::vector<ReplayEngine::Physics::Triangle>& source,
        const ReplayEngine::Scene::TransformData* transform)
    {
        if (!transform) return source;
        using namespace DirectX;
        const XMMATRIX world = XMMatrixScaling(transform->scale.x, transform->scale.y, transform->scale.z) *
            XMMatrixRotationRollPitchYaw(transform->rotation.x, transform->rotation.y, transform->rotation.z) *
            XMMatrixTranslation(transform->position.x, transform->position.y, transform->position.z);
        auto result = source;
        for (auto& triangle : result)
            for (auto& vertex : triangle.vertices)
                XMStoreFloat3(&vertex, XMVector3TransformCoord(XMLoadFloat3(&vertex), world));
        return result;
    }
}

ReplayEngine::Scene::SceneDocument& framework::active_scene_document() noexcept
{
    // 編集中は作業コピーを使い、プレイ中は確定済みの実行用コピーを使う。
    return edit_mode_active ? editor_scene_document : runtime_scene_document;
}

const ReplayEngine::Scene::SceneDocument& framework::active_scene_document() const noexcept
{
    return edit_mode_active ? editor_scene_document : runtime_scene_document;
}

void framework::select_scene_entity(ReplayEngine::Scene::EntityId id, bool additive)
{
    if (!additive) selected_scene_entity_ids.clear();
    if (id == 0)
    {
        if (!additive) selected_scene_entity_id = 0;
        return;
    }
    if (std::find(selected_scene_entity_ids.begin(), selected_scene_entity_ids.end(), id) ==
        selected_scene_entity_ids.end())
        selected_scene_entity_ids.push_back(id);
    selected_scene_entity_id = id;
    active_stage_placement_id = id;
    selected_editor_object = editor_selection::scene_entity;
    if (edit_mode_active)
    {
        if (const auto* entity = editor_scene_document.Find(id))
        {
            stage_asset_placed = entity->model_renderer.has_value();
            if (entity->transform && game_scene)
            {
                Stage& stage = game_scene->Gameplay().GetStage();
                stage.SetPosition(entity->transform->position);
                stage.SetAngle(entity->transform->rotation);
                stage.SetScale(entity->transform->scale);
            }
            if (entity->model_renderer)
            {
                shading_per_stage = entity->model_renderer->shading_model;
                outline_per_stage = entity->model_renderer->outline;
                restore_stage_shader_layers(*entity->model_renderer);
            }
        }
    }
}

void framework::copy_selected_entities()
{
    copied_scene_entities.clear();
    if (selected_scene_entity_ids.empty() && selected_scene_entity_id != 0)
        selected_scene_entity_ids.push_back(selected_scene_entity_id);
    for (const auto id : selected_scene_entity_ids)
        if (const auto* entity = editor_scene_document.Find(id))
            copied_scene_entities.push_back(*entity);
    scene_document_status = copied_scene_entities.empty()
        ? "コピーするEntityがありません"
        : std::to_string(copied_scene_entities.size()) + "個のEntityをコピーしました";
}

void framework::paste_copied_entities()
{
    if (!edit_mode_active || copied_scene_entities.empty()) return;
    const auto before = editor_scene_document;
    selected_scene_entity_ids.clear();
    for (const auto& source : copied_scene_entities)
    {
        auto& entity = editor_scene_document.ImportEntity(source);
        entity.name += " コピー";
        // 貼り付けたEntityが元の位置に完全に重ならないよう少しずらす。
        if (entity.transform)
        {
            entity.transform->position.x += 0.5f;
            entity.transform->position.z += 0.5f;
        }
        selected_scene_entity_ids.push_back(entity.id);
        selected_scene_entity_id = entity.id;
    }
    active_stage_placement_id = selected_scene_entity_id;
    selected_editor_object = editor_selection::scene_entity;
    stage_asset_placed = true;
    scene_undo_stack.Commit("Entityを貼り付け", before, editor_scene_document);
    scene_document_status = std::to_string(selected_scene_entity_ids.size()) +
        "個のEntityを貼り付けました";
}

void framework::duplicate_selected_entities()
{
    const auto preserved_clipboard = copied_scene_entities;
    copy_selected_entities();
    if (!copied_scene_entities.empty()) paste_copied_entities();
    copied_scene_entities = preserved_clipboard;
    if (!selected_scene_entity_ids.empty())
        scene_document_status = std::to_string(selected_scene_entity_ids.size()) +
            "個のEntityを複製しました";
}

void framework::handle_viewport_selection()
{
    if (!edit_mode_active || !game_scene) return;

    // 編集カメラがマウスを掴んでいるフレームは選択処理を動かさない。
    // カメラ操作とギズモ操作・矩形選択が同時に走らないようにする。
    if (editor_camera_consumed_input) return;

    POINT client_origin{ 0, 0 };
    ClientToScreen(hwnd, &client_origin);
    using namespace DirectX;

    // Scene View の描画と同じ行列を使う。
    // 別々に組み立てると、見えている位置と拾える位置がずれる。
    const XMMATRIX view = viewport_view_matrix();
    const XMMATRIX projection = viewport_projection_matrix();

    if (const auto* entity = editor_scene_document.Find(selected_scene_entity_id);
        entity && entity->transform)
    {
        const XMVECTOR projected = XMVector3Project(XMLoadFloat3(&entity->transform->position),
            0.0f, 0.0f, static_cast<float>(client_width), static_cast<float>(client_height),
            0.0f, 1.0f, projection, view, XMMatrixIdentity());
        XMFLOAT3 screen{};
        XMStoreFloat3(&screen, projected);
        if (screen.z >= 0.0f && screen.z <= 1.0f)
        {
            const ImVec2 center{ screen.x + static_cast<float>(client_origin.x),
                screen.y + static_cast<float>(client_origin.y) };
            ImDrawList* draw_list = ImGui::GetForegroundDrawList();
            draw_list->AddCircleFilled(center, 5.0f, IM_COL32(245, 245, 245, 255));
            draw_list->AddLine(center, { center.x + 42.0f, center.y }, IM_COL32(230, 70, 70, 255), 3.0f);
            draw_list->AddLine(center, { center.x, center.y - 42.0f }, IM_COL32(80, 220, 100, 255), 3.0f);
            draw_list->AddLine(center, { center.x - 28.0f, center.y + 28.0f }, IM_COL32(70, 130, 245, 255), 3.0f);
        }
    }

    const ImVec2 mouse = ImGui::GetMousePos();
    const float mouse_x = mouse.x - static_cast<float>(client_origin.x);
    const float mouse_y = mouse.y - static_cast<float>(client_origin.y);
    const bool inside_viewport = mouse_x >= 0.0f && mouse_y >= 0.0f &&
        mouse_x < static_cast<float>(client_width) &&
        mouse_y < static_cast<float>(client_height);

    if (!viewport_drag_selecting && !ImGui::GetIO().WantCaptureMouse &&
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
    const bool additive = ImGui::GetIO().KeyShift;
    // 4ピクセルを超えた操作をクリックではなく矩形選択として扱う。
    if (drag_distance_squared > 16.0f)
    {
        if (!additive) selected_scene_entity_ids.clear();
        const float minimum_x = (std::min)(drag_start.x, mouse.x);
        const float maximum_x = (std::max)(drag_start.x, mouse.x);
        const float minimum_y = (std::min)(drag_start.y, mouse.y);
        const float maximum_y = (std::max)(drag_start.y, mouse.y);
        // Entityの原点を画面へ投影し、選択矩形内に入ったものを集める。
        for (const auto& entity : editor_scene_document.Entities())
        {
            if (!entity.active || !entity.transform) continue;
            const XMVECTOR projected = XMVector3Project(XMLoadFloat3(&entity.transform->position),
                0.0f, 0.0f, static_cast<float>(client_width), static_cast<float>(client_height),
                0.0f, 1.0f, projection, view, XMMatrixIdentity());
            XMFLOAT3 screen{};
            XMStoreFloat3(&screen, projected);
            const float screen_x = screen.x + static_cast<float>(client_origin.x);
            const float screen_y = screen.y + static_cast<float>(client_origin.y);
            if (screen.z >= 0.0f && screen.z <= 1.0f &&
                screen_x >= minimum_x && screen_x <= maximum_x &&
                screen_y >= minimum_y && screen_y <= maximum_y)
                select_scene_entity(entity.id, true);
        }
        if (selected_scene_entity_ids.empty())
        {
            selected_scene_entity_id = 0;
            selected_editor_object = editor_selection::world;
        }
        scene_document_status = std::to_string(selected_scene_entity_ids.size()) + "個を選択しました";
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
    const auto picked = ReplayEngine::Editor::ViewportPicker::Pick(
        editor_scene_document, origin, direction);
    if (picked != 0)
        select_scene_entity(picked, additive);
    else if (!additive)
    {
        selected_scene_entity_ids.clear();
        selected_scene_entity_id = 0;
        selected_editor_object = editor_selection::world;
    }
}

void framework::draw_scene_document_toolbar()
{
    if (ImGui::Button("保存  Ctrl+S")) save_scene_document(false);
    ImGui::SameLine();
    if (ImGui::Button("別名保存  Ctrl+Shift+S")) save_scene_document(true);
    ImGui::SameLine();
    if (ImGui::Button("開く")) load_scene_document(true);
    ImGui::SameLine();
    if (ImGui::Button("元に戻す  Ctrl+Z"))
    {
        std::string label;
        if (scene_undo_stack.Undo(editor_scene_document, label))
        {
            scene_document_status = "元に戻す: " + label;
            selected_scene_entity_ids.erase(std::remove_if(selected_scene_entity_ids.begin(),
                selected_scene_entity_ids.end(), [this](ReplayEngine::Scene::EntityId id)
                { return editor_scene_document.Find(id) == nullptr; }), selected_scene_entity_ids.end());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("やり直す  Ctrl+Y"))
    {
        std::string label;
        if (scene_undo_stack.Redo(editor_scene_document, label))
        {
            scene_document_status = "やり直す: " + label;
            selected_scene_entity_ids.erase(std::remove_if(selected_scene_entity_ids.begin(),
                selected_scene_entity_ids.end(), [this](ReplayEngine::Scene::EntityId id)
                { return editor_scene_document.Find(id) == nullptr; }), selected_scene_entity_ids.end());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("コピー  Ctrl+C")) copy_selected_entities();
    ImGui::SameLine();
    if (ImGui::Button("貼り付け  Ctrl+V")) paste_copied_entities();
    ImGui::SameLine();
    if (ImGui::Button("複製  Ctrl+D")) duplicate_selected_entities();
}

void framework::save_scene_document(bool choose_path)
{
    if (choose_path)
    {
        const auto selected = BrowseSceneFile(hwnd, true, L"ステージ配置記録を保存", L"replaystage",
            L"RePlayステージ配置 (*.replaystage)\0*.replaystage\0\0");
        if (selected.empty()) return;
        current_scene_path = selected;
        editor_scene_document.SetSceneName(selected.stem().u8string());
    }
    std::string error;
    if (ReplayEngine::Scene::Legacy::LegacySceneDocumentSerializer::Save(
        editor_scene_document, current_scene_path, error))
        scene_document_status = std::to_string(editor_scene_document.Entities().size()) +
            "個の配置記録を保存しました: " + current_scene_path.generic_string();
    else
        scene_document_status = "保存失敗: " + error;
}

void framework::create_scene_document(const std::string& name)
{
    const std::string scene_name = name.empty() ? "新しいシーン" : name;
    std::error_code error;
    const std::filesystem::path folder = std::filesystem::path("resources") / "Scenes";
    std::filesystem::create_directories(folder, error);
    std::string safe_name = SafePrefabName(scene_name);
    if (safe_name.empty()) safe_name = "Scene";
    // 旧ステージ配置記録の拡張子。新しい GameObject シーンの .replayscene とは分ける。
    const std::string legacy_extension =
        ReplayEngine::Scene::Legacy::LegacySceneDocumentSerializer::file_extension;
    std::filesystem::path scene_path = folder / (safe_name + legacy_extension);
    for (std::uint32_t number = 2; std::filesystem::exists(scene_path); ++number)
        scene_path = folder / (safe_name + "_" + std::to_string(number) + legacy_extension);

    editor_scene_document.Clear();
    editor_scene_document.SetSceneName(scene_name);
    runtime_scene_document.Clear();
    scene_undo_stack.Clear();
    selected_scene_entity_id = 0;
    selected_scene_entity_ids.clear();
    active_stage_placement_id = 0;
    stage_asset_placed = false;
    enable_stage_render = false;
    selected_editor_object = editor_selection::world;
    current_scene_path = std::move(scene_path);
    save_scene_document(false);
}

bool framework::load_scene_document(bool choose_path,
    ReplayEngine::Scene::EntityId preferred_entity_id)
{
    std::filesystem::path path = current_scene_path;
    if (choose_path)
    {
        path = BrowseSceneFile(hwnd, false, L"シーンを開く", L"replayscene",
            L"RePlayシーン (*.replayscene)\0*.replayscene\0\0");
        if (path.empty()) return false;
    }
    ReplayEngine::Scene::SceneDocument loaded;
    std::string error;
    if (!ReplayEngine::Scene::Legacy::LegacySceneDocumentSerializer::Load(loaded, path, error))
    {
        scene_document_status = "読込失敗: " + error;
        return false;
    }
    // 読み込み成功後にだけ編集文書を差し替え、関連する編集状態を初期化する。
    editor_scene_document = std::move(loaded);
    for (auto& entity : editor_scene_document.Entities())
    {
        if (!entity.model_renderer || !entity.model_renderer->asset_name.empty()) continue;
        if (const auto* asset = asset_database.FindByGuid(entity.model_renderer->asset_guid))
            entity.model_renderer->asset_name = asset->display_name;
    }
    runtime_scene_document.Clear();
    scene_undo_stack.Clear();
    current_scene_path = path;
    selected_scene_entity_id = preferred_entity_id != 0 &&
        editor_scene_document.Find(preferred_entity_id) != nullptr
        ? preferred_entity_id
        : (editor_scene_document.Entities().empty() ? 0 : editor_scene_document.Entities().front().id);
    selected_scene_entity_ids.clear();
    if (selected_scene_entity_id != 0) selected_scene_entity_ids.push_back(selected_scene_entity_id);
    active_stage_placement_id = selected_scene_entity_id;
    stage_asset_placed = selected_scene_entity_id != 0;
    selected_editor_object = selected_scene_entity_id == 0
        ? editor_selection::world : editor_selection::scene_entity;
    if (const auto* entity = editor_scene_document.Find(selected_scene_entity_id);
        entity && entity->model_renderer)
    {
        shading_per_stage = entity->model_renderer->shading_model;
        outline_per_stage = entity->model_renderer->outline;
        restore_stage_shader_layers(*entity->model_renderer);
        if (const auto* asset = asset_database.FindByGuid(entity->model_renderer->asset_guid))
            load_stage_asset(asset->source_path.wstring());
    }
    if (const auto* entity = editor_scene_document.Find(selected_scene_entity_id);
        entity && entity->mesh_collider && game_scene)
    {
        std::vector<ReplayEngine::Physics::Triangle> triangles;
        ReplayEngine::Physics::CollisionCookResult result{};
        if (collision_cooker.Load(entity->mesh_collider->cooked_path, triangles, result, error))
            game_scene->Gameplay().GetStage().GetCollisionMesh().Build(
                TransformTriangles(triangles, entity->transform ? &*entity->transform : nullptr),
                entity->mesh_collider->cell_size);
    }
    scene_document_status = "読み込みました: " + path.generic_string();
    return true;
}

void framework::save_editor_session()
{
    if (!editor_session_active) return;

    std::error_code directory_error;
    std::filesystem::create_directories(EditorSessionFolder(), directory_error);
    if (directory_error) return;

    if (active_stage_placement_id != 0) sync_selected_entity_to_stage();
    std::string scene_error;
    // 旧 ReplayEngine::Scene::SceneSerializer は Legacy 名前空間へ移動している。
    // 同ファイル内の save_scene_document / load_scene_document と同じ経路を使う。
    if (!ReplayEngine::Scene::Legacy::LegacySceneDocumentSerializer::Save(
        editor_scene_document, EditorSessionScenePath(), scene_error)) return;

    std::ofstream state(EditorSessionStatePath(), std::ios::trunc);
    if (!state) return;
    state << "REPLAY_EDITOR_SESSION " << EditorSessionVersion << '\n';
    state << "SCENE_PATH " << std::quoted(current_scene_path.generic_string()) << '\n';
    state << "WORKSPACE " << static_cast<int>(active_editor_workspace) << '\n';
    state << "SELECTION " << static_cast<int>(selected_editor_object) << '\n';
    state << "ENTITY " << selected_scene_entity_id << '\n';
    state << "EDIT_MODE " << (edit_mode_active ? 1 : 0) << '\n';
    state << "CAMERA " << camera_position.x << ' ' << camera_position.y << ' '
        << camera_position.z << ' ' << camera_position.w << '\n';
}

void framework::restore_editor_session()
{
    std::ifstream state(EditorSessionStatePath());
    if (!state || !std::filesystem::exists(EditorSessionScenePath())) return;

    std::string signature;
    int version = 0;
    if (!(state >> signature >> version) || signature != "REPLAY_EDITOR_SESSION" ||
        version != EditorSessionVersion) return;

    std::string scene_path;
    int workspace = static_cast<int>(editor_workspace::general);
    int selection = static_cast<int>(editor_selection::world);
    ReplayEngine::Scene::EntityId entity_id = 0;
    int edit_mode = 1;
    std::string key;
    while (state >> key)
    {
        if (key == "SCENE_PATH") state >> std::quoted(scene_path);
        else if (key == "WORKSPACE") state >> workspace;
        else if (key == "SELECTION") state >> selection;
        else if (key == "ENTITY") state >> entity_id;
        else if (key == "EDIT_MODE") state >> edit_mode;
        else if (key == "CAMERA") state >> camera_position.x >> camera_position.y
            >> camera_position.z >> camera_position.w;
        else
        {
            std::string ignored;
            std::getline(state, ignored);
        }
    }

    const auto original_scene_path = scene_path.empty()
        ? std::filesystem::path("resources/Scenes/Main.replayscene")
        : std::filesystem::path(scene_path);
    current_scene_path = EditorSessionScenePath();
    if (!load_scene_document(false, entity_id))
    {
        current_scene_path = original_scene_path;
        return;
    }
    current_scene_path = original_scene_path;

    const int last_workspace = static_cast<int>(editor_workspace::shader_adjustment);
    const int last_selection = static_cast<int>(editor_selection::post_process);
    workspace = std::clamp(workspace, 0, last_workspace);
    selection = std::clamp(selection, 0, last_selection);
    active_editor_workspace = static_cast<editor_workspace>(workspace);
    selected_editor_object = static_cast<editor_selection>(selection);
    if (selected_editor_object == editor_selection::scene_entity &&
        editor_scene_document.Find(entity_id) == nullptr)
        selected_editor_object = editor_selection::world;

    edit_mode_active = edit_mode != 0;
    if (edit_mode_active) runtime_scene_document.Clear();
    else runtime_scene_document = editor_scene_document;
    editor_mode = true;
    editor_session_active = true;
    editor_layout_checked = false;
    editor_layout_dirty = false;
    scene_document_status = "前回の編集セッションを復元しました: " +
        current_scene_path.generic_string();
}

// Prefab は v7 の SceneData 方式へ移行済み。
// 対象は GameObject / Component 基盤の Scene であり、旧 SceneDocument ではない。
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

    // 配置は 1 操作として Undo できるようにする。
    object_editor_context.BeginEdit("Prefab を配置");

    std::string error;
    Serialization::SceneLoadReport report;
    const ReplayEngine::Core::ObjectID id =
        Serialization::PrefabSerializer::Instantiate(object_scene, path, error, &report);

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

void framework::sync_selected_entity_to_stage()
{
    // 表示中のStageインスタンスで行った調整を保存対象のEntityへ戻す。
    auto* entity = editor_scene_document.Find(active_stage_placement_id);
    if (!entity || !game_scene) return;
    const Stage& stage = game_scene->Gameplay().GetStage();
    if (!entity->transform) entity->transform.emplace();
    entity->transform->position = stage.GetPosition();
    entity->transform->rotation = stage.GetAngle();
    entity->transform->scale = stage.GetScale();
    if (entity->model_renderer)
    {
        entity->model_renderer->shading_model = shading_per_stage;
        entity->model_renderer->pixelate_grid = stage_pixelate_grid;
        entity->model_renderer->pixelate_strength = stage_pixelate_strength;
        entity->model_renderer->outline = outline_per_stage;
        store_stage_shader_layers(*entity->model_renderer);
    }
}

void framework::store_stage_shader_layers(ReplayEngine::Scene::ModelRendererData& renderer) const
{
    // 実行用レイヤーをシーン保存用の単純なデータ構造へ変換する。
    renderer.shader_layers.clear();
    renderer.shader_layers.reserve(stage_shader_layers.Layers().size());
    for (const auto& source : stage_shader_layers.Layers())
    {
        ReplayEngine::Scene::ModelRendererData::ShaderLayerData layer{};
        layer.type = static_cast<std::uint32_t>(source.type);
        layer.blend = static_cast<std::uint32_t>(source.blend);
        layer.enabled = source.enabled;
        layer.opacity = source.opacity;
        layer.strength = source.strength;
        layer.parameter = source.parameter;
        layer.tint = source.tint;
        renderer.shader_layers.push_back(layer);
    }
    renderer.character_material = stage_character_profile;
}

void framework::restore_stage_shader_layers(const ReplayEngine::Scene::ModelRendererData& renderer)
{
    stage_pixelate_grid = renderer.pixelate_grid;
    stage_pixelate_strength = renderer.pixelate_strength;
    stage_shader_layers.Clear();
    for (const auto& source : renderer.shader_layers)
    {
        auto& layer = stage_shader_layers.Add(
            static_cast<ReplayEngine::Rendering::ShaderLayerType>(source.type));
        layer.blend = static_cast<ReplayEngine::Rendering::ShaderLayerBlend>(source.blend);
        layer.enabled = source.enabled;
        layer.opacity = source.opacity;
        layer.strength = source.strength;
        layer.parameter = source.parameter;
        layer.tint = source.tint;
    }
    stage_character_profile = renderer.character_material
        ? *renderer.character_material
        : ReplayEngine::Rendering::CharacterMaterialProfile{};
}

void framework::draw_transform_gizmo_controls()
{
    auto* entity = editor_scene_document.Find(selected_scene_entity_id);
    if (!entity || !entity->transform) return;
    auto before = editor_scene_document;
    bool changed = false;
    if (ImGui::Button("移動 W")) transform_gizmo.SetOperation(ReplayEngine::Editor::GizmoOperation::Translate);
    ImGui::SameLine();
    if (ImGui::Button("回転 E")) transform_gizmo.SetOperation(ReplayEngine::Editor::GizmoOperation::Rotate);
    ImGui::SameLine();
    if (ImGui::Button("拡大 R")) transform_gizmo.SetOperation(ReplayEngine::Editor::GizmoOperation::Scale);
    bool snap = transform_gizmo.SnapEnabled();
    if (ImGui::Checkbox("スナップ", &snap)) transform_gizmo.SetSnapEnabled(snap);
    float step = transform_gizmo.SnapStep();
    if (ImGui::DragFloat("刻み", &step, 0.05f, 0.01f, 100.0f)) transform_gizmo.SetSnapStep(step);
    switch (transform_gizmo.Operation())
    {
    case ReplayEngine::Editor::GizmoOperation::Translate:
        changed = ImGui::DragFloat3("位置", &entity->transform->position.x, 0.05f);
        break;
    case ReplayEngine::Editor::GizmoOperation::Rotate:
        changed = ImGui::DragFloat3("回転", &entity->transform->rotation.x, 0.01f);
        break;
    case ReplayEngine::Editor::GizmoOperation::Scale:
        changed = ImGui::DragFloat3("拡大率", &entity->transform->scale.x, 0.01f, 0.001f, 1000.0f);
        break;
    }
    if (changed)
    {
        // 連続操作の各確定値を文書スナップショットとしてUndo履歴へ残す。
        scene_undo_stack.Commit("Transformを編集", before, editor_scene_document);
        if (game_scene)
        {
            Stage& stage = game_scene->Gameplay().GetStage();
            stage.SetPosition(entity->transform->position);
            stage.SetAngle(entity->transform->rotation);
            stage.SetScale(entity->transform->scale);
        }
    }
}

void framework::cook_selected_mesh_collision()
{
    auto* entity = editor_scene_document.Find(selected_scene_entity_id);
    if (!entity || !entity->model_renderer)
    {
        scene_document_status = "ModelRendererが必要です";
        return;
    }
    std::vector<ReplayEngine::Physics::Triangle> triangles;
    if (stage_gltf_model)
    {
        triangles = stage_gltf_model->CollisionTriangles();
    }
    else if (skinned_meshes[1])
    {
        using namespace DirectX;
        for (const auto& mesh : skinned_meshes[1]->meshes)
        {
            const XMMATRIX mesh_transform = XMLoadFloat4x4(&mesh.default_global_transform);
            for (size_t index = 0; index + 2 < mesh.indices.size(); index += 3)
            {
                if (mesh.indices[index] >= mesh.vertices.size() ||
                    mesh.indices[index + 1] >= mesh.vertices.size() ||
                    mesh.indices[index + 2] >= mesh.vertices.size()) continue;
                ReplayEngine::Physics::Triangle triangle{};
                for (int vertex = 0; vertex < 3; ++vertex)
                    XMStoreFloat3(&triangle.vertices[vertex], XMVector3TransformCoord(
                        XMLoadFloat3(&mesh.vertices[mesh.indices[index + vertex]].position),
                        mesh_transform));
                triangles.push_back(triangle);
            }
        }
    }
    ReplayEngine::Physics::CollisionCookResult result{};
    std::string error;
    if (!collision_cooker.Cook(entity->model_renderer->asset_guid, triangles, result, error))
    {
        scene_document_status = "衝突Cook失敗: " + error;
        return;
    }
    const auto before = editor_scene_document;
    if (!entity->mesh_collider) entity->mesh_collider.emplace();
    entity->mesh_collider->cooked_path = result.cache_path.generic_string();
    entity->mesh_collider->triangle_count = result.triangle_count;
    if (game_scene)
        game_scene->Gameplay().GetStage().GetCollisionMesh().Build(
            TransformTriangles(triangles, entity->transform ? &*entity->transform : nullptr),
            entity->mesh_collider->cell_size);
    scene_undo_stack.Commit("MeshColliderをCook", before, editor_scene_document);
    scene_document_status = "衝突をCookしました: " + std::to_string(result.triangle_count) + "三角形";
}

void framework::draw_scene_entity_inspector()
{
    if (!edit_mode_active)
    {
        const auto* runtime_entity = runtime_scene_document.Find(selected_scene_entity_id);
        ImGui::TextUnformatted("実行シーン（読み取り専用）");
        ImGui::Separator();
        if (!runtime_entity)
        {
            ImGui::TextDisabled("Entityが選択されていません");
            return;
        }
        ImGui::Text("%s  #%llu", runtime_entity->name.c_str(),
            static_cast<unsigned long long>(runtime_entity->id));
        ImGui::TextDisabled("プレイ中の変更は編集シーンへ書き戻しません");
        if (runtime_entity->transform)
            ImGui::Text("位置: %.3f, %.3f, %.3f", runtime_entity->transform->position.x,
                runtime_entity->transform->position.y, runtime_entity->transform->position.z);
        return;
    }
    auto* entity = editor_scene_document.Find(selected_scene_entity_id);
    if (!entity)
    {
        ImGui::TextDisabled("Entityが選択されていません");
        return;
    }
    ImGui::Text("識別子: %s", entity->identifier.c_str());
    ImGui::TextDisabled("内部ID: %llu", static_cast<unsigned long long>(entity->id));
    if (selected_scene_entity_ids.size() > 1)
        ImGui::TextColored({ 0.35f, 0.75f, 1.0f, 1.0f }, "%zu個を複数選択中（主選択を表示）",
            selected_scene_entity_ids.size());
    char name[256]{};
    strncpy_s(name, entity->name.c_str(), _TRUNCATE);
    if (ImGui::InputText("名前", name, IM_ARRAYSIZE(name))) entity->name = name;
    if (ImGui::Button("名前から識別子を再生成"))
    {
        const auto before = editor_scene_document;
        editor_scene_document.SetIdentifier(entity->id, entity->name);
        scene_undo_stack.Commit("識別子を再生成", before, editor_scene_document);
    }
    ImGui::Checkbox("有効", &entity->active);
    ImGui::Separator();

    if (entity->transform)
    {
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            draw_transform_gizmo_controls();
            if (ImGui::Button("削除##Transform"))
            {
                const auto before = editor_scene_document;
                entity->transform.reset();
                scene_undo_stack.Commit("Transformを削除", before, editor_scene_document);
            }
        }
    }
    if (entity->model_renderer)
    {
        if (ImGui::CollapsingHeader("ModelRenderer", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("アセット名: %s", entity->model_renderer->asset_name.empty()
                ? "（旧データ）" : entity->model_renderer->asset_name.c_str());
            ImGui::TextWrapped("Asset GUID: %s", entity->model_renderer->asset_guid.c_str());
            ImGui::Text("追加シェーダーパス: %zu", entity->model_renderer->shader_layers.size());
            if (const auto* asset = asset_database.FindByGuid(entity->model_renderer->asset_guid))
                ImGui::TextWrapped("配置元: %s", asset->source_path.generic_u8string().c_str());
            ImGui::Checkbox("表示", &entity->model_renderer->visible);
            ImGui::ColorEdit4("色", &entity->model_renderer->tint.x);
            ImGui::SliderInt("シェーディング", &entity->model_renderer->shading_model, 0, 4);
            ImGui::Checkbox("輪郭線", &entity->model_renderer->outline);
            if (ImGui::Button("削除##ModelRenderer"))
            {
                const auto before = editor_scene_document;
                entity->model_renderer.reset();
                scene_undo_stack.Commit("ModelRendererを削除", before, editor_scene_document);
            }
        }
    }
    if (entity->mesh_collider)
    {
        if (ImGui::CollapsingHeader("MeshCollider", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("衝突有効", &entity->mesh_collider->enabled);
            ImGui::DragFloat("セルサイズ", &entity->mesh_collider->cell_size, 0.1f, 0.1f, 100.0f);
            ImGui::Text("三角形: %llu", static_cast<unsigned long long>(entity->mesh_collider->triangle_count));
            if (ImGui::Button("衝突をCook / Cache")) cook_selected_mesh_collision();
            ImGui::SameLine();
            if (ImGui::Button("削除##MeshCollider"))
            {
                const auto before = editor_scene_document;
                entity->mesh_collider.reset();
                scene_undo_stack.Commit("MeshColliderを削除", before, editor_scene_document);
            }
        }
    }
    if (entity->gravity)
    {
        if (ImGui::CollapsingHeader("Gravity", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("重力有効", &entity->gravity->enabled);
            ImGui::DragFloat3("方向", &entity->gravity->direction.x, 0.01f, -1.0f, 1.0f);
            ImGui::DragFloat("強さ", &entity->gravity->strength, 0.1f, 0.0f, 100.0f);
            ImGui::DragFloat("倍率", &entity->gravity->scale, 0.01f, 0.0f, 10.0f);
            if (ImGui::Button("削除##Gravity"))
            {
                const auto before = editor_scene_document;
                entity->gravity.reset();
                scene_undo_stack.Commit("Gravityを削除", before, editor_scene_document);
            }
        }
    }
    if (entity->animation)
    {
        if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("再生", &entity->animation->playing);
            ImGui::DragInt("クリップ", &entity->animation->clip_index, 0.1f, 0);
            ImGui::DragFloat("速度", &entity->animation->speed, 0.01f, 0.0f, 10.0f);
            ImGui::Checkbox("ループ", &entity->animation->loop);
            if (ImGui::Button("削除##Animation"))
            {
                const auto before = editor_scene_document;
                entity->animation.reset();
                scene_undo_stack.Commit("Animationを削除", before, editor_scene_document);
            }
        }
    }

    if (ImGui::Button("コンポーネントを追加")) ImGui::OpenPopup("AddComponent");
    if (ImGui::BeginPopup("AddComponent"))
    {
        const auto add = [this, entity](const char* label, auto member)
        {
            if (entity->*member) return;
            if (ImGui::MenuItem(label))
            {
                const auto before = editor_scene_document;
                (entity->*member).emplace();
                scene_undo_stack.Commit(std::string(label) + "を追加", before, editor_scene_document);
            }
        };
        add("Transform", &ReplayEngine::Scene::SceneEntity::transform);
        add("ModelRenderer", &ReplayEngine::Scene::SceneEntity::model_renderer);
        add("MeshCollider", &ReplayEngine::Scene::SceneEntity::mesh_collider);
        add("Gravity", &ReplayEngine::Scene::SceneEntity::gravity);
        add("Animation", &ReplayEngine::Scene::SceneEntity::animation);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Prefabとして保存")) save_selected_prefab(true);
    ImGui::SameLine();
    if (ImGui::Button("Entityを削除"))
    {
        const auto before = editor_scene_document;
        if (selected_scene_entity_ids.empty())
            selected_scene_entity_ids.push_back(selected_scene_entity_id);
        const std::size_t delete_count = selected_scene_entity_ids.size();
        for (const auto id : selected_scene_entity_ids)
            editor_scene_document.DestroyEntity(id);
        scene_undo_stack.Commit(std::to_string(delete_count) + "個のEntityを削除",
            before, editor_scene_document);
        selected_scene_entity_ids.clear();
        selected_scene_entity_id = 0;
        selected_editor_object = editor_selection::world;
    }
    ImGui::TextWrapped("%s", scene_document_status.c_str());
}
