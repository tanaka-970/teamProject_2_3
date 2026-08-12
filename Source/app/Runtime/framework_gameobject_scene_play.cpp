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
#include <string>
#include <utility>
#include <vector>

namespace
{
    namespace SceneSerialization = ReplayEngine::Scene::Serialization;
}

void framework::enter_object_play_mode()
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

    reset_landscape_editor_state(true);

    // 編集 Scene の内容を実行用 Scene へ複製する。
    // 直接コピーせず SceneData を経由するのは、
    // Scene が生ポインタで結ばれておりコピー不可なため。
    // これにより Play 中の変更が編集 Scene へ戻らないことが構造的に保証される。
    initialize_runtime_services();

    // 編集 Scene の内容を RuntimeSceneService へ渡す。
    //
    // framework が自分で Scene を組み立てて持たない理由:
    //   持つと Runtime World の所有者が 2 つになる。どちらが本物かが
    //   場所ごとに変わり、Scene 切り替えのたびに食い違う。
    //   組み立ても入れ替えもサービス側の 1 経路へ寄せる。
    //
    // SceneData を経由するのは Scene がコピー不可なため。
    // これにより Play 中の変更が編集 Scene へ戻らないことが構造的に保証される。
    SceneSerialization::SceneData snapshot;
    SceneSerialization::CaptureScene(object_scene, snapshot);

    // Play From Here は SceneData の実行用コピーだけへ焼き込む。
    // Runtime World の OnAwake / OnStart より前に位置が確定するため、
    // Start 側が初期座標を読むゲームでも期待どおりの場所から始まる。
    apply_play_spawn_override(snapshot);

    // 未保存の Scene には AssetGUID が無い。空のまま渡す。
    // 空の場合、この World に対する Reload は InvalidRequest になるだけで、
    // 別の Scene が代わりに読まれることはない。
    const ReplayEngine::Runtime::SceneRequestResult request =
        object_runtime_scenes.RequestAdopt(snapshot, object_scene_asset_guid);
    if (request != ReplayEngine::Runtime::SceneRequestResult::Accepted)
    {
        // status だけだとプロジェクトタブを開いていないと見えない。
        // Play に入れないのは重大なので Console へも Error として出す。
        const std::string reason =
            "Play を開始できません（Scene 遷移が進行中です）。SceneRequestResult=" +
            std::to_string(static_cast<int>(request));
        object_editor_context.SetStatus(reason);
        push_editor_log("Error", reason);
        play_spawn_override.active = false;
        return;
    }

    // 構築と入れ替えをその場で済ませる。
    // Play 開始はフレーム境界を挟む必要がないうえ、
    // ここで済ませないと 1 フレームだけ「Play 中なのに空の World」になる。
    object_runtime_scenes.Tick();   // Staging World の構築
    object_runtime_scenes.Tick();   // 入れ替えと Scene::Start()

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
        play_spawn_override.active = false;
        return;
    }

    ReplayEngine::Scene::Scene& runtime_world = object_runtime_scenes.ActiveWorld();

    // 衝突世界を Runtime World へ差し替える。
    // 編集 Scene の ObjectID / ColliderID はここで完全に捨てられるので、
    // Play 中に編集 Scene の Collider へ当たることはない。
    attach_collision_world(runtime_world);

    // Attach 直後に登録表を作る。Play From Here の座標は SceneData へ
    // 事前反映済みなので、OnAwake / OnStart からも正しい開始位置が見える。
    object_collision_world.Refresh();
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

    // ---- Play 直後の全数診断 ------------------------------------------------
    //
    // Inspector が見ているのは編集 Scene の Component で、実際に動くのは
    // ここで作られた実行用 World の複製の方。複製側の状態は Inspector から
    // 一切見えないため、ここで洗いざらい出す。
    //
    // 「Play しても動かない」の原因になり得るものを全部並べる:
    //   ScriptRuntime が無い / Backend が無い / Backend 未初期化 /
    //   Assembly 未ロード / Play セッション未開始 / Catalog が空 /
    //   型が Catalog に無い / Schema 未解決 / インスタンス生成失敗
    {
        namespace Scripting = ReplayEngine::Scripting;

        push_editor_log("Info", "===== Play 診断 開始 =====");

        if (!object_script_runtime)
        {
            push_editor_log("Error", "ScriptRuntime がありません。C# は動きません");
        }
        else
        {
            push_editor_log("Info", std::string("Play セッション: ") +
                (object_script_runtime->PlaySessionActive() ? "有効" : "*** 無効 ***"));

            auto* backend = dynamic_cast<Scripting::CSharp::CSharpScriptBackend*>(
                object_script_runtime->Backend(Scripting::ScriptLanguage::CSharp));
            if (backend == nullptr)
            {
                push_editor_log("Error", "C# Backend が接続されていません");
            }
            else
            {
                push_editor_log(backend->Initialized() ? "Info" : "Error",
                    std::string("C# Backend 初期化: ") +
                    (backend->Initialized() ? "済" : "*** 未 ***"));
                push_editor_log(backend->AssemblyLoaded() ? "Info" : "Error",
                    std::string("C# Assembly ロード: ") +
                    (backend->AssemblyLoaded() ? "済" : "*** 未 ***"));
                push_editor_log("Info", "C# 生存インスタンス数: " +
                    std::to_string(backend->LiveInstanceCount()));
                if (!backend->LastErrorMessage().empty())
                {
                    push_editor_log("Error",
                        "C# Backend 直近エラー: " + backend->LastErrorMessage());
                }
            }

            const auto& all = object_script_runtime->Catalog().All();
            push_editor_log(all.empty() ? "Error" : "Info",
                "Catalog 登録数: " + std::to_string(all.size()));
            for (const Scripting::ScriptTypeDescriptor& descriptor : all)
            {
                const bool can = backend != nullptr &&
                    backend->CanInstantiate(descriptor.type_id);
                push_editor_log(can ? "Info" : "Warning",
                    "  Catalog: " + descriptor.DisplayName() +
                    " / class=" + descriptor.class_name +
                    " / typeid=" + descriptor.type_id.ToString() +
                    " / asset=" + descriptor.asset_guid +
                    " / 生成可否=" + (can ? "可" : "*** 不可 ***") +
                    (descriptor.last_error.empty()
                        ? std::string() : " / エラー=" + descriptor.last_error));
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
            : (script_with_instance == script_total ? "Info" : "Warning"),
            "実行用 World の Script Component: " + std::to_string(script_total) +
            " 個 / インスタンス生成済み " + std::to_string(script_with_instance) + " 個");

        if (script_total == 0)
        {
            push_editor_log("Error",
                "実行用 World に Script Component が 1 つもありません。"
                "編集 Scene から実行用 Scene への複製で落ちています");
        }

        push_editor_log("Info", "===== Play 診断 終了 =====");
    }
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

        push_editor_log(script->HasInstance() ? "Info" : "Error",
            "  [" + object.Name() + "] 状態=" +
            ReplayEngine::Scripting::ToString(script->Status()) +
            " / インスタンス=" + (script->HasInstance() ? "あり" : "*** なし ***") +
            " / class=" + script->ClassName() +
            " / typeid=" + script->ScriptType().ToString() +
            " / asset=" + script->ScriptAssetGUID() +
            " / Schema=" + (script->Schema() ? "あり" : "*** なし ***") +
            " / enabled=" + (script->Enabled() ? "true" : "false") +
            (script->LastError().empty()
                ? std::string() : " / 理由=" + script->LastError()));
    }

    for (ReplayEngine::Core::GameObject* child : object.Children())
    {
        if (child != nullptr) count_runtime_script_instances(*child, total, with_instance);
    }
}

void framework::exit_object_play_mode()
{
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
