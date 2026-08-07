#include "BehaviourValidation.h"

#include "../API/RuntimeContext.h"
#include "../Behaviour/BehaviourComponent.h"
#include "../Behaviour/BehaviourRegistry.h"
#include "../Events/CollisionEventDispatcher.h"
#include "../Events/EventBus.h"
#include "../Handles/HandleResolver.h"
#include "../../Components/Gameplay/CharacterMotorComponent.h"
#include "../../Components/Physics/SphereColliderComponent.h"
#include "../../Object/Registry/BuiltInComponents.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Reflection/Registry/PropertyRegistry.h"
#include "../../Scene/Runtime/Scene.h"

#include <cstdio>
#include <string>
#include <vector>

namespace ReplayEngine::Runtime::Validation
{
    namespace
    {
        using Core::ObjectID;

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

        // ライフサイクルの呼び出し順を記録するだけの検証用 Behaviour。
        // 本番コードへは登録せず、この検証の中だけで Registry へ入れる。
        class LifecycleProbeBehaviour final : public BehaviourComponent
        {
            REPLAY_COMPONENT_BODY(LifecycleProbeBehaviour)

        public:
            static constexpr Reflection::TypeGUID StaticTypeGUID() noexcept
            {
                return Reflection::MakeTypeGUID("c0000000000000000000000000000001");
            }

            // 呼ばれた順に記録する。全インスタンスで共有すると順序が混ざるため、
            // インスタンスごとに持つ。
            std::vector<std::string> calls;

            int awake_count = 0;
            int start_count = 0;
            int enable_count = 0;
            int disable_count = 0;
            int update_count = 0;
            int fixed_update_count = 0;
            int late_update_count = 0;
            int destroy_count = 0;

            // Awake の中で自分の破棄を要求するか。
            bool destroy_in_awake = false;

        protected:
            void OnAwake() override
            {
                ++awake_count;
                calls.push_back("Awake");
                if (destroy_in_awake) Destroy();
            }
            void OnEnable() override { ++enable_count; calls.push_back("Enable"); }
            void OnDisable() override { ++disable_count; calls.push_back("Disable"); }
            void OnStart() override { ++start_count; calls.push_back("Start"); }
            void OnUpdate(float) override { ++update_count; }
            void OnFixedUpdate(float) override { ++fixed_update_count; }
            void OnLateUpdate(float) override { ++late_update_count; }
            void OnDestroy() override { ++destroy_count; calls.push_back("Destroy"); }
        };

        void RegisterProbe()
        {
            Core::RegisterBuiltInComponents();
            Core::ComponentRegistry::Register<LifecycleProbeBehaviour>(
                Core::ComponentTypeInfo::Describe("Lifecycle Probe", "Internal")
                    .HiddenInEditor()
                    .AllowMultipleInstances()
                    .WithTypeGUID(LifecycleProbeBehaviour::StaticTypeGUID())
                    .InModule("RePlayEngine.Validation"));
            BehaviourRegistry::Register(LifecycleProbeBehaviour::StaticTypeGUID(),
                BehaviourRegistry::Native());
        }

        bool ContainsInOrder(const std::vector<std::string>& calls,
            const char* first, const char* second)
        {
            std::size_t first_index = calls.size();
            for (std::size_t i = 0; i < calls.size(); ++i)
            {
                if (calls[i] == first) { first_index = i; break; }
            }
            if (first_index == calls.size()) return false;
            for (std::size_t i = first_index + 1; i < calls.size(); ++i)
            {
                if (calls[i] == second) return true;
            }
            return false;
        }
    }

    // =====================================================================

