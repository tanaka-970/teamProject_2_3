// RuntimeSceneService / SceneFlowService と framework の接続部。
//
// ここが唯一の「Runtime World を結線する場所」。
//
// ---------------------------------------------------------------------------
// 【World の所有者は 1 つだけ】
//
//   Runtime 中の Scene は RuntimeSceneService が unique_ptr で所有する。
//   framework は Runtime World を値でも生ポインタでも持たない。
//   欲しいときに object_runtime_scenes.ActiveWorld() から取り直す。
//
//   以前は framework が object_scene_runtime を値メンバとして持っていたが、
//   その形だと Scene の入れ替えができない（Scene はムーブ禁止）ため、
//   「サービスが持つ World」と「framework が持つ World」の 2 つが並ぶことになる。
//   どちらが本物かが場所ごとに変わるので、参照を 1 つに寄せた。
//
//   Scene をムーブ可能にして値のまま差し替える案は採らなかった。
//   GameObject が Scene への生ポインタを持っているため、Scene の実体が動くと
//   その参照がすべて壊れる。ムーブを足すのは、その危険を型で許可することになる。
//
// ---------------------------------------------------------------------------
// 【生の Scene* を溜めない】
//
//   World が入れ替わると実体が変わる。参照を溜めると解放済みの Scene を指す。
//   実体番号 (WorldInstanceID) が変わったフレームだけ、
//   rebind_runtime_world_if_changed() が結線し直す。
//   結線先はいずれも「今の World」を受け取り直す形にしてある。
//
// 依存方向:
//   framework -> RePlayEngine の一方向。逆向きの参照は無い。

#include "framework.h"

#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Runtime/Behaviour/BehaviourRegistry.h"
#include "../../RePlayEngine/Scripting/CSharp/CSharpProject.h"
#include "../../RePlayEngine/Scripting/CSharp/CSharpScriptBackend.h"
#include "../../RePlayEngine/Scene/Serialization/PrefabSerializer.h"
#include "../../RePlayEngine/Scene/Serialization/SceneData.h"

#include <filesystem>
#include <string>

// 名前空間別名はファイルスコープへ置く。
// 無名名前空間の中でも通るが、外から見えるかどうかが直感的でないので避ける。
namespace RRuntime = ReplayEngine::Runtime;

// ---------------------------------------------------------------------------
// Scene Asset の解決
// ---------------------------------------------------------------------------

ReplayEngine::Runtime::RuntimeStatus framework::scene_asset_resolver::ResolveScenePath(
    const std::string& asset_guid, std::string& out_path) const
{
    if (owner_ == nullptr) return RRuntime::RuntimeStatus::ServiceUnavailable;
    if (asset_guid.empty()) return RRuntime::RuntimeStatus::InvalidArgument;

    const ReplayEngine::Assets::AssetRecord* record =
        owner_->asset_database.FindByGuid(asset_guid);
    if (record == nullptr) return RRuntime::RuntimeStatus::AssetMissing;

    // Scene 以外の Asset を Scene として読みに行かない。
    // 「拡張子が違うだけかもしれない」と推測して開くと、
    // 壊れた Scene として失敗するより分かりにくい形で失敗する。
    if (record->kind != ReplayEngine::Assets::AssetKind::Scene)
    {
        return RRuntime::RuntimeStatus::InvalidAssetType;
    }

    std::error_code filesystem_error;
    if (!std::filesystem::exists(record->source_path, filesystem_error) || filesystem_error)
    {
        // 登録はあるが実ファイルが無い。パスを組み立て直して探すことはしない。
        return RRuntime::RuntimeStatus::AssetMissing;
    }

    out_path = record->source_path.string();
    return RRuntime::RuntimeStatus::Ok;
}

// ---------------------------------------------------------------------------
// Prefab の生成
// ---------------------------------------------------------------------------

