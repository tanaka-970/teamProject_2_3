#include "framework.h"
#include "texture.h"
#include "../../RePlayEngine/Assets/AssetCache.h"
#include "../../RePlayEngine/Editor/Style/EditorStyle.h"
#include "../../RePlayEngine/Motion/CompositionAsset.h"
#include "../../RePlayEngine/Motion/MotionAsset.h"
#include "../../RePlayEngine/Rendering/Materials/MaterialAsset.h"
#include "../../RePlayEngine/Rendering/Shaders/ShaderAssetFactory.h"
#include "../../RePlayEngine/Rendering/Shaders/ShaderSource.h"
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
#include <regex>
#include <set>
#include <shellapi.h>
#include <sstream>
#include <vector>
#include "framework_project_browserInternal.h"
using namespace framework_project_browser::Detail;

// Project エントリ改名の関数本体

namespace
{
    std::filesystem::path AbsoluteProjectPath(const std::filesystem::path& value);
    bool IsPathInsideOrEqual(const std::filesystem::path& value,
        const std::filesystem::path& root);
    bool AssetDatabaseCanRelocate(const ReplayEngine::Assets::AssetDatabase& database,
        const std::filesystem::path& old_root, const std::filesystem::path& new_root,
        bool directory, std::string& collision_path);
}

bool framework::project_rename_entry(const std::filesystem::path& path,
    const std::string& new_name)
{
    if (!object_editor_context.CanEdit())
    {
        project_browser_status = "Play 中は Project Asset を改名できません";
        return false;
    }

    std::string name_error;
    if (!ValidateProjectEntryName(new_name, name_error))
    {
        project_browser_status = "改名できません: " + name_error;
        return false;
    }

    std::error_code error;
    const bool is_directory = std::filesystem::is_directory(path, error);
    if (error)
    {
        project_browser_status = "改名対象を確認できません";
        return false;
    }

    std::filesystem::path destination = path.parent_path() / std::filesystem::u8path(new_name);
    if (!is_directory) destination.replace_extension(path.extension());
    if (destination == path) return true;
    if (std::filesystem::exists(destination, error) && !error)
    {
        project_browser_status = "同名が既にあります: " + destination.filename().u8string();
        return false;
    }
    std::string database_collision;
    if (!AssetDatabaseCanRelocate(asset_database, path, destination, is_directory,
        database_collision))
    {
        project_browser_status = "改名できません。AssetDatabaseで予約済みです: " + database_collision;
        return false;
    }

    std::filesystem::rename(path, destination, error);
    if (error)
    {
        project_browser_status = "改名失敗: " + error.message();
        return false;
    }

    // Path だけを更新し GUID は絶対に変えない。
    if (is_directory) asset_database.RelocateTree(path, destination);
    else asset_database.RelocatePath(path, destination, true);
    std::string save_error;
    if (!asset_database.Save(save_error))
        push_editor_log("Warning", "AssetDatabase 保存失敗: " + save_error);

    project_notify_path_relocated(path, destination);

    std::string history_error;
    if (!external_file_history.RecordPathMove(path, destination,
        "Project Asset を改名", history_error) && !history_error.empty())
    {
        push_editor_log("Warning", "改名は完了しましたが Undo 記録に失敗: " + history_error);
    }

    const std::string renamed_extension = ToLowerCopy(destination.extension().u8string());
    if (renamed_extension == ".cs" || is_directory) refresh_csharp_scripts();
    if (renamed_extension == ".hlsl" || renamed_extension == ".fx" ||
        renamed_extension == ReplayEngine::Rendering::ShaderComposerAsset::file_extension ||
        is_directory)
    {
        std::error_code root_error;
        const std::filesystem::path root = std::filesystem::current_path(root_error);
        if (!root_error) shader_library.ScanAll(root);
    }

    project_browser_status = "改名しました: " + destination.filename().u8string() +
        "（GUID維持 / Ctrl+Zで戻せます）";
    return true;
}