    int RunBehaviourValidation()
    {
        RegisterProbe();
        Checker check(250);

        Scene::Scene world("BehaviourWorld");
        RuntimeContext runtime(world);
        world.Services().SetRuntime(&runtime);

        Core::GameObject* enabled_object = world.CreateGameObject("Enabled");
        Core::GameObject* disabled_object = world.CreateGameObject("Disabled");
        check.Expect(enabled_object != nullptr && disabled_object != nullptr,
            "検証用 GameObject を作れる");
        if (enabled_object == nullptr || disabled_object == nullptr)
        {
            return check.Report("Behaviour validation");
        }

        auto* enabled_probe = enabled_object->AddComponent<LifecycleProbeBehaviour>();
        auto* disabled_probe = disabled_object->AddComponent<LifecycleProbeBehaviour>();
        check.Expect(enabled_probe != nullptr && disabled_probe != nullptr,
            "Behaviour を GameObject へ追加できる");
        if (enabled_probe == nullptr || disabled_probe == nullptr)
        {
            return check.Report("Behaviour validation");
        }

        // 片方は最初から無効にしておく。
        disabled_probe->SetEnabled(false);

        // Scene を開始する前は何も呼ばれていないこと
        // （Editor で置いただけでは動き出さない）。
        check.Expect(enabled_probe->awake_count == 0 && enabled_probe->start_count == 0,
            "Scene 開始前は Awake も Start も呼ばれない");

        world.Start();

        check.Expect(enabled_probe->awake_count == 1, "有効な Behaviour の Awake が 1 回");
        check.Expect(disabled_probe->awake_count == 1,
            "無効な Behaviour でも Awake が 1 回呼ばれる");
        check.Expect(enabled_probe->start_count == 1, "有効な Behaviour の Start が 1 回");
        check.Expect(disabled_probe->start_count == 0,
            "無効な Behaviour の Start は呼ばれない");
        check.Expect(enabled_probe->enable_count == 1, "OnEnable が 1 回");
        check.Expect(ContainsInOrder(enabled_probe->calls, "Awake", "Enable"),
            "Awake は Enable より先");
        check.Expect(ContainsInOrder(enabled_probe->calls, "Enable", "Start"),
            "Enable は Start より先");

        // 更新
        world.Update(0.016f);
        world.FixedUpdate(0.016f);
        world.LateUpdate(0.016f);

        check.Expect(enabled_probe->update_count == 1, "OnUpdate が 1 回");
        check.Expect(enabled_probe->fixed_update_count == 1, "OnFixedUpdate が 1 回");
        check.Expect(enabled_probe->late_update_count == 1, "OnLateUpdate が 1 回");
        check.Expect(disabled_probe->update_count == 0,
            "無効な Behaviour は Update されない");

        // 二重 Update が起きていないこと。
        // Behaviour 専用の更新経路を作っていれば、ここで 2 になる。
        world.Update(0.016f);
        check.Expect(enabled_probe->update_count == 2,
            "1 フレームにつき Update は 1 回だけ（二重 Update が無い）");

        // 有効化・無効化
        enabled_probe->SetEnabled(false);
        world.Update(0.016f);
        check.Expect(enabled_probe->disable_count == 1, "無効化で OnDisable が 1 回");
        check.Expect(enabled_probe->update_count == 2, "無効化後は Update されない");

        enabled_probe->SetEnabled(true);
        world.Update(0.016f);
        check.Expect(enabled_probe->enable_count == 2, "再有効化で OnEnable が呼ばれる");
        check.Expect(enabled_probe->start_count == 1,
            "再有効化しても Start は増えない（一度だけ）");

        // 同じ有効状態を再設定しても重複して呼ばれないこと
        const int enable_before = enabled_probe->enable_count;
        enabled_probe->SetEnabled(true);
        world.Update(0.016f);
        check.Expect(enabled_probe->enable_count == enable_before,
            "同じ有効状態の再設定では OnEnable が重複しない");

        // 無効のまま置かれた Behaviour が、後から有効になったとき Start が走ること
        disabled_probe->SetEnabled(true);
        world.Update(0.016f);
        check.Expect(disabled_probe->start_count == 1,
            "無効だった Behaviour も、有効になった時点で Start が 1 回");
        check.Expect(disabled_probe->awake_count == 1,
            "後から有効化しても Awake は増えない");

        // 破棄
        auto* doomed_object = world.CreateGameObject("Doomed");
        auto* doomed = doomed_object->AddComponent<LifecycleProbeBehaviour>();
        world.Update(0.016f);   // Awake / Start を通す
        check.Expect(doomed != nullptr && doomed->awake_count == 1,
            "Runtime 中に追加した Behaviour も Awake される");

        int destroy_seen = 0;
        int disable_seen = 0;
        ObjectID doomed_id;
        if (doomed != nullptr)
        {
            // ID は破棄する前に控える。
            // 破棄後に doomed_object を読むと解放済みメモリへ触ることになる。
            doomed_id = doomed_object->ID();
            world.DestroyGameObject(doomed_object);
            world.ProcessPendingOperations();

            // ここで実体は解放済み。以降このポインタは使わない。
            doomed_object = nullptr;
            doomed = nullptr;
            destroy_seen = 1;
            disable_seen = 1;
        }
        check.Expect(destroy_seen == 1 && disable_seen == 1,
            "GameObject の遅延破棄が完了する");
        check.Expect(doomed_id.Valid() && world.FindGameObjectByID(doomed_id) == nullptr,
            "破棄後は ObjectID で引けなくなる");

        // Awake の中で Destroy を要求しても落ちないこと
        {
            Scene::Scene awake_world("AwakeDestroyWorld");
            RuntimeContext awake_runtime(awake_world);
            awake_world.Services().SetRuntime(&awake_runtime);

            Core::GameObject* object = awake_world.CreateGameObject("AwakeDestroy");
            auto* probe = object->AddComponent<LifecycleProbeBehaviour>();
            if (probe != nullptr) probe->destroy_in_awake = true;

            awake_world.Start();
            awake_world.Update(0.016f);
            awake_world.ProcessPendingOperations();

            check.Expect(object->ComponentCount() >= 1,
                "Awake 中の Destroy 要求でクラッシュしない");
        }

        // Handle が破棄後に無効になること（Behaviour 経由）
        {
            const HandleResolver resolver(world);
            const ObjectHandle handle = resolver.MakeHandle(enabled_object);
            check.Expect(resolver.IsValid(handle), "破棄前の Handle は有効");

            world.DestroyGameObject(enabled_object);
            world.ProcessPendingOperations();
            check.Expect(!resolver.IsValid(handle), "破棄後の Handle は無効");
        }

        world.Services().SetRuntime(nullptr);
        return check.Report("Behaviour validation");
    }

