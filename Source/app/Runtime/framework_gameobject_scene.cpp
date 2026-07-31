// GameObject / Component 基盤と既存 framework の接続部。
//
// この 1 ファイルへ新基盤との橋渡しをまとめている理由:
//   framework 側の既存ファイル（描画・入力・エディタ）への変更を最小限に抑え、
//   どこが新基盤との境界なのかを一目で分かるようにするため。
//
// 依存方向:
//   framework  ->  RePlayEngine (Scene / GameObject / Component / Editor)  の一方向。
//   RePlayEngine 側から framework を参照している箇所は無い。

#include "framework.h"

#include "gltf_model.h"
#include "skinned_mesh.h"

#include "../../RePlayEngine/Components/Rendering/MeshRendererComponent.h"
#include "../../RePlayEngine/Object/Registry/BuiltInComponents.h"
#include "../../RePlayEngine/Rendering/Adapter/SceneRenderCollector.h"
#include "../../RePlayEngine/Scene/Serialization/SceneData.h"
#include "../../RePlayEngine/Scene/Serialization/SceneSerializer.h"

#include <filesystem>

namespace
{
    namespace SceneSerialization = ReplayEngine::Scene::Serialization;
}

// ---------------------------------------------------------------------------
// 初期化
// ---------------------------------------------------------------------------

void framework::initialize_object_scene()
{
    // Component 型の登録。Scene を作る前・読む前に 1 回だけ。
    // 二重に呼んでも ComponentRegistry が重複を弾くので安全。
    ReplayEngine::Core::RegisterBuiltInComponents();

    object_scene.SetName("TrainingStage");
    object_editor_context.AttachScene(&object_scene);
    object_editor_context.SetAssetDatabase(&asset_database);
    object_editor_context.SetScenePath(object_scene_path);

    // 既定の Scene があれば読み込む。無くても失敗扱いにしない（新規シーンとして扱う）。
    std::error_code filesystem_error;
    if (std::filesystem::exists(object_scene_path, filesystem_error) && !filesystem_error)
    {
        load_object_scene(false);
    }
    else
    {
        object_editor_context.SetStatus("新規シーン");
    }

    // 編集中も Component の OnStart を回したいので Scene は開始状態にしておく。
    // 実際にゲームロジックが走るかどうかは Edit Mode 判定で制御する。
    object_scene.Start();
}

ReplayEngine::Scene::Scene& framework::active_object_scene() noexcept
{
    return object_scene_play_mode ? object_scene_runtime : object_scene;
}

const ReplayEngine::Scene::Scene& framework::active_object_scene() const noexcept
{
    return object_scene_play_mode ? object_scene_runtime : object_scene;
}

// ---------------------------------------------------------------------------
// 更新
// ---------------------------------------------------------------------------

void framework::update_object_scene(float elapsed_time)
{
    ReplayEngine::Scene::Scene& scene = active_object_scene();

    // 編集中（F3 で停止中）は Component を更新しない。
    // Editor で置いた GameObject が編集中に勝手に動くのを防ぐ。
    const bool editing_paused = editor_mode && edit_mode_active && !object_scene_play_mode;
    if (!editing_paused)
    {
        scene.Update(elapsed_time);
        scene.LateUpdate(elapsed_time);
    }
    else
    {
        // 停止中でも生成・削除の予約だけは反映する。
        // Editor 操作の結果が次のフレームまで残らないようにするため。
        scene.ProcessPendingOperations();
    }

    // 選択が消えた GameObject を指し続けないようにする。
    object_editor_context.Selection().PruneMissing(scene);

    // 描画提出リストを作り直す。ここでは D3D に触れない。
    ReplayEngine::Rendering::SceneRenderCollector::Collect(scene, object_render_items);
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

    if (choose_path || object_scene_path.empty())
    {
        // ファイルダイアログは既存の実装方針に合わせて後段で差し替えられるよう、
        // ここでは既定パスをそのまま使う。
        object_scene_path = std::filesystem::path("resources") / "Scenes" /
            (object_scene.Name() + ReplayEngine::Scene::Serialization::SceneSerializer::file_extension);
    }

    // メインスレッドでスナップショットを取ってから書き出す。
    // Scene の実体をファイル処理へ渡さないことで、
    // 将来書き込みだけを別スレッドへ回せる形にしてある。
    SceneSerialization::SceneData data;
    SceneSerialization::CaptureScene(object_scene, data);

    std::string error;
    if (!SceneSerialization::SceneSerializer::SaveToFile(data, object_scene_path, error))
    {
        object_editor_context.SetStatus("保存に失敗しました: " + error);
        return false;
    }

    object_editor_context.SetScenePath(object_scene_path);
    object_editor_context.ClearDirty();
    object_editor_context.SetStatus("保存しました: " + object_scene_path.filename().string());
    return true;
}