bool framework::project_move_entry(const std::filesystem::path& path,
    const std::filesystem::path& destination_folder)
{
    if (!object_editor_context.CanEdit())
    {
        project_browser_status = "Play 中は Project Asset を移動できません";
        return false;
    }
    std::error_code error;
    if (!std::filesystem::exists(path, error) || error ||
        !std::filesystem::is_directory(destination_folder, error) || error)
    {
        project_browser_status = "移動元または移動先が見つかりません";
        return false;
    }

    const bool directory = std::filesystem::is_directory(path, error);
    if (error) return false;
    const std::filesystem::path destination = destination_folder / path.filename();
    if (std::filesystem::equivalent(path.parent_path(), destination_folder, error) && !error)
        return true;
    error.clear();
    if (std::filesystem::exists(destination, error) && !error)
    {
        project_browser_status = "移動先に同名があります: " + destination.filename().u8string();
        return false;
    }
    if (directory)
    {
        const std::filesystem::path absolute_path = AbsoluteProjectPath(path);
        const std::filesystem::path absolute_destination = AbsoluteProjectPath(destination_folder);
        if (IsPathInsideOrEqual(absolute_destination, absolute_path))
        {
            project_browser_status = "フォルダを自分自身/子フォルダへ移動できません";
            return false;
        }
    }
    std::string database_collision;
    if (!AssetDatabaseCanRelocate(asset_database, path, destination, directory,
        database_collision))
    {
        project_browser_status = "移動できません。AssetDatabaseで予約済みです: " + database_collision;
        return false;
    }

    std::filesystem::rename(path, destination, error);
    if (error)
    {
        project_browser_status = "移動失敗: " + error.message();
        return false;
    }

    if (directory) asset_database.RelocateTree(path, destination);
    else asset_database.RelocatePath(path, destination, false);
    std::string save_error;
    if (!asset_database.Save(save_error))
        push_editor_log("Warning", "AssetDatabase 保存失敗: " + save_error);

    project_notify_path_relocated(path, destination);
    std::string history_error;
    if (!external_file_history.RecordPathMove(path, destination,
        "Project Asset を移動", history_error) && !history_error.empty())
    {
        push_editor_log("Warning", "移動は完了しましたが Undo 記録に失敗: " + history_error);
    }

    const std::string ext = ToLowerCopy(destination.extension().u8string());
    if (directory || ext == ".cs") refresh_csharp_scripts();
    if (directory || ext == ".hlsl" || ext == ".fx" ||
        ext == ReplayEngine::Rendering::ShaderComposerAsset::file_extension)
    {
        std::error_code root_error;
        const auto root = std::filesystem::current_path(root_error);
        if (!root_error) shader_library.ScanAll(root);
    }
    project_browser_status = "移動しました: " + destination.generic_u8string() +
        "（GUID維持 / Ctrl+Zで戻せます）";
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

    bool IsHiddenEditorStorage(const std::filesystem::path& path)
    {
        for (const auto& part : AbsoluteProjectPath(path))
        {
            const std::string name = ToLowerCopy(part.u8string());
            if (name == ".replaytrash" || name == ".replayeditorundo") return true;
        }
        return false;
    }

    bool TryRemapPath(const std::filesystem::path& value,
        const std::filesystem::path& old_root, const std::filesystem::path& new_root,
        std::filesystem::path& out)
    {
        if (value.empty()) return false;
        const std::filesystem::path absolute_value = AbsoluteProjectPath(value);
        const std::filesystem::path absolute_old = AbsoluteProjectPath(old_root);
        const std::filesystem::path absolute_new = AbsoluteProjectPath(new_root);
        std::error_code error;
        std::filesystem::path relative = std::filesystem::relative(absolute_value, absolute_old, error);
        if (error) return false;
        if (!relative.empty() && relative != ".")
        {
            const auto first = relative.begin();
            if (first == relative.end() || first->generic_u8string() == "..") return false;
        }
        const std::filesystem::path mapped = (relative.empty() || relative == ".")
            ? absolute_new : (absolute_new / relative).lexically_normal();
        out = value.is_absolute()
            ? mapped
            : ReplayEngine::Assets::AssetDatabase::NormalizeProjectPath(mapped);
        return true;
    }

    bool AssetDatabaseCanRelocate(const ReplayEngine::Assets::AssetDatabase& database,
        const std::filesystem::path& old_root, const std::filesystem::path& new_root,
        bool directory, std::string& collision_path)
    {
        std::set<std::string> moving_guids;
        for (const auto& record : database.Records())
        {
            const bool moving = directory
                ? IsPathInsideOrEqual(record.source_path, old_root)
                : AbsoluteProjectPath(record.source_path) == AbsoluteProjectPath(old_root);
            if (moving) moving_guids.insert(record.guid);
        }

        for (const auto& record : database.Records())
        {
            const bool moving = moving_guids.find(record.guid) != moving_guids.end();
            if (!moving) continue;
            std::filesystem::path mapped;
            if (!TryRemapPath(record.source_path, old_root, new_root, mapped)) continue;
            const auto* occupied = database.FindByPath(mapped);
            if (occupied != nullptr && moving_guids.find(occupied->guid) == moving_guids.end())
            {
                collision_path = mapped.generic_u8string();
                return false;
            }
        }
        collision_path.clear();
        return true;
    }

    std::filesystem::path UniqueHiddenMovePath(const std::filesystem::path& root,
        const std::filesystem::path& source)
    {
        std::error_code error;
        std::filesystem::create_directories(root, error);
        const std::string filename = source.filename().u8string();
        for (int suffix = 0; suffix < 100000; ++suffix)
        {
            const std::string name = suffix == 0 ? filename :
                (source.stem().u8string() + " (" + std::to_string(suffix) + ")" +
                    source.extension().u8string());
            const std::filesystem::path candidate = root / std::filesystem::u8path(name);
            error.clear();
            if (!std::filesystem::exists(candidate, error) || error) return candidate;
        }
        return root / std::filesystem::u8path("replay_deleted_" + filename);
    }

    bool ReadTextBytes(const std::filesystem::path& path, std::string& text)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) return false;
        text.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        return static_cast<bool>(stream) || stream.eof();
    }

    bool WriteTextBytes(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream) return false;
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        return static_cast<bool>(stream);
    }

    std::string ReplaceRegexAll(const std::string& text, const std::regex& expression,
        const std::string& replacement)
    {
        return std::regex_replace(text, expression, replacement);
    }

    bool RefreshDuplicatedCSharpIdentities(const std::filesystem::path& project_root,
        const std::filesystem::path& duplicate_root, std::string& error)
    {
        namespace CSharp = ReplayEngine::Scripting::CSharp;
        std::vector<std::filesystem::path> sources;
        std::error_code fs_error;
        if (std::filesystem::is_directory(duplicate_root, fs_error) && !fs_error)
        {
            for (std::filesystem::recursive_directory_iterator it(duplicate_root,
                std::filesystem::directory_options::skip_permission_denied, fs_error), end;
                !fs_error && it != end; it.increment(fs_error))
            {
                if (it->is_regular_file(fs_error) && !fs_error &&
                    ToLowerCopy(it->path().extension().u8string()) == ".cs")
                    sources.push_back(it->path());
                fs_error.clear();
            }
        }
        else if (ToLowerCopy(duplicate_root.extension().u8string()) == ".cs")
        {
            sources.push_back(duplicate_root);
        }
        if (sources.empty()) return true;

        std::set<std::string> used_names;
        for (const auto& info : CSharp::CSharpProject::DiscoverBehaviours(project_root))
        {
            if (!IsPathInsideOrEqual(info.source_path, duplicate_root))
                used_names.insert(info.class_name);
        }

        struct RenameInfo { std::filesystem::path path; std::string old_name; std::string new_name; };
        std::vector<RenameInfo> renames;
        for (const auto& source : sources)
        {
            CSharp::CSharpBehaviourInfo info;
            if (!CSharp::CSharpProject::TryReadBehaviourInfo(source, info)) continue;
            std::string base = info.class_name + "Copy";
            std::string candidate = base;
            for (int suffix = 2; used_names.find(candidate) != used_names.end() && suffix < 10000; ++suffix)
                candidate = base + std::to_string(suffix);
            used_names.insert(candidate);
            renames.push_back({ source, info.class_name, candidate });
        }

        // 同じ複製フォルダ内の型参照も新しい型名へ向ける。
        for (auto& item : renames)
        {
            std::string text;
            if (!ReadTextBytes(item.path, text))
            {
                error = "複製C#を読み取れません: " + item.path.generic_u8string();
                return false;
            }
            for (const auto& mapping : renames)
            {
                const std::regex word("\\b" + mapping.old_name + "\\b");
                text = ReplaceRegexAll(text, word, mapping.new_name);
            }
            const std::regex replay_guid(
                R"(\[\s*(?:ReplayEngine\.)?ReplayGuid\s*\(\s*\"[0-9a-fA-F\-\{\}]{32,38}\"\s*\)\s*\])");
            text = ReplaceRegexAll(text, replay_guid,
                "[ReplayGuid(\"" + CSharp::CSharpProject::GenerateTypeGuid() + "\")]" );
            if (!WriteTextBytes(item.path, text))
            {
                error = "複製C#を書き戻せません: " + item.path.generic_u8string();
                return false;
            }
        }

        for (auto& item : renames)
        {
            if (item.path.stem().u8string() != item.old_name) continue;
            const std::filesystem::path destination = item.path.parent_path() /
                std::filesystem::u8path(item.new_name + ".cs");
            std::filesystem::rename(item.path, destination, fs_error);
            if (fs_error)
            {
                error = "複製C#のファイル名を型名へ合わせられません: " + fs_error.message();
                return false;
            }
            item.path = destination;
        }
        return true;
    }

    bool RefreshDuplicatedShaderIdentities(const std::filesystem::path& duplicate_root,
        std::string& error)
    {
        std::vector<std::filesystem::path> sources;
        std::error_code fs_error;
        if (std::filesystem::is_directory(duplicate_root, fs_error) && !fs_error)
        {
            for (std::filesystem::recursive_directory_iterator it(duplicate_root,
                std::filesystem::directory_options::skip_permission_denied, fs_error), end;
                !fs_error && it != end; it.increment(fs_error))
            {
                if (!it->is_regular_file(fs_error) || fs_error) { fs_error.clear(); continue; }
                const std::string ext = ToLowerCopy(it->path().extension().u8string());
                if (ext == ".hlsl" || ext == ".fx") sources.push_back(it->path());
            }
        }
        else
        {
            const std::string ext = ToLowerCopy(duplicate_root.extension().u8string());
            if (ext == ".hlsl" || ext == ".fx") sources.push_back(duplicate_root);
        }

        const std::regex replay_guid(R"(#pragma\s+replay_guid\s+\"[0-9a-fA-F]{32}\")");
        for (const auto& source : sources)
        {
            std::string text;
            if (!ReadTextBytes(source, text)) continue;
            const std::string replacement = "#pragma replay_guid \"" +
                ReplayEngine::Rendering::ShaderSource::GenerateID().ToString() + "\"";
            text = ReplaceRegexAll(text, replay_guid, replacement);
            if (!WriteTextBytes(source, text))
            {
                error = "複製ShaderのGUIDを書き戻せません: " + source.generic_u8string();
                return false;
            }
        }
        return true;
    }

    bool RefreshDuplicatedComposerIdentity(const std::filesystem::path& project_root,
        const std::filesystem::path& path, std::filesystem::path& generated_path,
        std::string& error)
    {
        generated_path.clear();
        if (ToLowerCopy(path.extension().u8string()) !=
            ReplayEngine::Rendering::ShaderComposerAsset::file_extension) return true;

        ReplayEngine::Rendering::ShaderComposerAsset asset;
        if (!ReplayEngine::Rendering::ShaderComposerAsset::Load(path, asset, error)) return false;
        asset.shader_id = ReplayEngine::Rendering::ShaderSource::GenerateID();
        asset.display_name = path.stem().u8string();

        // Folder 複製でも元Graphと generated HLSL を共有しないよう、
        // 新Shader IDをファイル名へ含めて常に新しい生成先を使う。
        const std::string id_text = asset.shader_id.ToString();
        const std::string short_id = id_text.size() >= 8 ? id_text.substr(0, 8) : id_text;
        std::filesystem::path generated_folder = asset.generated_hlsl.parent_path();
        if (generated_folder.empty())
        {
            generated_folder = asset.domain == ReplayEngine::Rendering::ShaderDomain::Layer
                ? std::filesystem::path("Shader") / "Layers" / "Generated"
                : asset.domain == ReplayEngine::Rendering::ShaderDomain::PostProcess
                    ? std::filesystem::path("Shader") / "PostProcess" / "Generated"
                    : std::filesystem::path("Shader") / "Materials" / "Generated";
        }
        asset.generated_hlsl = generated_folder /
            std::filesystem::u8path(path.stem().u8string() + "_" + short_id + ".hlsl");

        if (!ReplayEngine::Rendering::ShaderComposerAsset::Save(asset, path, error)) return false;
        if (!ReplayEngine::Rendering::ShaderComposerGenerator::GenerateToFile(
            asset, project_root, error)) return false;
        generated_path = asset.generated_hlsl.is_absolute()
            ? asset.generated_hlsl : project_root / asset.generated_hlsl;
        return true;
    }

    void CollectComposerGraphs(const std::filesystem::path& container,
        std::vector<std::filesystem::path>& out)
    {
        out.clear();
        std::error_code error;
        if (std::filesystem::is_directory(container, error) && !error)
        {
            for (std::filesystem::recursive_directory_iterator it(container,
                std::filesystem::directory_options::skip_permission_denied, error), end;
                !error && it != end; it.increment(error))
            {
                if (!it->is_regular_file(error) || error) { error.clear(); continue; }
                if (ToLowerCopy(it->path().extension().u8string()) ==
                    ReplayEngine::Rendering::ShaderComposerAsset::file_extension)
                    out.push_back(it->path());
            }
        }
        else if (ToLowerCopy(container.extension().u8string()) ==
            ReplayEngine::Rendering::ShaderComposerAsset::file_extension)
        {
            out.push_back(container);
        }
    }

    std::filesystem::path ComposerCompanionStashPath(
        const std::filesystem::path& stash_root,
        const ReplayEngine::Rendering::ShaderComposerAsset& graph)
    {
        if (!graph.generated_hlsl.is_absolute())
            return stash_root / graph.generated_hlsl;
        return stash_root / std::filesystem::u8path(
            graph.shader_id.ToString() + "_" + graph.generated_hlsl.filename().u8string());
    }

    // Shader Composer の graph と generated HLSL は論理的には1つのAssetセット。
    // graph/graphを含むフォルダをProject Trashへ送るとき、フォルダ外に生成された
    // HLSLも同じUndo単位で退避する。AssetDatabase recordは消さず tombstone のまま
    // 維持するため、Material等が持つShader GUIDはUndoでそのまま復帰する。
    bool MoveComposerDeleteCompanions(const std::filesystem::path& current_container,
        const std::filesystem::path& original_root,
        const std::filesystem::path& hidden_container, bool deleting,
        ReplayEngine::Assets::AssetDatabase& database, std::string& error)
    {
        std::vector<std::filesystem::path> graphs;
        CollectComposerGraphs(current_container, graphs);
        if (graphs.empty()) { error.clear(); return true; }

        std::error_code root_error;
        const std::filesystem::path project_root = std::filesystem::current_path(root_error);
        if (root_error)
        {
            error = "Project rootを取得できません: " + root_error.message();
            return false;
        }
        std::error_code directory_error;
        const bool container_is_directory = std::filesystem::is_directory(
            current_container, directory_error) && !directory_error;
        // Directory自体はFileEditHistoryのrenameで往復するため、companion stashも
        // directoryの中へ置けば一緒に移動する。単一graphだけはファイルの中へ置けないので
        // hidden側の隣へ .generated companion を置く。
        const std::filesystem::path stash_root = container_is_directory
            ? current_container / ".ReplayGeneratedTrash"
            : std::filesystem::path(hidden_container.wstring() + L".generated");

        struct MovedPair final
        {
            std::filesystem::path from;
            std::filesystem::path to;
        };
        std::vector<MovedPair> moved;
        std::set<std::filesystem::path> handled;
        for (const auto& graph_path : graphs)
        {
            ReplayEngine::Rendering::ShaderComposerAsset graph;
            std::string graph_error;
            if (!ReplayEngine::Rendering::ShaderComposerAsset::Load(
                graph_path, graph, graph_error) || graph.generated_hlsl.empty())
                continue;

            const std::filesystem::path generated = graph.generated_hlsl.is_absolute()
                ? graph.generated_hlsl : project_root / graph.generated_hlsl;
            // generated HLSL 自体が削除フォルダ内なら main directory move に含まれる。
            if (IsPathInsideOrEqual(generated, original_root)) continue;
            const std::filesystem::path generated_key = AbsoluteProjectPath(generated);
            if (!handled.insert(generated_key).second) continue;

            const std::filesystem::path stash = ComposerCompanionStashPath(stash_root, graph);
            const std::filesystem::path from = deleting ? generated : stash;
            const std::filesystem::path to = deleting ? stash : generated;
            std::error_code fs_error;
            if (!std::filesystem::exists(from, fs_error) || fs_error)
            {
                // generated HLSLが元からMissingならgraph自体の削除/復元は妨げない。
                fs_error.clear();
                continue;
            }
            std::filesystem::create_directories(to.parent_path(), fs_error);
            if (!fs_error && std::filesystem::exists(to, fs_error) && !fs_error)
                fs_error = std::make_error_code(std::errc::file_exists);
            if (!fs_error) std::filesystem::rename(from, to, fs_error);
            if (fs_error)
            {
                // 初回Delete時も中途半端な companion 移動を残さない。
                for (auto it = moved.rbegin(); it != moved.rend(); ++it)
                {
                    std::error_code rollback_error;
                    std::filesystem::create_directories(it->from.parent_path(), rollback_error);
                    rollback_error.clear();
                    std::filesystem::rename(it->to, it->from, rollback_error);
                }
                error = "Shader Composer generated HLSL の移動に失敗: " +
                    fs_error.message();
                return false;
            }
            moved.push_back({ from, to });

            // 通常は初回生成時のrecordがtombstoneとして残る。古いProjectなどで
            // recordが無い場合だけ復元時に登録して、以後のGUID参照を正式管理する。
            if (!deleting && database.FindByPath(generated) == nullptr)
                database.Register(generated, ReplayEngine::Assets::AssetKind::Shader);
        }
        if (!deleting)
        {
            std::error_code cleanup_error;
            std::filesystem::remove_all(stash_root, cleanup_error);
        }
        error.clear();
        return true;
    }
}