    // =====================================================================

    int RunEventValidation()
    {
        RegisterProbe();
        Checker check(290);

        Scene::Scene world("EventWorld");
        RuntimeContext runtime(world);
        world.Services().SetRuntime(&runtime);
        EventBus& bus = runtime.Events();

        const Reflection::TypeGUID event_a =
            Reflection::MakeTypeGUID("d0000000000000000000000000000001");
        const Reflection::TypeGUID event_b =
            Reflection::MakeTypeGUID("d0000000000000000000000000000002");

        int received_a = 0;
        int received_b = 0;

        {
            ScopedSubscription token_a = bus.Subscribe(event_a,
                [&received_a](const EventRecord&) { ++received_a; });
            ScopedSubscription token_b = bus.Subscribe(event_b,
                [&received_b](const EventRecord&) { ++received_b; });

            check.Expect(token_a.Valid() && token_b.Valid(), "購読トークンが有効");
            check.Expect(bus.SubscriberCount() == 2, "購読が 2 件登録される");

            EventRecord record;
            record.type = event_a;
            bus.Publish(record);

            check.Expect(received_a == 0,
                "Publish しただけでは配送されない（配送は必ず遅延）");
            check.Expect(bus.PendingEventCount() == 1, "待ち行列へ 1 件積まれる");

            bus.Dispatch(&runtime.Resolver());
            check.Expect(received_a == 1, "Dispatch で配送される");
            check.Expect(received_b == 0, "別の型の購読者へは配送されない");
            check.Expect(bus.PendingEventCount() == 0, "配送後は待ち行列が空になる");
        }

        // トークンが破棄されたので購読も外れているはず
        check.Expect(bus.SubscriberCount() == 0, "トークンの破棄で購読が解除される");

        {
            EventRecord record;
            record.type = event_a;
            bus.Publish(record);
            bus.Dispatch(&runtime.Resolver());
            check.Expect(received_a == 1, "解除後は配送されない");
        }

        // 配送中の Subscribe / Unsubscribe
        {
            int outer = 0;
            int inner = 0;
            ScopedSubscription inner_token;

            ScopedSubscription outer_token = bus.Subscribe(event_a,
                [&](const EventRecord&)
                {
                    ++outer;
                    // 配送中に新しい購読を足す。この回では呼ばれないこと。
                    if (!inner_token.Valid())
                    {
                        inner_token = bus.Subscribe(event_a,
                            [&inner](const EventRecord&) { ++inner; });
                    }
                });

            EventRecord record;
            record.type = event_a;
            bus.Publish(record);
            bus.Dispatch(&runtime.Resolver());

            check.Expect(outer == 1, "配送中に Subscribe しても外側は 1 回だけ");
            check.Expect(inner == 0, "配送中に足した購読はその回では呼ばれない");

            bus.Publish(record);
            bus.Dispatch(&runtime.Resolver());
            check.Expect(inner == 1, "次の配送からは新しい購読も呼ばれる");
        }

        // 購読者の GameObject が消えたら配送しない
        {
            Core::GameObject* owner_object = world.CreateGameObject("EventOwner");
            const ObjectHandle owner = runtime.Resolver().MakeHandle(owner_object);

            int owned_received = 0;
            ScopedSubscription owned = bus.Subscribe(event_a,
                [&owned_received](const EventRecord&) { ++owned_received; }, owner);

            EventRecord record;
            record.type = event_a;
            bus.Publish(record);
            bus.Dispatch(&runtime.Resolver());
            check.Expect(owned_received == 1, "生きている購読者へは配送される");

            world.DestroyGameObject(owner_object);
            world.ProcessPendingOperations();

            bus.Publish(record);
            bus.Dispatch(&runtime.Resolver());
            check.Expect(owned_received == 1,
                "購読者の GameObject が消えたら配送されない");
            check.Expect(bus.DroppedEventCount() > 0, "配送しなかった件数が記録される");
        }

        // 宛先が消えている場合
        {
            int target_received = 0;
            ScopedSubscription token = bus.Subscribe(event_b,
                [&target_received](const EventRecord&) { ++target_received; });

            Core::GameObject* target_object = world.CreateGameObject("EventTarget");
            EventRecord record;
            record.type = event_b;
            record.target = runtime.Resolver().MakeHandle(target_object);

            world.DestroyGameObject(target_object);
            world.ProcessPendingOperations();

            bus.Publish(record);
            bus.Dispatch(&runtime.Resolver());
            check.Expect(target_received == 0, "宛先が消えたイベントは配送されない");
        }

        // 配送順が発行順であること
        {
            std::vector<int> order;
            ScopedSubscription token = bus.Subscribe(event_a,
                [&order](const EventRecord& record)
                {
                    const Reflection::PropertyValue* value = record.payload.Find("index");
                    order.push_back(value != nullptr ? value->AsInt() : -1);
                });

            for (int index = 0; index < 5; ++index)
            {
                EventRecord record;
                record.type = event_a;
                record.payload.Set("index", Reflection::PropertyValue::MakeInt(index));
                bus.Publish(record);
            }
            bus.Dispatch(&runtime.Resolver());

            bool ordered = order.size() == 5;
            for (std::size_t index = 0; ordered && index < order.size(); ++index)
            {
                ordered = order[index] == static_cast<int>(index);
            }
            check.Expect(ordered, "同一フレームの配送順が発行順（決定的）");
        }

        // 無限再帰の打ち切り
        {
            ScopedSubscription token = bus.Subscribe(event_a,
                [&bus, event_a](const EventRecord&)
                {
                    EventRecord next;
                    next.type = event_a;
                    bus.Publish(next);
                });

            EventRecord record;
            record.type = event_a;
            bus.Publish(record);
            bus.Dispatch(&runtime.Resolver());

            check.Expect(bus.RecursionLimitHit(),
                "配送の中から発行し続けた場合に打ち切られる");
            check.Expect(bus.PendingEventCount() == 0,
                "打ち切り後に待ち行列が残らない");
        }

        // Global Bus は Scene とは別物
        check.Expect(&EventBus::Global() != &bus,
            "Global Bus と Scene Bus は別の実体");

        world.Services().SetRuntime(nullptr);
        return check.Report("Event validation");
    }

