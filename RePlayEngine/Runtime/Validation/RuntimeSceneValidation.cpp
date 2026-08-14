// Runtime Scene 検証のうち、Service の状態遷移と全判定を持つ。
//
//   RuntimeSceneValidation.cpp       … Runtime Scene Service 判定（このファイル）
//   RuntimeSceneValidationInternal.h … Scene 生成・Resolver・Probe の検証 Fixture
//
// 判定は 1 つの状態遷移列として順序依存するため、公開検証関数は分断しない。

#include "RuntimeSceneValidationInternal.h"

namespace ReplayEngine::Runtime::Validation
{
    using namespace Detail::RuntimeSceneValidation;

    // =====================================================================
    // Runtime Scene Service
    // =====================================================================

    int RunRuntimeSceneValidation()
    {
        RegisterProbe();
        SceneProbeBehaviour::ResetCounters();

        Checker check(410);

        // ---- 一時 Scene の書き出し -----------------------------------------
        //
        // 既存の Scene 原本は読みも書きもしない。
        // 検証で使うファイルは毎回ここで作り直す。

        const std::filesystem::path folder =
            std::filesystem::path("Saved") / "Validation" / "RuntimeScene";
        std::error_code directory_error;
        std::filesystem::create_directories(folder, directory_error);

        const std::filesystem::path path_a = folder / "SceneA.replayscene";
        const std::filesystem::path path_b = folder / "SceneB.replayscene";
        const std::filesystem::path path_missing = folder / "SceneMissing.replayscene";
        const std::filesystem::path path_unknown = folder / "SceneUnknown.replayscene";
        const std::filesystem::path path_reference = folder / "SceneReference.replayscene";
        const std::filesystem::path path_corrupt = folder / "SceneCorrupt.replayscene";
        const std::filesystem::path path_future = folder / "SceneFuture.replayscene";

        std::string error;
        const bool written =
            !directory_error &&
            Serialization::SceneSerializer::SaveToFile(BuildSceneA(), path_a, error) &&
            Serialization::SceneSerializer::SaveToFile(BuildSceneB(), path_b, error) &&
            Serialization::SceneSerializer::SaveToFile(
                BuildMissingComponentScene(), path_missing, error) &&
            Serialization::SceneSerializer::SaveToFile(
                BuildUnknownPropertyScene(), path_unknown, error) &&
            Serialization::SceneSerializer::SaveToFile(
                BuildReferenceScene(), path_reference, error);
        check.Expect(written, "検証用の一時 Scene ファイル一式を書き出せる");

        const bool broken_written =
            WriteRawFile(path_corrupt, "THIS_IS_NOT_A_SCENE 1\nGARBAGE\n") &&
            WriteRawFile(path_future, "REPLAY_SCENE 9999\nSCENE_NAME \"FromTheFuture\"\n");
        check.Expect(broken_written,
            "壊れた Scene と未対応 Version の Scene を用意できる");

        if (!written || !broken_written)
        {
            std::fprintf(stderr, "  Scene 準備に失敗: %s\n", error.c_str());
            return check.Report("Runtime scene validation");
        }

        // ---- サービスの組み立て ---------------------------------------------

        RuntimeSceneService service;
        RuntimeContext runtime(service.ActiveWorld());
        service.ActiveWorld().Services().SetRuntime(&runtime);
        service.SetRuntimeContext(&runtime);

        CollisionEventDispatcher dispatcher;
        service.SetCollisionDispatcher(&dispatcher);

        ScriptedPhysics physics;

        const Core::WorldInstanceID initial_world = service.ActiveWorldID();

        // 空の World にも GameObject を 1 つ置き、
        // 「入れ替えで消えること」を数で確かめられるようにする。
        Core::GameObject* bootstrap = service.ActiveWorld().CreateGameObject("Bootstrap");
        const ObjectHandle bootstrap_handle = runtime.Resolver().MakeHandle(bootstrap);

        check.Expect(service.State() == SceneLoadState::Idle &&
            service.ActiveWorld().GameObjectCount() == 1 && !service.IsBusy(),
            "初期状態では World が 1 つあり、State は Idle");

        // ---- Resolver 未接続 -------------------------------------------------

        service.RequestLoad(guid_scene_a);
        service.Tick();
        check.Expect(service.State() == SceneLoadState::Failed &&
            service.LastStatus() == RuntimeStatus::ServiceUnavailable,
            "Scene Asset Resolver 未接続の読み込みは ServiceUnavailable で失敗する");
        check.Expect(service.ActiveWorldID() == initial_world &&
            service.ActiveWorld().GameObjectCount() == 1 &&
            runtime.IsValid(bootstrap_handle),
            "未接続で失敗しても現在の World と Handle は無傷");

        // ---- Resolver を接続 -------------------------------------------------

        TestSceneAssetResolver resolver;
        resolver.Map(guid_scene_a, path_a);
        resolver.Map(guid_scene_b, path_b);
        resolver.Map(guid_missing_component, path_missing);
        resolver.Map(guid_unknown_property, path_unknown);
        resolver.Map(guid_reference, path_reference);
        resolver.Map(guid_corrupt, path_corrupt);
        resolver.Map(guid_future_version, path_future);
        resolver.MapWrongType(guid_wrong_asset_type);
        service.SetAssetResolver(&resolver);

        // ---- Scene A の読み込み ----------------------------------------------

        check.Expect(service.RequestLoad(guid_scene_a) == SceneRequestResult::Accepted,
            "Scene A の読み込み要求が受理される");

        service.Tick();
        check.Expect(service.State() == SceneLoadState::ReadyToSwap,
            "1 回目の Tick で Staging World が完成し ReadyToSwap になる");
        check.Expect(service.IsBusy() && service.ActiveWorldID() == initial_world,
            "ReadyToSwap の間はまだ入れ替わっておらず IsBusy");

        service.Tick();
        check.Expect(service.State() == SceneLoadState::Completed && !service.IsBusy(),
            "2 回目の Tick で入れ替えが済み Completed になる");
        check.Expect(service.CurrentSceneGUID() == guid_scene_a &&
            service.PendingSceneGUID().empty(),
            "CurrentSceneGUID が要求した GUID になり Pending が空になる");

        Core::GameObject* alpha = Find(service, "Alpha");
        Core::GameObject* beta = Find(service, "Beta");
        check.Expect(service.ActiveWorld().GameObjectCount() == 3 &&
            alpha != nullptr && beta != nullptr && beta->Parent() == alpha,
            "Scene A の GameObject 数と親子関係が復元される");

        SceneProbeBehaviour* alpha_probe = FindProbe(service, "Alpha");
        check.Expect(alpha_probe != nullptr && alpha_probe->marker == 11,
            "Behaviour の Property が復元されている");
        check.Expect(alpha_probe != nullptr && alpha_probe->AwakeCalled() &&
            SceneProbeBehaviour::live_count == 2,
            "入れ替え後に Scene::Start() が通り OnAwake が全 Behaviour へ届く");

        check.Expect(!runtime.IsValid(bootstrap_handle),
            "入れ替え前に取った Handle は無効になる");

        const ObjectHandle alpha_handle = runtime.Resolver().MakeHandle(alpha);
        const Core::WorldInstanceID world_a = service.ActiveWorldID();
        check.Expect(runtime.IsValid(alpha_handle) && world_a != initial_world,
            "新しい World の Handle が有効で World 実体番号が変わっている");

        // ---- 入れ替えで捨てられるべき状態を作る ---------------------------------

        int scene_event_hits = 0;
        ScopedSubscription scene_subscription = runtime.Events().Subscribe(
            SceneProbeBehaviour::StaticTypeGUID(),
            [&scene_event_hits](const EventRecord&) { ++scene_event_hits; });
        check.Expect(scene_subscription.Valid() &&
            runtime.Events().SubscriberCount() == 1,
            "切替前の World で Event を購読できる");

        Core::GameObject* character = Find(service, "Character");
        auto* collider = character != nullptr
            ? character->GetComponent<Components::SphereColliderComponent>() : nullptr;
        auto* motor = character != nullptr
            ? character->GetComponent<Components::CharacterMotorComponent>() : nullptr;
        if (collider != nullptr && motor != nullptr && alpha != nullptr)
        {
            motor->SetPrimaryCollider(*collider);
            physics.ground_hit = true;
            physics.ground_object = alpha->ID();
            service.ActiveWorld().Services().SetPhysics(&physics);
            service.ActiveWorld().FixedUpdate(fixed_step);
            dispatcher.Dispatch(service.ActiveWorld(), 1);
        }
        check.Expect(dispatcher.ActiveContactCount() > 0,
            "切替前の World で Collision の接触状態を作れる");

        // 削除予約を立てたまま切り替える。
        // 予約の回収前に World ごと消えるので、宙に浮いた予約が残らないことを確かめる。
        service.ActiveWorld().DestroyGameObject(beta);

        // ---- Scene A -> Scene B ------------------------------------------------

        check.Expect(service.RequestLoad(guid_scene_b) == SceneRequestResult::Accepted,
            "削除予約中でも Scene B への切替要求が受理される");
        service.Tick();
        service.Tick();

        const Core::WorldInstanceID world_b = service.ActiveWorldID();
        check.Expect(service.State() == SceneLoadState::Completed &&
            service.CurrentSceneGUID() == guid_scene_b && world_b != world_a,
            "切替が完了し World 実体番号が変わる");
        check.Expect(service.ActiveWorld().GameObjectCount() == 1 &&
            Find(service, "Solo") != nullptr && Find(service, "Alpha") == nullptr,
            "削除予約を跨いでも Scene B の内容だけが残る");
        check.Expect(!runtime.IsValid(alpha_handle),
            "Scene A の Handle は切替後に無効になる");

        SceneProbeBehaviour* solo_probe = FindProbe(service, "Solo");
        const ObjectHandle solo_handle =
            runtime.Resolver().MakeHandle(Find(service, "Solo"));
        check.Expect(solo_probe != nullptr && solo_probe->marker == 99 &&
            runtime.IsValid(solo_handle),
            "Scene B の Handle と Property が有効");
        check.Expect(SceneProbeBehaviour::live_count == 1 &&
            SceneProbeBehaviour::destroy_total >= 2,
            "旧 World の Behaviour へ OnDestroy が届き生存数が持ち越されない");
        check.Expect(runtime.Events().SubscriberCount() == 0 && scene_event_hits == 0,
            "旧 World の Event 購読が新しい World へ漏れない");
        check.Expect(dispatcher.ActiveContactCount() == 0,
            "旧 World の Collision 接触状態が新しい World へ漏れない");

        // ---- 同一 Scene の Reload ------------------------------------------------

        const std::uint64_t swaps_before_reload = service.SwapCount();
        check.Expect(service.RequestReload() == SceneRequestResult::Accepted,
            "同一 Scene の Reload 要求が受理される");
        service.Tick();
        service.Tick();
        const Core::WorldInstanceID world_reload = service.ActiveWorldID();
        check.Expect(service.State() == SceneLoadState::Completed &&
            service.CurrentSceneGUID() == guid_scene_b,
            "Reload しても CurrentSceneGUID は変わらない");
        check.Expect(world_reload != world_b &&
            service.SwapCount() == swaps_before_reload + 1,
            "Reload で World 実体が作り直され入れ替え回数が増える");
        check.Expect(!runtime.IsValid(solo_handle),
            "Reload 前に取った Handle は無効になる");

        // ---- 失敗経路 --------------------------------------------------------------
        //
        // どの失敗でも現在の World には触れない。
        // 触れていないことを、World 実体番号・GameObject 数・Handle の 3 点で確かめる。

        const ObjectHandle survivor = runtime.Resolver().MakeHandle(Find(service, "Solo"));
        const std::size_t count_before_failures = service.ActiveWorld().GameObjectCount();
        const std::uint64_t failures_before = service.FailureCount();

        service.RequestLoad(guid_not_registered);
        service.Tick();
        check.Expect(service.State() == SceneLoadState::Failed &&
            service.LastStatus() == RuntimeStatus::AssetMissing,
            "存在しない AssetGUID は AssetMissing で失敗する");

        service.RequestLoad(guid_wrong_asset_type);
        service.Tick();
        check.Expect(service.State() == SceneLoadState::Failed &&
            service.LastStatus() == RuntimeStatus::InvalidAssetType,
            "Scene Asset ではない GUID は InvalidAssetType で失敗する");

        service.RequestLoad(guid_corrupt);
        service.Tick();
        check.Expect(service.State() == SceneLoadState::Failed &&
            service.LastStatus() == RuntimeStatus::SceneLoadFailed,
            "壊れた Scene は SceneLoadFailed で失敗する");

        service.RequestLoad(guid_future_version);
        service.Tick();
        check.Expect(service.State() == SceneLoadState::Failed &&
            service.LastStatus() == RuntimeStatus::SceneLoadFailed,
            "未対応 Version の Scene は SceneLoadFailed で失敗する");

        check.Expect(service.ActiveWorldID() == world_reload &&
            service.ActiveWorld().GameObjectCount() == count_before_failures,
            "読み込みに失敗しても現在の World は入れ替わらない");
        check.Expect(runtime.IsValid(survivor),
            "読み込みに失敗しても既存の ObjectHandle は無効化されない");
        check.Expect(service.CurrentSceneGUID() == guid_scene_b &&
            service.PendingSceneGUID().empty(),
            "読み込みに失敗しても CurrentSceneGUID は維持される");
        check.Expect(service.FailureCount() == failures_before + 4 &&
            !service.LastFailedStage().empty() && !service.LastError().empty(),
            "失敗した段階と件数が診断として残る");

        check.Expect(service.RequestLoad(std::string()) ==
            SceneRequestResult::InvalidRequest,
            "空の AssetGUID は要求そのものが InvalidRequest");

        // ---- Missing Component を含む Scene ---------------------------------------

        service.RequestLoad(guid_missing_component);
        service.Tick();
        service.Tick();
        check.Expect(service.State() == SceneLoadState::Completed &&
            service.LastLoadReport().missing_components >= 1 &&
            service.LastLoadReport().skipped_components == 0,
            "Missing Component を含む Scene は読み込みが成功し保持数が記録される");

        Core::GameObject* ghost_owner = Find(service, "Ghost");
        Core::Component* ghost = ghost_owner != nullptr
            ? ghost_owner->FindComponent(Core::MissingComponent::StaticTypeID()) : nullptr;
        const auto* missing = dynamic_cast<const Core::MissingComponent*>(ghost);
        check.Expect(missing != nullptr &&
            missing->Original().type_name == ghost_type_name &&
            missing->Original().module_id == "Game.NotLoaded" &&
            missing->Original().properties.Size() == 2,
            "未知の型は元の型名・モジュール・プロパティごと MissingComponent へ預けられる");

        // ---- Unknown Property を含む Scene ------------------------------------------

        service.RequestLoad(guid_unknown_property);
        service.Tick();
        service.Tick();
        check.Expect(service.State() == SceneLoadState::Completed &&
            service.LastLoadReport().unknown_properties >= 1,
            "未知のプロパティを含む Scene は読み込みが成功し件数が記録される");

        SceneProbeBehaviour* holder = FindProbe(service, "Holder");
        check.Expect(holder != nullptr && holder->marker == 7,
            "未知のプロパティがあっても既知のプロパティは正しく反映される");

        // ---- 参照の解決 ----------------------------------------------------------------

        service.RequestLoad(guid_reference);
        service.Tick();
        service.Tick();
        SceneProbeBehaviour* source_probe = FindProbe(service, "Source");
        const ObjectHandle referenced = source_probe != nullptr
            ? runtime.Resolver().FindByObjectID(source_probe->target_object.object)
            : ObjectHandle::None();
        check.Expect(service.State() == SceneLoadState::Completed &&
            source_probe != nullptr && runtime.IsValid(referenced),
            "保存された ObjectReference を読み込み後の World で解決できる");

        const ComponentHandle referenced_component = source_probe != nullptr
            ? runtime.Resolver().FindComponentByStableID(referenced,
                source_probe->target_component.component)
            : ComponentHandle::None();
        check.Expect(source_probe != nullptr &&
            source_probe->target_component.owner == ObjectID{ 2 } &&
            runtime.Resolver().IsValid(referenced_component),
            "保存された ComponentReference を StableID から解決できる");

        // ---- 連続要求と Busy -------------------------------------------------------------

        check.Expect(service.RequestLoad(guid_scene_a) == SceneRequestResult::Accepted &&
            service.RequestLoad(guid_scene_b) == SceneRequestResult::Busy,
            "進行中の読み込み要求は Busy で拒否され、先の要求を壊さない");

        service.Tick();
        check.Expect(service.State() == SceneLoadState::ReadyToSwap &&
            service.RequestLoad(guid_scene_b) == SceneRequestResult::Busy &&
            service.RequestReload() == SceneRequestResult::Busy,
            "ReadyToSwap の間の要求も Busy で拒否される");

        service.Tick();
        check.Expect(service.CurrentSceneGUID() == guid_scene_a &&
            service.ActiveWorld().GameObjectCount() == 3,
            "連続要求のあとも最初に受理した Scene だけが読み込まれる");

        // ---- CancelPending ----------------------------------------------------------------

        const Core::WorldInstanceID world_before_cancel = service.ActiveWorldID();
        service.RequestLoad(guid_scene_b);
        service.CancelPending();
        service.Tick();
        service.Tick();
        check.Expect(service.State() == SceneLoadState::Idle &&
            service.ActiveWorldID() == world_before_cancel &&
            service.CurrentSceneGUID() == guid_scene_a &&
            service.PendingSceneGUID().empty(),
            "CancelPending で Idle へ戻り、以降 Tick しても World が入れ替わらない");

        // Resolver が「推測でパスを組み立てていない」ことの裏取り。
        // 解決要求は必ずこのテスト実装を通っている。
        std::fprintf(stderr, "Runtime scene validation: resolver calls=%zu, loads=%llu, "
            "swaps=%llu, failures=%llu\n",
            resolver.ResolveCallCount(),
            static_cast<unsigned long long>(service.LoadCount()),
            static_cast<unsigned long long>(service.SwapCount()),
            static_cast<unsigned long long>(service.FailureCount()));

        return check.Report("Runtime scene validation");
    }
}
