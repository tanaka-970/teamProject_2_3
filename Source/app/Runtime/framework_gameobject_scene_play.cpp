// GameObject / Component 基盤のうち「Play Mode の開始・終了」を持つ。
// 関数本体は分割前のまま移動し、Scene の所有権切替と復帰順序は変更しない。
#include "framework.h"

#include "../../RePlayEngine/Components/Camera/CameraComponent.h"
#include "../../RePlayEngine/Components/Camera/CameraTargetComponent.h"
#include "../../RePlayEngine/Components/Camera/FollowTargetComponent.h"
#include "../../RePlayEngine/Components/Motion/MotionPlayerComponent.h"
#include "../../RePlayEngine/Components/Core/PropertyLinkComponent.h"
#include "../../RePlayEngine/Components/UI/UIEffectStackComponent.h"
#include "../../RePlayEngine/Components/UI/UISpriteAnimatorComponent.h"
#include "../../RePlayEngine/Components/UI/UITextComponent.h"
#include "../../RePlayEngine/Components/Rendering/LightComponents.h"
#include "../../RePlayEngine/Components/Rendering/MeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/PrimitiveMeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/SkinnedMeshRendererComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeRendererComponent.h"
#include "../../RePlayEngine/Object/Registry/BuiltInComponents.h"
#include "../../RePlayEngine/Project/ProjectSettingsSerializer.h"
#include "../../RePlayEngine/Rendering/Adapter/SceneRenderCollector.h"
#include "../../RePlayEngine/Motion/MotionBindingResolver.h"
#include "../../RePlayEngine/Motion/MotionEvaluator.h"
#include "../../RePlayEngine/UI/UILayout.h"
#include "../../RePlayEngine/Runtime/Events/EventBus.h"
#include "../../RePlayEngine/Scene/Serialization/SceneData.h"
#include "../../RePlayEngine/Scene/Serialization/SceneSerializer.h"
#include "../../RePlayEngine/Scripting/CSharp/CSharpScriptBackend.h"
#include "../../RePlayEngine/Scripting/Core/ScriptComponent.h"
#include "../../RePlayEngine/Scripting/Core/ScriptRuntime.h"
#include "../../RePlayEngine/Scripting/Core/ScriptTypeCatalog.h"
#include "../../RePlayEngine/Scripting/Core/ScriptTypes.h"
#include "../../game/Behaviours/ValidationBehaviours.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    namespace SceneSerialization = ReplayEngine::Scene::Serialization;

}

