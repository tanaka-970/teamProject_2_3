#include "BehaviourValidationInternal.h"

namespace ReplayEngine::Runtime::Validation
{
    using namespace Detail::BehaviourValidation;

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
            !runtime.SaveGameAvailable(),
            "未接続 Service は利用不可を明示する（偽の成功を返さない）");

        // Runtime UI だけは接続する Service が無い。
        // 既存の UI Component を World 経由で直接触る設計なので、
        // World があれば利用可能と答えるのが正しい（RUNTIME_API_1_2_DESIGN.md）。
        // ただし「利用可能」が無条件の成功を意味しないことをここで固定する。
        check.Expect(runtime.RuntimeUIAvailable(),
            "Runtime UI は World があれば利用可能（接続する Service が無い）");
        check.Expect(runtime.SetUIText(ObjectHandle::None(), "text") !=
            RuntimeStatus::Ok,
            "利用可能でも、無効な Handle への UI 操作は成功しない");

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
}
