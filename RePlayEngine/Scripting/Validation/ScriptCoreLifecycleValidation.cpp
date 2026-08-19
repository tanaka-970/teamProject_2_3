#include "ScriptCoreValidationInternal.h"

namespace ReplayEngine::Scripting::Validation
{
    using namespace Detail;

    // -----------------------------------------------------------------------
    // 680-739  script-lifecycle
    // -----------------------------------------------------------------------

    int RunScriptLifecycleValidation()
    {
        EnsureRegistries();
        Checker check(680);

        Fixture fixture;

        // 要件 1: Active な GameObject 上の Disabled な ScriptComponent
        GameObject* active_object = fixture.world.CreateGameObject("ActiveObject");
        // 要件 2: Inactive な GameObject 上の ScriptComponent
        GameObject* inactive_object = fixture.world.CreateGameObject("InactiveObject");
        // 通常
        GameObject* normal_object = fixture.world.CreateGameObject("NormalObject");

        check.Expect(active_object != nullptr && inactive_object != nullptr &&
            normal_object != nullptr, "検証用 GameObject を 3 体作れる");
        if (active_object == nullptr || inactive_object == nullptr || normal_object == nullptr)
        {
            return check.Report("script-lifecycle");
        }

        ScriptComponent* disabled_script = fixture.AddRotating(*active_object);
        ScriptComponent* inactive_script = fixture.AddRotating(*inactive_object);
        ScriptComponent* normal_script = fixture.AddRotating(*normal_object);

        check.Expect(disabled_script != nullptr && inactive_script != nullptr &&
            normal_script != nullptr, "ScriptComponent を 3 つ作れる");
        if (disabled_script == nullptr || inactive_script == nullptr ||
            normal_script == nullptr)
        {
            return check.Report("script-lifecycle");
        }

        disabled_script->SetEnabled(false);   // Component を無効化（GameObject は有効）
        inactive_object->SetEnabled(false);   // GameObject ごと無効化

        MockScriptBackend& lua = *fixture.lua_backend;

        check.Expect(lua.CallLog().empty(), "Scene 開始前は Callback が 1 つも呼ばれない");

        // ---- Play セッション開始 ------------------------------------------------

        fixture.BeginPlaySession();

        const ScriptInstanceHandle normal_handle = normal_script->InstanceHandle();
        const ScriptInstanceHandle disabled_handle = disabled_script->InstanceHandle();

        check.Expect(normal_handle != invalid_script_instance_handle,
            "有効なスクリプトのインスタンスが作られる");

        // 要件 1-a / 1-b
        check.Expect(disabled_handle != invalid_script_instance_handle,
            "要件1: 有効な GameObject 上の無効な ScriptComponent にもインスタンスが作られる");
        check.Expect(lua.CountCalls(disabled_handle, ScriptCallback::Awake) == 1,
            "要件1: 無効な ScriptComponent でも Awake が 1 回呼ばれる");
        check.Expect(lua.CountCalls(disabled_handle, ScriptCallback::OnEnable) == 0,
            "要件1: 無効な ScriptComponent に OnEnable は呼ばれない");
        check.Expect(lua.CountCalls(disabled_handle, ScriptCallback::Start) == 0,
            "要件1: 無効な ScriptComponent に Start は呼ばれない");

        // 要件 2-a / 2-b
        check.Expect(inactive_script->InstanceHandle() == invalid_script_instance_handle,
            "要件2: 無効な GameObject 上ではインスタンスが作られない");
        check.Expect(inactive_script->Status() != ScriptStatus::Running,
            "要件2: 無効な GameObject 上のスクリプトは Running にならない");

        // 通常の順序
        check.Expect(lua.CountCalls(normal_handle, ScriptCallback::Awake) == 1,
            "Awake が 1 回");
        check.Expect(lua.CountCalls(normal_handle, ScriptCallback::OnEnable) == 1,
            "OnEnable が 1 回");
        check.Expect(lua.CountCalls(normal_handle, ScriptCallback::Start) == 1,
            "Start が 1 回");
        check.Expect(lua.CallLogContainsInOrder(
            { ScriptCallback::Awake, ScriptCallback::OnEnable, ScriptCallback::Start }),
            "Awake -> OnEnable -> Start の順");

        // ---- 更新 -----------------------------------------------------------------

        fixture.world.Update(0.016f);
        fixture.world.FixedUpdate(0.016f);
        fixture.world.LateUpdate(0.016f);

        check.Expect(lua.CountCalls(normal_handle, ScriptCallback::Update) == 1,
            "Update が 1 回");
        check.Expect(lua.CountCalls(normal_handle, ScriptCallback::FixedUpdate) == 1,
            "FixedUpdate が 1 回");
        check.Expect(lua.CountCalls(normal_handle, ScriptCallback::LateUpdate) == 1,
            "LateUpdate が 1 回");
        check.Expect(lua.CountCalls(disabled_handle, ScriptCallback::Update) == 0,
            "無効な ScriptComponent は Update されない");

        // 二重更新が無いこと。専用の更新経路を作っていれば 2 になる。
        fixture.world.Update(0.016f);
        check.Expect(lua.CountCalls(normal_handle, ScriptCallback::Update) == 2,
            "1 フレームにつき Update は 1 回だけ（二重更新が無い）");

        // ---- 要件 1-c: 後から有効化 ------------------------------------------------

        disabled_script->SetEnabled(true);
        fixture.world.Update(0.016f);

        check.Expect(lua.CountCalls(disabled_handle, ScriptCallback::OnEnable) == 1,
            "要件1: 有効化で OnEnable が 1 回");
        check.Expect(lua.CountCalls(disabled_handle, ScriptCallback::Start) == 1,
            "要件1: 有効化で Start が 1 回");
        check.Expect(lua.CountCalls(disabled_handle, ScriptCallback::Awake) == 1,
            "要件1: 有効化しても Awake は増えない");

        // ---- 要件 2-c: GameObject を有効化 -----------------------------------------

        inactive_object->SetEnabled(true);
        fixture.world.Update(0.016f);

        const ScriptInstanceHandle inactive_handle = inactive_script->InstanceHandle();
        check.Expect(inactive_handle != invalid_script_instance_handle,
            "要件2: GameObject が有効になるとインスタンスが作られる");
        check.Expect(lua.CountCalls(inactive_handle, ScriptCallback::Awake) == 1,
            "要件2: このタイミングで Awake が 1 回");
        check.Expect(lua.CountCalls(inactive_handle, ScriptCallback::OnEnable) == 1,
            "要件2: 続いて OnEnable が 1 回");
        check.Expect(lua.CountCalls(inactive_handle, ScriptCallback::Start) == 1,
            "要件2: 続いて Start が 1 回");

        // 同じ同期点で Awake -> OnEnable -> Start の順に並んでいること。
        {
            std::vector<ScriptCallback> observed;
            for (const MockScriptBackend::CallEntry& entry : lua.CallLog())
            {
                if (entry.instance == inactive_handle) observed.push_back(entry.callback);
            }
            const bool ordered = observed.size() >= 3 &&
                observed[0] == ScriptCallback::Awake &&
                observed[1] == ScriptCallback::OnEnable &&
                observed[2] == ScriptCallback::Start;
            check.Expect(ordered, "要件2: Awake -> OnEnable -> Start の順で呼ばれる");
        }

        // ---- 要件 3: Disable / Enable の反復 ----------------------------------------

        const std::size_t enable_before = lua.CountCalls(normal_handle, ScriptCallback::OnEnable);
        const std::size_t disable_before = lua.CountCalls(normal_handle, ScriptCallback::OnDisable);

        for (int index = 0; index < 10; ++index)
        {
            normal_script->SetEnabled(false);
            fixture.world.Update(0.016f);
            normal_script->SetEnabled(true);
            fixture.world.Update(0.016f);
        }

        check.Expect(lua.CountCalls(normal_handle, ScriptCallback::OnDisable) ==
            disable_before + 10, "要件3: OnDisable がちょうど 10 回増える");
        check.Expect(lua.CountCalls(normal_handle, ScriptCallback::OnEnable) ==
            enable_before + 10, "要件3: OnEnable がちょうど 10 回増える");
        check.Expect(lua.CountCalls(normal_handle, ScriptCallback::Awake) == 1,
            "要件3: 反復しても Awake は 1 回のまま");
        check.Expect(lua.CountCalls(normal_handle, ScriptCallback::Start) == 1,
            "要件3: 反復しても Start は 1 回のまま");

        // GameObject 側での反復も同じ結果になること。
        const std::size_t go_enable_before =
            lua.CountCalls(inactive_handle, ScriptCallback::OnEnable);
        for (int index = 0; index < 5; ++index)
        {
            inactive_object->SetEnabled(false);
            fixture.world.Update(0.016f);
            inactive_object->SetEnabled(true);
            fixture.world.Update(0.016f);
        }
        check.Expect(lua.CountCalls(inactive_handle, ScriptCallback::OnEnable) ==
            go_enable_before + 5, "要件3: GameObject 側の反復でも OnEnable が対で増える");
        check.Expect(lua.CountCalls(inactive_handle, ScriptCallback::Awake) == 1,
            "要件3: GameObject 側の反復でも Awake は 1 回のまま");

        // ---- execution_order の安定性 -----------------------------------------------

        {
            inactive_script->SetExecutionOrder(-100);
            normal_script->SetExecutionOrder(0);
            disabled_script->SetExecutionOrder(100);
            const std::vector<ObjectID> expected = {
                inactive_object->ID(), normal_object->ID(), active_object->ID(),
            };
            lua.ClearCallLog();
            fixture.world.Update(0.016f);
            fixture.world.FixedUpdate(0.016f);
            fixture.world.LateUpdate(0.016f);

            const auto collect = [&lua](ScriptCallback callback)
            {
                std::vector<ObjectID> result;
                for (const MockScriptBackend::CallEntry& entry : lua.CallLog())
                {
                    if (entry.callback == callback) result.push_back(entry.object);
                }
                return result;
            };
            check.Expect(collect(ScriptCallback::Update) == expected,
                "Script Update は execution_order -100, 0, 100 の順");
            check.Expect(collect(ScriptCallback::FixedUpdate) == expected,
                "Script FixedUpdate は execution_order -100, 0, 100 の順");
            check.Expect(collect(ScriptCallback::LateUpdate) == expected,
                "Script LateUpdate は execution_order -100, 0, 100 の順");

            lua.ClearCallLog();
            fixture.world.Update(0.016f);
            std::vector<ObjectID> second;
            for (const MockScriptBackend::CallEntry& entry : lua.CallLog())
            {
                if (entry.callback != ScriptCallback::Update) continue;
                second.push_back(entry.object);
            }

            lua.ClearCallLog();
            fixture.world.Update(0.016f);
            std::vector<ObjectID> third;
            for (const MockScriptBackend::CallEntry& entry : lua.CallLog())
            {
                if (entry.callback != ScriptCallback::Update) continue;
                third.push_back(entry.object);
            }

            check.Expect(second == third,
                "execution_order の呼び出し順は毎フレーム安定している");
        }

        // ---- 要件 2-c の続き: Awake していないスクリプトの破棄 ------------------------

        {
            GameObject* never = fixture.world.CreateGameObject("NeverAwake");
            check.Expect(never != nullptr, "検証用 GameObject を作れる");
            if (never != nullptr)
            {
                never->SetEnabled(false);
                ScriptComponent* script = fixture.AddRotating(*never);
                fixture.world.Update(0.016f);

                check.Expect(script != nullptr &&
                    script->InstanceHandle() == invalid_script_instance_handle,
                    "無効な GameObject 上ではインスタンスが作られない");

                const std::size_t destroy_before = lua.CountCalls(ScriptCallback::OnDestroy);
                const std::uint64_t destroyed_before = lua.DestroyedCount();

                never->Destroy();
                fixture.world.Update(0.016f);

                check.Expect(lua.CountCalls(ScriptCallback::OnDestroy) == destroy_before,
                    "Awake していないスクリプトへ OnDestroy は呼ばれない");
                check.Expect(lua.DestroyedCount() == destroyed_before,
                    "作っていないインスタンスを破棄しようとしない");
            }
        }

        // ---- Play セッション終了 -------------------------------------------------------

        const std::size_t destroy_before_end = lua.CountCalls(ScriptCallback::OnDestroy);
        const std::size_t live_before_end = lua.LiveInstanceCount();
        check.Expect(live_before_end > 0, "終了前は生存インスタンスがある");

        fixture.EndPlaySession();

        check.Expect(lua.CountCalls(ScriptCallback::OnDestroy) > destroy_before_end,
            "終了時に OnDestroy が呼ばれる");
        check.Expect(lua.LiveInstanceCount() == 0,
            "終了後に Lua 側の生存インスタンスが 0 になる");
        check.Expect(fixture.csharp_backend->LiveInstanceCount() == 0,
            "終了後に C# 側の生存インスタンスが 0 になる");
        check.Expect(fixture.runtime->LastLeakedInstanceCount() == 0,
            "解放漏れが 0 と記録される");
        check.Expect(fixture.runtime->World() == nullptr,
            "ScriptWorld が破棄されている");
        check.Expect(!fixture.runtime->PlaySessionActive(),
            "Play セッションが終わっている");

        return check.Report("script-lifecycle");
    }
}
