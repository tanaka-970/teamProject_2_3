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

// シェーダー／Shader Composer 作成の関数本体

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

    std::filesystem::path path = UniqueProjectPath(folder, safe, ".hlsl");

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