bool framework::project_duplicate_entry(const std::filesystem::path& path)
{
    if (!object_editor_context.CanEdit())
    {
        project_browser_status = "Play 中は Project Asset を複製できません";
        return false;
    }
    std::error_code error;
    const std::filesystem::path project_root_guard = std::filesystem::current_path(error);
    if (!error && AbsoluteProjectPath(path) == AbsoluteProjectPath(project_root_guard))
    {
        project_browser_status = "Project root 自体は複製できません";
        return false;
    }
    error.clear();
    if (!std::filesystem::exists(path, error) || error)
    {
        project_browser_status = "複製元が見つかりません";
        return false;
    }

    const bool directory = std::filesystem::is_directory(path, error);
    const std::string extension = directory ? std::string() : path.extension().u8string();
    std::string copy_stem = path.stem().u8string() +
        (ToLowerCopy(extension) == ".cs" ? "Copy" : " Copy");
    std::filesystem::path destination = directory
        ? UniqueProjectPath(path.parent_path(), path.filename().u8string() + " Copy", {}, &asset_database)
        : UniqueProjectPath(path.parent_path(), copy_stem, extension, &asset_database);

    if (directory)
    {
        std::filesystem::copy(path, destination,
            std::filesystem::copy_options::recursive, error);
    }
    else
    {
        std::filesystem::copy_file(path, destination,
            std::filesystem::copy_options::none, error);
    }
    if (error)
    {
        project_browser_status = "複製失敗: " + error.message();
        return false;
    }

    const std::filesystem::path project_root = std::filesystem::current_path(error);
    std::string fix_error;
    if (!error && !RefreshDuplicatedCSharpIdentities(project_root, destination, fix_error))
    {
        std::error_code cleanup_error;
        std::filesystem::remove_all(destination, cleanup_error);
        project_browser_status = "複製を中止しました（C# identity 更新失敗）: " + fix_error;
        return false;
    }
    fix_error.clear();
    if (!RefreshDuplicatedShaderIdentities(destination, fix_error))
    {
        std::error_code cleanup_error;
        std::filesystem::remove_all(destination, cleanup_error);
        project_browser_status = "複製を中止しました（Shader GUID 更新失敗）: " + fix_error;
        return false;
    }

    std::vector<std::filesystem::path> generated_companions;
    if (directory)
    {
        for (std::filesystem::recursive_directory_iterator it(destination,
            std::filesystem::directory_options::skip_permission_denied, error), end;
            !error && it != end; it.increment(error))
        {
            if (!it->is_regular_file(error) || error) { error.clear(); continue; }
            std::string composer_error;
            std::filesystem::path generated_shader;
            if (!RefreshDuplicatedComposerIdentity(project_root, it->path(), generated_shader,
                composer_error))
            {
                std::error_code cleanup_error;
                std::filesystem::remove_all(destination, cleanup_error);
                for (const auto& generated : generated_companions)
                {
                    cleanup_error.clear();
                    std::filesystem::remove(generated, cleanup_error);
                }
                project_browser_status = "複製を中止しました（Shader Composer identity 更新失敗）: " +
                    composer_error;
                return false;
            }
            if (!generated_shader.empty()) generated_companions.push_back(generated_shader);
        }

        error.clear();
        for (std::filesystem::recursive_directory_iterator it(destination,
            std::filesystem::directory_options::skip_permission_denied, error), end;
            !error && it != end; it.increment(error))
        {
            if (!it->is_regular_file(error) || error) { error.clear(); continue; }
            const auto kind = project_kind_for(it->path());
            if (kind != ReplayEngine::Assets::AssetKind::Unknown)
                asset_database.Register(it->path(), kind);
        }
        for (const auto& generated : generated_companions)
            asset_database.Register(generated, ReplayEngine::Assets::AssetKind::Shader);
    }
    else
    {
        std::string composer_error;
        std::filesystem::path generated_shader;
        if (!RefreshDuplicatedComposerIdentity(project_root, destination, generated_shader,
            composer_error))
        {
            std::error_code cleanup_error;
            std::filesystem::remove(destination, cleanup_error);
            project_browser_status = "複製を中止しました（Shader Composer identity 更新失敗）: " +
                composer_error;
            return false;
        }
        if (!generated_shader.empty())
            asset_database.Register(generated_shader, ReplayEngine::Assets::AssetKind::Shader);
        const auto kind = project_kind_for(destination);
        if (kind != ReplayEngine::Assets::AssetKind::Unknown)
        {
            const auto& record = asset_database.Register(destination, kind);
            selected_asset_guid = record.guid;
        }
    }
    std::string save_error;
    if (!asset_database.Save(save_error)) push_editor_log("Warning", save_error);

    project_selected_entry_path = destination;
    selected_editor_object = editor_selection::asset;
    project_record_created_path(destination, "Project Asset を複製");

    if (directory || ToLowerCopy(destination.extension().u8string()) == ".cs")
        refresh_csharp_scripts();
    if (directory || ToLowerCopy(destination.extension().u8string()) == ".hlsl" ||
        ToLowerCopy(destination.extension().u8string()) == ".fx" ||
        ToLowerCopy(destination.extension().u8string()) ==
            ReplayEngine::Rendering::ShaderComposerAsset::file_extension)
    {
        if (!error) shader_library.ScanAll(project_root);
    }

    project_browser_status = "複製しました: " + destination.filename().u8string() +
        "（新しいAsset GUID / Ctrl+Zで取り消せます）";
    return true;
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
    const std::filesystem::path project_root = std::filesystem::current_path(error);
    if (!error && AbsoluteProjectPath(project_selected_entry_path) == AbsoluteProjectPath(project_root))
    {
        project_browser_status = "Project root 自体は改名できません";
        return;
    }
    error.clear();
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
    std::error_code delete_root_guard_error;
    const std::filesystem::path delete_root_guard = std::filesystem::current_path(delete_root_guard_error);
    if (!delete_root_guard_error && AbsoluteProjectPath(path) == AbsoluteProjectPath(delete_root_guard))
    {
        project_browser_status = "Project root 自体は削除できません";
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

    // Shader Composerを削除する場合、graph本体だけでなくフォルダ外にある
    // generated HLSLも同じ削除単位へ含める。これによりMaterial等からgenerated
    // Shader GUIDが参照されている場合も確認ダイアログへ正しく出る。
    std::vector<std::filesystem::path> delete_composer_graphs;
    CollectComposerGraphs(path, delete_composer_graphs);
    std::error_code root_error;
    const std::filesystem::path project_root = std::filesystem::current_path(root_error);
    if (!root_error)
    {
        for (const auto& graph_path : delete_composer_graphs)
        {
            ReplayEngine::Rendering::ShaderComposerAsset graph;
            std::string graph_error;
            if (!ReplayEngine::Rendering::ShaderComposerAsset::Load(
                graph_path, graph, graph_error) || graph.generated_hlsl.empty())
                continue;
            const std::filesystem::path generated = graph.generated_hlsl.is_absolute()
                ? graph.generated_hlsl : project_root / graph.generated_hlsl;
            if (IsPathInsideOrEqual(generated, path)) continue;
            if (const auto* generated_record = asset_database.FindByPath(generated))
                guids.insert(generated_record->guid);
        }
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

    const auto path_is_affected = [&](const std::filesystem::path& candidate)
    {
        return !candidate.empty() && (directory
            ? IsPathInsideOrEqual(candidate, path)
            : AbsoluteProjectPath(candidate) == AbsoluteProjectPath(path));
    };
    if (motion_editor_loaded && path_is_affected(motion_editor_path))
        project_delete_references.push_back("現在 Motion Editor で開いています");
    if (sprite_atlas_editor_loaded && path_is_affected(sprite_atlas_editor_path))
        project_delete_references.push_back("現在 Sprite Atlas Editor で開いています");
    if (scene_flow_editor_loaded && path_is_affected(scene_flow_editor_path))
        project_delete_references.push_back("現在 Scene Flow Editor で開いています");
    if (shader_composer_editor.HasAsset() && path_is_affected(shader_composer_editor.Path()))
        project_delete_references.push_back("現在 Shader Composer で開いています");
    if (material_editor_loaded && !material_editor_guid.empty())
    {
        if (const auto* material_record = asset_database.FindByGuid(material_editor_guid);
            material_record != nullptr && path_is_affected(material_record->source_path))
            project_delete_references.push_back("現在 Material Editor で開いています");
    }
    if (!object_scene_path.empty() && path_is_affected(object_scene_path))
        project_delete_references.push_back("現在編集中の Scene ファイルです");

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
    const std::filesystem::path root = std::filesystem::current_path(error);
    if (error) return false;
    const bool directory = std::filesystem::is_directory(project_delete_target, error);
    if (error) return false;

    // 完全削除しない。Project 内の隠し Trash へ move して、Ctrl+Z で確実に戻せる。
    const std::filesystem::path trash_root = root / ".ReplayTrash";
    const std::filesystem::path original = project_delete_target;
    const std::filesystem::path trash = UniqueHiddenMovePath(trash_root, original);
    std::filesystem::create_directories(trash.parent_path(), error);
    if (error)
    {
        project_browser_status = "Project Trash を作成できません";
        return false;
    }
    std::filesystem::rename(original, trash, error);
    if (error)
    {
        project_browser_status = "削除（Trashへ移動）失敗: " + error.message();
        return false;
    }

    std::string companion_error;
    if (!MoveComposerDeleteCompanions(trash, original, trash, true,
        asset_database, companion_error))
    {
        std::error_code rollback_error;
        std::filesystem::rename(trash, original, rollback_error);
        project_browser_status = "削除を中止しました: " + companion_error;
        if (rollback_error)
            push_editor_log("Error", "Shader companion失敗後のgraph復元にも失敗: " +
                rollback_error.message(), original);
        return false;
    }

    const auto deleted_path_contains = [&](const std::filesystem::path& candidate)
    {
        return !candidate.empty() && (directory
            ? IsPathInsideOrEqual(candidate, original)
            : AbsoluteProjectPath(candidate) == AbsoluteProjectPath(original));
    };
    if (motion_editor_loaded && deleted_path_contains(motion_editor_path))
    {
        motion_editor_loaded = false; motion_composition_loaded = false; motion_editor_dirty = false;
        motion_editor_status = "削除されたAssetを閉じました（Ctrl+Zでファイル復元可能）";
    }
    if (sprite_atlas_editor_loaded && deleted_path_contains(sprite_atlas_editor_path))
    {
        sprite_atlas_editor_loaded = false; sprite_atlas_editor_dirty = false;
        sprite_atlas_editor_status = "削除されたAtlasを閉じました（Ctrl+Zでファイル復元可能）";
    }
    if (scene_flow_editor_loaded && deleted_path_contains(scene_flow_editor_path))
    {
        scene_flow_editor_loaded = false; scene_flow_editor_dirty = false;
        scene_flow_editor_status = "削除されたScene Flowを閉じました（Ctrl+Zでファイル復元可能）";
    }
    if (shader_composer_editor.HasAsset() && deleted_path_contains(shader_composer_editor.Path()))
        shader_composer_editor.ClearAsset();
    if (material_editor_loaded && !material_editor_guid.empty())
    {
        if (const auto* material_record = asset_database.FindByGuid(material_editor_guid);
            material_record != nullptr && deleted_path_contains(material_record->source_path))
        {
            material_editor_loaded = false;
            material_editor_dirty = false;
            material_editor_guid.clear();
            material_editor_status = "削除されたMaterialを閉じました（Ctrl+Zでファイル復元可能）";
        }
    }

    // AssetDatabase の GUID record は削除しない。
    // source_path は元の場所を保持して「Missing」になるため、Undoで同一GUIDのまま完全復帰する。
    std::string history_error;
    if (!external_file_history.RecordPathMove(original, trash,
        "Project Asset を削除", history_error) && !history_error.empty())
    {
        push_editor_log("Warning", "Trash移動は完了しましたが Undo 記録に失敗: " + history_error);
    }

    if (project_selected_entry_path == original) project_selected_entry_path.clear();
    if (const auto* selected = asset_database.FindByGuid(selected_asset_guid))
    {
        std::filesystem::path ignored;
        if (TryRemapPath(selected->source_path, original, original, ignored)) selected_asset_guid.clear();
    }

    project_browser_status = "Project Trashへ移動しました: " + original.filename().u8string() +
        "（参照GUIDは保持 / Ctrl+Zで復元）";
    const std::string ext = ToLowerCopy(original.extension().u8string());
    if (ext == ".cs" || directory) refresh_csharp_scripts();
    if (ext == ".hlsl" || ext == ".fx" ||
        ext == ReplayEngine::Rendering::ShaderComposerAsset::file_extension || directory)
    {
        shader_library.ScanAll(root);
    }
    project_delete_target.clear();
    return true;
}


void framework::project_notify_path_relocated(const std::filesystem::path& from,
    const std::filesystem::path& to)
{
    const auto remap = [&](std::filesystem::path& value)
    {
        std::filesystem::path mapped;
        if (TryRemapPath(value, from, to, mapped)) value = std::move(mapped);
    };

    remap(project_selected_entry_path);
    remap(project_rename_target);
    remap(project_delete_target);
    remap(motion_editor_path);
    remap(sprite_atlas_editor_path);
    remap(scene_flow_editor_path);

    std::filesystem::path mapped_scene;
    if (TryRemapPath(object_scene_path, from, to, mapped_scene))
    {
        object_scene_path = std::move(mapped_scene);
        object_editor_context.SetScenePath(object_scene_path);
    }
    for (auto& recent : recent_scene_paths) remap(recent);

    if (shader_composer_editor.HasAsset())
    {
        const std::filesystem::path old_graph = shader_composer_editor.Path();
        std::filesystem::path new_graph;
        if (TryRemapPath(old_graph, from, to, new_graph))
            shader_composer_editor.NotifyAssetRenamed(old_graph, new_graph);
    }

    std::error_code root_error;
    const std::filesystem::path root = std::filesystem::current_path(root_error);
    if (!root_error && !project_current_folder.empty())
    {
        std::filesystem::path current = root / project_current_folder;
        std::filesystem::path mapped;
        if (TryRemapPath(current, from, to, mapped)) set_project_folder(mapped);
    }
}

void framework::project_record_created_path(const std::filesystem::path& path,
    const std::string& label)
{
    std::error_code error;
    const std::filesystem::path root = std::filesystem::current_path(error);
    if (error) return;
    std::string history_error;
    if (!external_file_history.RecordPathCreated(path, root / ".ReplayEditorUndo",
        label, history_error) && !history_error.empty())
    {
        push_editor_log("Warning", "作成は完了しましたが Undo 記録に失敗: " + history_error);
    }
}

void framework::project_apply_external_history_change()
{
    const auto& change = external_file_history.LastAppliedChange();
    using AppliedKind = ReplayEngine::Editor::FileEditHistory::AppliedKind;
    if (change.kind == AppliedKind::None) return;
    if (change.kind == AppliedKind::FileContent)
    {
        reload_external_file_edit_target(change.to_path);
        return;
    }

    std::string database_error;
    if (change.kind == AppliedKind::PathMove)
    {
        // Delete を Undo 後にEditorで再度開き、その後 Ctrl+Y した場合も
        // 開いたままのAssetが保存操作で復活しないよう、Trashへ戻る瞬間に閉じる。
        if (IsHiddenEditorStorage(change.to_path))
        {
            std::error_code source_error;
            const bool source_directory = std::filesystem::is_directory(
                change.from_path, source_error) && !source_error;
            const auto affected = [&](const std::filesystem::path& candidate)
            {
                if (candidate.empty()) return false;
                return source_directory ? IsPathInsideOrEqual(candidate, change.from_path) :
                    AbsoluteProjectPath(candidate) == AbsoluteProjectPath(change.from_path);
            };
            if (motion_editor_loaded && affected(motion_editor_path))
            {
                motion_editor_loaded = false; motion_composition_loaded = false;
                motion_editor_dirty = false;
                motion_editor_status = "Redoで削除されたAssetを閉じました";
            }
            if (sprite_atlas_editor_loaded && affected(sprite_atlas_editor_path))
            {
                sprite_atlas_editor_loaded = false; sprite_atlas_editor_dirty = false;
                sprite_atlas_editor_status = "Redoで削除されたAtlasを閉じました";
            }
            if (scene_flow_editor_loaded && affected(scene_flow_editor_path))
            {
                scene_flow_editor_loaded = false; scene_flow_editor_dirty = false;
                scene_flow_editor_status = "Redoで削除されたScene Flowを閉じました";
            }
            if (shader_composer_editor.HasAsset() && affected(shader_composer_editor.Path()))
                shader_composer_editor.ClearAsset();
            if (material_editor_loaded && !material_editor_guid.empty())
            {
                if (const auto* material_record = asset_database.FindByGuid(material_editor_guid);
                    material_record != nullptr && affected(material_record->source_path))
                {
                    material_editor_loaded = false; material_editor_dirty = false;
                    material_editor_guid.clear();
                    material_editor_status = "Redoで削除されたMaterialを閉じました";
                }
            }
        }

        // TrashへのDelete/Restoreは DB record を元パスの tombstone として維持する。
        const bool from_hidden = IsHiddenEditorStorage(change.from_path);
        const bool to_hidden = IsHiddenEditorStorage(change.to_path);
        if (!from_hidden && !to_hidden)
        {
            std::error_code fs_error;
            const bool directory = std::filesystem::is_directory(change.to_path, fs_error) && !fs_error;
            if (directory) asset_database.RelocateTree(change.from_path, change.to_path);
            else asset_database.RelocatePath(change.from_path, change.to_path, true);
            project_notify_path_relocated(change.from_path, change.to_path);
            asset_database.Save(database_error);
        }
        else if (from_hidden != to_hidden)
        {
            // Project TrashのUndo/RedoではShader Composerのgenerated HLSLも
            // graphと同じ操作として復元/再退避する。graph本体のmoveはFileEditHistory
            // が既に終えているので、現在存在するchange.to_pathからgraphを読む。
            const bool deleting = to_hidden;
            const std::filesystem::path original_root = deleting
                ? change.from_path : change.to_path;
            const std::filesystem::path hidden_container = deleting
                ? change.to_path : change.from_path;
            std::string companion_error;
            if (!MoveComposerDeleteCompanions(change.to_path, original_root,
                hidden_container, deleting, asset_database, companion_error))
            {
                push_editor_log("Warning", companion_error, change.to_path);
            }
            asset_database.Save(database_error);
        }
    }
    else if (change.kind == AppliedKind::PathCreate)
    {
        // Shader Composer は graph と generated HLSL が別パスにある。
        // 単一Graphの作成/複製だけでなく、Graphを含むフォルダ複製も1回のUndoで
        // generated HLSLまで同時に退避/復元する。
        const bool undoing_create = IsHiddenEditorStorage(change.to_path);
        const std::filesystem::path current_container = change.to_path;
        std::error_code current_error;
        const bool current_is_directory = std::filesystem::is_directory(
            current_container, current_error) && !current_error;
        const std::filesystem::path hidden_container = undoing_create
            ? change.to_path : change.from_path;
        std::filesystem::path companion_root;
        if (current_is_directory)
            companion_root = current_container / ".ReplayGeneratedUndo";
        else
            companion_root = std::filesystem::path(hidden_container.wstring() + L".generated");

        std::vector<std::filesystem::path> composer_graphs;
        CollectComposerGraphs(current_container, composer_graphs);
        std::error_code root_error;
        const std::filesystem::path project_root = std::filesystem::current_path(root_error);
        if (!root_error)
        {
            for (const auto& graph_path : composer_graphs)
            {
                ReplayEngine::Rendering::ShaderComposerAsset graph;
                std::string graph_error;
                if (!ReplayEngine::Rendering::ShaderComposerAsset::Load(
                    graph_path, graph, graph_error) || graph.generated_hlsl.empty())
                    continue;

                const std::filesystem::path generated = graph.generated_hlsl.is_absolute()
                    ? graph.generated_hlsl : project_root / graph.generated_hlsl;
                const std::filesystem::path stash = ComposerCompanionStashPath(
                    companion_root, graph);
                std::error_code move_error;
                if (undoing_create)
                {
                    if (std::filesystem::exists(generated, move_error) && !move_error)
                    {
                        std::filesystem::create_directories(stash.parent_path(), move_error);
                        move_error.clear();
                        std::filesystem::rename(generated, stash, move_error);
                    }
                    if (const auto* generated_record = asset_database.FindByPath(generated))
                        asset_database.Remove(generated_record->guid);
                }
                else if (std::filesystem::exists(stash, move_error) && !move_error)
                {
                    std::filesystem::create_directories(generated.parent_path(), move_error);
                    move_error.clear();
                    std::filesystem::rename(stash, generated, move_error);
                    if (!move_error)
                        asset_database.Register(generated, ReplayEngine::Assets::AssetKind::Shader);
                }
                if (move_error)
                    push_editor_log("Warning", "Shader Composer companion のUndo/Redoに失敗: " +
                        move_error.message(), generated);
            }
            if (!undoing_create)
            {
                std::error_code cleanup_error;
                std::filesystem::remove_all(companion_root, cleanup_error);
            }
        }

        if (IsHiddenEditorStorage(change.to_path))
        {
            // Undo create/duplicate: 作成したAsset recordだけ外す。
            const auto created_contains = [&](const std::filesystem::path& candidate)
            {
                if (candidate.empty()) return false;
                return IsPathInsideOrEqual(candidate, change.from_path);
            };
            if (created_contains(project_selected_entry_path)) project_selected_entry_path.clear();
            if (motion_editor_loaded && created_contains(motion_editor_path))
            {
                motion_editor_loaded=false; motion_composition_loaded=false; motion_editor_dirty=false;
                motion_editor_status="Undoで作成Assetを閉じました";
            }
            if (sprite_atlas_editor_loaded && created_contains(sprite_atlas_editor_path))
            {
                sprite_atlas_editor_loaded=false; sprite_atlas_editor_dirty=false;
                sprite_atlas_editor_status="Undoで作成Atlasを閉じました";
            }
            if (scene_flow_editor_loaded && created_contains(scene_flow_editor_path))
            {
                scene_flow_editor_loaded=false; scene_flow_editor_dirty=false;
                scene_flow_editor_status="Undoで作成Scene Flowを閉じました";
            }
            if (shader_composer_editor.HasAsset() && created_contains(shader_composer_editor.Path()))
                shader_composer_editor.ClearAsset();
            if (material_editor_loaded && !material_editor_guid.empty())
            {
                if (const auto* material_record = asset_database.FindByGuid(material_editor_guid);
                    material_record != nullptr && created_contains(material_record->source_path))
                {
                    material_editor_loaded = false;
                    material_editor_dirty = false;
                    material_editor_guid.clear();
                    material_editor_status = "Undoで作成Materialを閉じました";
                }
            }
            // Project Browserから新規作成したInput Actionは作成時に自動で
            // ProjectSettingsのActive Inputへ設定される。作成そのものをUndoするなら
            // その参照も同じUndoで戻し、Missing GUIDを残さない。複製など別ラベルの
            // PathCreateには適用しない。
            if (change.label == "Input Action Asset を作成")
            {
                if (const auto* input_record = asset_database.FindByPath(change.from_path);
                    input_record != nullptr &&
                    project_settings.InputActionAssetGuid() == input_record->guid)
                {
                    const bool old_undo = project_settings_file_undo_enabled;
                    project_settings_file_undo_enabled = false;
                    project_settings.SetInputActionAssetGuid("");
                    save_project_settings();
                    project_settings_file_undo_enabled = old_undo;
                    load_active_input_action_asset();
                }
            }

            std::vector<std::string> remove_guids;
            for (const auto& record : asset_database.Records())
            {
                if (IsPathInsideOrEqual(record.source_path, change.from_path))
                    remove_guids.push_back(record.guid);
            }
            for (const std::string& guid : remove_guids) asset_database.Remove(guid);
            if (!selected_asset_guid.empty() && asset_database.FindByGuid(selected_asset_guid) == nullptr)
                selected_asset_guid.clear();
        }
        else
        {
            // Redo create/duplicate: filesystem を走査して再登録。初回と同じpathなのでGUIDも同じ。
            std::error_code fs_error;
            if (std::filesystem::is_directory(change.to_path, fs_error) && !fs_error)
            {
                for (std::filesystem::recursive_directory_iterator it(change.to_path,
                    std::filesystem::directory_options::skip_permission_denied, fs_error), end;
                    !fs_error && it != end; it.increment(fs_error))
                {
                    if (!it->is_regular_file(fs_error) || fs_error) { fs_error.clear(); continue; }
                    const auto kind = project_kind_for(it->path());
                    if (kind != ReplayEngine::Assets::AssetKind::Unknown)
                        asset_database.Register(it->path(), kind);
                }
            }
            else
            {
                const auto kind = project_kind_for(change.to_path);
                if (kind != ReplayEngine::Assets::AssetKind::Unknown)
                    asset_database.Register(change.to_path, kind);
            }

            if (change.label == "Input Action Asset を作成" &&
                project_settings.InputActionAssetGuid().empty())
            {
                if (const auto* input_record = asset_database.FindByPath(change.to_path);
                    input_record != nullptr &&
                    input_record->kind == ReplayEngine::Assets::AssetKind::InputAction)
                {
                    const bool old_undo = project_settings_file_undo_enabled;
                    project_settings_file_undo_enabled = false;
                    project_settings.SetInputActionAssetGuid(input_record->guid);
                    save_project_settings();
                    project_settings_file_undo_enabled = old_undo;
                    load_active_input_action_asset();
                }
            }
        }
        asset_database.Save(database_error);
    }

    if (!database_error.empty()) push_editor_log("Warning", database_error);
    const std::string from_ext = ToLowerCopy(change.from_path.extension().u8string());
    const std::string to_ext = ToLowerCopy(change.to_path.extension().u8string());
    if (from_ext == ".cs" || to_ext == ".cs" ||
        change.from_path.extension().empty() || change.to_path.extension().empty())
        refresh_csharp_scripts();
    if (from_ext == ".hlsl" || from_ext == ".fx" || to_ext == ".hlsl" || to_ext == ".fx" ||
        from_ext == ReplayEngine::Rendering::ShaderComposerAsset::file_extension ||
        to_ext == ReplayEngine::Rendering::ShaderComposerAsset::file_extension)
    {
        std::error_code root_error;
        const auto root = std::filesystem::current_path(root_error);
        if (!root_error) shader_library.ScanAll(root);
    }
}

void framework::project_show_in_explorer(const std::filesystem::path& path)
{
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    if (error) return;
    if (std::filesystem::is_directory(absolute, error) && !error)
    {
        ShellExecuteW(nullptr, L"open", absolute.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    else
    {
        std::wstring parameters = L"/select,\"" + absolute.wstring() + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", parameters.c_str(), nullptr, SW_SHOWNORMAL);
    }
}

void framework::project_copy_path(const std::filesystem::path& path, bool absolute)
{
    std::error_code error;
    std::filesystem::path value = absolute ? std::filesystem::absolute(path, error) :
        ReplayEngine::Assets::AssetDatabase::NormalizeProjectPath(path);
    if (error) value = path;
    const std::string text = value.generic_u8string();
    ImGui::SetClipboardText(text.c_str());
    project_browser_status = std::string(absolute ? "絶対パス" : "Project相対パス") +
        "をコピーしました: " + text;
}
