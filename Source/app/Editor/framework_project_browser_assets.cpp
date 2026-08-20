#include "framework.h"
#include "texture.h"
#include "../../RePlayEngine/Assets/AssetCache.h"
#include "../../RePlayEngine/Assets/SpriteAtlasAsset.h"
#include "../../RePlayEngine/Localization/LocalizationTable.h"
#include "../../RePlayEngine/Rendering/Effects/EffectPresetAsset.h"
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

// 種別判定・基本操作・通常アセット作成の関数本体

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
    if (extension == ReplayEngine::Motion::MotionAsset::file_extension)
        return AssetKind::Motion;
    if (extension == ReplayEngine::Motion::CompositionAsset::file_extension)
        return AssetKind::Composition;
    if (extension == ReplayEngine::Localization::LocalizationTable::file_extension)
        return AssetKind::Localization;
    if (extension == ReplayEngine::Rendering::Effects::EffectPresetAsset::file_extension)
        return AssetKind::EffectPreset;
    if (extension == GameInput::InputState::action_asset_extension)
        return AssetKind::InputAction;
    if (extension == ReplayEngine::Assets::SpriteAtlasAsset::file_extension)
        return AssetKind::SpriteAtlas;
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

    std::filesystem::path path = UniqueProjectPath(root / project_current_folder, safe, {}, &asset_database);

    std::filesystem::create_directories(path, error);
    if (error)
    {
        project_browser_status = "フォルダ作成失敗: " + path.generic_u8string();
        return false;
    }
    project_selected_entry_path = path;
    selected_editor_object = editor_selection::asset;
    selected_asset_guid.clear();
    project_tree_reveal_selection_pending = true;
    project_record_created_path(path, "フォルダを作成");
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
    project_selected_entry_path = info.source_path;
    selected_editor_object = editor_selection::asset;
    project_tree_reveal_selection_pending = true;

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
    project_record_created_path(info.source_path, "C# Behaviour を作成");
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
    std::filesystem::path path = UniqueProjectPath(folder, safe, MaterialAsset::file_extension, &asset_database);

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
    project_selected_entry_path = path;
    selected_editor_object = editor_selection::asset;
    project_tree_reveal_selection_pending = true;
    project_record_created_path(path, "Material を作成");
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

    std::filesystem::path path = UniqueProjectPath(folder, safe, MotionAsset::file_extension, &asset_database);

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
    project_selected_entry_path = path;
    selected_editor_object = editor_selection::asset;
    project_tree_reveal_selection_pending = true;
    set_project_folder(path.parent_path());
    if (!open_motion_asset(record))
    {
        project_browser_status = "Motion は作成しましたが Editor で開けません: " +
            path.filename().u8string();
        return false;
    }

    project_record_created_path(path, "Motion を作成");
    project_browser_status = "Motion を作成しました: " + path.filename().u8string();
    push_editor_log("Info", project_browser_status, path, 1);
    return true;
}


bool framework::project_create_composition(const std::string& name)
{
    using ReplayEngine::Assets::AssetKind;
    using ReplayEngine::Motion::CompositionAsset;

    const std::string safe = SafeProjectFileName(
        name.empty() ? std::string("NewComposition") : name);
    if (safe.empty())
    {
        project_browser_status = "Composition 名が空です";
        return false;
    }

    std::error_code error;
    const std::filesystem::path root = std::filesystem::current_path(error);
    if (error) return false;
    const std::filesystem::path folder = root / project_current_folder;
    std::filesystem::create_directories(folder, error);
    if (error)
    {
        project_browser_status = "Composition folder を作成できません";
        return false;
    }

    const std::filesystem::path path = UniqueProjectPath(
        folder, safe, CompositionAsset::file_extension, &asset_database);
    CompositionAsset composition;
    composition.name = safe;
    composition.duration = 1.0f;

    std::string save_error;
    if (!CompositionAsset::SaveToFile(path, composition, save_error))
    {
        project_browser_status = "Composition 作成失敗: " + save_error;
        return false;
    }
    const auto& record = asset_database.Register(path, AssetKind::Composition);
    if (!asset_database.Save(save_error))
    {
        project_browser_status = "Composition は作成しましたが DB 保存失敗: " + save_error;
        return false;
    }
    selected_asset_guid = record.guid;
    project_selected_entry_path = path;
    selected_editor_object = editor_selection::asset;
    project_tree_reveal_selection_pending = true;
    set_project_folder(path.parent_path());
    if (!open_motion_asset(record))
    {
        project_browser_status = "Composition は作成しましたが Editor で開けません: " +
            path.filename().u8string();
        return false;
    }
    project_record_created_path(path, "Composition を作成");
    project_browser_status = "Composition を作成しました: " + path.filename().u8string();
    push_editor_log("Info", project_browser_status, path, 1);
    return true;
}



