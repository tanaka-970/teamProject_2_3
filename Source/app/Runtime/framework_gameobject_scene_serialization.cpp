// GameObject / Component 基盤のうち「保存・復旧・Scene 操作」を持つ。
//
//   framework_gameobject_scene_serialization.cpp          … Save / Load / Autosave / Recovery / Scene action（このファイル）
//   framework_gameobject_scene_serialization_creation.cpp … Default Ground と新規 Scene 生成
//
// 関数本体は分割前のまま移動し、永続化データと Scene 状態の更新順序は変更しない。
#include "framework.h"

#include "gltf_model.h"
#include "skinned_mesh.h"

#include "../../RePlayEngine/Components/Camera/CameraComponent.h"
#include "../../RePlayEngine/Components/Camera/CameraTargetComponent.h"
#include "../../RePlayEngine/Components/Camera/FollowTargetComponent.h"
#include "../../RePlayEngine/Components/Motion/MotionPlayerComponent.h"
#include "../../RePlayEngine/Components/Core/PropertyLinkComponent.h"
#include "../../RePlayEngine/Components/UI/UIEffectStackComponent.h"
#include "../../RePlayEngine/Components/UI/UISpriteAnimatorComponent.h"
#include "../../RePlayEngine/Components/UI/UITextComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeColliderComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/LightComponents.h"
#include "../../RePlayEngine/Components/Rendering/MeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/PrimitiveMeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/SkinnedMeshRendererComponent.h"
#include "../../RePlayEngine/Rendering/Shaders/BuiltInShaders.h"
#include "../../RePlayEngine/Rendering/ShaderStack/BuiltInShaderLayers.h"
#include "../../RePlayEngine/Object/Registry/BuiltInComponents.h"
#include "../../RePlayEngine/Project/ProjectSettingsSerializer.h"
#include "../../RePlayEngine/Rendering/Adapter/SceneRenderCollector.h"
#include "../../RePlayEngine/Motion/MotionBindingResolver.h"
#include "../../RePlayEngine/Motion/MotionEvaluator.h"
#include "../../RePlayEngine/UI/UILayout.h"
#include "../../RePlayEngine/Runtime/Events/EventBus.h"
#include "../../RePlayEngine/Scene/Serialization/PrefabSerializer.h"
#include "../../RePlayEngine/Scene/Serialization/SceneData.h"
#include "../../RePlayEngine/Scene/Serialization/SceneSerializer.h"
#include "../../RePlayEngine/Scripting/CSharp/CSharpScriptBackend.h"
#include "../../RePlayEngine/Scripting/Core/ScriptComponent.h"
#include "../../RePlayEngine/Scripting/Core/ScriptRuntime.h"
#include "../../RePlayEngine/Scripting/Core/ScriptTypeCatalog.h"
#include "../../RePlayEngine/Scripting/Core/ScriptTypes.h"
#include "../../game/Behaviours/ValidationBehaviours.h"

