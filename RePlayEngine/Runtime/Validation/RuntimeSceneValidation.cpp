#include "RuntimeSceneValidation.h"

#include "../API/RuntimeContext.h"
#include "../Behaviour/BehaviourComponent.h"
#include "../Behaviour/BehaviourRegistry.h"
#include "../Core/RuntimeResult.h"
#include "../Events/CollisionEventDispatcher.h"
#include "../Events/EventBus.h"
#include "../Handles/HandleResolver.h"
#include "../Scene/RuntimeSceneService.h"
#include "../../Components/Gameplay/CharacterMotorComponent.h"
#include "../../Components/Physics/SphereColliderComponent.h"
#include "../../Object/Component/MissingComponent.h"
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
        using Reflection::PropertyValue;

        // ------------------------------------------------------------------
        // 定数は名前空間スコープへ置く。
        //
        // 関数ローカルの constexpr をキャプチャ無しラムダから参照すると
        // MSVC が C3493 で落ちるため、この検証で使う固定値はすべてここに集める。
        // ------------------------------------------------------------------

        constexpr const char* guid_scene_a = "5ce4e000000000000000000000000a01";
        constexpr const char* guid_scene_b = "5ce4e000000000000000000000000b01";
        constexpr const char* guid_missing_component = "5ce4e000000000000000000000000c01";
        constexpr const char* guid_unknown_property = "5ce4e000000000000000000000000d01";
        constexpr const char* guid_reference = "5ce4e000000000000000000000000e01";
        constexpr const char* guid_corrupt = "5ce4e000000000000000000000000f01";
        constexpr const char* guid_future_version = "5ce4e00000000000000000000000fd01";
        constexpr const char* guid_not_registered = "5ce4e00000000000000000000000ff01";
        constexpr const char* guid_wrong_asset_type = "5ce4e00000000000000000000000fe01";

        constexpr const char* ghost_type_name = "ValidationGhostComponent";

        constexpr float fixed_step = 1.0f / 60.0f;

        // 検査の記録係。最初の失敗で打ち切らず、全項目を実行してから
        // 最初の失敗番号を返す。1 回のビルド確認で見つかる不具合を増やすため。
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

        // ------------------------------------------------------------------
        // 検証専用の Behaviour
        //
        // 本番コードへは登録せず、この検証の中だけで Registry へ入れる。
        // World の入れ替えで OnAwake / OnDestroy が正しく流れることを、
        // 全インスタンス共通のカウンタで観測する。
        // ------------------------------------------------------------------
        class SceneProbeBehaviour final : public BehaviourComponent
        {
            REPLAY_COMPONENT_BODY(SceneProbeBehaviour)

        public:
            static constexpr Reflection::TypeGUID StaticTypeGUID() noexcept
            {
                return Reflection::MakeTypeGUID("c0000000000000000000000000000010");
            }

            // 保存・復元を確かめるための値。
            int marker = 0;

            // 参照解決を確かめるための保存済み参照。
            Reflection::ObjectReference target_object;
            Reflection::ComponentReference target_component;

            static int awake_total;
            static int destroy_total;
            static int live_count;

            static void ResetCounters() noexcept
            {
                awake_total = 0;
                destroy_total = 0;
                live_count = 0;
            }

        protected:
            void OnAwake() override
            {
                ++awake_total;
                ++live_count;
            }
            void OnDestroy() override
            {
                ++destroy_total;
                --live_count;
            }
        };

        int SceneProbeBehaviour::awake_total = 0;
        int SceneProbeBehaviour::destroy_total = 0;
        int SceneProbeBehaviour::live_count = 0;

        void RegisterProbe()
        {
            Core::RegisterBuiltInComponents();
            Core::ComponentRegistry::Register<SceneProbeBehaviour>(
                Core::ComponentTypeInfo::Describe("Runtime Scene Probe", "Internal")
                    .HiddenInEditor()
                    .WithTypeGUID(SceneProbeBehaviour::StaticTypeGUID())
                    .InModule("RePlayEngine.Validation"));

            Reflection::PropertyRegistry::Register<SceneProbeBehaviour>(
                Reflection::MakeProperty("marker", &SceneProbeBehaviour::marker)
                    .Display("目印"));
            Reflection::PropertyRegistry::Register<SceneProbeBehaviour>(
                Reflection::MakeProperty("target_object",
                    &SceneProbeBehaviour::target_object).Display("参照先 GameObject"));
            Reflection::PropertyRegistry::Register<SceneProbeBehaviour>(
                Reflection::MakeProperty("target_component",
                    &SceneProbeBehaviour::target_component).Display("参照先 Component"));

            BehaviourRegistry::Register(SceneProbeBehaviour::StaticTypeGUID(),
                BehaviourRegistry::Native());
        }

        // ------------------------------------------------------------------
        // 検証用の Scene Asset 解決。
        //
        // GUID からパスへの対応を検証側から差し替えられるようにする。
        // AssetDatabase を使わないので、この検証は Asset 管理の状態に依存しない。
        // ------------------------------------------------------------------
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

            // 「GUID はあるが Scene Asset ではない」状態を作る。
            void MapWrongType(std::string guid)
            {
                wrong_type_.push_back(std::move(guid));
            }

            RuntimeStatus ResolveScenePath(const std::string& asset_guid,
                std::string& out_path) const override
            {
                ++resolve_calls_;
                for (const std::string& guid : wrong_type_)
                {
                    if (guid == asset_guid) return RuntimeStatus::InvalidAssetType;
                }
                for (const Entry& entry : entries_)
                {
                    if (entry.guid != asset_guid) continue;
                    out_path = entry.path;
                    return RuntimeStatus::Ok;
                }
                // 推測でパスを組み立てない。無ければ AssetMissing で止める。
                return RuntimeStatus::AssetMissing;
            }

            std::size_t ResolveCallCount() const noexcept { return resolve_calls_; }

        private:
            struct Entry
            {
                std::string guid;
                std::string path;
            };

            std::vector<Entry> entries_;
            std::vector<std::string> wrong_type_;
            mutable std::size_t resolve_calls_ = 0;
        };

        // 返す Hit を検証側から指定できる問い合わせサービス。
        // Collision の接触状態を意図した瞬間に作るためだけに使う。
        class ScriptedPhysics final : public Scene::IPhysicsQueryService
        {
        public:
            bool ground_hit = false;
            ObjectID ground_object;
            Scene::ColliderID ground_collider = 11;

            bool CollisionAvailable() const override { return true; }

            bool QueryGround(const DirectX::XMFLOAT3& origin, float /*radius*/,
                float /*up_offset*/, float /*down_distance*/, float /*walkable_normal_y*/,
                Scene::GroundHit& hit) const override
            {
                if (!ground_hit) return false;
                hit.valid = true;
                hit.position = DirectX::XMFLOAT3{ origin.x, 0.0f, origin.z };
                hit.normal = DirectX::XMFLOAT3{ 0.0f, 1.0f, 0.0f };
                hit.source.backend = Scene::CollisionBackend::SceneCollider;
                hit.source.object = ground_object;
                hit.source.collider = ground_collider;
                return true;
            }

            bool SweepSphere(const DirectX::XMFLOAT3& /*start*/,
                const DirectX::XMFLOAT3& /*end*/, float /*radius*/,
                float /*maximum_normal_y*/, Scene::SphereSweepHit& /*hit*/) const override
            {
                return false;
            }
        };

        // ------------------------------------------------------------------
        // Scene データの組み立て
        // ------------------------------------------------------------------

        Serialization::ComponentData MakeProbeComponent(int marker,
            Core::ComponentStableID stable_id)
        {
            Serialization::ComponentData component;
            component.type_name = SceneProbeBehaviour::StaticTypeName();
            component.type_id = SceneProbeBehaviour::StaticTypeID();
            component.type_guid = SceneProbeBehaviour::StaticTypeGUID();
            component.module_id = "RePlayEngine.Validation";
            component.type_version = 1;
            component.stable_id = stable_id;
            component.properties.Set("marker", PropertyValue::MakeInt(marker));
            return component;
        }

        Serialization::ComponentData MakeSimpleComponent(const char* type_name)
        {
            Serialization::ComponentData component;
            component.type_name = type_name;
            component.type_id = Core::MakeComponentTypeID(type_name);
            component.stable_id = 1;
            return component;
        }

        Serialization::GameObjectData MakeObject(ObjectID::ValueType id,
            const char* name, ObjectID::ValueType parent = 0)
        {
            Serialization::GameObjectData object;
            object.id = ObjectID{ id };
            object.parent_id = ObjectID{ parent };
            object.name = name;
            return object;
        }

        // Scene A: 親子・Behaviour・CharacterMotor を含む「普通の Scene」。
        Serialization::SceneData BuildSceneA()
        {
            Serialization::SceneData data;
            data.scene_name = "RuntimeSceneA";

            Serialization::GameObjectData alpha = MakeObject(1, "Alpha");
            alpha.components.push_back(MakeProbeComponent(11, 1));
            data.objects.push_back(std::move(alpha));

            Serialization::GameObjectData beta = MakeObject(2, "Beta", 1);
            beta.components.push_back(MakeProbeComponent(22, 1));
            data.objects.push_back(std::move(beta));

            Serialization::GameObjectData character = MakeObject(3, "Character");
            character.components.push_back(MakeSimpleComponent(
                Components::SphereColliderComponent::StaticTypeName()));
            Serialization::ComponentData motor = MakeSimpleComponent(
                Components::CharacterMotorComponent::StaticTypeName());
            motor.stable_id = 2;
            character.components.push_back(std::move(motor));
            data.objects.push_back(std::move(character));

            return data;
        }

        // Scene B: 中身の違う別 Scene。GameObject 数で切替を判別する。
        Serialization::SceneData BuildSceneB()
        {
            Serialization::SceneData data;
            data.scene_name = "RuntimeSceneB";

            Serialization::GameObjectData solo = MakeObject(1, "Solo");
            solo.components.push_back(MakeProbeComponent(99, 1));
            data.objects.push_back(std::move(solo));

            return data;
        }

        // 登録されていない型を含む Scene。MissingComponent として預かられる。
        Serialization::SceneData BuildMissingComponentScene()
        {
            Serialization::SceneData data;
            data.scene_name = "RuntimeSceneMissing";

            Serialization::GameObjectData object = MakeObject(1, "Ghost");
            Serialization::ComponentData ghost;
            ghost.type_name = ghost_type_name;
            ghost.type_id = Core::MakeComponentTypeID(ghost_type_name);
            ghost.type_guid =
                Reflection::MakeTypeGUID("ee000000000000000000000000000001");
            ghost.module_id = "Game.NotLoaded";
            ghost.type_version = 4;
            ghost.stable_id = 1;
            ghost.properties.Set("ghost_int", PropertyValue::MakeInt(1234));
            ghost.properties.Set("ghost_text", PropertyValue::MakeString("消えてはいけない値"));
            object.components.push_back(std::move(ghost));
            data.objects.push_back(std::move(object));

            return data;
        }

        // 型は解決できるが、その型が知らないプロパティを含む Scene。
        Serialization::SceneData BuildUnknownPropertyScene()
        {
            Serialization::SceneData data;
            data.scene_name = "RuntimeSceneUnknownProperty";

            Serialization::GameObjectData object = MakeObject(1, "Holder");
            Serialization::ComponentData probe = MakeProbeComponent(7, 1);
            probe.properties.Set("ghost_property",
                PropertyValue::MakeString("未知のまま保持されるべき値"));
            object.components.push_back(std::move(probe));
            data.objects.push_back(std::move(object));

            return data;
        }

        // ObjectReference / ComponentReference を含む Scene。
        Serialization::SceneData BuildReferenceScene()
        {
            Serialization::SceneData data;
            data.scene_name = "RuntimeSceneReference";

            Serialization::GameObjectData source = MakeObject(1, "Source");
            Serialization::ComponentData probe = MakeProbeComponent(1, 1);
            probe.properties.Set("target_object",
                PropertyValue::MakeObjectReference(ObjectID{ 2 }));

            Reflection::ComponentReference reference;
            reference.owner = ObjectID{ 2 };
            reference.component = 1;
            probe.properties.Set("target_component",
                PropertyValue::MakeComponentReference(reference));
            source.components.push_back(std::move(probe));
            data.objects.push_back(std::move(source));

            Serialization::GameObjectData target = MakeObject(2, "Target");
            target.components.push_back(MakeProbeComponent(2, 1));
            data.objects.push_back(std::move(target));

            return data;
        }

        bool WriteRawFile(const std::filesystem::path& path, const char* text)
        {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            if (!stream) return false;
            stream << text;
            return static_cast<bool>(stream);
        }

        // 直近に読み込んだ World から、名前で GameObject を引く小道具。
        Core::GameObject* Find(RuntimeSceneService& service, const char* name)
        {
            return service.ActiveWorld().FindGameObjectByName(name);
        }

        SceneProbeBehaviour* FindProbe(RuntimeSceneService& service, const char* name)
        {
            Core::GameObject* object = Find(service, name);
            if (object == nullptr) return nullptr;
            return object->GetComponent<SceneProbeBehaviour>();
        }
    }

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