// Play を始める前に、C# の Assembly をソースへ追いつかせる。
//
// 変更検出は poll_csharp_script_changes() が 1 秒ごとに回し、さらに
// 「変化が落ち着いた次の回」まで再コンパイルを待つ。保存してすぐ F5 を押すと
// その待ちの途中で Play が始まり、古い Assembly のまま実行用 World ができる。
// 画面は出るのに直したはずの挙動が変わらず、原因が追えなくなる。
// 起動時のビルド失敗で Assembly が 1 つも無い場合もここで拾い直す。
void framework::ensure_csharp_ready_for_play()
{
    namespace CSharp = ReplayEngine::Scripting::CSharp;
    namespace Scripting = ReplayEngine::Scripting;

    // 書き出したゲームに dotnet は無い。ビルドは Editor だけの機能。
    if (standalone_game_mode || !object_script_runtime) return;

    auto* backend = dynamic_cast<CSharp::CSharpScriptBackend*>(
        object_script_runtime->Backend(Scripting::ScriptLanguage::CSharp));
    if (backend == nullptr) return;

    // ファイル時刻を直接見る。巡回の周期に Play の正しさを依存させない。
    // freshness と入力 revision を同じ走査で取得する。
    const bool assembly_missing = !backend->AssemblyLoaded();
    const CSharp::CSharpBuildState build_state =
        CSharp::CSharpProject::QueryGameScriptsBuildState(content_root_path());
    if (!assembly_missing && !build_state.build_required)
    {
        csharp_last_play_build_failed_revision = 0;
        csharp_last_play_build_succeeded_revision = build_state.input_revision;
        csharp_last_play_build_skip_logged_revision = 0;
        return;
    }

    // mtime ベース判定は安全側へ倒すため、生成物の timestamp が更新されない
    // MSBuild の no-op 成功では build_required が true のままになる場合がある。
    // 同じ入力 revision で既に一度成功しており Assembly もロード済みなら、
    // F5 ごとの dotnet process 起動を禁止する。入力が変われば revision も変わる。
    if (!assembly_missing && build_state.input_revision != 0 &&
        csharp_last_play_build_succeeded_revision == build_state.input_revision)
    {
        return;
    }

    // 同じ入力状態で一度失敗済みなら、F5 のたびに同じ dotnet build を
    // 何度も同期実行しない。最後に成功した Assembly があればそれを使い、
    // 無い場合も「C# 無効」の診断を残して Play 自体は続ける。
    // ソース / project / Managed API が変われば revision が変わるため、
    // 次の Play では自動的に再試行される。
    if (build_state.input_revision != 0 &&
        csharp_last_play_build_failed_revision == build_state.input_revision)
    {
        if (csharp_last_play_build_skip_logged_revision != build_state.input_revision)
        {
            push_editor_log("Warning",
                "C# は同じソース状態で直前のビルドに失敗しているため、"
                "Play 前の再ビルドを省略します。ソースを変更するか "
                "Build && Reload C# で明示的に再試行できます");
            csharp_last_play_build_skip_logged_revision = build_state.input_revision;
        }
        return;
    }

    push_editor_log("Info", assembly_missing
        ? "C# Assembly が無いため、Play の前にビルドします"
        : "C# のソースが Assembly より新しいため、Play の前にビルドします");

    if (build_and_reload_csharp_scripts())
    {
        csharp_last_play_build_failed_revision = 0;
        csharp_last_play_build_skip_logged_revision = 0;
        return;
    }

    // build_and_reload_csharp_scripts() が失敗後の入力 revision を記録済み。
    // Play 前に取得した revision で上書きすると、ビルド中に Managed API が更新された
    // ケースで次回また同じ失敗ビルドを走らせてしまうので、ここでは触らない。

    // 失敗しても Play は止めない。直前に成功した Assembly が残っていれば動く。
    // 何も無ければ、この直後の Play 診断が「Assembly ロード: 未」を出す。
    push_editor_log("Error",
        "Play 前の C# ビルドに失敗しました。次回 Play では同じ入力の再ビルドを"
        "繰り返しません。C# の変更は反映されていません");
}

