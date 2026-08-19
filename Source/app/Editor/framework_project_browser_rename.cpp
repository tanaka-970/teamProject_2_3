#include "framework.h"
#include "texture.h"
#include "../../RePlayEngine/Assets/AssetCache.h"
#include "../../RePlayEngine/Editor/Style/EditorStyle.h"
#include "../../RePlayEngine/Motion/CompositionAsset.h"
#include "../../RePlayEngine/Motion/MotionAsset.h"
#include "../../RePlayEngine/Rendering/Materials/MaterialAsset.h"
#include "../../RePlayEngine/Rendering/Shaders/ShaderAssetFactory.h"
#include "../../RePlayEngine/Rendering/ShaderComposer/ShaderComposerAsset.h"
#include "../../RePlayEngine/Rendering/ShaderComposer/ShaderComposerGenerator.h"
#include "../../RePlayEngine/Runtime/Scene/SceneFlowAsset.h"
#include "../../RePlayEngine/Reflection/Registry/PropertyRegistry.h"
#include "../../RePlayEngine/Scripting/CSharp/CSharpProject.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <fstream>
#include <set>
#include <shellapi.h>
#include <vector>
#include "framework_project_browserInternal.h"
using namespace framework_project_browser::Detail;

// Project エントリ改名の関数本体

bool framework::project_rename_entry(const std::filesystem::path& path,
    const std::string& new_name)
{
    if (!object_editor_context.CanEdit())
    {
        project_browser_status = "Play 中は Project Asset を改名できません";
        return false;
    }
    const std::string safe = SafeProjectFileName(new_name);
    if (safe.empty())
    {
        project_browser_status = "名前が空です";
        return false;
    }

    std::error_code error;
    const bool is_directory = std::filesystem::is_directory(path, error);
    if (error) return false;

    std::filesystem::path destination = path.parent_path() / safe;
    if (!is_directory && destination.extension().empty())
    {
        destination.replace_extension(path.extension());
    }
    if (destination == path) return true;

    if (std::filesystem::exists(destination, error))
    {
        project_browser_status = "同名が既にあります: " + safe;
        return false;
    }

    std::filesystem::rename(path, destination, error);
    if (error)
    {
        project_browser_status = "改名失敗: " + path.filename().u8string();
        return false;
    }

    // AssetDatabase の登録を差し替える。GUID はパスから導出されるため、
    // 古い登録を消して新しいパスで登録し直す。
    if (const ReplayEngine::Assets::AssetRecord* old_record =
        asset_database.FindByPath(path))
    {
        const ReplayEngine::Assets::AssetKind kind = old_record->kind;
        const std::string old_guid = old_record->guid;
        asset_database.Remove(old_guid);
        if (!is_directory)
        {
            const ReplayEngine::Assets::AssetRecord& fresh =
                asset_database.Register(destination, kind);
            selected_asset_guid = fresh.guid;

            // AssetDatabase は現在 path-derived GUID なので、Scene Flow の改名時は
            // 開いている Editor と Active 参照の両方を同時に追従させる。
            if (kind == ReplayEngine::Assets::AssetKind::SceneFlow)
            {
                if (scene_flow_editor_guid == old_guid)
                {
                    scene_flow_editor_guid = fresh.guid;
                    scene_flow_editor_path = destination;
                }
                if (project_settings.SceneFlowGuid() == old_guid)
                {
                    project_settings.SetSceneFlowGuid(fresh.guid);
                    save_project_settings();
                    sync_runtime_scene_flow_asset();
                }
            }
        }
        std::string save_error;
        if (!asset_database.Save(save_error))
        {
            push_editor_log("Warning", "AssetDatabase 保存失敗: " + save_error);
        }
    }

    const std::string renamed_extension =
        ToLowerCopy(destination.extension().u8string());
    if (renamed_extension == ".cs") refresh_csharp_scripts();
    if (renamed_extension == ReplayEngine::Rendering::ShaderComposerAsset::file_extension)
    {
        shader_composer_editor.NotifyAssetRenamed(path, destination);
    }
    if (renamed_extension == ".hlsl" || renamed_extension == ".fx")
    {
        std::error_code root_error;
        const std::filesystem::path root = std::filesystem::current_path(root_error);
        if (!root_error)
        {
            // Shader 内部の replay_guid は書き換えない。
            // Source path だけ Catalog へ即反映し、Material の ShaderGUID 参照を保つ。
            shader_library.ScanAll(root);
        }
    }
    project_browser_status = "改名しました: " + destination.filename().u8string();
    return true;
}

// -----------------------------------------------------------------------------
//  左ペイン: フォルダツリー
// -----------------------------------------------------------------------------


namespace
{
    std::filesystem::path AbsoluteProjectPath(const std::filesystem::path& value)
    {
        std::error_code error;
        std::filesystem::path absolute = std::filesystem::absolute(value, error);
        if (error) absolute = value;
        return absolute.lexically_normal();
    }