ReplayEngine::Runtime::RuntimeStatus
framework::runtime_prefab_instantiator::InstantiatePrefab(
    const std::string& asset_guid, ReplayEngine::Scene::Scene& world,
    const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& rotation_euler,
    const DirectX::XMFLOAT3& scale, ReplayEngine::Core::ObjectID parent,
    ReplayEngine::Core::ObjectID& created_root)
{
    if (owner_ == nullptr) return RRuntime::RuntimeStatus::ServiceUnavailable;
    if (asset_guid.empty()) return RRuntime::RuntimeStatus::InvalidArgument;

    const ReplayEngine::Assets::AssetRecord* record =
        owner_->asset_database.FindByGuid(asset_guid);
    if (record == nullptr) return RRuntime::RuntimeStatus::AssetMissing;

    std::string error;
    ReplayEngine::Scene::Serialization::SceneLoadReport report;
    const ReplayEngine::Core::ObjectID root =
        ReplayEngine::Scene::Serialization::PrefabSerializer::Instantiate(
            world, record->source_path, error, &report, asset_guid);
    if (!root.Valid()) return RRuntime::RuntimeStatus::SceneLoadFailed;

    ReplayEngine::Core::GameObject* object = world.FindGameObjectByID(root);
    if (object == nullptr) return RRuntime::RuntimeStatus::SceneLoadFailed;

    object->GetTransform().SetLocalPosition(position);
    object->GetTransform().SetLocalRotationEuler(rotation_euler);
    object->GetTransform().SetLocalScale(scale);

    if (parent.Valid())
    {
        if (ReplayEngine::Core::GameObject* parent_object = world.FindGameObjectByID(parent))
        {
            object->SetParent(parent_object, false);
        }
    }

    created_root = root;
    return RRuntime::RuntimeStatus::Ok;
}

// ---------------------------------------------------------------------------
// Runtime サービスの初期化
// ---------------------------------------------------------------------------

void framework::initialize_runtime_services()
{
    if (object_runtime_context) return;

    // 自分自身への参照をここで渡す。
    // メンバ初期化子ではなくこの位置にしているのは、
    // framework が完全に構築されたあとで結線するため。
    object_scene_resolver.Bind(*this);
    object_prefab_instantiator.Bind(*this);

    // RuntimeContext は World の view。所有者より後に作り、先に壊す。
    // framework.h の宣言順がその寿命を保証している。
    object_runtime_context = std::make_unique<RRuntime::RuntimeContext>(
        object_runtime_scenes.ActiveWorld());
    object_scene_flow = std::make_unique<RRuntime::SceneFlowService>(object_runtime_scenes);

    // スクリプト機構。GameObject 側の実体は常に ScriptComponent。
    // C# Backend は managed instance だけを持ち、Runtime API は Handle 経由で触る。
    object_script_runtime = std::make_unique<ReplayEngine::Scripting::ScriptRuntime>();
    object_script_runtime->InstallBackend(
        std::make_unique<ReplayEngine::Scripting::CSharp::CSharpScriptBackend>(
            std::filesystem::current_path()));
    object_script_runtime->Initialize();

    object_runtime_scenes.SetRuntimeContext(object_runtime_context.get());
    object_runtime_scenes.SetAssetResolver(&object_scene_resolver);
    object_runtime_scenes.SetCollisionDispatcher(&object_collision_events);

    // World の入れ替えへ接続する。
    //
    // ここが要点。Play Mode の開始処理ではなく World 入れ替えのフックへ挿すことで、
    // 「Play 開始」も「ゲーム中の Scene 遷移」も「Play 停止」も同じ 1 経路になる。
    // Scene::Start() が OnRuntimeAwake を流す前に Play Session が用意される。
    object_runtime_scenes.SetWorldLifecycleListener(object_script_runtime.get());

    object_runtime_context->SetPrefabInstantiator(&object_prefab_instantiator);
    object_runtime_context->SetSceneFlow(object_scene_flow.get());

    // ProjectSettings の Active Scene Flow を Runtime へ接続する。
    // 未設定/欠損なら空のままにし、直接 LoadScene の既存経路は壊さない。
    sync_runtime_scene_flow_asset();

    object_runtime_scenes.ActiveWorld().Services().SetRuntime(object_runtime_context.get());

    // 編集 Scene からも Schema を引けるようにする。
    // Play セッションはまだ無いので、Inspector の表示だけが動く。
    // インスタンスの生成は PlaySessionActive() が false のあいだ起きない。
    object_scene.Services().SetScripts(object_script_runtime.get());

    refresh_csharp_scripts();

    // シェーダ資産の走査。
    //
    // ここへ置くのは、Console のログ経路が既に使える状態だから。
    // まだ描画には使わないので、失敗しても他へ影響しない。
    scan_shader_library();

    object_bound_world_instance = object_runtime_scenes.ActiveWorldID();
}

// ---------------------------------------------------------------------------
// Startup Scene からの起動
// ---------------------------------------------------------------------------

void framework::set_runtime_blocked(const std::string& reason)
{
    object_runtime_blocked = true;
    object_runtime_block_reason = reason;

    // Editor は落とさない。止めるのは Runtime の開始だけ。
    object_editor_context.SetStatus("Runtime 開始を停止しました: " + reason);
    OutputDebugStringA(("[Runtime] " + reason + "\n").c_str());
}

