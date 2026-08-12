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
#include "../../RePlayEngine/Scripting/CSharp/CSharpProject.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>
#include "framework_project_browserInternal.h"
using namespace framework_project_browser::Detail;

// Project エントリ改名の関数本体

bool framework::project_rename_entry(const std::filesystem::path& path,
    const std::string& new_name)
{
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