    bool IsPathInsideOrEqual(const std::filesystem::path& value,
        const std::filesystem::path& root)
    {
        const std::filesystem::path absolute_value = AbsoluteProjectPath(value);
        const std::filesystem::path absolute_root = AbsoluteProjectPath(root);
        std::error_code error;
        const std::filesystem::path relative =
            std::filesystem::relative(absolute_value, absolute_root, error);
        if (error) return absolute_value == absolute_root;
        if (relative.empty() || relative == ".") return true;
        const auto first = relative.begin();
        return first != relative.end() && first->generic_u8string() != "..";
    }

    bool PropertyReferencesAnyGuid(const ReplayEngine::Reflection::PropertyValue& value,
        const std::set<std::string>& guids)
    {
        if (value.IsArray())
        {
            for (const auto& child : value.ArrayElements())
            {
                if (PropertyReferencesAnyGuid(child, guids)) return true;
            }
            return false;
        }
        const auto type = value.Type();
        return (type == ReplayEngine::Reflection::PropertyType::AssetReference ||
            type == ReplayEngine::Reflection::PropertyType::SceneReference) &&
            guids.find(value.AsString()) != guids.end();
    }
}

void framework::project_begin_rename_selected()
{
    if (!object_editor_context.CanEdit())
    {
        project_browser_status = "Play 中は Project Asset を改名できません";
        return;
    }
    if (project_selected_entry_path.empty()) return;
    std::error_code error;
    if (!std::filesystem::exists(project_selected_entry_path, error) || error) return;
    project_rename_target = project_selected_entry_path;
    project_rename_focus_pending = true;
    const bool directory = std::filesystem::is_directory(project_rename_target, error);
    const std::string stem = directory ? project_rename_target.filename().u8string() :
        project_rename_target.stem().u8string();
    strncpy_s(project_rename_buffer, sizeof(project_rename_buffer), stem.c_str(), _TRUNCATE);
}

void framework::project_request_delete(const std::filesystem::path& path)
{
    if (!object_editor_context.CanEdit())
    {
        project_browser_status = "Play 中は Project Asset を削除できません";
        return;
    }
    project_delete_target = path;
    project_delete_references.clear();
    project_delete_contents.clear();

    std::error_code error;
    const bool directory = std::filesystem::is_directory(path, error);
    if (error || (!directory && !std::filesystem::is_regular_file(path, error)))
    {
        project_browser_status = "削除対象が見つかりません";
        project_delete_target.clear();
        return;
    }

    std::set<std::string> guids;
    for (const auto& record : asset_database.Records())
    {
        const bool affected = directory ? IsPathInsideOrEqual(record.source_path, path) :
            AbsoluteProjectPath(record.source_path) == AbsoluteProjectPath(path);
        if (affected) guids.insert(record.guid);
    }

    // 現在の Scene の serializable property を走査し、GUID参照を列挙する。
    ReplayEngine::Scene::Scene* scene = object_editor_context.GetScene();
    if (scene != nullptr && !guids.empty())
    {
        for (std::size_t oi = 0; oi < scene->GameObjectCount(); ++oi)
        {
            ReplayEngine::Core::GameObject* object = scene->GameObjectAt(oi);
            if (object == nullptr || object->PendingDestroy()) continue;
            for (std::size_t ci = 0; ci < object->ComponentCount(); ++ci)
            {
                ReplayEngine::Core::Component* component = object->ComponentAt(ci);
                if (component == nullptr || component->PendingDestroy()) continue;
                ReplayEngine::Reflection::PropertyBag bag;
                ReplayEngine::Reflection::PropertyRegistry::Capture(*component, bag);
                for (const auto& entry : bag.Entries())
                {
                    if (PropertyReferencesAnyGuid(entry.value, guids))
                    {
                        project_delete_references.push_back(object->Name() + " / " +
                            component->TypeName() + "." + entry.name);
                    }
                }
            }
        }
    }

    const auto append_setting_reference = [&](const std::string& guid, const char* label)
    {
        if (!guid.empty() && guids.find(guid) != guids.end())
            project_delete_references.push_back(std::string("Project Settings / ") + label);
    };
    append_setting_reference(project_settings.DefaultCharacterPrefabGuid(), "Default Character Prefab");
    append_setting_reference(project_settings.StartupSceneGuid(), "Startup Scene");
    append_setting_reference(project_settings.SceneFlowGuid(), "Scene Flow");
    append_setting_reference(project_settings.LocalizationTableGuid(), "Localization Table");
    append_setting_reference(project_settings.InputActionAssetGuid(), "Input Action Asset");

    if (directory)
    {
        std::size_t count = 0;
        for (std::filesystem::recursive_directory_iterator it(path,
            std::filesystem::directory_options::skip_permission_denied, error), end;
            !error && it != end; it.increment(error))
        {
            ++count;
            if (project_delete_contents.size() < 24)
                project_delete_contents.push_back(it->path().lexically_relative(path).generic_u8string());
            if (count > 100000) break;
        }
        if (count > project_delete_contents.size())
            project_delete_contents.push_back("... 他 " +
                std::to_string(count - project_delete_contents.size()) + " 件");
    }
    project_delete_popup_pending = true;
}

