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

// =============================================================================
//  Project ブラウザ
//
//  Unity の Project ビューと同じ 2 ペイン構成。
//    左  : フォルダツリー
//    右  : そのフォルダの中身（アイコン / サムネイル付き）
//
//  ここに集約した理由:
//    以前は framework_editor.cpp の draw_project_panel が
//    「フォームで名前を打って作る」形になっており、
//    作る場所と作られた物が出る場所が違っていた。
//    フォルダを持ち、その場で作り、その場で改名できるようにしたほうがかんりしやすいでしょ
// =============================================================================

namespace
{
    // Project ブラウザに出さないフォルダ。
    // ビルド生成物・外部ライブラリ・VCS 内部データはノイズにしかならない。
    bool IsHiddenProjectFolder(const std::string& name)
    {
        static const char* const hidden[] =
        {
            ".git", ".vs", ".vscode", ".github",
            "obj", "bin", "x64", "x86", "ipch",
            "DirectXTK-main", "imgui", "cereal-master", "tinygltf-release",
            "Saved", "Tools", "node_modules",
        };
        for (const char* entry : hidden)
        {
            if (_stricmp(name.c_str(), entry) == 0) return true;
        }
        return false;
    }

    std::string ToLowerCopy(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    bool IsImageExtension(const std::string& lower_extension)
    {
        return lower_extension == ".png" || lower_extension == ".jpg" ||
            lower_extension == ".jpeg" || lower_extension == ".bmp" ||
            lower_extension == ".dds" || lower_extension == ".tga" ||
            lower_extension == ".tif" || lower_extension == ".tiff" ||
            lower_extension == ".gif";
    }

    std::string SafeProjectFileName(std::string name)
    {
        for (char& character : name)
        {
            if (character == '<' || character == '>' || character == ':' ||
                character == '"' || character == '/' || character == '\\' ||
                character == '|' || character == '?' || character == '*')
            {
                character = '_';
            }
        }
        while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
        return name;
    }

    struct ProjectEntry final
    {
        std::filesystem::path path;
        std::string name;
        bool is_directory = false;
    };

    // フォルダ 1 段ぶんを読む。フォルダが先、その中で名前順。
    std::vector<ProjectEntry> ListProjectFolder(const std::filesystem::path& folder,
        bool directories_only)
    {
        std::vector<ProjectEntry> entries;
        std::error_code error;
        if (!std::filesystem::is_directory(folder, error) || error) return entries;

        std::filesystem::directory_iterator iterator(folder,
            std::filesystem::directory_options::skip_permission_denied, error);
        if (error) return entries;

        for (const std::filesystem::directory_entry& item : iterator)
        {
            std::error_code entry_error;
            const bool is_directory = item.is_directory(entry_error);
            if (entry_error) continue;
            if (!is_directory && directories_only) continue;

            const std::string name = item.path().filename().u8string();
            if (name.empty() || name.front() == '.') continue;
            if (is_directory && IsHiddenProjectFolder(name)) continue;

            ProjectEntry entry;
            entry.path = item.path();
            entry.name = name;
            entry.is_directory = is_directory;
            entries.push_back(std::move(entry));
        }

        std::sort(entries.begin(), entries.end(),
            [](const ProjectEntry& a, const ProjectEntry& b)
            {
                if (a.is_directory != b.is_directory) return a.is_directory;
                return ToLowerCopy(a.name) < ToLowerCopy(b.name);
            });
        return entries;
    }

    // Search 入力中は current folder だけでなく Project 全体を検索する。
    // .git/.vs/build 系の隠しフォルダは通常ツリーと同じ規則で再帰しない。
    std::vector<ProjectEntry> SearchProjectFiles(const std::filesystem::path& root,
        const std::string& lowered_query)
    {
        std::vector<ProjectEntry> entries;
        if (lowered_query.empty()) return entries;
        std::error_code error;
        std::filesystem::recursive_directory_iterator iterator(root,
            std::filesystem::directory_options::skip_permission_denied, error), end;
        for (; iterator != end && !error; iterator.increment(error))
        {
            if (error) break;
            const auto& item = *iterator;
            std::error_code entry_error;
            const bool directory = item.is_directory(entry_error);
            if (entry_error) continue;
            const std::string filename = item.path().filename().u8string();
            if (directory)
            {
                if (filename.empty() || filename.front() == '.' || IsHiddenProjectFolder(filename))
                    iterator.disable_recursion_pending();
                continue;
            }
            const std::filesystem::path relative = std::filesystem::relative(item.path(), root, entry_error);
            if (entry_error) continue;
            const std::string label = relative.generic_u8string();
            if (ToLowerCopy(label).find(lowered_query) == std::string::npos) continue;
            ProjectEntry entry; entry.path = item.path(); entry.name = label; entry.is_directory = false;
            entries.push_back(std::move(entry));
        }
        std::sort(entries.begin(), entries.end(), [](const ProjectEntry& a, const ProjectEntry& b)
        { return ToLowerCopy(a.name) < ToLowerCopy(b.name); });
        return entries;
    }

    // アイコン画像が無い種別のための文字ラベル。
    // 画像アイコンを用意するまでの繋ぎで、サムネイルとは別物。
    const char* KindBadge(ReplayEngine::Assets::AssetKind kind, bool is_directory)
    {
        using ReplayEngine::Assets::AssetKind;
        if (is_directory) return "FOLDER";
        switch (kind)
        {
        case AssetKind::Model:    return "MESH";
        case AssetKind::Image:    return "IMAGE";
        case AssetKind::Material: return "MAT";
        case AssetKind::Scene:    return "SCENE";
        case AssetKind::Script:   return "C#";
        case AssetKind::Audio:    return "AUDIO";
        case AssetKind::Shader:   return "SHADER";
        case AssetKind::SceneFlow:return "FLOW";
        case AssetKind::Motion:   return "MOTION";
        case AssetKind::Font:     return "FONT";
        default:                  return "FILE";
        }
    }

    ImVec4 KindColor(ReplayEngine::Assets::AssetKind kind, bool is_directory)
    {
        using ReplayEngine::Assets::AssetKind;
        if (is_directory) return ImVec4(0.98f, 0.80f, 0.36f, 1.0f);
        switch (kind)
        {
        case AssetKind::Model:    return ImVec4(0.47f, 0.76f, 0.98f, 1.0f);
        case AssetKind::Image:    return ImVec4(0.62f, 0.90f, 0.62f, 1.0f);
        case AssetKind::Material: return ImVec4(0.95f, 0.60f, 0.85f, 1.0f);
        case AssetKind::Scene:    return ImVec4(0.75f, 0.72f, 0.98f, 1.0f);
        case AssetKind::Script:   return ImVec4(0.45f, 0.88f, 0.80f, 1.0f);
        case AssetKind::Shader:   return ImVec4(0.98f, 0.67f, 0.28f, 1.0f);
        case AssetKind::SceneFlow:return ImVec4(0.55f, 0.86f, 1.00f, 1.0f);
        case AssetKind::Motion:   return ImVec4(0.95f, 0.78f, 0.36f, 1.0f);
        case AssetKind::Font:     return ImVec4(0.80f, 0.92f, 0.98f, 1.0f);
        default:                  return ImVec4(0.72f, 0.72f, 0.72f, 1.0f);
        }
    }
}

// -----------------------------------------------------------------------------
//  種別判定
// -----------------------------------------------------------------------------
ReplayEngine::Assets::AssetKind framework::project_kind_for(
    const std::filesystem::path& path) const
{
    using ReplayEngine::Assets::AssetKind;

    const std::string extension = ToLowerCopy(path.extension().u8string());
    if (extension == ".cs") return AssetKind::Script;
    if (extension == ".replayscene") return AssetKind::Scene;
    if (extension == ".replayprefab") return AssetKind::Scene;
    if (extension == ".replaymaterial") return AssetKind::Material;
    if (extension == ReplayEngine::Runtime::SceneFlowAsset::file_extension)
        return AssetKind::SceneFlow;
    if (extension == ReplayEngine::Motion::MotionAsset::file_extension ||
        extension == ReplayEngine::Motion::CompositionAsset::file_extension)
        return AssetKind::Motion;
    if (extension == ".fbx" || extension == ".glb" || extension == ".gltf" ||
        extension == ".obj") return AssetKind::Model;
    if (IsImageExtension(extension)) return AssetKind::Image;
    if (extension == ".wav" || extension == ".mp3" || extension == ".ogg")
        return AssetKind::Audio;
    if (extension == ".ttf" || extension == ".otf" || extension == ".ttc")
        return AssetKind::Font;
    if (extension == ".hlsl" || extension == ".fx" || extension == ".cso" ||
        extension == ReplayEngine::Rendering::ShaderComposerAsset::file_extension)
        return AssetKind::Shader;
    return AssetKind::Unknown;
}

// -----------------------------------------------------------------------------
//  サムネイル
//
//  load_texture_from_file はパスをキーに内部キャッシュを持ち、
//  失敗も記録するので毎フレーム呼んでも再読込は起きない。
//  ここで独自キャッシュは持たない。
// -----------------------------------------------------------------------------
ID3D11ShaderResourceView* framework::project_thumbnail_for(
    const std::filesystem::path& path)
{
    if (!device) return nullptr;

    const std::string extension = ToLowerCopy(path.extension().u8string());
    if (!IsImageExtension(extension)) return nullptr;

    ID3D11ShaderResourceView* view = nullptr;
    D3D11_TEXTURE2D_DESC description{};
    const HRESULT result = load_texture_from_file(device.Get(),
        path.wstring().c_str(), &view, &description);
    if (FAILED(result)) return nullptr;

    // load_texture_from_file は cache 所有 SRV を CopyTo(AddRef) する。Browser は毎 frame
    // 呼ぶため、呼び出し分を Release しないと RefCount が frame ごとに増え続ける。
    ID3D11ShaderResourceView* borrowed = view;
    if (view != nullptr) view->Release();
    return borrowed;
}

// -----------------------------------------------------------------------------
//  フォルダ移動
// -----------------------------------------------------------------------------
void framework::set_project_folder(const std::filesystem::path& folder)
{
    std::error_code error;
    const std::filesystem::path root = std::filesystem::current_path(error);
    if (error) return;

    std::filesystem::path relative =
        std::filesystem::relative(folder, root, error);
    if (error || relative.empty() || relative.u8string().rfind("..", 0) == 0)
    {
        project_current_folder.clear();
        return;
    }
    if (relative == ".") relative.clear();
    project_current_folder = relative;
    project_rename_target.clear();
}

// -----------------------------------------------------------------------------
//  作成
// -----------------------------------------------------------------------------
bool framework::project_create_folder(const std::string& name)
{
    const std::string safe = SafeProjectFileName(name);
    if (safe.empty())
    {
        project_browser_status = "フォルダ名が空です";
        return false;
    }

    std::error_code error;
    const std::filesystem::path root = std::filesystem::current_path(error);
    if (error) return false;

    std::filesystem::path path = root / project_current_folder / safe;
    for (int suffix = 2; std::filesystem::exists(path) && suffix < 10000; ++suffix)
    {
        path = root / project_current_folder / (safe + std::to_string(suffix));
    }

    std::filesystem::create_directories(path, error);
    if (error)
    {
        project_browser_status = "フォルダ作成失敗: " + path.generic_u8string();
        return false;
    }
    project_browser_status = "フォルダを作成しました: " + path.filename().u8string();
    return true;
}

bool framework::project_create_csharp_behaviour(const std::string& class_name)
{
    namespace CSharp = ReplayEngine::Scripting::CSharp;

    std::error_code error;
    const std::filesystem::path root = std::filesystem::current_path(error);
    if (error) return false;

    // 現在のフォルダが Scripts/ の中なら、その位置に作る。
    // 外にいる場合は Scripts/ 直下へ落とす。
    const std::filesystem::path scripts_root =
        CSharp::CSharpProject::GameScriptsProjectPath(root).parent_path();

    std::filesystem::path subfolder;
    const std::filesystem::path current = root / project_current_folder;
    std::error_code relative_error;
    const std::filesystem::path relative =
        std::filesystem::relative(current, scripts_root, relative_error);
    if (!relative_error && !relative.empty() &&
        relative.u8string().rfind("..", 0) != 0 && relative != ".")
    {
        subfolder = relative;
    }

    CSharp::CSharpBehaviourInfo info;
    std::string create_error;
    if (!CSharp::CSharpProject::CreateBehaviour(root, class_name,
        new_csharp_namespace, info, create_error, subfolder))
    {
        project_browser_status = "C# Behaviour 作成失敗: " + create_error;
        push_editor_log("Error", project_browser_status);
        return false;
    }

    const ReplayEngine::Assets::AssetRecord& record =
        asset_database.Register(info.source_path, ReplayEngine::Assets::AssetKind::Script);
    selected_asset_guid = record.guid;

    std::string save_error;
    if (!asset_database.Save(save_error))
    {
        push_editor_log("Warning",
            "C# script asset registration could not be saved: " + save_error,
            info.source_path);
    }

    refresh_csharp_scripts();
    set_project_folder(info.source_path.parent_path());

    std::string open_error;
    if (!CSharp::CSharpProject::OpenVisualStudio(info.source_path, 1, open_error))
    {
        push_editor_log("Warning", open_error, info.source_path);
    }

    project_browser_status =
        "C# Behaviour を作成しました。Add Component の Scripts/C# から載せられます: " +
        info.source_path.filename().u8string();
    push_editor_log("Info", project_browser_status, info.source_path, 1);
    return true;
}

bool framework::project_create_material(const std::string& name)
{
    using ReplayEngine::Assets::AssetKind;
    using ReplayEngine::Rendering::MaterialAsset;

    const std::string safe = SafeProjectFileName(name);
    if (safe.empty())
    {
        project_browser_status = "Material 名が空です";
        return false;
    }

    std::error_code error;
    const std::filesystem::path root = std::filesystem::current_path(error);
    if (error) return false;

    const std::filesystem::path folder = root / project_current_folder;
    std::filesystem::path path = folder / (safe + MaterialAsset::file_extension);
    for (int suffix = 2; std::filesystem::exists(path) && suffix < 10000; ++suffix)
    {
        path = folder / (safe + std::to_string(suffix) + MaterialAsset::file_extension);
    }

    MaterialAsset material;
    std::string save_error;
    if (!MaterialAsset::Save(material, path, save_error))
    {
        project_browser_status = "Material 作成失敗: " + save_error;
        return false;
    }

    const ReplayEngine::Assets::AssetRecord& record =
        asset_database.Register(path, AssetKind::Material);
    if (!asset_database.Save(save_error))
    {
        project_browser_status = "Material は作成しましたが DB 保存失敗: " + save_error;
        return false;
    }
    selected_asset_guid = record.guid;
    project_browser_status = "Material を作成しました: " + path.filename().u8string();
    return true;
}

bool framework::project_create_motion(const std::string& name)
{
    using ReplayEngine::Assets::AssetKind;
    using ReplayEngine::Motion::MotionAsset;

    const std::string safe = SafeProjectFileName(
        name.empty() ? std::string("NewMotion") : name);
    if (safe.empty())
    {
        project_browser_status = "Motion 名が空です";
        return false;
    }

    std::error_code error;
    const std::filesystem::path root = std::filesystem::current_path(error);
    if (error) return false;

    const std::filesystem::path folder = root / project_current_folder;
    std::filesystem::create_directories(folder, error);
    if (error)
    {
        project_browser_status = "Motion folder を作成できません";
        return false;
    }

    std::filesystem::path path = folder / (safe + MotionAsset::file_extension);
    for (int suffix = 2; std::filesystem::exists(path) && suffix < 10000; ++suffix)
        path = folder / (safe + std::to_string(suffix) + MotionAsset::file_extension);

    MotionAsset motion;
    motion.name = safe;
    motion.duration = 1.0f;

    std::string save_error;
    if (!MotionAsset::SaveToFile(path, motion, save_error))
    {
        project_browser_status = "Motion 作成失敗: " + save_error;
        return false;
    }

    const ReplayEngine::Assets::AssetRecord& record =
        asset_database.Register(path, AssetKind::Motion);
    if (!asset_database.Save(save_error))
    {
        project_browser_status = "Motion は作成しましたが DB 保存失敗: " + save_error;
        return false;
    }

    selected_asset_guid = record.guid;
    set_project_folder(path.parent_path());
    if (!open_motion_asset(record))
    {
        project_browser_status = "Motion は作成しましたが Editor で開けません: " +
            path.filename().u8string();
        return false;
    }

    project_browser_status = "Motion を作成しました: " + path.filename().u8string();
    push_editor_log("Info", project_browser_status, path, 1);
    return true;
}


bool framework::project_create_scene_flow(const std::string& name)
{
    using ReplayEngine::Assets::AssetKind;
    using ReplayEngine::Runtime::SceneFlowAsset;

    const std::string safe = SafeProjectFileName(
        name.empty() ? std::string("GameFlow") : name);
    if (safe.empty())
    {
        project_browser_status = "Scene Flow 名が空です";
        return false;
    }

    std::error_code error;
    const std::filesystem::path root = std::filesystem::current_path(error);
    if (error) return false;
    const std::filesystem::path folder = root / project_current_folder;
    std::filesystem::path path = folder / (safe + SceneFlowAsset::file_extension);
    for (int suffix = 2; std::filesystem::exists(path) && suffix < 10000; ++suffix)
        path = folder / (safe + std::to_string(suffix) + SceneFlowAsset::file_extension);

    SceneFlowAsset flow;
    flow.name = safe;
    auto& first = flow.AddTransition();
    first.event_name = "Next";

    std::string save_error;
    if (!SceneFlowAsset::Save(flow, path, save_error))
    {
        project_browser_status = "Scene Flow 作成失敗: " + save_error;
        return false;
    }

    const ReplayEngine::Assets::AssetRecord& record =
        asset_database.Register(path, AssetKind::SceneFlow);
    if (!asset_database.Save(save_error))
    {
        project_browser_status = "Scene Flow は作成しましたが DB 保存失敗: " + save_error;
        return false;
    }
    selected_asset_guid = record.guid;
    load_scene_flow_editor(record);
    project_browser_status = "Scene Flow を作成しました: " + path.filename().u8string();
    push_editor_log("Info", project_browser_status, path, 1);
    return true;
}


bool framework::project_create_surface_shader(const std::string& name)
{
    using ReplayEngine::Assets::AssetKind;
    using ReplayEngine::Rendering::ShaderAssetFactory;
    using ReplayEngine::Rendering::ShaderID;

    const std::string safe = SafeProjectFileName(
        name.empty() ? std::string("NewShader") : name);

    std::error_code error;
    const std::filesystem::path root = std::filesystem::current_path(error);
    if (error)
    {
        project_browser_status = "Project root を取得できません";
        return false;
    }

    // ShaderLibrary は Shader/Materials/** だけを surface shader として走査する。
    // Project Browser のどこで Create を押しても使える Shader が作られるよう、
    // Shader/Materials 外なら Shader/Materials/Project へ自動的に置く。
    std::filesystem::path folder = root / project_current_folder;
    std::filesystem::path relative = std::filesystem::relative(folder, root, error);
    if (error) relative.clear();
    const std::string relative_lower = ToLowerCopy(relative.generic_u8string());
    if (relative_lower.rfind("shader/materials", 0) != 0)
        folder = root / "Shader" / "Materials" / "Project";

    std::filesystem::create_directories(folder, error);
    if (error)
    {
        project_browser_status = "Shader folder を作成できません";
        return false;
    }

    std::filesystem::path path = folder / (safe + ".hlsl");
    for (int suffix = 2; std::filesystem::exists(path) && suffix < 10000; ++suffix)
        path = folder / (safe + std::to_string(suffix) + ".hlsl");

    // Picker のカテゴリもフォルダ構造から自動で作る。
    // Shader/Materials/Characters/Skin.hlsl -> Project/Characters/Skin
    // Editor 側に custom shader 名を hard-code しない。
    std::string picker_category = "Project";
    const std::filesystem::path materials_root = root / "Shader" / "Materials";
    std::filesystem::path shader_subfolder =
        std::filesystem::relative(folder, materials_root, error);
    if (!error && !shader_subfolder.empty() && shader_subfolder != ".")
    {
        const std::string sub = shader_subfolder.generic_u8string();
        // 既定の Shader/Materials/Project は "Project/Project" にしない。
        // Project/Characters のような配下だけ、そのまま分類へ反映する。
        if (sub == "Project") picker_category = "Project";
        else if (sub.rfind("Project/", 0) == 0) picker_category = sub;
        else picker_category = "Project/" + sub;
    }
    error.clear();

    ShaderID shader_id;
    std::string shader_error;
    if (!ShaderAssetFactory::CreateSurfaceShader(
        path, safe, picker_category, shader_id, shader_error))
    {
        project_browser_status = "Shader 作成失敗: " + shader_error;
        return false;
    }

    const ReplayEngine::Assets::AssetRecord& record =
        asset_database.Register(path, AssetKind::Shader);
    if (!asset_database.Save(shader_error))
    {
        project_browser_status = "Shader は作成しましたが DB 保存失敗: " + shader_error;
        return false;
    }

    // 作った瞬間に Picker へ出す。次回起動待ちにしない。
    const auto report = shader_library.ScanAll(root);
    selected_asset_guid = record.guid;
    set_project_folder(path.parent_path());

    project_browser_status = "Surface Shader を作成しました: " +
        path.filename().u8string() + " / ShaderGUID=" + shader_id.ToString();
    if (report.compile_failed != 0)
        project_browser_status += " (compile error は Shader Catalog で確認)";
    push_editor_log("Info", project_browser_status, path, 1);
    return true;
}


bool framework::project_create_layer_shader(const std::string& name)
{
    using ReplayEngine::Assets::AssetKind;
    using ReplayEngine::Rendering::ShaderAssetFactory;
    using ReplayEngine::Rendering::ShaderID;

    const std::string safe = SafeProjectFileName(
        name.empty() ? std::string("NewLayer") : name);

    std::error_code error;
    const std::filesystem::path root = std::filesystem::current_path(error);
    if (error)
    {
        project_browser_status = "Project root を取得できません";
        return false;
    }

    std::filesystem::path folder = root / project_current_folder;
    std::filesystem::path relative = std::filesystem::relative(folder, root, error);
    if (error) relative.clear();
    const std::string relative_lower = ToLowerCopy(relative.generic_u8string());
    if (relative_lower.rfind("shader/layers", 0) != 0)
        folder = root / "Shader" / "Layers" / "Project";

    std::filesystem::create_directories(folder, error);
    if (error)
    {
        project_browser_status = "Layer Shader folder を作成できません";
        return false;
    }

    std::filesystem::path path = folder / (safe + ".hlsl");
    for (int suffix = 2; std::filesystem::exists(path) && suffix < 10000; ++suffix)
        path = folder / (safe + std::to_string(suffix) + ".hlsl");

    std::string picker_category = "Project";
    const std::filesystem::path layers_root = root / "Shader" / "Layers";
    std::filesystem::path shader_subfolder =
        std::filesystem::relative(folder, layers_root, error);
    if (!error && !shader_subfolder.empty() && shader_subfolder != ".")
    {
        const std::string sub = shader_subfolder.generic_u8string();
        if (sub == "Project") picker_category = "Project";
        else if (sub.rfind("Project/", 0) == 0) picker_category = sub;
        else picker_category = "Project/" + sub;
    }
    error.clear();

    ShaderID shader_id;
    std::string shader_error;
    if (!ShaderAssetFactory::CreateLayerShader(
        path, safe, picker_category, shader_id, shader_error))
    {
        project_browser_status = "Layer Shader 作成失敗: " + shader_error;
        return false;
    }

    const ReplayEngine::Assets::AssetRecord& record =
        asset_database.Register(path, AssetKind::Shader);
    if (!asset_database.Save(shader_error))
    {
        project_browser_status = "Layer Shader は作成しましたが DB 保存失敗: " + shader_error;
        return false;
    }

    const auto report = shader_library.ScanAll(root);
    selected_asset_guid = record.guid;
    set_project_folder(path.parent_path());
    project_browser_status = "Layer Shader を作成しました: " +
        path.filename().u8string() + " / ShaderGUID=" + shader_id.ToString();
    if (report.compile_failed != 0)
        project_browser_status += " (compile error は Shader Catalog で確認)";
    push_editor_log("Info", project_browser_status, path, 1);
    return true;
}


bool framework::project_create_shader_composer(const std::string& name,
    ReplayEngine::Rendering::ShaderDomain domain)
{
    using ReplayEngine::Assets::AssetKind;
    using ReplayEngine::Rendering::ShaderComposerAsset;
    using ReplayEngine::Rendering::ShaderComposerGenerator;
    using ReplayEngine::Rendering::ShaderDomain;

    if (domain != ShaderDomain::Surface && domain != ShaderDomain::Layer)
    {
        project_browser_status = "Shader Composer v1 は Surface / Layer のみ対応です";
        return false;
    }

    const std::string safe = SafeProjectFileName(
        name.empty() ? (domain == ShaderDomain::Layer ? std::string("NewLayerGraph")
                                                   : std::string("NewShaderGraph")) : name);
    std::error_code ec;
    const std::filesystem::path root = std::filesystem::current_path(ec);
    if (ec)
    {
        project_browser_status = "Project root を取得できません";
        return false;
    }

    // Graph Asset itself is created in the current Project Browser folder.
    // Generated HLSL is kept in Shader/Materials|Layers/Generated so ShaderLibrary
    // can discover it without a special renderer path.
    std::filesystem::path graph_folder = root / project_current_folder;
    std::filesystem::create_directories(graph_folder, ec);
    if (ec)
    {
        project_browser_status = "Shader Composer folder を作成できません";
        return false;
    }

    std::filesystem::path graph_path = graph_folder /
        (safe + ShaderComposerAsset::file_extension);
    int suffix = 2;
    while (std::filesystem::exists(graph_path) && suffix < 10000)
        graph_path = graph_folder / (safe + std::to_string(suffix++) + ShaderComposerAsset::file_extension);

    const std::string generated_stem = graph_path.stem().u8string();
    ShaderComposerAsset graph = ShaderComposerAsset::CreateDefault(
        domain, generated_stem, {});
    const std::string id_text = graph.shader_id.ToString();
    const std::string short_id = id_text.size() >= 8 ? id_text.substr(0, 8) : id_text;
    const std::filesystem::path generated_relative =
        (domain == ShaderDomain::Layer
            ? std::filesystem::path("Shader") / "Layers" / "Generated"
            : std::filesystem::path("Shader") / "Materials" / "Generated") /
        (generated_stem + "_" + short_id + ".hlsl");
    graph.generated_hlsl = generated_relative;
    std::string error;
    if (!ShaderComposerAsset::Save(graph, graph_path, error) ||
        !ShaderComposerGenerator::GenerateToFile(graph, root, error))
    {
        project_browser_status = "Shader Composer 作成失敗: " + error;
        return false;
    }

    const auto& graph_record = asset_database.Register(graph_path, AssetKind::Shader);
    asset_database.Register(root / generated_relative, AssetKind::Shader);
    if (!asset_database.Save(error))
    {
        project_browser_status = "Shader Composer は作成しましたが DB 保存失敗: " + error;
        return false;
    }

    const auto report = shader_library.ScanAll(root);
    selected_asset_guid = graph_record.guid;
    set_project_folder(graph_path.parent_path());
    if (!shader_composer_editor.Open(graph_path, error))
    {
        project_browser_status = "Graph は作成しましたが Editor で開けません: " + error;
        return false;
    }

    project_browser_status = "Shader Composer を作成しました: " + graph_path.filename().u8string();
    if (report.compile_failed != 0)
        project_browser_status += " (compile error は Shader Catalog で確認)";
    push_editor_log("Info", project_browser_status, graph_path, 1);
    return true;
}

// -----------------------------------------------------------------------------
//  改名
//
//  .cs の改名はファイル名を変えるだけで、クラス名は変えない。
//  クラス名まで書き換えると Type GUID との対応が壊れるので触らない。
// -----------------------------------------------------------------------------
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
void framework::draw_project_folder_tree(const std::filesystem::path& folder, int depth)
{
    if (depth > 12) return;

    std::error_code error;
    const std::filesystem::path root = std::filesystem::current_path(error);
    if (error) return;

    const std::vector<ProjectEntry> children = ListProjectFolder(folder, true);
    const std::filesystem::path current = root / project_current_folder;

    for (const ProjectEntry& child : children)
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_SpanAvailWidth;

        const std::vector<ProjectEntry> grandchildren =
            ListProjectFolder(child.path, true);
        if (grandchildren.empty()) flags |= ImGuiTreeNodeFlags_Leaf;

        std::error_code compare_error;
        if (std::filesystem::equivalent(child.path, current, compare_error) &&
            !compare_error)
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        ImGui::PushID(child.path.generic_u8string().c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, KindColor(
            ReplayEngine::Assets::AssetKind::Unknown, true));
        const bool opened = ImGui::TreeNodeEx(child.name.c_str(), flags);
        ImGui::PopStyleColor();

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            set_project_folder(child.path);
        }
        if (opened)
        {
            draw_project_folder_tree(child.path, depth + 1);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

// -----------------------------------------------------------------------------
//  右ペイン: フォルダの中身
// -----------------------------------------------------------------------------
void framework::draw_project_folder_contents()
{
    using ReplayEngine::Assets::AssetKind;

    std::error_code error;
    const std::filesystem::path root = std::filesystem::current_path(error);
    if (error) return;

    const std::filesystem::path current = root / project_current_folder;
    const std::string query = ToLowerCopy(asset_search_text);
    const std::vector<ProjectEntry> entries = query.empty()
        ? ListProjectFolder(current, false)
        : SearchProjectFiles(root, query);

    const float cell = project_thumbnail_size + 22.0f;
    const float available = ImGui::GetContentRegionAvail().x;
    int columns = project_grid_view ? static_cast<int>(available / (cell + 8.0f)) : 1;
    if (columns < 1) columns = 1;

    int drawn = 0;
    bool registered_any = false;
    for (const ProjectEntry& entry : entries)
    {
        if (!query.empty() &&
            ToLowerCopy(entry.name).find(query) == std::string::npos)
        {
            continue;
        }

        const AssetKind kind = entry.is_directory
            ? AssetKind::Unknown : project_kind_for(entry.path);
        if (!entry.is_directory && asset_type_filter != 0)
        {
            int filter_type = 10;
            if (kind == AssetKind::Model) filter_type = 1;
            else if (ToLowerCopy(entry.path.extension().u8string()) == ".replayprefab")
                filter_type = 2;
            else if (ToLowerCopy(entry.path.extension().u8string()) == ".replayscene")
                filter_type = 3;
            else if (kind == AssetKind::Material) filter_type = 4;
            else if (kind == AssetKind::Script) filter_type = 5;
            else if (kind == AssetKind::Shader) filter_type = 6;
            else if (kind == AssetKind::SceneFlow) filter_type = 7;
            else if (kind == AssetKind::Motion) filter_type = 8;
            else if (kind == AssetKind::Font) filter_type = 9;
            if (asset_type_filter != filter_type) continue;
        }

        // 未登録のファイルはここで AssetDatabase へ登録する。
        // 登録されていないと GUID が無く、ドラッグもシーン配置もできない。
        // Register は正規化パスから GUID を導くので、既にあれば
        // 既存レコードがそのまま返る。Save は新規が出た時だけ最後に 1 回。
        const ReplayEngine::Assets::AssetRecord* record =
            asset_database.FindByPath(entry.path);
        if (record == nullptr && !entry.is_directory && kind != AssetKind::Unknown)
        {
            record = &asset_database.Register(entry.path, kind);
            registered_any = true;
        }
        const bool selected = record != nullptr &&
            !selected_asset_guid.empty() && record->guid == selected_asset_guid;

        if (project_grid_view && drawn % columns != 0) ImGui::SameLine();
        ++drawn;

        ImGui::PushID(entry.path.generic_u8string().c_str());
        ImGui::BeginGroup();

        const bool renaming = !project_rename_target.empty() &&
            project_rename_target == entry.path;

        ID3D11ShaderResourceView* thumbnail =
            entry.is_directory ? nullptr : project_thumbnail_for(entry.path);

        const ImVec2 icon_size(project_thumbnail_size, project_thumbnail_size);
        if (thumbnail != nullptr)
        {
            if (ImGui::ImageButton(reinterpret_cast<ImTextureID>(thumbnail),
                icon_size, ImVec2(0, 0), ImVec2(1, 1), 2))
            {
                if (record != nullptr) selected_asset_guid = record->guid;
            }
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Text, KindColor(kind, entry.is_directory));
            if (ImGui::Button(KindBadge(kind, entry.is_directory), icon_size))
            {
                if (record != nullptr) selected_asset_guid = record->guid;
            }
            ImGui::PopStyleColor();
        }

        const bool icon_hovered = ImGui::IsItemHovered();
        const bool icon_double_clicked = icon_hovered &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

        // .cs も含めて全種別をドラッグできるようにする。
        // 受け側 (Scene ビュー / Inspector) が種別で判断する。
        //
        // 直前のアイコンボタンが「掴まれている」時だけドラッグ元にする。
        // ImGui の BeginDragDropSource は LastItemId が 0 のまま呼ぶと
        // IM_ASSERT(0) で落ちる。子ウィンドウがクリップされて SkipItems が
        // 立つと Button() が ItemAdd を呼ばず LastItemId が 0 のまま残るので、
        // ここを無条件で呼ぶとクリックした瞬間に落ちていた。
        // SourceAllowNullID も付けて二重に防ぐ。
        if (record != nullptr && !entry.is_directory && ImGui::IsItemActive() &&
            ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            ImGui::SetDragDropPayload("REPLAY_ASSET_GUID", record->guid.c_str(),
                record->guid.size() + 1);
            ImGui::TextUnformatted(entry.name.c_str());
            ImGui::EndDragDropSource();
        }

        // 項目ごとの右クリックメニュー。
        // EndGroup 後だと LastItemId が保証されないので、
        // アイコンボタン直後に置く。
        if (ImGui::BeginPopupContextItem("##ProjectItemMenu"))
        {
            if (ImGui::MenuItem("名前を変更"))
            {
                project_rename_target = entry.path;
                project_rename_focus_pending = true;
                const std::string stem = entry.is_directory
                    ? entry.name : entry.path.stem().u8string();
                strncpy_s(project_rename_buffer, sizeof(project_rename_buffer),
                    stem.c_str(), _TRUNCATE);
            }
            if (!entry.is_directory && kind == AssetKind::Script &&
                ImGui::MenuItem("Visual Studio で開く"))
            {
                if (record != nullptr) selected_asset_guid = record->guid;
                open_selected_csharp_asset();
            }
            if (!entry.is_directory && kind == AssetKind::SceneFlow &&
                ImGui::MenuItem("Scene Flow で開く"))
            {
                if (record != nullptr)
                {
                    selected_asset_guid = record->guid;
                    load_scene_flow_editor(*record);
                }
            }
            if (!entry.is_directory && kind == AssetKind::Motion &&
                ImGui::MenuItem("Motion Workspace で開く"))
            {
                if (record != nullptr)
                {
                    selected_asset_guid = record->guid;
                    open_motion_asset(*record);
                }
            }
            if (!entry.is_directory && kind == AssetKind::Shader)
            {
                const bool is_composer = ToLowerCopy(entry.path.extension().u8string()) ==
                    ReplayEngine::Rendering::ShaderComposerAsset::file_extension;
                if (is_composer)
                {
                    if (ImGui::MenuItem("Shader Composer で開く"))
                    {
                        std::string open_error;
                        if (!shader_composer_editor.Open(entry.path, open_error))
                            push_editor_log("Warning", open_error, entry.path);
                    }
                }
                else if (ImGui::MenuItem("Visual Studio で開く"))
                {
                    std::string open_error;
                    if (!ReplayEngine::Scripting::CSharp::CSharpProject::OpenVisualStudio(
                        entry.path, 1, open_error))
                    {
                        push_editor_log("Warning", open_error, entry.path);
                    }
                }
            }
            if (entry.is_directory && ImGui::MenuItem("このフォルダを開く"))
            {
                set_project_folder(entry.path);
            }
            ImGui::Separator();
            ImGui::TextDisabled("%s", entry.name.c_str());
            ImGui::EndPopup();
        }

        if (icon_double_clicked)
        {
            if (entry.is_directory)
            {
                set_project_folder(entry.path);
            }
            else if (kind == AssetKind::Script)
            {
                if (record != nullptr) selected_asset_guid = record->guid;
                open_selected_csharp_asset();
            }
            else if (kind == AssetKind::SceneFlow)
            {
                if (record != nullptr)
                {
                    selected_asset_guid = record->guid;
                    load_scene_flow_editor(*record);
                }
            }
            else if (kind == AssetKind::Motion)
            {
                if (record != nullptr)
                {
                    selected_asset_guid = record->guid;
                    open_motion_asset(*record);
                }
            }
            else if (kind == AssetKind::Shader)
            {
                if (record != nullptr) selected_asset_guid = record->guid;
                std::string open_error;
                if (ToLowerCopy(entry.path.extension().u8string()) ==
                    ReplayEngine::Rendering::ShaderComposerAsset::file_extension)
                {
                    if (!shader_composer_editor.Open(entry.path, open_error))
                        push_editor_log("Warning", open_error, entry.path);
                }
                else if (!ReplayEngine::Scripting::CSharp::CSharpProject::OpenVisualStudio(
                    entry.path, 1, open_error))
                {
                    push_editor_log("Warning", open_error, entry.path);
                }
            }
            else if (record != nullptr)
            {
                // Double click は「開く/選択」であり Scene を変更しない。
                // 配置/割当は Scene View D&D または明示的な配置ボタンだけ。
                selected_asset_guid = record->guid;
                project_browser_status = "Asset を選択しました: " + record->display_name +
                    "（Sceneへ配置するにはドラッグ&ドロップ）";
            }
        }

        if (icon_hovered)
        {
            ImGui::SetTooltip("%s", entry.path.generic_u8string().c_str());
        }

        // 名前。改名中はインライン入力に差し替える。
        ImGui::PushItemWidth(project_thumbnail_size);
        if (renaming)
        {
            if (project_rename_focus_pending)
            {
                ImGui::SetKeyboardFocusHere();
                project_rename_focus_pending = false;
            }
            const bool committed = ImGui::InputText("##ProjectRename",
                project_rename_buffer, IM_ARRAYSIZE(project_rename_buffer),
                ImGuiInputTextFlags_EnterReturnsTrue |
                ImGuiInputTextFlags_AutoSelectAll);
            if (committed)
            {
                project_rename_entry(entry.path, project_rename_buffer);
                project_rename_target.clear();
            }
            else if (ImGui::IsKeyPressed(VK_ESCAPE))
            {
                project_rename_target.clear();
            }
        }
        else
        {
            std::string label = entry.name;
            if (label.size() > 16) label = label.substr(0, 15) + "..";
            if (selected)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "%s", label.c_str());
            }
            else
            {
                ImGui::TextUnformatted(label.c_str());
            }
        }
        ImGui::PopItemWidth();