    // =====================================================================

    int RunRuntimeApiValidation()
    {
        RegisterProbe();
        Checker check(330);

        Scene::Scene world("RuntimeApiWorld");
        RuntimeContext runtime(world);
        world.Services().SetRuntime(&runtime);

        RuntimeTime time;
        time.delta_time = 0.016f;
        time.fixed_delta_time = 0.02f;
        time.unscaled_delta_time = 0.016f;
        time.frame_index = 42;
        runtime.SetTime(time);

        check.Expect(runtime.FrameIndex() == 42, "FrameIndex を取得できる");
        check.Expect(runtime.FixedDeltaTime() > 0.019f, "FixedDeltaTime を取得できる");
        check.Expect(runtime.CurrentWorldID() == world.WorldInstanceID(),
            "CurrentWorldID が World と一致する");

        // 生成
        ObjectHandle handle;
        check.Expect(Succeeded(runtime.CreateGameObject("Api", handle)),
            "Runtime API から GameObject を作れる");
        check.Expect(runtime.IsValid(handle), "作った直後の Handle が有効");

        // 名前
        std::string name;
        check.Expect(Succeeded(runtime.GetName(handle, name)) && name == "Api",
            "名前を取得できる");
        check.Expect(Succeeded(runtime.SetName(handle, "改名")) &&
            Succeeded(runtime.GetName(handle, name)) && name == "改名",
            "名前を設定できる");

        // Transform
        const DirectX::XMFLOAT3 position{ 1.0f, 2.0f, 3.0f };
        DirectX::XMFLOAT3 read{ 0.0f, 0.0f, 0.0f };
        check.Expect(Succeeded(runtime.SetLocalPosition(handle, position)) &&
            Succeeded(runtime.GetLocalPosition(handle, read)) &&
            read.x == 1.0f && read.y == 2.0f && read.z == 3.0f,
            "ローカル座標を設定・取得できる");

        // 階層
        ObjectHandle child;
        runtime.CreateGameObject("Child", child);
        check.Expect(Succeeded(runtime.SetParent(child, handle, false)),
            "親子関係を設定できる");

        ObjectHandle parent_out;
        check.Expect(Succeeded(runtime.GetParent(child, parent_out)) &&
            parent_out == handle, "親 Handle を取得できる");

        std::vector<ObjectHandle> children;
        check.Expect(Succeeded(runtime.GetChildren(handle, children)) &&
            children.size() == 1 && children[0] == child, "子 Handle を列挙できる");

        // 自分自身を親にしようとした場合
        check.Expect(Failed(runtime.SetParent(handle, handle, false)),
            "自分自身を親にできない");

        // Component
        ComponentHandle component;
        check.Expect(Succeeded(runtime.AddComponent(handle,
            LifecycleProbeBehaviour::StaticTypeID(), component)),
            "Runtime API から Component を追加できる");
        check.Expect(runtime.HasComponent(handle, LifecycleProbeBehaviour::StaticTypeID()),
            "追加した Component を型 ID で見つけられる");

        bool component_enabled = false;
        check.Expect(Succeeded(runtime.IsComponentEnabled(component, component_enabled)) &&
            component_enabled, "Component の有効状態を取得できる");
        check.Expect(Succeeded(runtime.SetComponentEnabled(component, false)) &&
            Succeeded(runtime.IsComponentEnabled(component, component_enabled)) &&
            !component_enabled, "Component の有効状態を設定できる");

        // 未登録の型
        ComponentHandle unknown;
        check.Expect(runtime.AddComponent(handle, 0xDEADBEEFu, unknown) ==
            RuntimeStatus::TypeMismatch, "未登録の型は TypeMismatch");

        // 遅延破棄
        check.Expect(Succeeded(runtime.DestroyComponent(component)),
            "Component の遅延破棄を要求できる");
        check.Expect(runtime.DestroyComponent(component) ==
            RuntimeStatus::ComponentDestroyed,
            "破棄予約済みの Component は ComponentDestroyed");
        world.ProcessPendingOperations();

        check.Expect(Succeeded(runtime.DestroyGameObject(child)),
            "GameObject の遅延破棄を要求できる");
        check.Expect(runtime.IsValid(child) == false,
            "破棄予約された時点で Handle が無効になる");
        world.ProcessPendingOperations();
        check.Expect(runtime.DestroyGameObject(child) == RuntimeStatus::ObjectDestroyed,
            "破棄済みへの再要求は ObjectDestroyed");

        // 壊れた Handle
        check.Expect(runtime.GetName(ObjectHandle::None(), name) ==
            RuntimeStatus::InvalidHandle, "空 Handle は InvalidHandle");
        {
            Scene::Scene other_world("Other");
            RuntimeContext other_runtime(other_world);
            check.Expect(other_runtime.GetName(handle, name) == RuntimeStatus::WrongWorld,
                "別 World の Handle は WrongWorld");
        }

        // 未接続 Service
        check.Expect(runtime.InstantiatePrefab("guid", DirectX::XMFLOAT3{ 0,0,0 },
            DirectX::XMFLOAT3{ 0,0,0 }, DirectX::XMFLOAT3{ 1,1,1 },
            ObjectHandle::None(), child) == RuntimeStatus::ServiceUnavailable,
            "Prefab Instantiator 未接続なら ServiceUnavailable");
        check.Expect(runtime.InstantiatePrefab("", DirectX::XMFLOAT3{ 0,0,0 },
            DirectX::XMFLOAT3{ 0,0,0 }, DirectX::XMFLOAT3{ 1,1,1 },
            ObjectHandle::None(), child) == RuntimeStatus::InvalidArgument,
            "空 GUID は InvalidArgument");
        check.Expect(!runtime.PhysicsAvailable(),
            "Physics 未接続なら利用不可を返す");

        Scene::GroundHit ground;
        check.Expect(runtime.QueryGround(DirectX::XMFLOAT3{ 0,0,0 }, 1.0f, 1.0f, 10.0f,
            0.7f, ObjectHandle::None(), ground) == RuntimeStatus::ServiceUnavailable,
            "Physics 未接続の問い合わせは ServiceUnavailable");

        check.Expect(!runtime.AudioAvailable() && !runtime.InputActionAvailable() &&
            !runtime.SaveGameAvailable() && !runtime.RuntimeUIAvailable(),
            "未実装 Service は利用不可を明示する（偽の成功を返さない）");

        // 遅延生成の要求は Instantiator が無ければ受け付けない
        check.Expect(runtime.InstantiatePrefabDeferred("guid", DirectX::XMFLOAT3{ 0,0,0 },
            DirectX::XMFLOAT3{ 0,0,0 }, DirectX::XMFLOAT3{ 1,1,1 },
            ObjectHandle::None()) == RuntimeStatus::ServiceUnavailable,
            "Instantiator 未接続なら遅延生成も受け付けない");
        check.Expect(runtime.PendingDeferredOperationCount() == 0,
            "受け付けなかった要求は積まれない");

        // 診断カウンタ
        check.Expect(runtime.Diagnostics().object_resolve_failure > 0,
            "解決失敗が診断へ記録される");

        world.Services().SetRuntime(nullptr);
        return check.Report("Runtime API validation");
    }

