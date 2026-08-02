#include "HandleValidation.h"

#include "../Handles/HandleResolver.h"
#include "../../Components/Core/TransformComponent.h"
#include "../../Components/Gameplay/HealthComponent.h"
#include "../../Components/Physics/SphereColliderComponent.h"
#include "../../Object/Registry/BuiltInComponents.h"
#include "../../Scene/Runtime/Scene.h"

#include <cstdio>

namespace ReplayEngine::Runtime::Validation
{
    namespace
    {
        using Core::Component;
        using Core::GameObject;
        using Core::ObjectID;

        // 検査の記録係。
        //
        // 最初の失敗で打ち切らず、全項目を実行してから最初の失敗番号を返す。
        // 1 回のビルド確認でできるだけ多くの不具合を見つけたいため。
        class Checker final
        {
        public:
            void Expect(bool condition, const char* what)
            {
                const int code = next_code_++;
                ++total_;
                if (condition) return;

                ++failures_;
                if (first_failure_ == 0) first_failure_ = code;
                std::fprintf(stderr, "  [FAIL %d] %s\n", code, what);
            }

            int Result() const noexcept { return first_failure_; }
            int Total() const noexcept { return total_; }
            int Failures() const noexcept { return failures_; }

        private:
            static constexpr int first_code = 80;

            int next_code_ = first_code;
            int first_failure_ = 0;
            int total_ = 0;
            int failures_ = 0;
        };
    }