bool framework::project_create_sprite_atlas(const std::string& name)
{
    using ReplayEngine::Assets::AssetKind;
    using ReplayEngine::Assets::SpriteAtlasAsset;
    const std::string safe = SafeProjectFileName(name.empty() ? "NewSpriteAtlas" : name);
    if (safe.empty()) return false;
    std::error_code error;
    const std::filesystem::path root = std::filesystem::current_path(error);
    if (error) return false;
    const std::filesystem::path folder = root / project_current_folder;
    std::filesystem::create_directories(folder, error);
    if (error) return false;
    const std::filesystem::path path = UniqueProjectPath(folder, safe,
        SpriteAtlasAsset::file_extension, &asset_database);
    SpriteAtlasAsset atlas;
    atlas.name = safe;
    std::string save_error;
    if (!SpriteAtlasAsset::SaveToFile(path, atlas, save_error))
    {
        project_browser_status = "Sprite Atlas 作成失敗: " + save_error;
        return false;
    }
    const auto& record = asset_database.Register(path, AssetKind::SpriteAtlas);
    if (!asset_database.Save(save_error))
    {
        project_browser_status = "Sprite Atlas は作成しましたが DB 保存失敗: " + save_error;
        return false;
    }
    selected_asset_guid = record.guid;
    project_selected_entry_path = path;
    selected_editor_object = editor_selection::asset;
    project_tree_reveal_selection_pending = true;
    set_project_folder(path.parent_path());
    open_sprite_atlas_asset(record);
    project_record_created_path(path, "Sprite Atlas を作成");
    project_browser_status = "Sprite Atlas を作成しました: " + path.filename().u8string();
    return true;
}

bool framework::open_sprite_atlas_asset(const ReplayEngine::Assets::AssetRecord& asset)
{
    if (asset.kind != ReplayEngine::Assets::AssetKind::SpriteAtlas) return false;
    ReplayEngine::Assets::SpriteAtlasAsset atlas;
    std::string error;
    if (!ReplayEngine::Assets::SpriteAtlasAsset::LoadFromFile(asset.source_path, atlas, error))
    {
        sprite_atlas_editor_status = error;
        return false;
    }
    sprite_atlas_editor_asset = std::move(atlas);
    sprite_atlas_editor_path = asset.source_path;
    sprite_atlas_editor_guid = asset.guid;
    sprite_atlas_editor_loaded = true;
    sprite_atlas_editor_dirty = false;
    sprite_atlas_selected_region = sprite_atlas_editor_asset.regions.empty() ? -1 : 0;
    sprite_atlas_draw_region_mode = false;
    sprite_atlas_region_dragging = false;
    sprite_atlas_region_transform_dragging = false;
    sprite_atlas_active_handle = -1;
    sprite_atlas_history.clear();
    sprite_atlas_history_cursor = 0;
    sprite_atlas_history_transaction = false;
    sprite_atlas_history_label.clear();
    show_sprite_atlas_editor_panel = true;
    sprite_atlas_editor_status = "Sprite Atlasを開きました: " + asset.display_name;
    return true;
}

