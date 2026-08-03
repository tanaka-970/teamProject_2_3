#pragma once

#include "RuntimeHandles.h"
#include "../Core/RuntimeResult.h"

#include <cstdint>
#include <vector>

namespace ReplayEngine::Core
{
    class Component;
    class GameObject;
}

namespace ReplayEngine::Scene
{
    class Scene;
}

namespace ReplayEngine::Runtime
{
    // Handle の解決回数を数えるだけの入れ物。
    //
    // Scene 側へカウンタを持たせなかった理由:
    //   Scene の責任は GameObject の所有と更新であり、診断用の統計ではない。
    //   HandleResolver は使い捨ての軽い view にしておきたいので、
    //   数えたい側 (Phase 5 の RuntimeContext / Diagnostics パネル) が
    //   この構造体を 1 つ持ち、resolver へ渡す形にする。
    //   渡さなければ数えないだけで、動作は変わらない。
    struct HandleDiagnostics final
    {
        std::uint64_t object_resolve_success = 0;
        std::uint64_t object_resolve_failure = 0;
        std::uint64_t component_resolve_success = 0;
        std::uint64_t component_resolve_failure = 0;

        // 直近の失敗理由。Diagnostics 表示用。
        RuntimeStatus last_object_failure = RuntimeStatus::Ok;
        RuntimeStatus last_component_failure = RuntimeStatus::Ok;

        void Reset() noexcept
        {
            object_resolve_success = 0;
            object_resolve_failure = 0;
            component_resolve_success = 0;
            component_resolve_failure = 0;
            last_object_failure = RuntimeStatus::Ok;
            last_component_failure = RuntimeStatus::Ok;
        }
    };

    // ObjectHandle / ComponentHandle と実体の間を取り持つ。
    //
    // Scene を所有しない軽い view。値渡ししてよい。
    // Scene の寿命より長く保持しないこと（Behaviour の中で一時的に作るのが基本）。
    //
    // 失敗しても assert しない。理由を RuntimeStatus で返すだけにする。
    // 壊れた Handle を渡されるのは Script API では異常ではなく通常だから。
    class HandleResolver final
    {
    public:
        explicit HandleResolver(Scene::Scene& world,
            HandleDiagnostics* diagnostics = nullptr) noexcept;

        Scene::Scene& World() const noexcept { return *world_; }
        Core::WorldInstanceID WorldID() const noexcept;

        // ---- 実体から Handle を作る ----------------------------------------
        //
        // 別の Scene の実体を渡された場合は空 Handle を返す（黙って別 World の
        // Handle を作らない）。nullptr でも空 Handle を返すだけで落ちない。

        ObjectHandle MakeHandle(const Core::GameObject* object) const noexcept;
        ComponentHandle MakeHandle(const Core::Component* component) const noexcept;

        // ---- 解決 ----------------------------------------------------------

        bool IsValid(const ObjectHandle& handle) const noexcept;
        bool IsValid(const ComponentHandle& handle) const noexcept;

        // 失敗理由つきの解決。out は成功時のみ書き換える。
        RuntimeStatus TryResolve(const ObjectHandle& handle,
            Core::GameObject*& out) const noexcept;
        RuntimeStatus TryResolve(const ComponentHandle& handle,
            Core::Component*& out) const noexcept;

        // 理由が要らない場合。失敗は nullptr。
        Core::GameObject* Resolve(const ObjectHandle& handle) const noexcept;
        Core::Component* Resolve(const ComponentHandle& handle) const noexcept;

        // 期待する型で解決する。型が違えば TypeMismatch。
        template<class T>
        RuntimeStatus TryResolveAs(const ComponentHandle& handle, T*& out) const noexcept
        {
            Core::Component* component = nullptr;
            const RuntimeStatus status = TryResolve(handle, component);
            if (Failed(status)) return status;

            T* casted = dynamic_cast<T*>(component);
            if (casted == nullptr) return RuntimeStatus::TypeMismatch;

            out = casted;
            return RuntimeStatus::Ok;
        }

        // ---- 階層 ----------------------------------------------------------

        ObjectHandle GetParentHandle(const ObjectHandle& handle) const noexcept;
        std::vector<ObjectHandle> GetChildrenHandles(const ObjectHandle& handle) const;

        // ---- 検索 ----------------------------------------------------------

        // 主要な検索経路。ObjectID から現在の World の Handle を作る。
        ObjectHandle FindByObjectID(Core::ObjectID id) const noexcept;

        // 補助機能。名前は一意ではないので主要な参照手段にしないこと。
        // 同名が複数ある場合は最初に見つかったものを返す。
        ObjectHandle FindByName(const std::string& name) const noexcept;

        // Scene が持つ操作対象。設定されていなければ空 Handle。
        // 「居なければ誰かを選ぶ」ことはしない。
        ObjectHandle GetControlledObjectHandle() const noexcept;

        // ---- Component 検索 -------------------------------------------------

        // 同じ型が複数ある場合は最初の 1 つ。
        ComponentHandle FindComponent(const ObjectHandle& owner,
            Core::ComponentTypeID type_id) const noexcept;

        std::vector<ComponentHandle> FindComponents(const ObjectHandle& owner,
            Core::ComponentTypeID type_id) const;

        // 保存済み参照 (ComponentReference) の解決に使う。
        // Phase 2 の ComponentReference はこの経路を通る。
        ComponentHandle FindComponentByStableID(const ObjectHandle& owner,
            Core::ComponentStableID stable_id) const noexcept;

    private:
        void CountObject(RuntimeStatus status) const noexcept;
        void CountComponent(RuntimeStatus status) const noexcept;

        Scene::Scene* world_ = nullptr;
        HandleDiagnostics* diagnostics_ = nullptr;
    };
}
