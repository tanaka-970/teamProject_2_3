// Behaviour 検証のうち、Lifecycle の判定だけを持つ。
//
//   BehaviourValidation.cpp            … Behaviour Lifecycle（このファイル）
//   BehaviourEventValidation.cpp       … Event Bus
//   BehaviourRuntimeApiValidation.cpp  … Runtime API
//   BehaviourCollisionValidation.cpp   … Collision Event 配送
//   BehaviourValidationSupport.cpp     … 共有 Probe の登録と順序判定
//   BehaviourValidationInternal.h      … 分割内部の Checker と Probe 型

#include "BehaviourValidationInternal.h"

namespace ReplayEngine::Runtime::Validation
{
    using namespace Detail::BehaviourValidation;

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

        const int execution_order_result = RunExecutionOrderValidation();
        if (execution_order_result != 0)
        {
            world.Services().SetRuntime(nullptr);
            return execution_order_result;
        }

        const int dependency_result = RunComponentDependencyValidation();
        if (dependency_result != 0)
        {
            world.Services().SetRuntime(nullptr);
            return dependency_result;
        }

        world.Services().SetRuntime(nullptr);
        return check.Report("Behaviour validation");
    }
}