    // =====================================================================
    // Collision Event 配送
    // =====================================================================

    namespace
    {
        // 返す Hit を検証側から指定できる問い合わせサービス。
        //
        // 本物の SceneCollisionWorld を使わない理由:
        //   接触の有無・相手・位置を意図した順序で切り替えたいのに、
        //   実際の地形を使うと「その状況を作る」こと自体が難しい。
        //   Motor から先（Dispatcher の状態遷移）を確かめるのが目的なので、
        //   入口の問い合わせだけ差し替える。
        //
        //   Motor の移動計算そのものは本物をそのまま通る。
        class ScriptedPhysics final : public Scene::IPhysicsQueryService
        {
        public:
            bool ground_hit = false;
            Core::ObjectID ground_object;
            Scene::ColliderID ground_collider = 11;
            float ground_y = 0.0f;

            bool wall_hit = false;
            Core::ObjectID wall_object;
            Scene::ColliderID wall_collider = 22;

            bool ray_hit = false;
            Core::ObjectID ray_object;
            Scene::ColliderID ray_collider = 33;

            bool CollisionAvailable() const override { return true; }

            bool QueryGround(const DirectX::XMFLOAT3& origin, float /*radius*/,
                float /*up_offset*/, float /*down_distance*/, float /*walkable_normal_y*/,
                Scene::GroundHit& hit) const override
            {
                if (!ground_hit) return false;
                hit.valid = true;
                hit.position = DirectX::XMFLOAT3{ origin.x, ground_y, origin.z };
                hit.normal = DirectX::XMFLOAT3{ 0.0f, 1.0f, 0.0f };
                hit.source.backend = Scene::CollisionBackend::SceneCollider;
                hit.source.object = ground_object;
                hit.source.collider = ground_collider;
                return true;
            }