void framework::enter_object_play_mode(bool show_loading_screen)
{
    // 呼ばれたこと自体を必ず残す。
    // 「Play を押しても何も起きない」ときに、ボタンが繋がっていないのか
    // 中で弾かれているのかを切り分けられないと追えない。
    push_editor_log("Info", "Play 開始要求を受けました");

    if (object_scene_play_mode)
    {
        push_editor_log("Info", "既に Play 中のため何もしません");
        return;
    }
    if (object_editor_play_loading)
    {
        push_editor_log("Info", "Play 用 Scene を準備中のため何もしません");
        return;
    }

    // 描画フレームを持たない回帰テストは従来どおり同期で完了させる。
    // 通常の Editor は下の段階実行へ入り、まず 1 フレーム UI を描いてから
    // C# / Scene Capture / World 構築を進める。
    if (standalone_game_mode || !show_loading_screen)
    {
        reset_landscape_editor_state(true);
        initialize_runtime_services();
        ensure_csharp_ready_for_play();

        if (!standalone_game_mode && object_editor_context.Dirty() &&
            !project_settings.SceneFlowGuid().empty())
        {
            push_editor_log("Warning",
                "未保存の編集があります。SceneFlow の遷移先は保存済みのファイルから"
                "読み込むため、遷移した先の画面には反映されません");
        }

        SceneSerialization::SceneData snapshot;
        SceneSerialization::CaptureScene(
            object_scene, snapshot, SceneSerialization::SceneCaptureMode::Play);
        apply_play_spawn_override(snapshot);

        const ReplayEngine::Runtime::SceneRequestResult request =
            object_runtime_scenes.RequestAdopt(std::move(snapshot), object_scene_asset_guid);
        if (request != ReplayEngine::Runtime::SceneRequestResult::Accepted)
        {
            const std::string reason =
                "Play を開始できません（Scene 遷移が進行中です）。SceneRequestResult=" +
                std::to_string(static_cast<int>(request));
            object_editor_context.SetStatus(reason);
            push_editor_log("Error", reason);
            play_spawn_override.active = false;
            return;
        }

        object_runtime_scenes.Tick();   // Staging World の構築
        object_runtime_scenes.Tick();   // 入れ替えと Scene::Start()
        complete_object_play_mode_start();
        return;
    }

    // 通常の Editor Play はここでは「開始要求を記録するだけ」にする。
    // 重い処理をこのボタンクリックのコールスタックで行うと、ImGui が次のフレームを
    // Present できず、読み込み表示そのものが出る前に固まって見えるため。
    object_editor_play_request_snapshot.reset();
    object_editor_play_snapshot_reused = false;
    object_editor_play_started_at = std::chrono::steady_clock::now();
    object_editor_play_start_stage = editor_play_start_stage::prepare_runtime;
    object_editor_play_stage_label = u8"実行環境を準備しています";
    object_editor_play_progress = 0.03f;
    object_editor_play_loading = true;
    object_loading_progress_provider.SetEditorPlayLoading(true);
    object_loading_progress_provider.SetEditorPlayProgress(object_editor_play_progress);
    object_editor_context.SetStatus("Play の準備を開始しました…");
    push_editor_log("Info", "Editor Play 段階読み込みを開始しました");
}