bool framework::load_object_scene(bool choose_path)
{
    if (object_scene_play_mode) exit_object_play_mode();

    if (choose_path)
    {
        object_scene_path = std::filesystem::path("resources") / "Scenes" /
            (object_scene.Name() + ReplayEngine::Scene::Serialization::SceneSerializer::file_extension);
    }

    SceneSerialization::SceneData data;
    std::string error;
    if (!SceneSerialization::SceneSerializer::LoadFromFile(data, object_scene_path, error))
    {
        // 旧形式・破損・未存在。いずれもクラッシュさせず、現在の Scene を維持する。
        object_editor_context.SetStatus("読み込めませんでした: " + error);
        return false;
    }

    SceneSerialization::SceneLoadReport report;
    SceneSerialization::ApplySceneData(data, object_scene, report);

    // 読み込み後に Scene を開始する。
    // ApplySceneData の中では OnStart / OnEnable を呼ばないので、
    // 途中まで構築された状態で Component が動くことはない。
    object_scene.Start();

    object_editor_context.AttachScene(&object_scene);
    object_editor_context.SetScenePath(object_scene_path);
    object_editor_context.ClearDirty();

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
    return true;
}

// ---------------------------------------------------------------------------
// Play Mode
// ---------------------------------------------------------------------------

void framework::enter_object_play_mode()
{
    if (object_scene_play_mode) return;

    // 編集 Scene の内容を実行用 Scene へ複製する。
    // 直接コピーせず SceneData を経由するのは、
    // Scene が生ポインタで結ばれておりコピー不可なため。
    // これにより Play 中の変更が編集 Scene へ戻らないことが構造的に保証される。
    SceneSerialization::SceneData snapshot;
    SceneSerialization::CaptureScene(object_scene, snapshot);

    SceneSerialization::SceneLoadReport report;
    SceneSerialization::ApplySceneData(snapshot, object_scene_runtime, report);
    object_scene_runtime.Start();

    object_scene_play_mode = true;
    object_editor_context.SetPlayMode(true);
    object_editor_context.AttachScene(&object_scene_runtime);
    object_editor_context.SetStatus("実行中（編集シーンは保持されています）");
}

void framework::exit_object_play_mode()
{
    if (!object_scene_play_mode) return;

    // 実行用 Scene を捨てる。編集 Scene には一切触れていないので、
    // Play 前の状態がそのまま残っている。
    object_scene_runtime.Clear();

    object_scene_play_mode = false;
    object_editor_context.SetPlayMode(false);
    object_editor_context.AttachScene(&object_scene);
    object_editor_context.SetStatus("編集モードへ戻りました");
}

// ---------------------------------------------------------------------------
// 描画
// ---------------------------------------------------------------------------

skinned_mesh* framework::resolve_object_mesh(const std::string& asset_guid)
{
    if (asset_guid.empty() || !device) return nullptr;

    const auto cached = object_mesh_cache.find(asset_guid);
    if (cached != object_mesh_cache.end()) return cached->second.get();

    const ReplayEngine::Assets::AssetRecord* record = asset_database.FindByGuid(asset_guid);
    if (record == nullptr)
    {
        // 見つからない Asset は「空」として記録し、毎フレーム検索し直さないようにする。
        object_mesh_cache.emplace(asset_guid, nullptr);
        return nullptr;
    }

    // FBX / cereal キャッシュのみ対応する。
    // glTF は既存のステージ描画側が別経路で扱っており、今回は接続しない。
    std::filesystem::path source = record->source_path;
    std::filesystem::path cache = source;
    cache.replace_extension(L".cereal");

    std::error_code filesystem_error;
    if (!std::filesystem::exists(cache, filesystem_error) || filesystem_error)
    {
        object_mesh_cache.emplace(asset_guid, nullptr);
        return nullptr;
    }

    std::unique_ptr<skinned_mesh> loaded;
    try
    {
        loaded = std::make_unique<skinned_mesh>(device.Get(), source.string().c_str());
    }
    catch (...)
    {
        // 既存プロジェクトは例外を前提にしていないため、ここで握り潰して
        // 「描けない Asset」として扱う。Scene 全体の描画は継続する。
        object_mesh_cache.emplace(asset_guid, nullptr);
        return nullptr;
    }

    skinned_mesh* raw = loaded.get();
    object_mesh_cache.emplace(asset_guid, std::move(loaded));
    return raw;
}

void framework::draw_object_scene_meshes(ID3D11PixelShader* override_pixel_shader,
    bool gbuffer_pass)
{
    if (object_render_items.Empty()) return;

    for (const ReplayEngine::Rendering::RenderItem& item : object_render_items.Items())
    {
        skinned_mesh* mesh = resolve_object_mesh(item.mesh_asset);
        if (mesh == nullptr) continue;

        // GBuffer パスでは Component が指定した描画方式を材質定数へ流す。
        if (gbuffer_pass) bind_gbuffer_material(deferred_shading_model(item.shading_model));

        // アニメーションは今回接続しない（keyframe は nullptr = バインドポーズ）。
        mesh->render(immediate_context.Get(), item.world, item.tint,
            nullptr, override_pixel_shader);
    }
}

void framework::clear_object_mesh_cache() noexcept
{
    object_mesh_cache.clear();
}