    int RunHandleValidation()
    {
        // 重複登録は RegisterInfo が弾くので、二重に呼んでも安全。
        Core::RegisterBuiltInComponents();

        Checker check;

        // ---- World の実体番号 ---------------------------------------------

        Scene::Scene world_a("WorldA");
        Scene::Scene world_b("WorldB");

        check.Expect(world_a.WorldInstanceID() != Core::invalid_world_instance_id,
            "World は構築時に有効な WorldInstanceID を持つ");
        check.Expect(world_a.WorldInstanceID() != world_b.WorldInstanceID(),
            "別の World は別の WorldInstanceID を持つ");

        HandleResolver resolver_a(world_a);
        HandleResolver resolver_b(world_b);

        // ---- ObjectHandle の基本 -------------------------------------------

        GameObject* object = world_a.CreateGameObject("Object");
        check.Expect(object != nullptr, "GameObject を生成できる");
        if (object == nullptr)
        {
            std::fprintf(stderr, "Handle validation aborted: GameObject 生成に失敗\n");
            return check.Result() != 0 ? check.Result() : 80;
        }

        check.Expect(object->Generation() != Core::invalid_object_generation,
            "GameObject は有効な世代番号を持つ");

        const ObjectHandle handle = resolver_a.MakeHandle(object);
        check.Expect(!handle.IsEmpty(), "生存中の GameObject から Handle を作れる");
        check.Expect(resolver_a.IsValid(handle), "作った直後の Handle は有効");
        check.Expect(resolver_a.Resolve(handle) == object, "Handle が元の実体へ解決される");

        GameObject* resolved = nullptr;
        check.Expect(
            resolver_a.TryResolve(ObjectHandle::None(), resolved) == RuntimeStatus::InvalidHandle,
            "空 Handle は InvalidHandle（assert しない）");
        check.Expect(resolver_a.MakeHandle(static_cast<const GameObject*>(nullptr)).IsEmpty(),
            "nullptr から作った Handle は空");

        // ---- World 違い -----------------------------------------------------

        check.Expect(resolver_b.TryResolve(handle, resolved) == RuntimeStatus::WrongWorld,
            "別 World の resolver では WrongWorld");
        check.Expect(resolver_b.MakeHandle(object).IsEmpty(),
            "別 World の resolver は他 Scene の実体から Handle を作らない");

        // ---- 存在しない Object ----------------------------------------------

        ObjectHandle missing = handle;
        missing.object = ObjectID{ 999999 };
        check.Expect(resolver_a.TryResolve(missing, resolved) == RuntimeStatus::ObjectDestroyed,
            "存在しない ObjectID は ObjectDestroyed");

        ObjectHandle stale_generation = handle;
        stale_generation.generation = handle.generation + 1;
        check.Expect(
            resolver_a.TryResolve(stale_generation, resolved) == RuntimeStatus::ObjectDestroyed,
            "世代番号が違う Handle は ObjectDestroyed");

        // ---- ComponentHandle の基本 ------------------------------------------

        Component* transform =
            object->FindComponent(Components::TransformComponent::StaticTypeID());
        check.Expect(transform != nullptr, "組み込みの TransformComponent が付いている");

        if (transform != nullptr)
        {
            check.Expect(transform->StableID() != Core::invalid_component_stable_id,
                "結線済み Component は StableID を持つ");
            check.Expect(transform->InstanceID() != Core::invalid_component_instance_id,
                "結線済み Component は InstanceID を持つ");

            const ComponentHandle transform_handle = resolver_a.MakeHandle(transform);
            check.Expect(resolver_a.IsValid(transform_handle), "Component Handle が有効");
            check.Expect(resolver_a.Resolve(transform_handle) == transform,
                "Component Handle が元の実体へ解決される");
        }

        Component* component_out = nullptr;
        check.Expect(
            resolver_a.TryResolve(ComponentHandle::None(), component_out) ==
            RuntimeStatus::InvalidHandle,
            "空の Component Handle は InvalidHandle");

        // ---- 同じ型を複数持つ場合 ---------------------------------------------

        auto* sphere_a = object->AddComponent<Components::SphereColliderComponent>();
        auto* sphere_b = object->AddComponent<Components::SphereColliderComponent>();
        check.Expect(sphere_a != nullptr && sphere_b != nullptr && sphere_a != sphere_b,
            "同じ型の Component を 2 つ追加できる");

        if (sphere_a != nullptr && sphere_b != nullptr)
        {
            check.Expect(sphere_a->StableID() != sphere_b->StableID(),
                "同じ型でも StableID は別々");
            check.Expect(sphere_a->InstanceID() != sphere_b->InstanceID(),
                "同じ型でも InstanceID は別々");

            const ComponentHandle handle_a = resolver_a.MakeHandle(sphere_a);
            const ComponentHandle handle_b = resolver_a.MakeHandle(sphere_b);
            check.Expect(handle_a != handle_b, "同型 2 つの Handle は区別できる");
            check.Expect(resolver_a.Resolve(handle_a) == sphere_a &&
                resolver_a.Resolve(handle_b) == sphere_b,
                "同型 2 つがそれぞれ正しい実体へ解決される");

            check.Expect(
                resolver_a.FindComponentByStableID(handle, sphere_b->StableID()) == handle_b,
                "StableID から目的の Component を引ける");

            check.Expect(resolver_a.FindComponents(handle,
                Components::SphereColliderComponent::StaticTypeID()).size() == 2,
                "型 ID で 2 つとも列挙できる");

            // ---- 型違い -------------------------------------------------------

            ComponentHandle wrong_type = handle_a;
            wrong_type.type_id = Components::HealthComponent::StaticTypeID();
            check.Expect(
                resolver_a.TryResolve(wrong_type, component_out) == RuntimeStatus::TypeMismatch,
                "期待する型が違えば TypeMismatch");

            Components::HealthComponent* health_out = nullptr;
            check.Expect(
                resolver_a.TryResolveAs(handle_a, health_out) == RuntimeStatus::TypeMismatch,
                "TryResolveAs で型が違えば TypeMismatch");

            Components::SphereColliderComponent* sphere_out = nullptr;
            check.Expect(
                resolver_a.TryResolveAs(handle_a, sphere_out) == RuntimeStatus::Ok &&
                sphere_out == sphere_a,
                "TryResolveAs で正しい型なら解決できる");

            // ---- Component の遅延削除 -------------------------------------------

            const Core::ComponentStableID removed_stable_id = sphere_b->StableID();

            object->RemoveComponent(sphere_b);
            check.Expect(
                resolver_a.TryResolve(handle_b, component_out) ==
                RuntimeStatus::ComponentDestroyed,
                "削除予約された時点で ComponentDestroyed");
            check.Expect(resolver_a.IsValid(handle_a),
                "同じ GameObject の別 Component は影響を受けない");

            world_a.ProcessPendingOperations();
            check.Expect(
                resolver_a.TryResolve(handle_b, component_out) ==
                RuntimeStatus::ComponentDestroyed,
                "実際に破棄されたあとも ComponentDestroyed");
            check.Expect(resolver_a.IsValid(handle_a),
                "破棄の後片付け後も生き残った Component の Handle は有効");

            // ---- 作り直しても古い Handle は無効 ------------------------------------

            auto* sphere_c = object->AddComponent<Components::SphereColliderComponent>();
            check.Expect(sphere_c != nullptr, "削除後に同じ型を追加できる");
            if (sphere_c != nullptr)
            {
                check.Expect(sphere_c->StableID() != removed_stable_id,
                    "削除した Component の StableID は再利用しない");
                check.Expect(sphere_c->InstanceID() != handle_b.instance,
                    "削除した Component の InstanceID は再利用しない");
                check.Expect(
                    resolver_a.TryResolve(handle_b, component_out) ==
                    RuntimeStatus::ComponentDestroyed,
                    "作り直したあとも古い Component Handle は無効");
            }
        }

        // ---- 親子 -----------------------------------------------------------

        GameObject* child = world_a.CreateGameObject("Child");
        check.Expect(child != nullptr, "子 GameObject を生成できる");

        ObjectHandle child_handle = ObjectHandle::None();
        ObjectID reusable_id = ObjectID::Invalid();
        if (child != nullptr)
        {
            child->SetParent(object, false);
            child_handle = resolver_a.MakeHandle(child);
            reusable_id = child->ID();

            check.Expect(resolver_a.GetParentHandle(child_handle) == handle,
                "親 Handle を取得できる");

            const std::vector<ObjectHandle> children = resolver_a.GetChildrenHandles(handle);
            check.Expect(children.size() == 1 && children[0] == child_handle,
                "子 Handle を列挙できる");
        }

        // ---- 操作対象 --------------------------------------------------------

        check.Expect(resolver_a.GetControlledObjectHandle().IsEmpty(),
            "操作対象が未設定なら空 Handle（勝手に選ばない）");
        world_a.Services().SetControlledObject(object->ID());
        check.Expect(resolver_a.GetControlledObjectHandle() == handle,
            "設定された操作対象の Handle を取得できる");

        // ---- GameObject の遅延削除と ObjectID 再利用 ---------------------------

        if (child != nullptr)
        {
            world_a.DestroyGameObject(child);
            check.Expect(
                resolver_a.TryResolve(child_handle, resolved) == RuntimeStatus::ObjectDestroyed,
                "削除予約された時点で ObjectDestroyed");

            world_a.ProcessPendingOperations();
            check.Expect(
                resolver_a.TryResolve(child_handle, resolved) == RuntimeStatus::ObjectDestroyed,
                "実際に破棄されたあとも ObjectDestroyed");

            GameObject* recreated = world_a.CreateGameObjectWithID(reusable_id, "Recreated");
            check.Expect(recreated != nullptr && recreated->ID() == reusable_id,
                "破棄した ObjectID を復元して作り直せる");
            if (recreated != nullptr)
            {
                check.Expect(recreated->Generation() != child_handle.generation,
                    "作り直した GameObject の世代番号は前回と違う");
                check.Expect(
                    resolver_a.TryResolve(child_handle, resolved) ==
                    RuntimeStatus::ObjectDestroyed,
                    "同じ ObjectID が再利用されても古い Handle は無効");
                check.Expect(resolver_a.IsValid(resolver_a.MakeHandle(recreated)),
                    "作り直した GameObject の新しい Handle は有効");
            }
        }

        // ---- Scene 再読み込み相当 (Clear) --------------------------------------

        const Core::WorldInstanceID before_clear = world_a.WorldInstanceID();
        world_a.Clear();
        check.Expect(world_a.WorldInstanceID() != before_clear,
            "Clear() で WorldInstanceID が採り直される");
        check.Expect(resolver_a.TryResolve(handle, resolved) == RuntimeStatus::WrongWorld,
            "Clear() 後は Clear 前の Handle が WrongWorld になる");

        // ---- 診断カウンタ ------------------------------------------------------

        HandleDiagnostics diagnostics;
        HandleResolver counted(world_a, &diagnostics);

        counted.TryResolve(handle, resolved);
        check.Expect(diagnostics.object_resolve_failure == 1 &&
            diagnostics.last_object_failure == RuntimeStatus::WrongWorld,
            "Object の解決失敗が記録される");

        GameObject* fresh = world_a.CreateGameObject("Fresh");
        const ObjectHandle fresh_handle = counted.MakeHandle(fresh);
        counted.TryResolve(fresh_handle, resolved);
        check.Expect(diagnostics.object_resolve_success == 1,
            "Object の解決成功が記録される");

        diagnostics.Reset();
        ComponentHandle stale_component = ComponentHandle::None();
        stale_component.owner = handle;
        stale_component.instance = 1;
        counted.TryResolve(stale_component, component_out);
        check.Expect(diagnostics.component_resolve_failure == 1 &&
            diagnostics.object_resolve_failure == 0,
            "Component の解決失敗を Object 側へ二重計上しない");

        // ---- 結果 -------------------------------------------------------------

        if (check.Result() == 0)
        {
            std::fprintf(stderr, "Handle validation OK: %d checks passed\n", check.Total());
            return 0;
        }

        std::fprintf(stderr, "Handle validation FAILED: %d/%d checks failed (first=%d)\n",
            check.Failures(), check.Total(), check.Result());
        return check.Result();
    }
}