            bool SweepSphere(const DirectX::XMFLOAT3& start, const DirectX::XMFLOAT3& /*end*/,
                float /*radius*/, float /*maximum_normal_y*/,
                Scene::SphereSweepHit& hit) const override
            {
                if (!wall_hit) return false;
                hit.valid = true;
                hit.center = start;
                hit.normal = DirectX::XMFLOAT3{ 1.0f, 0.0f, 0.0f };
                hit.fraction = 0.5f;
                hit.source.backend = Scene::CollisionBackend::SceneCollider;
                hit.source.object = wall_object;
                hit.source.collider = wall_collider;
                return true;
            }

            bool RaycastFiltered(const DirectX::XMFLOAT3& origin,
                const DirectX::XMFLOAT3& direction, float /*max_distance*/,
                const Scene::CollisionQueryFilter& /*filter*/,
                Scene::RaycastHit& hit) const override
            {
                hit = Scene::RaycastHit{};
                if (!ray_hit) return false;
                hit.valid = true;
                hit.point = DirectX::XMFLOAT3{
                    origin.x + direction.x * 2.0f,
                    origin.y + direction.y * 2.0f,
                    origin.z + direction.z * 2.0f };
                hit.normal = DirectX::XMFLOAT3{ 0.0f, 1.0f, 0.0f };
                hit.distance = 2.0f;
                hit.source.backend = Scene::CollisionBackend::SceneCollider;
                hit.source.object = ray_object;
                hit.source.collider = ray_collider;
                return true;
            }
        };

        // 受け取った CollisionEvent を記録するだけの Behaviour。
        class CollisionProbeBehaviour final : public BehaviourComponent
        {
            REPLAY_COMPONENT_BODY(CollisionProbeBehaviour)

        public:
            static constexpr Reflection::TypeGUID StaticTypeGUID() noexcept
            {
                return Reflection::MakeTypeGUID("c0000000000000000000000000000002");
            }

            struct Record
            {
                ContactPhase phase = ContactPhase::Enter;
                CollisionHitKind kind = CollisionHitKind::Unknown;
                Core::ObjectID other;
                bool other_valid = false;
                bool self_valid = false;
            };

            std::vector<Record> records;

            void Clear() { records.clear(); }

            int CountOf(ContactPhase phase, CollisionHitKind kind) const
            {
                int total = 0;
                for (const Record& record : records)
                {
                    if (record.phase == phase && record.kind == kind) ++total;
                }
                return total;
            }

        protected:
            void OnCollisionEnter(const CollisionEvent& event) override { Push(event); }
            void OnCollisionStay(const CollisionEvent& event) override { Push(event); }
            void OnCollisionExit(const CollisionEvent& event) override { Push(event); }

            // Trigger 側も記録しておき、Collision と混ざっていないことを確かめる。
            void OnTriggerEnter(const TriggerEvent&) override { ++trigger_calls; }
            void OnTriggerStay(const TriggerEvent&) override { ++trigger_calls; }
            void OnTriggerExit(const TriggerEvent&) override { ++trigger_calls; }

        public:
            int trigger_calls = 0;

        private:
            void Push(const CollisionEvent& event)
            {
                Record record;
                record.phase = event.phase;
                record.kind = event.hit_kind;
                record.other = event.other.object;
                record.other_valid = event.other_valid;
                record.self_valid = !event.self.IsEmpty();
                records.push_back(record);
            }
        };

        void RegisterCollisionProbe()
        {
            Core::RegisterBuiltInComponents();
            Core::ComponentRegistry::Register<CollisionProbeBehaviour>(
                Core::ComponentTypeInfo::Describe("Collision Probe", "Internal")
                    .HiddenInEditor()
                    .WithTypeGUID(CollisionProbeBehaviour::StaticTypeGUID())
                    .InModule("RePlayEngine.Validation"));
            BehaviourRegistry::Register(CollisionProbeBehaviour::StaticTypeGUID(),
                BehaviourRegistry::Native());
        }
    }