bool framework::complete_object_play_mode_start()
{
    if (object_runtime_scenes.State() != ReplayEngine::Runtime::SceneLoadState::Completed)
    {
        // 失敗しても編集 Scene には一切触れていない。そのまま Edit Mode を続ける。
        //
        // ここで黙って戻ると「Play を押しても EDIT MODE のまま」に見え、
        // 原因がまったく分からなくなる。Console へ理由を必ず残す。
        const std::string reason =
            "実行用 Scene を構築できませんでした: " + object_runtime_scenes.LastError() +
            " / SceneLoadState=" +
            std::to_string(static_cast<int>(object_runtime_scenes.State()));
        object_editor_context.SetStatus(reason);
        push_editor_log("Error", reason);
        object_runtime_scenes.ResetToEmptyWorld();
        object_runtime_world_active = false;
        object_scene_play_mode = false;
        object_scene_paused = false;
        object_editor_context.SetPlayMode(false);
        object_editor_context.AttachScene(&object_scene);
        object_editor_context.ResetSceneState();
        play_spawn_override.active = false;
        return false;
    }

    ReplayEngine::Scene::Scene& runtime_world = object_runtime_scenes.ActiveWorld();

    // 衝突世界を Runtime World へ差し替える。
    // 編集 Scene の ObjectID / ColliderID はここで完全に捨てられるので、
    // Play 中に編集 Scene の Collider へ当たることはない。
    const auto collision_started = std::chrono::steady_clock::now();
    attach_collision_world(runtime_world);

    // Attach 直後に登録表を作る。Play From Here の座標は SceneData へ
    // 事前反映済みなので、OnAwake / OnStart からも正しい開始位置が見える。
    // Landscape collision は shared cook cache を使うため、同じ geometry の
    // 再 Play ではここで三角形 grid を Cook し直さない。
    object_collision_world.Refresh();
    const double collision_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - collision_started).count();
    push_editor_log("Info", "Play 準備: Collision world " +
        std::to_string(collision_ms) + " ms");
    if (play_spawn_override.active)
    {
        push_editor_log("Info", play_spawn_override.label + " から Play を開始しました");
        play_spawn_override.active = false; // 一回限り。通常 Play へ持ち越さない。
    }

    // Play 開始時に貯まっていた時間を捨てる。開始直後に物理が飛ぶのを防ぐ。
    object_fixed_accumulator = 0.0f;
    object_time_scale = 1.0f;
    object_collision_events.Reset();

    object_scene_play_mode = true;
    object_scene_paused = false;
    object_runtime_world_active = true;
    object_bound_world_instance = object_runtime_scenes.ActiveWorldID();
    object_editor_context.SetPlayMode(true);
    object_editor_context.AttachScene(&runtime_world);
    object_editor_context.ResetSceneState();
    object_editor_context.SetStatus("実行中（編集シーンは保持されています）");

    // ---- Play 直後の軽量診断 ----------------------------------------------
    //
    // 以前は Play のたびに Catalog 全型 + ScriptComponent 全個体を 1 行ずつ
    // editor_log.txt へ書いていた。push_editor_log は永続ログも更新するため、
    // Scene が大きいほど「診断そのもの」が Play 開始時間へ乗っていた。
    // 成功時は集計 1 行だけ、問題がある項目だけ詳細を出す。診断能力は落とさず、
    // 正常系の O(N) ファイル I/O を無くす。
    {
        const auto diagnostics_started = std::chrono::steady_clock::now();
        namespace Scripting = ReplayEngine::Scripting;

        std::size_t catalog_count = 0;
        std::size_t catalog_unavailable = 0;
        Scripting::CSharp::CSharpScriptBackend* backend = nullptr;

        if (!object_script_runtime)
        {
            push_editor_log("Error", "ScriptRuntime がありません。C# は動きません");
        }
        else
        {
            if (!object_script_runtime->PlaySessionActive())
                push_editor_log("Error", "Play セッションが無効です");

            backend = dynamic_cast<Scripting::CSharp::CSharpScriptBackend*>(
                object_script_runtime->Backend(Scripting::ScriptLanguage::CSharp));
            if (backend == nullptr)
            {
                push_editor_log("Error", "C# Backend が接続されていません");
            }
            else
            {
                if (!backend->Initialized())
                    push_editor_log("Error", "C# Backend が初期化されていません");
                if (!backend->AssemblyLoaded())
                    push_editor_log("Error", "C# Assembly がロードされていません");
                if (!backend->LastErrorMessage().empty())
                    push_editor_log("Error",
                        "C# Backend 直近エラー: " + backend->LastErrorMessage());
            }

            const auto& all = object_script_runtime->Catalog().All();
            catalog_count = all.size();
            if (all.empty())
            {
                push_editor_log("Error", "C# Catalog が空です");
            }
            else
            {
                // 正常な descriptor は列挙しない。生成不能だけ詳細を残す。
                for (const Scripting::ScriptTypeDescriptor& descriptor : all)
                {
                    const bool can = backend != nullptr &&
                        backend->CanInstantiate(descriptor.type_id);
                    if (can && descriptor.last_error.empty()) continue;
                    ++catalog_unavailable;
                    push_editor_log("Warning",
                        "Catalog 生成不可: " + descriptor.DisplayName() +
                        " / class=" + descriptor.class_name +
                        " / typeid=" + descriptor.type_id.ToString() +
                        " / asset=" + descriptor.asset_guid +
                        (descriptor.last_error.empty()
                            ? std::string() : " / エラー=" + descriptor.last_error));
                }
            }
        }

        std::size_t script_total = 0;
        std::size_t script_with_instance = 0;
        for (ReplayEngine::Core::GameObject* root : runtime_world.RootGameObjects())
        {
            if (root == nullptr) continue;
            count_runtime_script_instances(*root, script_total, script_with_instance);
        }

        push_editor_log(script_total == 0 ? "Error"
            : (script_with_instance == script_total && catalog_unavailable == 0
                ? "Info" : "Warning"),
            "Play 診断: Catalog " + std::to_string(catalog_count) +
            " 型 (生成不可 " + std::to_string(catalog_unavailable) +
            ") / Script " + std::to_string(script_total) +
            " 個 / インスタンス " + std::to_string(script_with_instance) + " 個");

        if (script_total == 0)
        {
            push_editor_log("Error",
                "実行用 World に Script Component が 1 つもありません。"
                "編集 Scene から実行用 Scene への複製で落ちています");
        }
        const double diagnostics_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - diagnostics_started).count();
        push_editor_log("Info", "Play 準備: Post-start diagnostics " +
            std::to_string(diagnostics_ms) + " ms");
    }
    return true;
}