#include <commdlg.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace
{
    namespace SceneSerialization = ReplayEngine::Scene::Serialization;

    std::filesystem::path BrowseObjectSceneFile(HWND owner, bool save,
        const std::filesystem::path& current)
    {
        wchar_t filename[32768]{};
        if (!current.empty())
        {
            const std::wstring initial = std::filesystem::absolute(current).wstring();
            wcsncpy_s(filename, initial.c_str(), _TRUNCATE);
        }

        static const wchar_t filter[] =
            L"RePlayEngine Scene (*.replayscene)\0*.replayscene\0All Files (*.*)\0*.*\0\0";
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = owner;
        dialog.lpstrFile = filename;
        dialog.nMaxFile = static_cast<DWORD>(_countof(filename));
        dialog.lpstrFilter = filter;
        dialog.lpstrDefExt = L"replayscene";
        dialog.lpstrTitle = save ? L"Save RePlayEngine Scene" : L"Open RePlayEngine Scene";
        dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
            (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        const BOOL accepted = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
        return accepted ? std::filesystem::path(filename) : std::filesystem::path{};
    }

    std::filesystem::path AutosavePathFor(const std::filesystem::path& scene_path)
    {
        const std::string source = scene_path.lexically_normal().generic_string();
        std::uint64_t hash = 1469598103934665603ull;
        for (const unsigned char byte : source)
        {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        const std::string stem = scene_path.stem().empty() ? "Untitled" : scene_path.stem().string();
        return std::filesystem::path("Saved") / "Autosave" /
            (stem + "_" + std::to_string(hash) + ".autosave.replayscene");

    }
}

// ---------------------------------------------------------------------------
// 保存・読み込み
// ---------------------------------------------------------------------------
//
// Editor パネルの描画はここでは行わない。
// 既存の「階層」「インスペクター」ウィンドウの中へ埋め込む形にしてあり、
// HierarchyPanel::DrawContents / InspectorPanel::DrawContents を
// framework_editor.cpp と framework_inspector.cpp から直接呼んでいる。
// 同じ内容のウィンドウを新旧で二重に出さないため、この配置にしている。

bool framework::save_object_scene(bool choose_path)
{
    if (object_scene_play_mode)
    {
        // 実行中の一時的な変化（HP の減少・移動後の位置）をファイルへ焼き込まない。
        object_editor_context.SetStatus("実行中はシーンを保存できません");
        return false;
    }

    const std::filesystem::path previous_path = object_scene_path;
    std::filesystem::path destination = object_scene_path;
    if (choose_path || destination.empty())
    {
        destination = BrowseObjectSceneFile(hwnd, true, destination);
        if (destination.empty())
        {
            object_editor_context.SetStatus("保存をキャンセルしました");
            return false;
        }
        if (destination.extension().empty())
            destination += ReplayEngine::Scene::Serialization::SceneSerializer::file_extension;
    }

    // メインスレッドでスナップショットを取ってから書き出す。
    // Scene の実体をファイル処理へ渡さないことで、
    // 将来書き込みだけを別スレッドへ回せる形にしてある。
    SceneSerialization::SceneData data;
    SceneSerialization::CaptureScene(object_scene, data);

    std::string error;
    if (!SceneSerialization::SceneSerializer::SaveToFile(data, destination, error))
    {
        object_editor_context.SetStatus("保存に失敗しました: " + error);
        return false;
    }

    object_scene_path = std::move(destination);
    object_editor_context.SetScenePath(object_scene_path);
    object_editor_context.ClearDirty();
    object_autosave_elapsed = 0.0f;
    register_object_scene_asset();
    add_recent_object_scene(object_scene_path);
    std::error_code remove_error;
    if (!previous_path.empty())
        std::filesystem::remove(AutosavePathFor(previous_path), remove_error);
    remove_error.clear();
    std::filesystem::remove(AutosavePathFor(object_scene_path), remove_error);
    object_recovery_available = false;
    editor_camera_state_key = make_editor_camera_state_key();
    save_editor_camera_state();
    save_editor_session();
    object_editor_context.SetStatus("保存しました: " + object_scene_path.filename().string());
    return true;
}
bool framework::load_object_scene(bool choose_path)
{
    std::filesystem::path source = object_scene_path;
    if (choose_path)
    {
        source = BrowseObjectSceneFile(hwnd, false, source);
        if (source.empty())
        {
            object_editor_context.SetStatus("読み込みをキャンセルしました");
            return false;
        }
        if (object_editor_context.Dirty())
        {
            request_object_scene_action(object_scene_action::open_path, source);
            return false;
        }
    }

    return load_object_scene_from_path(source);
}

bool framework::load_object_scene_from_path(const std::filesystem::path& source)
{
    if (source.empty())
    {
        object_editor_context.SetStatus("読み込むシーンが指定されていません");
        return false;
    }
    if (object_scene_play_mode) exit_object_play_mode();
    reset_landscape_editor_state(true);

    // Scene を切り替える前に、今の Scene の編集カメラ状態を残す。
    // 戻ってきたときに同じ視点から再開できる。
    if (!editor_camera_state_key.empty()) save_editor_camera_state();

    SceneSerialization::SceneData data;
    std::string error;
    if (!SceneSerialization::SceneSerializer::LoadFromFile(data, source, error))
    {
        // 旧形式・破損・未存在。いずれもクラッシュさせず、現在の Scene を維持する。
        object_editor_context.SetStatus("読み込めませんでした: " + error);
        return false;
    }

    // Scene の中身が総入れ替えになるので、先に衝突世界を切り離す。
    // 古い ObjectID / ColliderID を持ったまま新しい Scene を引くと、
    // まったく別の GameObject へ当たることになる。
    detach_collision_world();

    SceneSerialization::SceneLoadReport report;
    SceneSerialization::ApplySceneData(data, object_scene, report);
    object_editor_context.ResetSceneState();

    // 読み込み後に Scene を開始する。
    // ApplySceneData の中では OnStart / OnEnable を呼ばないので、
    // 途中まで構築された状態で Component が動くことはない。
    object_scene.Start();

    object_editor_context.AttachScene(&object_scene);
    object_scene_path = source;
    object_editor_context.SetScenePath(object_scene_path);
    object_editor_context.ClearDirty();
    register_object_scene_asset();
    add_recent_object_scene(object_scene_path);

    // 新しい Scene の Backend Mode と移行済み集合でつなぎ直す。
    attach_collision_world(object_scene);

    std::string status = "読み込みました: " + object_scene_path.filename().string();
    if (!report.Clean())
    {
        status += "（警告 " + std::to_string(report.warnings.size()) + " 件）";
        for (const std::string& warning : report.warnings)
        {
            OutputDebugStringA(("[Scene] " + warning + "\n").c_str());
        }
    }
    object_editor_context.SetStatus(status);

    // 新しい Scene に対応する編集カメラ状態を読み込む。
    // 保存が無ければ既定位置になる。
    if (!standalone_game_mode) load_editor_camera_state();
    if (!standalone_game_mode) check_object_scene_recovery();
    save_editor_session();
    return true;
}

bool framework::autosave_object_scene()
{
    if (object_scene_play_mode || !object_editor_context.Dirty()) return false;

    SceneSerialization::SceneData data;
    SceneSerialization::CaptureScene(object_scene, data);
    const std::filesystem::path path = AutosavePathFor(object_scene_path);
    std::string error;
    if (!SceneSerialization::SceneSerializer::SaveToFile(data, path, error))
    {
        object_autosave_status = "Autosave failed: " + error;
        return false;
    }
    object_autosave_status = "Autosaved: " + path.filename().string();
    return true;
}

void framework::check_object_scene_recovery()
{
    object_recovery_path = AutosavePathFor(object_scene_path);
    object_recovery_available = false;
    object_recovery_prompt_opened = false;

    std::error_code error;
    if (!std::filesystem::exists(object_recovery_path, error) || error) return;
    const auto autosave_time = std::filesystem::last_write_time(object_recovery_path, error);
    if (error) return;
    if (!std::filesystem::exists(object_scene_path, error) || error)
    {
        object_recovery_available = true;
        return;
    }
    const auto scene_time = std::filesystem::last_write_time(object_scene_path, error);
    object_recovery_available = !error && autosave_time > scene_time;
}

void framework::register_object_scene_asset()
{
    object_scene_asset_guid.clear();
    if (object_scene_path.empty()) return;

    const ReplayEngine::Assets::AssetRecord& record = asset_database.Register(
        object_scene_path, ReplayEngine::Assets::AssetKind::Scene);
    object_scene_asset_guid = record.guid;
    std::string error;
    if (!asset_database.Save(error))
        OutputDebugStringA(("[Scene] AssetDatabase save failed: " + error + "\n").c_str());
}

void framework::add_recent_object_scene(const std::filesystem::path& path)
{
    if (path.empty()) return;
    const std::filesystem::path normalized =
        ReplayEngine::Assets::AssetDatabase::NormalizeProjectPath(path);
    std::string key = normalized.generic_u8string();
    std::transform(key.begin(), key.end(), key.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });

    recent_scene_paths.erase(std::remove_if(recent_scene_paths.begin(), recent_scene_paths.end(),
        [&key](const std::filesystem::path& candidate)
        {
            std::string candidate_key = ReplayEngine::Assets::AssetDatabase::
                NormalizeProjectPath(candidate).generic_u8string();
            std::transform(candidate_key.begin(), candidate_key.end(), candidate_key.begin(),
                [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            return candidate_key == key;
        }), recent_scene_paths.end());
    recent_scene_paths.insert(recent_scene_paths.begin(), normalized);
    constexpr std::size_t maximum_recent_scenes = 10;
    if (recent_scene_paths.size() > maximum_recent_scenes)
        recent_scene_paths.resize(maximum_recent_scenes);
}

void framework::discard_object_scene_autosave()
{
    std::error_code error;
    std::filesystem::remove(AutosavePathFor(object_scene_path), error);
    object_recovery_available = false;
    object_autosave_elapsed = 0.0f;
    object_autosave_status.clear();
}

void framework::request_object_scene_action(object_scene_action action,
    std::filesystem::path path)
{
    // 新規 / 開く / 終了 はどれも編集シーンに対する操作。
    // Play 中のまま進むと save_object_scene が
    // 「実行中はシーンを保存できません」で false を返し、
    // 「保存して終了」を押しても何も起きず終了できなくなる。
    // 先に実行を止めてから確認へ進む。
    if (object_scene_play_mode) exit_object_play_mode();

    pending_object_scene_action = action;
    pending_object_scene_path = std::move(path);
    if (object_editor_context.Dirty())
    {
        object_scene_unsaved_prompt_requested = true;
        editor_mode = true;
        set_edit_mode(true);
        return;
    }
    execute_pending_object_scene_action();
}

void framework::execute_pending_object_scene_action()
{
    const object_scene_action action = pending_object_scene_action;
    const std::filesystem::path path = pending_object_scene_path;
    pending_object_scene_action = object_scene_action::none;
    pending_object_scene_path.clear();

    switch (action)
    {
    case object_scene_action::new_empty:
        create_object_scene(u8"新しいシーン", false);
        break;
    case object_scene_action::new_default:
        create_object_scene(u8"新しいシーン", true);
        break;
    case object_scene_action::open_path:
        load_object_scene_from_path(path);
        break;
    case object_scene_action::exit_application:
        PostMessage(hwnd, WM_CLOSE, 0, 0);
        break;
    default:
        break;
    }
}

bool framework::confirm_object_scene_close()
{
    // ユーザーが既に「保存して終了 / 破棄して終了」を選んでいる。
    // ここで Dirty を見直すと、確認のあとに何かが Dirty を立て直しただけで
    // 終了できなくなる。選んだ結果を尊重してそのまま閉じる。
    if (object_exit_confirmed) return true;

    if (!object_editor_context.Dirty()) return true;
    request_object_scene_action(object_scene_action::exit_application);
    return false;
}
// ---------------------------------------------------------------------------
// 新規 Scene 作成
// ---------------------------------------------------------------------------
//
// ここが Prefab を配置する唯一の場所。
//
// 【なぜ「唯一」と言い切れるか】
//   PrefabSerializer::Instantiate を呼ぶのは、この関数と load_prefab()
//   （ユーザーが Prefab ファイルを選んで配置する操作）の 2 か所だけ。
//   どちらもユーザーの明示操作からしか呼ばれない。
//   起動処理・Scene 読み込み・Component 不足の検出からは呼ばれない。