void framework::draw_project_delete_popup()
{
    if (project_delete_popup_pending)
    {
        ImGui::OpenPopup("Project Asset を削除");
        project_delete_popup_pending = false;
    }
    if (!ImGui::BeginPopupModal("Project Asset を削除", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize)) return;

    ImGui::TextWrapped("ゴミ箱へ移動します: %s",
        project_delete_target.generic_u8string().c_str());
    if (!project_delete_references.empty())
    {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
            "この Asset を参照している項目があります:");
        for (std::size_t i = 0; i < project_delete_references.size() && i < 24; ++i)
            ImGui::BulletText("%s", project_delete_references[i].c_str());
    }
    if (!project_delete_contents.empty())
    {
        ImGui::Separator();
        ImGui::TextUnformatted("フォルダ内:");
        for (const std::string& item : project_delete_contents)
            ImGui::BulletText("%s", item.c_str());
    }
    ImGui::Separator();
    if (ImGui::Button("ゴミ箱へ移動"))
    {
        project_delete_confirmed();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("キャンセル"))
    {
        project_delete_target.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

bool framework::project_delete_confirmed()
{
    if (!object_editor_context.CanEdit())
    {
        project_browser_status = "Play 中は Project Asset を削除できません";
        return false;
    }
    if (project_delete_target.empty()) return false;
    std::error_code error;
    const bool directory = std::filesystem::is_directory(project_delete_target, error);
    if (error) return false;

    // Windows のごみ箱へ送る。完全削除はしない。
    std::wstring source = std::filesystem::absolute(project_delete_target, error).wstring();
    if (error) source = project_delete_target.wstring();
    source.push_back(L'\0'); // SHFileOperation は double-NUL 終端。
    SHFILEOPSTRUCTW operation{};
    operation.wFunc = FO_DELETE;
    operation.pFrom = source.c_str();
    operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    const int result = SHFileOperationW(&operation);
    if (result != 0 || operation.fAnyOperationsAborted)
    {
        project_browser_status = "削除をキャンセル/失敗しました";
        return false;
    }

    std::vector<std::string> remove_guids;
    for (const auto& record : asset_database.Records())
    {
        const bool affected = directory ?
            IsPathInsideOrEqual(record.source_path, project_delete_target) :
            AbsoluteProjectPath(record.source_path) == AbsoluteProjectPath(project_delete_target);
        if (affected) remove_guids.push_back(record.guid);
    }
    for (const std::string& guid : remove_guids) asset_database.Remove(guid);
    std::string save_error;
    if (!asset_database.Save(save_error)) push_editor_log("Warning", save_error);

    if (!selected_asset_guid.empty() &&
        std::find(remove_guids.begin(), remove_guids.end(), selected_asset_guid) != remove_guids.end())
        selected_asset_guid.clear();

    // Project Settings の参照も dangling にしない。削除自体は Undo 対象外だが、
    // 設定側は安全な未設定へ戻す。
    bool settings_changed = false;
    const auto removed = [&remove_guids](const std::string& guid)
    {
        return !guid.empty() && std::find(remove_guids.begin(), remove_guids.end(), guid) != remove_guids.end();
    };
    if (removed(project_settings.DefaultCharacterPrefabGuid())) { project_settings.ClearDefaultCharacterPrefab(); settings_changed = true; }
    if (removed(project_settings.StartupSceneGuid())) { project_settings.ClearStartupScene(); settings_changed = true; }
    if (removed(project_settings.SceneFlowGuid())) { project_settings.ClearSceneFlow(); settings_changed = true; }
    if (removed(project_settings.LocalizationTableGuid())) { project_settings.ClearLocalizationTable(); settings_changed = true; }
    if (removed(project_settings.InputActionAssetGuid())) { project_settings.ClearInputActionAsset(); load_active_input_action_asset(); settings_changed = true; }
    if (settings_changed) save_project_settings();

    project_selected_entry_path.clear();
    project_browser_status = "ゴミ箱へ移動しました: " + project_delete_target.filename().u8string();
    const std::string ext = ToLowerCopy(project_delete_target.extension().u8string());
    if (ext == ".cs" || directory) refresh_csharp_scripts();
    if (ext == ".hlsl" || ext == ".fx" || directory)
    {
        std::error_code root_error;
        const auto root = std::filesystem::current_path(root_error);
        if (!root_error) shader_library.ScanAll(root);
    }
    project_delete_target.clear();
    return true;
}