void framework::update_editor_play_loading()
{
    if (!object_editor_play_loading) return;

    const auto set_stage = [this](editor_play_start_stage stage,
        float progress, const char* label)
    {
        object_editor_play_start_stage = stage;
        object_editor_play_progress = (std::max)(0.0f, (std::min)(1.0f, progress));
        object_editor_play_stage_label = label != nullptr ? label : "";
        object_loading_progress_provider.SetEditorPlayProgress(object_editor_play_progress);
        if (!object_editor_play_stage_label.empty())
            object_editor_context.SetStatus(object_editor_play_stage_label);
    };
    const auto log_step = [this](const char* name,
        std::chrono::steady_clock::time_point started)
    {
        const double milliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        push_editor_log("Info", std::string("Play 準備: ") + name + " " +
            std::to_string(milliseconds) + " ms");
    };
    const auto fail_loading = [this](const std::string& reason)
    {
        push_editor_log("Error", reason);
        object_editor_context.SetStatus(reason);
        object_runtime_scenes.CancelPending();
        object_editor_play_request_snapshot.reset();
        object_editor_play_loading = false;
        object_editor_play_start_stage = editor_play_start_stage::idle;
        object_editor_play_stage_label.clear();
        object_editor_play_progress = 1.0f;
        object_loading_progress_provider.SetEditorPlayLoading(false);
        play_spawn_override.active = false;
    };

    switch (object_editor_play_start_stage)
    {
    case editor_play_start_stage::prepare_runtime:
    {
        const auto started = std::chrono::steady_clock::now();
        reset_landscape_editor_state(true);
        initialize_runtime_services();

        // 旧 LoadingScene 経路は Play ごとに GameScene を作り直していた。
        // Editor を描画したまま開始する新経路でも、その初期状態だけは維持する。
        // 既存 GameScene がある通常ケースは再確保せず ResetGameplay() だけにし、
        // SceneManager の所有権や Editor の描画経路を揺らさない。
        if (game_scene != nullptr)
        {
            game_scene->Gameplay().ResetGameplay();
        }
        else
        {
            auto next_scene = std::make_unique<GameScene>(
                static_cast<float>(client_width) / static_cast<float>(client_height));
            GameScene* next_game_scene = next_scene.get();
            if (!scene_manager.SetScene(std::move(next_scene)))
            {
                fail_loading("Play 用 GameScene の初期化に失敗しました");
                return;
            }
            game_scene = next_game_scene;
        }

        if (!standalone_game_mode && object_editor_context.Dirty() &&
            !project_settings.SceneFlowGuid().empty())
        {
            push_editor_log("Warning",
                "未保存の編集があります。SceneFlow の遷移先は保存済みのファイルから"
                "読み込むため、遷移した先の画面には反映されません");
        }
        log_step("Runtime services", started);
        set_stage(editor_play_start_stage::prepare_scripts, 0.12f,
            u8"C# スクリプトを確認しています");
        break;
    }

    case editor_play_start_stage::prepare_scripts:
    {
        const auto started = std::chrono::steady_clock::now();
        ensure_csharp_ready_for_play();
        log_step("C# ready", started);
        set_stage(editor_play_start_stage::capture_scene, 0.26f,
            u8"編集中のシーンを実行用に複製しています");
        break;
    }

    case editor_play_start_stage::capture_scene:
    {
        const auto started = std::chrono::steady_clock::now();

        const ReplayEngine::Core::WorldInstanceID world_instance =
            object_scene.WorldInstanceID();
        const std::uint32_t structure_generation = object_scene.StructureGeneration();
        const std::uint64_t content_revision = object_editor_context.ContentRevision();
        const bool cache_valid = object_editor_play_cached_snapshot != nullptr &&
            object_editor_play_cached_world_instance == world_instance &&
            object_editor_play_cached_structure_generation == structure_generation &&
            object_editor_play_cached_content_revision == content_revision &&
            object_editor_play_cached_script_generation == csharp_catalog_generation;

        object_editor_play_snapshot_reused = cache_valid;
        if (!cache_valid)
        {
            SceneSerialization::SceneData captured;
            // File 保存用の mesh_data 文字列化を通さない Play 専用 Capture。
            // Landscape は immutable geometry を共有し、Runtime 側だけが実データを複製する。
            SceneSerialization::CaptureScene(object_scene, captured,
                SceneSerialization::SceneCaptureMode::Play);
            object_editor_play_cached_snapshot =
                std::make_shared<SceneSerialization::SceneData>(std::move(captured));
            object_editor_play_cached_world_instance = world_instance;
            object_editor_play_cached_structure_generation = structure_generation;
            object_editor_play_cached_content_revision = content_revision;
            object_editor_play_cached_script_generation = csharp_catalog_generation;
            ++object_editor_play_snapshot_cache_misses;
            push_editor_log("Info", "Play snapshot cache: MISS (再Capture)");
        }
        else
        {
            ++object_editor_play_snapshot_cache_hits;
            push_editor_log("Info", "Play snapshot cache: HIT (Scene Capture を省略)");
        }

        // 通常 Play は immutable cache をそのまま RuntimeSceneService と共有する。
        // Play From Here だけは開始 Transform を一時的に変えるため、cache 本体を
        // 汚さないようこの 1 回の request 用コピーへ override を焼き込む。
        if (play_spawn_override.active)
        {
            auto overridden = std::make_shared<SceneSerialization::SceneData>(
                *object_editor_play_cached_snapshot);
            apply_play_spawn_override(*overridden);
            object_editor_play_request_snapshot = std::move(overridden);
        }
        else
        {
            object_editor_play_request_snapshot = object_editor_play_cached_snapshot;
        }

        log_step(cache_valid ? "Scene snapshot cache hit" : "Scene capture", started);
        set_stage(editor_play_start_stage::queue_runtime_world, 0.43f,
            cache_valid
                ? u8"シーン変更なし：前回の Play スナップショットを再利用しています"
                : u8"実行用ワールドへ新しいスナップショットを渡しています");
        break;
    }

    case editor_play_start_stage::queue_runtime_world:
    {
        const auto started = std::chrono::steady_clock::now();
        const ReplayEngine::Runtime::SceneRequestResult request =
            object_runtime_scenes.RequestAdoptShared(
                object_editor_play_request_snapshot, object_scene_asset_guid);
        // RuntimeSceneService が shared_ptr を保持したので、この request 側の参照は不要。
        object_editor_play_request_snapshot.reset();
        log_step("Queue runtime world", started);
        if (request != ReplayEngine::Runtime::SceneRequestResult::Accepted)
        {
            fail_loading(
                "Play を開始できません（Scene 遷移が進行中です）。SceneRequestResult=" +
                std::to_string(static_cast<int>(request)));
            return;
        }
        set_stage(editor_play_start_stage::build_runtime_world, 0.50f,
            u8"実行用シーンを構築しています");
        break;
    }

    case editor_play_start_stage::build_runtime_world:
    {
        const auto started = std::chrono::steady_clock::now();
        if (object_runtime_scenes.State() == ReplayEngine::Runtime::SceneLoadState::Loading)
            object_runtime_scenes.Tick();
        log_step("Build runtime world", started);

        if (object_runtime_scenes.State() == ReplayEngine::Runtime::SceneLoadState::Failed)
        {
            fail_loading("実行用 Scene の構築に失敗しました: " +
                object_runtime_scenes.LastError());
            return;
        }
        if (object_runtime_scenes.State() == ReplayEngine::Runtime::SceneLoadState::ReadyToSwap)
        {
            set_stage(editor_play_start_stage::activate_runtime_world, 0.78f,
                u8"実行用シーンを有効化しています");
        }
        else
        {
            // ファイル読み込み経路へ将来拡張しても、サービス側の進捗をこの帯域へ反映できる。
            object_editor_play_progress = 0.50f +
                object_runtime_scenes.Progress() * 0.24f;
            object_loading_progress_provider.SetEditorPlayProgress(
                object_editor_play_progress);
        }
        break;
    }

    case editor_play_start_stage::activate_runtime_world:
    {
        const auto started = std::chrono::steady_clock::now();
        if (object_runtime_scenes.State() == ReplayEngine::Runtime::SceneLoadState::ReadyToSwap)
            object_runtime_scenes.Tick();
        log_step("Activate runtime world", started);

        if (object_runtime_scenes.State() == ReplayEngine::Runtime::SceneLoadState::Failed)
        {
            fail_loading("実行用 Scene の有効化に失敗しました: " +
                object_runtime_scenes.LastError());
            return;
        }
        if (object_runtime_scenes.State() == ReplayEngine::Runtime::SceneLoadState::Completed)
        {
            set_stage(editor_play_start_stage::finalize_play_mode, 0.91f,
                u8"物理・スクリプト・エディタ状態を最終化しています");
        }
        break;
    }

    case editor_play_start_stage::finalize_play_mode:
    {
        const auto started = std::chrono::steady_clock::now();
        const bool completed = complete_object_play_mode_start();
        log_step("Finalize play mode", started);
        if (!completed)
        {
            fail_loading("Play Mode の最終化に失敗しました");
            return;
        }
        set_stage(editor_play_start_stage::completed, 1.0f,
            u8"実行準備が完了しました");
        break;
    }

    case editor_play_start_stage::completed:
        // 100% を 1 フレーム描いてから通常の実行表示へ戻す。
        finish_editor_play_loading();
        break;

    case editor_play_start_stage::idle:
        finish_editor_play_loading();
        break;
    }
}

