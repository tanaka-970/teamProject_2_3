#include "StressValidation.h"

#include "../API/RuntimeContext.h"
#include "../Behaviour/BehaviourComponent.h"
#include "../Behaviour/BehaviourRegistry.h"
#include "../Events/CollisionEventDispatcher.h"
#include "../Events/EventBus.h"
#include "../Scene/RuntimeSceneService.h"
#include "../Scene/SceneFlowService.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Object/Registry/BuiltInComponents.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Reflection/Registry/PropertyRegistry.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Scene/Serialization/SceneData.h"
#include "../../Scene/Serialization/SceneSerializer.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace ReplayEngine::Runtime::Validation
{
    namespace
    {
        namespace Serialization = Scene::Serialization;
        using Core::ObjectID;

        // 定数は名前空間スコープへ置く（MSVC C3493 回避）。
        constexpr const char* guid_a = "5721e550000000000000000000000001";
        constexpr const char* guid_b = "5721e550000000000000000000000002";
        constexpr const char* guid_large = "5721e550000000000000000000000003";
        constexpr const char* guid_broken = "5721e550000000000000000000000004";
        constexpr const char* guid_absent = "5721e5500000000000000000000000ff";

        constexpr int load_cycles = 120;          // 100 回以上
        constexpr int failure_cycles = 120;       // 100 回以上
        constexpr int play_cycles = 40;
        constexpr int large_object_count = 1200;  // 1000 以上

        class Checker final
        {
        public:
            explicit Checker(int first_code) : next_code_(first_code) {}

            void Expect(bool condition, const char* what)
            {
                const int code = next_code_++;
                ++total_;
                if (condition) return;
                ++failures_;
                if (first_failure_ == 0) first_failure_ = code;
                std::fprintf(stderr, "  [FAIL %d] %s\n", code, what);
            }

            int Report(const char* title) const
            {
                if (first_failure_ == 0)
                {
                    std::fprintf(stderr, "%s OK: %d checks passed\n", title, total_);
                    return 0;
                }
                std::fprintf(stderr, "%s FAILED: %d/%d checks failed (first=%d)\n",
                    title, failures_, total_, first_failure_);
                return first_failure_;
            }

        private:
            int next_code_ = 0;
            int first_failure_ = 0;
            int total_ = 0;
            int failures_ = 0;
        };

        class StressProbeBehaviour final : public BehaviourComponent
        {
            REPLAY_COMPONENT_BODY(StressProbeBehaviour)

        public:
            static constexpr Reflection::TypeGUID StaticTypeGUID() noexcept
            {
                return Reflection::MakeTypeGUID("57000000000000000000000000000001");
            }

            int marker = 0;

            static int live_count;
            static int awake_total;
            static int destroy_total;

            static void ResetCounters() noexcept
            {
                live_count = 0;
                awake_total = 0;
                destroy_total = 0;
            }

        protected:
            void OnAwake() override { ++awake_total; ++live_count; }
            void OnDestroy() override { ++destroy_total; --live_count; }
        };

        int StressProbeBehaviour::live_count = 0;
        int StressProbeBehaviour::awake_total = 0;
        int StressProbeBehaviour::destroy_total = 0;

        void RegisterProbe()
        {
            Core::RegisterBuiltInComponents();
            Core::ComponentRegistry::Register<StressProbeBehaviour>(
                Core::ComponentTypeInfo::Describe("Stress Probe", "Internal")
                    .HiddenInEditor()
                    .WithTypeGUID(StressProbeBehaviour::StaticTypeGUID())
                    .InModule("RePlayEngine.Validation"));
            Reflection::PropertyRegistry::Register<StressProbeBehaviour>(
                Reflection::MakeProperty("marker", &StressProbeBehaviour::marker));
            BehaviourRegistry::Register(StressProbeBehaviour::StaticTypeGUID(),
                BehaviourRegistry::Native());
        }

        class TestSceneAssetResolver final : public ISceneAssetResolver
        {
        public:
            void Map(std::string guid, const std::filesystem::path& path)
            {
                Entry entry;
                entry.guid = std::move(guid);
                entry.path = path.string();
                entries_.push_back(std::move(entry));
            }

            RuntimeStatus ResolveScenePath(const std::string& asset_guid,
                std::string& out_path) const override
            {
                for (const Entry& entry : entries_)
                {
                    if (entry.guid != asset_guid) continue;
                    out_path = entry.path;
                    return RuntimeStatus::Ok;
                }
                return RuntimeStatus::AssetMissing;
            }

        private:
            struct Entry { std::string guid; std::string path; };
            std::vector<Entry> entries_;
        };

        Serialization::ComponentData MakeProbe(int marker)
        {
            Serialization::ComponentData component;
            component.type_name = StressProbeBehaviour::StaticTypeName();
            component.type_id = StressProbeBehaviour::StaticTypeID();
            component.type_guid = StressProbeBehaviour::StaticTypeGUID();
            component.stable_id = 1;
            component.properties.Set("marker", Reflection::PropertyValue::MakeInt(marker));
            return component;
        }

        Serialization::SceneData BuildScene(const char* name, int object_count,
            bool with_probe)
        {
            Serialization::SceneData data;
            data.scene_name = name;
            for (int index = 0; index < object_count; ++index)
            {
                Serialization::GameObjectData object;
                object.id = ObjectID{ static_cast<ObjectID::ValueType>(index + 1) };
                object.name = std::string(name) + "_" + std::to_string(index);
                if (with_probe) object.components.push_back(MakeProbe(index));
                data.objects.push_back(std::move(object));
            }
            return data;
        }

        bool WriteRawFile(const std::filesystem::path& path, const char* text)
        {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            if (!stream) return false;
            stream << text;
            return static_cast<bool>(stream);
        }
    }

    // =====================================================================
    // 反復・大量データの耐久検査
    // =====================================================================

    int RunStressValidation()
    {
        RegisterProbe();
        StressProbeBehaviour::ResetCounters();
        Checker check(580);

        const std::filesystem::path folder =
            std::filesystem::path("Saved") / "Validation" / "Stress";
        std::error_code directory_error;
        std::filesystem::create_directories(folder, directory_error);

        const std::filesystem::path path_a = folder / "A.replayscene";
        const std::filesystem::path path_b = folder / "B.replayscene";
        const std::filesystem::path path_large = folder / "Large.replayscene";
        const std::filesystem::path path_broken = folder / "Broken.replayscene";

        std::string error;
        const bool written = !directory_error &&
            Serialization::SceneSerializer::SaveToFile(
                BuildScene("A", 4, true), path_a, error) &&
            Serialization::SceneSerializer::SaveToFile(
                BuildScene("B", 2, true), path_b, error) &&
            Serialization::SceneSerializer::SaveToFile(
                BuildScene("L", large_object_count, false), path_large, error) &&
            WriteRawFile(path_broken, "NOT_A_SCENE 1\n");
        check.Expect(written, "耐久検査用の一時 Scene を書き出せる");
        if (!written)
        {
            std::fprintf(stderr, "  Scene 準備に失敗: %s\n", error.c_str());
            return check.Report("Stress validation");
        }

        TestSceneAssetResolver resolver;
        resolver.Map(guid_a, path_a);
        resolver.Map(guid_b, path_b);
        resolver.Map(guid_large, path_large);
        resolver.Map(guid_broken, path_broken);

        RuntimeSceneService scenes;
        RuntimeContext runtime(scenes.ActiveWorld());
        scenes.ActiveWorld().Services().SetRuntime(&runtime);
        scenes.SetRuntimeContext(&runtime);
        scenes.SetAssetResolver(&resolver);

        CollisionEventDispatcher dispatcher;
        scenes.SetCollisionDispatcher(&dispatcher);

        SceneFlowService flow(scenes);
        runtime.SetSceneFlow(&flow);

        // ---- 100 回以上の Load / Reload ------------------------------------

        std::vector<ObjectHandle> stale_handles;
        std::vector<Core::WorldInstanceID> seen_worlds;
        bool cycle_ok = true;

        for (int cycle = 0; cycle < load_cycles; ++cycle)
        {
            const char* target = (cycle % 3 == 2) ? nullptr : ((cycle % 2 == 0) ? guid_a : guid_b);

            // 3 回に 1 回は Reload にして、両方の経路を通す。
            const SceneRequestResult request = target != nullptr
                ? scenes.RequestLoad(target) : scenes.RequestReload();
            if (request != SceneRequestResult::Accepted) { cycle_ok = false; break; }

            // 切り替え直前に Handle を取り、切り替え後に無効化されることを確かめる。
            if (scenes.ActiveWorld().GameObjectCount() > 0)
            {
                stale_handles.push_back(runtime.Resolver().MakeHandle(
                    scenes.ActiveWorld().GameObjectAt(0)));
            }

            // 削除予約を立てたまま切り替える回も混ぜる。
            if (cycle % 5 == 0 && scenes.ActiveWorld().GameObjectCount() > 0)
            {
                scenes.ActiveWorld().DestroyGameObject(
                    scenes.ActiveWorld().GameObjectAt(0));
            }

            // 旧 World の EventBus へ購読を積む。入れ替えで捨てられるはず。
            ScopedSubscription token = runtime.Events().Subscribe(
                StressProbeBehaviour::StaticTypeGUID(), [](const EventRecord&) {});
            (void)token;

            scenes.TickBlocking();
            scenes.TickBlocking();

            if (scenes.State() != SceneLoadState::Completed) { cycle_ok = false; break; }
            seen_worlds.push_back(scenes.ActiveWorldID());

            // 毎回ここで漏れが無いことを確かめる。
            // 最後にまとめて見ると、どの周回で漏れ始めたか分からなくなる。
            if (runtime.Events().SubscriberCount() != 0) { cycle_ok = false; break; }
            if (dispatcher.ActiveContactCount() != 0) { cycle_ok = false; break; }
        }

        check.Expect(cycle_ok, "100 回以上の Scene Load / Reload を続けられる");
        check.Expect(scenes.SwapCount() >= static_cast<std::uint64_t>(load_cycles),
            "反復した回数だけ World が入れ替わっている");
        check.Expect(runtime.Events().SubscriberCount() == 0,
            "反復後に Event 購読が 1 件も残っていない");
        check.Expect(dispatcher.ActiveContactCount() == 0,
            "反復後に Collision の接触状態が残っていない");
        check.Expect(StressProbeBehaviour::live_count ==
            static_cast<int>(scenes.ActiveWorld().GameObjectCount()),
            "生きている Behaviour 数が現在の World の GameObject 数と一致する");
        check.Expect(StressProbeBehaviour::awake_total ==
            StressProbeBehaviour::destroy_total + StressProbeBehaviour::live_count,
            "Awake と Destroy の総数が釣り合う（Behaviour の取りこぼしが無い）");

        bool worlds_unique = true;
        for (std::size_t i = 1; i < seen_worlds.size(); ++i)
        {
            if (seen_worlds[i] == seen_worlds[i - 1]) worlds_unique = false;
        }
        check.Expect(worlds_unique,
            "入れ替えのたびに World 実体番号が必ず変わる（実体の使い回しが無い）");

        bool all_stale_invalid = true;
        for (const ObjectHandle& handle : stale_handles)
        {
            if (runtime.IsValid(handle)) all_stale_invalid = false;
        }
        check.Expect(!stale_handles.empty() && all_stale_invalid,
            "切り替え前に取った ObjectHandle がすべて無効になっている");

        // ---- 連続した失敗 Load ----------------------------------------------

        const Core::WorldInstanceID world_before_failures = scenes.ActiveWorldID();
        const std::size_t count_before_failures = scenes.ActiveWorld().GameObjectCount();
        const ObjectHandle survivor = scenes.ActiveWorld().GameObjectCount() > 0
            ? runtime.Resolver().MakeHandle(scenes.ActiveWorld().GameObjectAt(0))
            : ObjectHandle::None();
        const std::uint64_t failures_before = scenes.FailureCount();

        bool failures_ok = true;
        for (int cycle = 0; cycle < failure_cycles; ++cycle)
        {
            const char* target = (cycle % 2 == 0) ? guid_broken : guid_absent;
            if (scenes.RequestLoad(target) != SceneRequestResult::Accepted)
            {
                failures_ok = false;
                break;
            }
            scenes.TickBlocking();
            if (scenes.State() != SceneLoadState::Failed) { failures_ok = false; break; }

            // 失敗のたびに現在の World が無傷であることを確かめる。
            if (scenes.ActiveWorldID() != world_before_failures) { failures_ok = false; break; }
            if (scenes.ActiveWorld().GameObjectCount() != count_before_failures)
            {
                failures_ok = false;
                break;
            }
        }

        check.Expect(failures_ok, "100 回以上連続して読み込みに失敗しても壊れない");
        check.Expect(scenes.FailureCount() ==
            failures_before + static_cast<std::uint64_t>(failure_cycles),
            "失敗回数が正しく数えられている");
        check.Expect(scenes.ActiveWorldID() == world_before_failures &&
            scenes.ActiveWorld().GameObjectCount() == count_before_failures,
            "連続失敗のあとも現在の World が維持されている");
        check.Expect(survivor.IsEmpty() || runtime.IsValid(survivor),
            "連続失敗のあとも既存の ObjectHandle が有効なまま");

        // ---- 1000 以上の GameObject ------------------------------------------

        check.Expect(scenes.RequestLoad(guid_large) == SceneRequestResult::Accepted,
            "大量 GameObject の Scene 読み込み要求が受理される");
        scenes.TickBlocking();
        scenes.TickBlocking();
        check.Expect(scenes.State() == SceneLoadState::Completed &&
            scenes.ActiveWorld().GameObjectCount() ==
                static_cast<std::size_t>(large_object_count),
            "1000 以上の GameObject を持つ Scene を読み込める");

        // 大量 World をそのまま切り替えて、破棄側も耐えることを確かめる。
        scenes.RequestLoad(guid_a);
        scenes.TickBlocking();
        scenes.TickBlocking();
        check.Expect(scenes.State() == SceneLoadState::Completed &&
            scenes.ActiveWorld().GameObjectCount() == 4,
            "大量 GameObject の World から切り替えられる");

        // ---- Play 開始 / 停止の反復 --------------------------------------------

        Scene::Scene editor_scene("EditorScene");
        for (int index = 0; index < 8; ++index)
        {
            Core::GameObject* object =
                editor_scene.CreateGameObject("Edit_" + std::to_string(index));
            if (object != nullptr) object->AddComponent(StressProbeBehaviour::StaticTypeID());
        }
        const std::size_t editor_count = editor_scene.GameObjectCount();

        bool play_ok = true;
        for (int cycle = 0; cycle < play_cycles; ++cycle)
        {
            Serialization::SceneData snapshot;
            Serialization::CaptureScene(editor_scene, snapshot);

            if (scenes.RequestAdopt(snapshot, std::string()) !=
                SceneRequestResult::Accepted)
            {
                play_ok = false;
                break;
            }
            scenes.TickBlocking();
            scenes.TickBlocking();
            if (scenes.State() != SceneLoadState::Completed) { play_ok = false; break; }
            if (scenes.ActiveWorld().GameObjectCount() != editor_count)
            {
                play_ok = false;
                break;
            }

            // Play 中に World を汚す。停止で消えるはず。
            scenes.ActiveWorld().CreateGameObject("SpawnedAtRuntime");

            scenes.ResetToEmptyWorld();
            if (scenes.ActiveWorld().GameObjectCount() != 0) { play_ok = false; break; }
        }

        check.Expect(play_ok, "Play 開始と停止を繰り返しても壊れない");
        check.Expect(editor_scene.GameObjectCount() == editor_count &&
            editor_scene.FindGameObjectByName("SpawnedAtRuntime") == nullptr,
            "Play を繰り返しても編集 Scene が汚れない");
        check.Expect(runtime.Events().SubscriberCount() == 0 &&
            dispatcher.ActiveContactCount() == 0,
            "Play の反復後も購読と接触状態が残らない");
        check.Expect(scenes.ActiveSceneGuid().empty(),
            "Play 停止後は Runtime Scene GUID が未設定へ戻る");

        // ---- SceneFlow の反復と履歴上限 -----------------------------------------

        bool flow_ok = true;
        for (int cycle = 0; cycle < load_cycles; ++cycle)
        {
            const char* target = (cycle % 2 == 0) ? guid_a : guid_b;
            if (Failed(flow.LoadScene(std::string(target)))) { flow_ok = false; break; }
            flow.TickBlocking();
            flow.TickBlocking();
            if (flow.CurrentTransitionState() != SceneTransitionState::Completed)
            {
                flow_ok = false;
                break;
            }
            if (flow.History().size() > SceneFlowService::maximum_history)
            {
                flow_ok = false;
                break;
            }
        }
        check.Expect(flow_ok, "SceneFlow 経由でも 100 回以上の遷移に耐える");
        check.Expect(flow.History().size() == SceneFlowService::maximum_history,
            "遷移を繰り返しても履歴が上限で頭打ちになる");

        // 往復を繰り返しても履歴が伸びない。
        std::size_t history_before_returns = flow.History().size();
        bool return_ok = true;
        for (int cycle = 0; cycle < 8; ++cycle)
        {
            if (!flow.CanReturn()) { return_ok = false; break; }
            if (Failed(flow.ReturnToPreviousScene())) { return_ok = false; break; }
            flow.TickBlocking();
            flow.TickBlocking();
            if (flow.History().size() != history_before_returns - 1)
            {
                return_ok = false;
                break;
            }
            history_before_returns = flow.History().size();
        }
        check.Expect(return_ok, "戻る操作を繰り返しても履歴が 1 件ずつ減る");

        // ---- 終了要求 ------------------------------------------------------------

        check.Expect(Succeeded(flow.QuitApplication("stress")) && flow.QuitRequested(),
            "終了要求は記録され、プロセスは終了しない");
        flow.ClearQuitRequest();
        check.Expect(!flow.QuitRequested(), "終了要求を受け取り済みにできる");

        std::fprintf(stderr,
            "Stress validation: swaps=%llu failures=%llu awake=%d destroy=%d live=%d\n",
            static_cast<unsigned long long>(scenes.SwapCount()),
            static_cast<unsigned long long>(scenes.FailureCount()),
            StressProbeBehaviour::awake_total, StressProbeBehaviour::destroy_total,
            StressProbeBehaviour::live_count);

        return check.Report("Stress validation");
    }
}