void framework::clear_runtime_blocked() noexcept
{
    object_runtime_blocked = false;
    object_runtime_block_reason.clear();
}

void framework::begin_startup_scene()
{
    initialize_runtime_services();
    if (!object_scene_flow) return;

    clear_runtime_blocked();

    // Editor を出さない起動では、この時点から Runtime World が本番になる。
    //
    // 読み込みに失敗しても編集 Scene へ落とさない。
    // 落とすと「起動 Scene が壊れているのに、別の Scene で動き出す」ことになり、
    // 何が起きたのか分からないまま遊べてしまう。
    // 失敗したときは空の World のまま止め、理由を診断へ残す。
    object_runtime_world_active = true;
    object_bound_world_instance = object_runtime_scenes.ActiveWorldID();

    const std::string& startup_guid = project_settings.StartupSceneGuid();

    // 未設定は「エラー」ではなく「設定されていない」という状態。
    // 代わりの Scene を勝手に選ばない。
    if (startup_guid.empty())
    {
        object_scene_flow->BeginStartupScene(std::string());
        set_runtime_blocked(
            "Startup Scene が設定されていません（Project Settings で指定してください）");
        return;
    }

    // Asset Database 側の状態も先に見ておく。
    // 「登録が消えている」「Scene Asset ではない」を、読み込み失敗より前に診断へ出す。
    const ReplayEngine::Project::AssetReferenceStatus status =
        project_settings.ResolveStartupScene(asset_database);
    if (!status.IsResolved())
    {
        object_scene_flow->BeginStartupScene(startup_guid);
        set_runtime_blocked(
            "Startup Scene の Asset が見つかりません（GUID は保持しています）");
        return;
    }

    const RRuntime::RuntimeStatus request =
        object_scene_flow->BeginStartupScene(startup_guid);
    if (RRuntime::Failed(request))
    {
        set_runtime_blocked("Startup Scene の読み込み要求が受理されませんでした: " +
            std::string(RRuntime::DescribeJapanese(request)));
    }
}

// ---------------------------------------------------------------------------
// 毎フレームの進行
// ---------------------------------------------------------------------------

void framework::tick_runtime_scene_flow()
{
    if (!object_scene_flow) return;

    // SceneFlowService::Tick() が内部で RuntimeSceneService::Tick() を呼ぶ。
    // ここで両方を呼ぶと 1 フレームに 2 段進み、
    // 「構築」と「入れ替え」の間へフレーム境界を挟めなくなる。
    object_scene_flow->Tick();

    // 起動の診断状態を拾い上げる。
    if (object_scene_flow->StartupState() == RRuntime::StartupSceneState::Failed &&
        !object_runtime_blocked)
    {
        set_runtime_blocked("Startup Scene を読み込めませんでした: " +
            object_scene_flow->LastError());
    }
    else if (object_scene_flow->StartupState() == RRuntime::StartupSceneState::Ready &&
        object_runtime_blocked)
    {
        clear_runtime_blocked();
    }

    // 終了要求はアプリケーション層が受け取る。
    // SceneFlowService はプロセスを落とさないので、ここで初めて実際の終了になる。
    if (object_scene_flow->QuitRequested())
    {
        object_scene_flow->ClearQuitRequest();
        request_object_scene_action(object_scene_action::exit_application);
    }

    rebind_runtime_world_if_changed();
}

void framework::rebind_runtime_world_if_changed()
{
    const ReplayEngine::Core::WorldInstanceID current =
        object_runtime_scenes.ActiveWorldID();
    if (current == object_bound_world_instance) return;

    // 実体番号は「見た」時点で更新する。
    // ここで更新しておかないと、下の分岐で抜けた場合に毎フレーム
    // 張り替えを試み続けることになる。
    object_bound_world_instance = current;

    // Runtime World を動かしていないなら、張り替える相手がいない。
    if (!object_runtime_world_active) return;

    ReplayEngine::Scene::Scene& world = object_runtime_scenes.ActiveWorld();

    // 衝突世界を新しい World へ張り替える。
    // AttachScene の中で登録表と接触ペアが捨てられるので、
    // 旧 World の ObjectID / ColliderID は 1 件も残らない。
    attach_collision_world(world);

    // 操作対象は新しい World の設定から取り直す。
    // 前の World の ObjectID は、新しい World ではまったく別の意味になる。
    player_control_system.SetControlledObject(world.Services().ControlledObject());

    // ---- ここから先は Editor が居るときだけ ------------------------------
    //
    // Editor を出していない通常のゲーム起動では EditorContext を触らない。
    // Editor が無い実行で Editor の状態を動かす経路を作らないでおく。
    if (!object_scene_play_mode) return;

    // ResetSceneState() で Selection と Undo 履歴を明示的に捨てる。
    // EditorSelection は ObjectID しか持たないので解放済みメモリは触らないが、
    // 旧 World の ObjectID を新 World で引き直すと無関係な GameObject を
    // 指すことになるため、番号ごと捨てる。
    object_editor_context.AttachScene(&world);
    object_editor_context.ResetSceneState();
    object_editor_context.SetStatus("Runtime Scene が切り替わりました（選択を解除しました）");
}