void framework::finish_editor_play_loading()
{
    if (!object_editor_play_loading) return;

    const double milliseconds = object_editor_play_started_at.time_since_epoch().count() == 0
        ? 0.0
        : std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - object_editor_play_started_at).count();

    object_editor_play_loading = false;
    object_editor_play_start_stage = editor_play_start_stage::idle;
    object_editor_play_request_snapshot.reset();
    object_editor_play_stage_label.clear();
    object_editor_play_progress = 1.0f;
    object_loading_progress_provider.SetEditorPlayProgress(1.0f);
    object_loading_progress_provider.SetEditorPlayLoading(false);
    push_editor_log("Info", "Editor Play 準備完了: " +
        std::to_string(milliseconds) + " ms / snapshot=" +
        (object_editor_play_snapshot_reused ? "HIT" : "MISS") +
        " / cache totals H=" + std::to_string(object_editor_play_snapshot_cache_hits) +
        " M=" + std::to_string(object_editor_play_snapshot_cache_misses));
}

void framework::cancel_editor_play_loading()
{
    if (!object_editor_play_loading) return;

    object_runtime_scenes.CancelPending();
    object_editor_play_request_snapshot.reset();
    object_editor_play_loading = false;
    object_editor_play_start_stage = editor_play_start_stage::idle;
    object_editor_play_stage_label.clear();
    object_editor_play_progress = 1.0f;
    object_loading_progress_provider.SetEditorPlayProgress(1.0f);
    object_loading_progress_provider.SetEditorPlayLoading(false);
    play_spawn_override.active = false;
    object_editor_context.SetStatus("Play 用 Scene の読み込みを中止しました");
    push_editor_log("Info", "Editor Play の準備を中止しました");
}