    int RunCollisionValidation()
    {
        RegisterCollisionProbe();
        Checker check(370);

        Scene::Scene world("CollisionWorld");
        RuntimeContext runtime(world);
        world.Services().SetRuntime(&runtime);

        ScriptedPhysics physics;
        world.Services().SetPhysics(&physics);

        // 接触相手として置く GameObject。実体があることを確かめるために作る。
        Core::GameObject* floor_a = world.CreateGameObject("FloorA");
        Core::GameObject* floor_b = world.CreateGameObject("FloorB");
        Core::GameObject* wall = world.CreateGameObject("Wall");
        check.Expect(floor_a != nullptr && floor_b != nullptr && wall != nullptr,
            "接触相手の GameObject を作れる");
        if (floor_a == nullptr || floor_b == nullptr || wall == nullptr)
        {
            return check.Report("Collision validation");
        }
        physics.ground_object = floor_a->ID();
        physics.wall_object = wall->ID();
        physics.ray_object = wall->ID();

        // Runtime Raycast は同じ Physics Service を通り、Object/Collider 情報まで返す。
        physics.ray_hit = true;
        Scene::RaycastHit raycast_hit{};
        check.Expect(runtime.Raycast(DirectX::XMFLOAT3{ 0, 1, 0 },
            DirectX::XMFLOAT3{ 0, 0, 1 }, 100.0f, 0, -1,
            ObjectHandle::None(), raycast_hit) == RuntimeStatus::Ok &&
            raycast_hit.valid && raycast_hit.source.object == wall->ID() &&
            raycast_hit.source.collider == physics.ray_collider,
            "Runtime Raycast が Hit Object / Collider を返す");
        physics.ray_hit = false;
        check.Expect(runtime.Raycast(DirectX::XMFLOAT3{ 0, 1, 0 },
            DirectX::XMFLOAT3{ 0, 0, 1 }, 100.0f, 0, -1,
            ObjectHandle::None(), raycast_hit) == RuntimeStatus::Ok && !raycast_hit.valid,
            "Runtime Raycast の miss は成功した問い合わせ + invalid hit として返る");

        // 動く側。Collider と Motor と Probe を付ける。
        Core::GameObject* character = world.CreateGameObject("Character");
        auto* collider = character->AddComponent<Components::SphereColliderComponent>();
        auto* motor = character->AddComponent<Components::CharacterMotorComponent>();
        auto* probe = character->AddComponent<CollisionProbeBehaviour>();
        check.Expect(collider != nullptr && motor != nullptr && probe != nullptr,
            "Collider / Motor / Probe を追加できる");
        if (collider == nullptr || motor == nullptr || probe == nullptr)
        {
            return check.Report("Collision validation");
        }
        motor->SetPrimaryCollider(*collider);

        world.Start();

        CollisionEventDispatcher dispatcher;

        // 1 ステップ進めて配送する、を 1 回分にまとめる。
        const auto step = [&](std::uint64_t frame)
        {
            world.FixedUpdate(1.0f / 60.0f);
            dispatcher.Dispatch(world, frame);
        };

        // ---- 接地なし。何も起きない --------------------------------------

        physics.ground_hit = false;
        physics.wall_hit = false;
        probe->Clear();
        step(1);
        check.Expect(probe->records.empty(), "接触が無ければイベントは配られない");

        // ---- Ground Enter -------------------------------------------------

        physics.ground_hit = true;
        probe->Clear();
        step(2);
        check.Expect(probe->CountOf(ContactPhase::Enter,
            CollisionHitKind::CharacterGround) == 1, "接地で Ground Enter が 1 回");
        check.Expect(probe->records.size() == 1, "Enter 以外は配られない");
        check.Expect(!probe->records.empty() && probe->records[0].other == floor_a->ID(),
            "Ground Enter の相手が正しい");
        check.Expect(!probe->records.empty() && probe->records[0].other_valid,
            "相手が生きていれば other_valid が true");
        check.Expect(!probe->records.empty() && probe->records[0].self_valid,
            "self は必ず Handle として渡される");

        // ---- Ground Stay ---------------------------------------------------

        probe->Clear();
        step(3);
        check.Expect(probe->CountOf(ContactPhase::Stay,
            CollisionHitKind::CharacterGround) == 1, "接触が続けば Ground Stay");
        check.Expect(probe->CountOf(ContactPhase::Enter,
            CollisionHitKind::CharacterGround) == 0, "続いている間 Enter は再送されない");

        // ---- 接触相手の変更で Exit -> Enter ---------------------------------

        physics.ground_object = floor_b->ID();
        probe->Clear();
        step(4);
        check.Expect(probe->records.size() == 2,
            "相手が変わったら Exit と Enter の 2 件");
        check.Expect(probe->records.size() == 2 &&
            probe->records[0].phase == ContactPhase::Exit &&
            probe->records[0].other == floor_a->ID(),
            "先に前の相手への Exit が来る");
        check.Expect(probe->records.size() == 2 &&
            probe->records[1].phase == ContactPhase::Enter &&
            probe->records[1].other == floor_b->ID(),
            "次に新しい相手への Enter が来る");

        // ---- 地面から離れたら Exit -------------------------------------------

        physics.ground_hit = false;
        probe->Clear();
        step(5);
        check.Expect(probe->CountOf(ContactPhase::Exit,
            CollisionHitKind::CharacterGround) == 1, "地面から離れたら Ground Exit");

        probe->Clear();
        step(6);
        check.Expect(probe->records.empty(), "離れたあとは何も配られない");

        // ---- Wall Enter / Stay / Exit ----------------------------------------

        physics.wall_hit = true;
        probe->Clear();
        step(7);
        check.Expect(probe->CountOf(ContactPhase::Enter,
            CollisionHitKind::CharacterWall) == 1, "壁接触で Wall Enter");
        check.Expect(probe->CountOf(ContactPhase::Enter,
            CollisionHitKind::CharacterGround) == 0,
            "Wall の接触が Ground として配られない");

        probe->Clear();
        step(8);
        check.Expect(probe->CountOf(ContactPhase::Stay,
            CollisionHitKind::CharacterWall) == 1, "壁接触が続けば Wall Stay");

        physics.wall_hit = false;
        probe->Clear();
        step(9);
        check.Expect(probe->CountOf(ContactPhase::Exit,
            CollisionHitKind::CharacterWall) == 1, "壁から離れたら Wall Exit");

        // ---- Ground と Wall が同時に起きても混ざらない --------------------------

        physics.ground_hit = true;
        physics.wall_hit = true;
        probe->Clear();
        step(10);
        check.Expect(probe->CountOf(ContactPhase::Enter,
            CollisionHitKind::CharacterGround) == 1, "同時接触でも Ground Enter が 1 回");
        check.Expect(probe->CountOf(ContactPhase::Enter,
            CollisionHitKind::CharacterWall) == 1, "同時接触でも Wall Enter が 1 回");
        check.Expect(probe->records.size() == 2, "同時接触で余分なイベントが出ない");

        // ---- Trigger と混ざらない ----------------------------------------------

        check.Expect(probe->trigger_calls == 0,
            "Collision の配送が Trigger のコールバックへ流れ込まない");

        // ---- 無効な Behaviour へは配送しない -------------------------------------

        probe->SetEnabled(false);
        world.FixedUpdate(1.0f / 60.0f);   // SetEnabled を同期点へ反映させる
        probe->Clear();
        step(11);
        check.Expect(probe->records.empty(), "無効な Behaviour へは配送しない");
        check.Expect(dispatcher.SkippedCount() > 0, "配送しなかった件数が記録される");

        probe->SetEnabled(true);
        world.FixedUpdate(1.0f / 60.0f);
        probe->Clear();
        step(12);
        check.Expect(!probe->records.empty(), "再有効化すると配送が再開する");

        // ---- 削除予約済みへは配送しない -------------------------------------------

        probe->Destroy();
        probe->Clear();

        // 予約直後、まだ実体があるうちに配送を試す。
        // step() を挟むと FixedUpdate の同期点で実体が解放され、
        // そのあとに probe を読むと解放済みメモリへ触ることになる。
        dispatcher.Dispatch(world, 13);
        check.Expect(probe->records.empty(), "削除予約済みの Behaviour へは配送しない");

        // ここで probe の実体が解放される。以降このポインタは使わない。
        world.ProcessPendingOperations();
        probe = nullptr;

        // ---- World が入れ替わったら接触状態を捨てる ---------------------------------

        const std::size_t before_clear = dispatcher.ActiveContactCount();
        check.Expect(before_clear > 0, "接触状態が保持されている");

        const std::uint64_t exit_before = dispatcher.ExitCount();
        world.Clear();
        dispatcher.Dispatch(world, 14);
        check.Expect(dispatcher.ActiveContactCount() == 0,
            "World の入れ替えで接触状態が捨てられる");
        check.Expect(dispatcher.ExitCount() == exit_before,
            "消えた World の接触へ Exit を配らない（配送先が存在しないため）");

        // ---- 明示的な Reset --------------------------------------------------------

        dispatcher.Reset();
        check.Expect(dispatcher.ActiveContactCount() == 0, "Reset で接触状態が空になる");

        // ---- 一般 RigidBody 衝突は配れないことの確認 -----------------------------------
        //
        // Motor を持たない GameObject どうしを重ねても、
        // CollisionEvent は 1 件も発生しないこと。
        // 「実装したふり」になっていないかをここで固定する。
        {
            Scene::Scene rigid_world("RigidBodyWorld");
            RuntimeContext rigid_runtime(rigid_world);
            rigid_world.Services().SetRuntime(&rigid_runtime);
            rigid_world.Services().SetPhysics(&physics);

            Core::GameObject* a = rigid_world.CreateGameObject("A");
            Core::GameObject* b = rigid_world.CreateGameObject("B");
            a->AddComponent<Components::SphereColliderComponent>();
            b->AddComponent<Components::SphereColliderComponent>();
            auto* probe_a = a->AddComponent<CollisionProbeBehaviour>();
            auto* probe_b = b->AddComponent<CollisionProbeBehaviour>();

            rigid_world.Start();

            CollisionEventDispatcher rigid_dispatcher;
            for (std::uint64_t frame = 0; frame < 3; ++frame)
            {
                rigid_world.FixedUpdate(1.0f / 60.0f);
                rigid_dispatcher.Dispatch(rigid_world, frame);
            }

            check.Expect(probe_a != nullptr && probe_a->records.empty() &&
                probe_b != nullptr && probe_b->records.empty(),
                "CharacterMotor が無い Collider どうしには CollisionEvent が発生しない");
            check.Expect(rigid_dispatcher.ActiveContactCount() == 0,
                "一般 Collider 対 Collider の接触状態は作られない");

            rigid_world.Services().SetRuntime(nullptr);
            rigid_world.Services().SetPhysics(nullptr);
        }

        world.Services().SetPhysics(nullptr);
        world.Services().SetRuntime(nullptr);
        return check.Report("Collision validation");
    }
}