        ImGui::EndGroup();
        ImGui::PopID();
    }

    if (registered_any)
    {
        std::string save_error;
        if (!asset_database.Save(save_error))
        {
            push_editor_log("Warning", "AssetDatabase 保存失敗: " + save_error);
        }
    }

    if (drawn == 0)
    {
        ImGui::TextDisabled("このフォルダには表示できる項目がありません");
    }
}

// -----------------------------------------------------------------------------
//  Project ブラウザ本体
// -----------------------------------------------------------------------------
void framework::draw_project_browser()
{
    std::error_code error;
    const std::filesystem::path root = std::filesystem::current_path(error);
    if (error)
    {
        ImGui::TextDisabled("プロジェクトフォルダを取得できません");
        return;
    }

    // --- パンくず ---
    if (ImGui::SmallButton("Project"))
    {
        project_current_folder.clear();
    }
    std::filesystem::path walked;
    for (const std::filesystem::path& part : project_current_folder)
    {
        walked /= part;
        ImGui::SameLine();
        ImGui::TextDisabled("/");
        ImGui::SameLine();
        ImGui::PushID(walked.generic_u8string().c_str());
        if (ImGui::SmallButton(part.u8string().c_str()))
        {
            set_project_folder(root / walked);
        }
        ImGui::PopID();
    }

    // --- 検索とフィルタ ---
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##ProjectSearch", "Search Project...",
        asset_search_text, IM_ARRAYSIZE(asset_search_text));
    if (asset_search_text[0] != '\0' && ImGui::IsItemHovered())
        ImGui::SetTooltip("Project 全体を再帰検索します");
    ImGui::SameLine();
    const char* filters[] =
        { "All", "Model", "Prefab", "Scene", "Material", "Script", "Shader", "Flow", "Motion", "Font", "Other" };
    ImGui::SetNextItemWidth(120.0f);
    ImGui::Combo("##ProjectFilter", &asset_type_filter, filters, IM_ARRAYSIZE(filters));
    ImGui::SameLine();
    ImGui::Checkbox("グリッド", &project_grid_view);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("##ProjectThumbSize", &project_thumbnail_size,
        48.0f, 160.0f, "%.0f px");

    ImGui::Separator();

    // --- 左: フォルダツリー ---
    //
    // BeginChild の戻り値は必ず見ること。false のときは SkipItems が立ち、
    // 中のウィジェットが ItemAdd を呼ばないため LastItemId が古いまま残る。
    // EndChild は戻り値に関わらず必ず呼ぶ。
    if (ImGui::BeginChild("##ProjectTree", ImVec2(project_tree_width, 320.0f), true))
    {
        ImGuiTreeNodeFlags root_flags = ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
        if (project_current_folder.empty()) root_flags |= ImGuiTreeNodeFlags_Selected;

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.80f, 0.36f, 1.0f));
        const bool root_open = ImGui::TreeNodeEx("Project", root_flags);
        ImGui::PopStyleColor();
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            project_current_folder.clear();
        }
        if (root_open)
        {
            draw_project_folder_tree(root, 0);
            ImGui::TreePop();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // --- 右: フォルダの中身 ---
    if (ImGui::BeginChild("##ProjectContents", ImVec2(0.0f, 320.0f), true))
    {
        draw_project_folder_contents();

        // 空白部分の右クリック = このフォルダに作る。
        // Unity と同じで「今いる場所に作られる」。
        if (ImGui::BeginPopupContextWindow("##ProjectCreateMenu", 1))
        {
            ImGui::TextDisabled("%s に作成",
                project_current_folder.empty()
                    ? "Project" : project_current_folder.generic_u8string().c_str());
            ImGui::Separator();
            ImGui::SetNextItemWidth(200.0f);
            ImGui::InputTextWithHint("##ProjectNewName", "名前",
                project_new_item_name, IM_ARRAYSIZE(project_new_item_name));
            if (ImGui::MenuItem("C# Script"))
            {
                project_create_csharp_behaviour(project_new_item_name);
            }
            if (ImGui::MenuItem("Material"))
            {
                project_create_material(project_new_item_name);
            }
            if (ImGui::MenuItem("Scene Flow"))
            {
                project_create_scene_flow(project_new_item_name);
            }
            if (ImGui::MenuItem("Motion Asset"))
            {
                project_create_motion(project_new_item_name);
            }
            if (ImGui::MenuItem("Surface Shader"))
            {
                project_create_surface_shader(project_new_item_name);
            }
            if (ImGui::MenuItem("Layer Shader"))
            {
                project_create_layer_shader(project_new_item_name);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Shader Composer (Surface)"))
            {
                project_create_shader_composer(project_new_item_name,
                    ReplayEngine::Rendering::ShaderDomain::Surface);
            }
            if (ImGui::MenuItem("Shader Composer (Layer)"))
            {
                project_create_shader_composer(project_new_item_name,
                    ReplayEngine::Rendering::ShaderDomain::Layer);
            }
            if (ImGui::MenuItem("Folder"))
            {
                project_create_folder(project_new_item_name);
            }
            ImGui::Separator();
            ImGui::TextDisabled("Namespace: %s", new_csharp_namespace);
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();

    if (!project_browser_status.empty())
    {
        ImGui::TextWrapped("%s", project_browser_status.c_str());
    }
}