// 実行用 World の Script Component を数える。
// Play 直後の 1 回だけ呼ぶ診断用。
void framework::count_runtime_script_instances(
    ReplayEngine::Core::GameObject& object,
    std::size_t& total, std::size_t& with_instance)
{
    for (std::size_t index = 0; index < object.ComponentCount(); ++index)
    {
        ReplayEngine::Core::Component* component = object.ComponentAt(index);
        if (component == nullptr) continue;
        auto* script = dynamic_cast<ReplayEngine::Scripting::ScriptComponent*>(component);
        if (script == nullptr) continue;

        ++total;
        if (script->HasInstance()) ++with_instance;

        if (!script->HasInstance())
        {
            push_editor_log("Error",
                "  [" + object.Name() + "] 状態=" +
                ReplayEngine::Scripting::ToString(script->Status()) +
                " / インスタンス=*** なし ***" +
                " / class=" + script->ClassName() +
                " / typeid=" + script->ScriptType().ToString() +
                " / asset=" + script->ScriptAssetGUID() +
                " / Schema=" + (script->Schema() ? "あり" : "*** なし ***") +
                " / enabled=" + (script->Enabled() ? "true" : "false") +
                (script->LastError().empty()
                    ? std::string() : " / 理由=" + script->LastError()));
        }
    }

    for (ReplayEngine::Core::GameObject* child : object.Children())
    {
        if (child != nullptr) count_runtime_script_instances(*child, total, with_instance);
    }
}