bool framework::save_current_sprite_atlas()
{
    if (!sprite_atlas_editor_loaded || sprite_atlas_editor_path.empty()) return false;

    // Atlas はScene外Assetなので、保存前bytesを既存FileEditHistoryへ積む。
    // 新しいUndo基盤は作らない。
    std::vector<std::uint8_t> undo_before;
    {
        std::ifstream before_stream(sprite_atlas_editor_path, std::ios::binary);
        if (before_stream)
        {
            undo_before.assign(std::istreambuf_iterator<char>(before_stream),
                std::istreambuf_iterator<char>());
        }
    }

    std::string error;
    if (!ReplayEngine::Assets::SpriteAtlasAsset::SaveToFile(sprite_atlas_editor_path,
        sprite_atlas_editor_asset, error))
    {
        sprite_atlas_editor_status = error;
        return false;
    }

    if (!undo_before.empty())
    {
        std::string undo_error;
        if (!external_file_history.RecordSavedChange(sprite_atlas_editor_path,
            "Sprite Atlasを保存", undo_before, undo_error) && !undo_error.empty())
        {
            sprite_atlas_editor_status = "Atlasは保存しましたがUndo記録失敗: " + undo_error;
        }
    }

    const auto& record = asset_database.Register(sprite_atlas_editor_path,
        ReplayEngine::Assets::AssetKind::SpriteAtlas);
    sprite_atlas_editor_guid = record.guid;
    std::string db_error;
    if (!asset_database.Save(db_error))
    {
        sprite_atlas_editor_status = "Atlasは保存しましたがDB保存失敗: " + db_error;
        return false;
    }
    sprite_atlas_editor_dirty = false;
    sprite_atlas_editor_status = "保存しました: " +
        sprite_atlas_editor_path.filename().u8string();
    return true;
}

bool framework::project_create_localization(const std::string& name)
{
    using ReplayEngine::Assets::AssetKind;
    using ReplayEngine::Localization::LocalizationTable;
    const std::string safe = SafeProjectFileName(name.empty() ? "Localization" : name);
    if (safe.empty()) return false;
    std::error_code error;
    const std::filesystem::path root = std::filesystem::current_path(error);
    if (error) return false;
    const std::filesystem::path folder = root / project_current_folder;
    std::filesystem::create_directories(folder, error);
    if (error) return false;
    std::filesystem::path path = UniqueProjectPath(folder, safe, LocalizationTable::file_extension, &asset_database);
    LocalizationTable table;
    table.SetLanguages({ "ja", "en" });
    table.Set("sample.hello", "ja", u8"こんにちは");
    table.Set("sample.hello", "en", "Hello");
    std::string save_error;
    if (!table.SaveToFile(path, save_error))
    {
        project_browser_status = "Localization 作成失敗: " + save_error;
        return false;
    }
    const auto& record = asset_database.Register(path, AssetKind::Localization);
    if (!asset_database.Save(save_error))
    {
        project_browser_status = "Localization は作成しましたが DB 保存失敗: " + save_error;
        return false;
    }
    selected_asset_guid = record.guid;
    project_selected_entry_path = path;
    selected_editor_object = editor_selection::asset;
    project_tree_reveal_selection_pending = true;
    project_record_created_path(path, "Localization を作成");
    project_browser_status = "Localization を作成しました: " + path.filename().u8string();
    return true;
}