// ---------------------------------------------------------------------------
// Editor 向けの読み取り専用診断
// ---------------------------------------------------------------------------
//
// Runtime 側から Editor を参照しない。Editor がここを読むだけ。

void framework::draw_runtime_diagnostics_panel()
{
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader(u8"Runtime 診断")) return;

    const bool has_flow = static_cast<bool>(object_scene_flow);

    ImGui::Text(u8"World 実体番号: %llu",
        static_cast<unsigned long long>(object_runtime_scenes.ActiveWorldID()));
    ImGui::Text(u8"World の所有者: RuntimeSceneService");
    ImGui::Text(u8"GameObject 数: %zu",
        object_runtime_scenes.ActiveWorld().GameObjectCount());

    const std::string& active_guid = object_runtime_scenes.ActiveSceneGuid();
    ImGui::Text(u8"Active Scene GUID: %s",
        active_guid.empty() ? u8"（未読み込み）" : active_guid.c_str());

    const std::string& pending_guid = object_runtime_scenes.PendingSceneGUID();
    ImGui::Text(u8"Pending Scene GUID: %s",
        pending_guid.empty() ? u8"（なし）" : pending_guid.c_str());

    ImGui::Text(u8"RuntimeSceneService State: %s",
        RRuntime::ToString(object_runtime_scenes.State()));
    ImGui::Text(u8"SceneFlow State: %s", has_flow
        ? RRuntime::ToString(object_scene_flow->CurrentTransitionState()) : u8"（未接続）");
    ImGui::Text(u8"Startup State: %s", has_flow
        ? RRuntime::ToString(object_scene_flow->StartupState()) : u8"（未接続）");

    const std::string& startup_guid = project_settings.StartupSceneGuid();
    ImGui::Text(u8"Startup Scene: %s",
        startup_guid.empty() ? u8"（未設定）" : startup_guid.c_str());

    ImGui::Text(u8"StartupBlocked: %s",
        (has_flow && object_scene_flow->StartupBlocked()) ? u8"はい" : u8"いいえ");
    ImGui::Text(u8"Runtime Blocked: %s", object_runtime_blocked ? u8"はい" : u8"いいえ");
    if (object_runtime_blocked && !object_runtime_block_reason.empty())
    {
        ImGui::TextWrapped(u8"停止理由: %s", object_runtime_block_reason.c_str());
    }

    ImGui::Text(u8"QuitRequested: %s",
        (has_flow && object_scene_flow->QuitRequested()) ? u8"はい" : u8"いいえ");
    ImGui::Text(u8"モード: %s", object_scene_play_mode ? u8"Play" : u8"Edit");

    const std::string& last_error = object_runtime_scenes.LastError();
    if (!last_error.empty()) ImGui::TextWrapped(u8"Last Error: %s", last_error.c_str());
    else                     ImGui::Text(u8"Last Error: （なし）");

    if (has_flow)
    {
        ImGui::Text(u8"履歴: %zu 件（戻れる: %s）",
            object_scene_flow->History().size(),
            object_scene_flow->CanReturn() ? u8"はい" : u8"いいえ");
    }

    const ReplayEngine::Scene::Serialization::SceneLoadReport& report =
        object_runtime_scenes.LastLoadReport();
    ImGui::Text(u8"直近の読み込み: Missing %d / 未知プロパティ %d / 復元失敗 %d",
        report.missing_components, report.unknown_properties, report.skipped_components);

    ImGui::Text(u8"Load %llu / Swap %llu / 失敗 %llu",
        static_cast<unsigned long long>(object_runtime_scenes.LoadCount()),
        static_cast<unsigned long long>(object_runtime_scenes.SwapCount()),
        static_cast<unsigned long long>(object_runtime_scenes.FailureCount()));

    ImGui::Text(u8"Behaviour 登録数: %zu",
        RRuntime::BehaviourRegistry::All().size());
#endif
}