void framework::exit_object_play_mode()
{
    if (object_editor_play_loading)
    {
        const bool play_already_started = object_scene_play_mode;
        cancel_editor_play_loading();
        if (!play_already_started) return;
    }
    if (!object_scene_play_mode) return;

    object_audio_system.StopAll();

    // 先に衝突世界を切り離す。
    // Scene を消してから切り離すと、その間に問い合わせが来た場合に
    // 破棄済みの GameObject を引きに行ってしまう。
    detach_collision_world();

    // Runtime World を捨てる。
    //
    // 編集 Scene へ書き戻すことはしない。
    // Play 中の変更（生成された Prefab、動いた Transform、増えた Component）は
    // すべてここで消える。暗黙保存の経路そのものを置かない。
    object_runtime_scenes.ResetToEmptyWorld();
    object_collision_events.Reset();

    object_fixed_accumulator = 0.0f;
    object_time_scale = 1.0f;

    object_scene_play_mode = false;
    object_scene_paused = false;
    object_runtime_world_active = false;
    object_bound_world_instance = object_runtime_scenes.ActiveWorldID();

    // 編集 Scene へ戻す。Play 中の Selection と Undo 履歴はここで捨てる。
    // Runtime の操作が Edit Mode の Undo へ混ざらないのはこのため。
    object_editor_context.SetPlayMode(false);
    object_editor_context.AttachScene(&object_scene);
    object_editor_context.ResetSceneState();

    // 編集 Scene の衝突世界を張り直す。
    attach_collision_world(object_scene);
    object_editor_context.SetStatus("編集モードへ戻りました");
}