bool framework::project_create_effect_preset(const std::string& name)
{
    using ReplayEngine::Assets::AssetKind;
    using ReplayEngine::Rendering::Effects::EffectPresetAsset;
    const std::string safe = SafeProjectFileName(name.empty() ? "NewEffectPreset" : name);
    if (safe.empty()) return false;
    std::error_code error;
    const std::filesystem::path root = std::filesystem::current_path(error);
    if (error) return false;
    const std::filesystem::path folder = root / project_current_folder;
    std::filesystem::create_directories(folder, error);
    if (error) return false;
    std::filesystem::path path = UniqueProjectPath(folder, safe, EffectPresetAsset::file_extension, &asset_database);
    EffectPresetAsset preset;
    ReplayEngine::UI::UIEffect glow;
    glow.kind = static_cast<int>(ReplayEngine::UI::UIEffectKind::Glow);
    glow.enabled = true;
    glow.radius = 8.0f;
    glow.intensity = 1.0f;
    preset.effects.push_back(glow);
    std::string save_error;
    if (!preset.SaveToFile(path, save_error))
    {
        project_browser_status = "Effect Preset 作成失敗: " + save_error;
        return false;
    }
    const auto& record = asset_database.Register(path, AssetKind::EffectPreset);
    if (!asset_database.Save(save_error))
    {
        project_browser_status = "Effect Preset は作成しましたが DB 保存失敗: " + save_error;
        return false;
    }
    selected_asset_guid = record.guid;
    project_selected_entry_path = path;
    selected_editor_object = editor_selection::asset;
    project_tree_reveal_selection_pending = true;
    project_record_created_path(path, "Effect Preset を作成");
    project_browser_status = "Effect Preset を作成しました: " + path.filename().u8string();
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
    std::filesystem::path path = UniqueProjectPath(folder, safe, SceneFlowAsset::file_extension, &asset_database);

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
    project_selected_entry_path = path;
    selected_editor_object = editor_selection::asset;
    project_tree_reveal_selection_pending = true;
    load_scene_flow_editor(record);
    project_record_created_path(path, "Scene Flow を作成");
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

    std::filesystem::path path = UniqueProjectPath(folder, safe, ".hlsl", &asset_database);

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
    project_selected_entry_path = path;
    selected_editor_object = editor_selection::asset;
    project_tree_reveal_selection_pending = true;
    set_project_folder(path.parent_path());

    project_browser_status = "Surface Shader を作成しました: " +
        path.filename().u8string() + " / ShaderGUID=" + shader_id.ToString();
    if (report.compile_failed != 0)
        project_browser_status += " (compile error は Shader Catalog で確認)";
    project_record_created_path(path, "Surface Shader を作成");
    push_editor_log("Info", project_browser_status, path, 1);
    return true;
}


bool framework::project_create_input_action_asset(const std::string& name)
{
    using ReplayEngine::Assets::AssetKind;
    const std::string safe = SafeProjectFileName(name.empty() ? "DefaultInput" : name);
    if (safe.empty()) return false;

    std::error_code error;
    const std::filesystem::path root = std::filesystem::current_path(error);
    if (error) return false;
    const std::filesystem::path folder = root / project_current_folder;
    std::filesystem::create_directories(folder, error);
    if (error) return false;

    const std::filesystem::path path = UniqueProjectPath(folder, safe,
        GameInput::InputState::action_asset_extension, &asset_database);
    GameInput::InputState defaults;
    std::string save_error;
    if (!defaults.SaveActionAsset(path, save_error))
    {
        project_browser_status = "Input Action Asset 作成失敗: " + save_error;
        return false;
    }

    const auto& record = asset_database.Register(path, AssetKind::InputAction);
    if (!asset_database.Save(save_error))
    {
        project_browser_status = "Input Asset は作成しましたが DB 保存失敗: " + save_error;
        return false;
    }

    selected_asset_guid = record.guid;
    project_selected_entry_path = path;
    selected_editor_object = editor_selection::asset;
    project_tree_reveal_selection_pending = true;
    project_settings.SetInputActionAssetGuid(record.guid);
    save_project_settings();
    load_active_input_action_asset();
    project_record_created_path(path, "Input Action Asset を作成");
    project_browser_status = "Input Action Asset を作成しました: " + path.filename().u8string();
    return true;
}

bool framework::load_active_input_action_asset()
{
    game_input.ResetDefaultBindings();
    const std::string guid = project_settings.InputActionAssetGuid();
    if (guid.empty()) return true;

    const auto* record = asset_database.FindByGuid(guid);
    if (record == nullptr || record->kind != ReplayEngine::Assets::AssetKind::InputAction)
    {
        push_editor_log("Warning",
            "Input Action Asset が見つからないため hard-coded default を使用します");
        return false;
    }

    std::string error;
    if (!game_input.LoadActionAsset(content_path(record->source_path), error))
    {
        push_editor_log("Warning", "Input Action Asset: " + error, record->source_path);
        return false;
    }
    return true;
}
