#include "HandleResolver.h"

#include "../../Object/Component/Component.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Scene/Runtime/Scene.h"

namespace ReplayEngine::Runtime
{
    using Core::Component;
    using Core::GameObject;
    using Core::ObjectID;

    HandleResolver::HandleResolver(Scene::Scene& world,
        HandleDiagnostics* diagnostics) noexcept
        : world_(&world), diagnostics_(diagnostics)
    {
    }

    Core::WorldInstanceID HandleResolver::WorldID() const noexcept
    {
        return world_->WorldInstanceID();
    }

    void HandleResolver::CountObject(RuntimeStatus status) const noexcept
    {
        if (diagnostics_ == nullptr) return;
        if (Succeeded(status))
        {
            ++diagnostics_->object_resolve_success;
        }
        else
        {
            ++diagnostics_->object_resolve_failure;
            diagnostics_->last_object_failure = status;
        }
    }

    void HandleResolver::CountComponent(RuntimeStatus status) const noexcept
    {
        if (diagnostics_ == nullptr) return;
        if (Succeeded(status))
        {
            ++diagnostics_->component_resolve_success;
        }
        else
        {
            ++diagnostics_->component_resolve_failure;
            diagnostics_->last_component_failure = status;
        }
    }

    // ---- 実体 -> Handle ---------------------------------------------------
    //
    // ここは「識別」だけを行う。削除予約中かどうかは見ない。
    // 予約中の実体から作った Handle は、Resolve の時点で ObjectDestroyed になる。
    // 生存確認を Resolve 側だけに集めておくと、判定が二か所へ散らばらない。

    ObjectHandle HandleResolver::MakeHandle(const GameObject* object) const noexcept
    {
        if (object == nullptr) return ObjectHandle::None();

        // 別の Scene の実体から、この World の Handle を作らない。
        if (object->GetScene() != world_) return ObjectHandle::None();

        ObjectHandle handle;
        handle.world = world_->WorldInstanceID();
        handle.object = object->ID();
        handle.generation = object->Generation();
        return handle;
    }

    ComponentHandle HandleResolver::MakeHandle(const Component* component) const noexcept
    {
        if (component == nullptr) return ComponentHandle::None();

        const GameObject* owner = component->Owner();
        if (owner == nullptr) return ComponentHandle::None();

        const ObjectHandle owner_handle = MakeHandle(owner);
        if (owner_handle.IsEmpty()) return ComponentHandle::None();

        ComponentHandle handle;
        handle.owner = owner_handle;
        handle.instance = component->InstanceID();
        handle.type_id = component->TypeID();
        return handle;
    }

    // ---- 解決 -------------------------------------------------------------

    RuntimeStatus HandleResolver::TryResolve(const ObjectHandle& handle,
        GameObject*& out) const noexcept
    {
        const RuntimeStatus status = [&]() noexcept -> RuntimeStatus
        {
            if (handle.IsEmpty()) return RuntimeStatus::InvalidHandle;

            // World が違えば、その先を見る意味がない。
            // Scene 切り替え・再読み込みをまたいだ古い参照はここで止まる。
            if (handle.world != world_->WorldInstanceID()) return RuntimeStatus::WrongWorld;

            GameObject* found = world_->FindGameObjectByID(handle.object);
            if (found == nullptr) return RuntimeStatus::ObjectDestroyed;

            // 同じ ObjectID が作り直された場合はここで弾かれる。
            if (found->Generation() != handle.generation) return RuntimeStatus::ObjectDestroyed;

            // 削除予約済みは「もう居ない」として扱う。
            // 予約から実際の破棄までの間に Script が触れないようにするため。
            if (found->PendingDestroy()) return RuntimeStatus::ObjectDestroyed;

            out = found;
            return RuntimeStatus::Ok;
        }();

        CountObject(status);
        return status;
    }

    RuntimeStatus HandleResolver::TryResolve(const ComponentHandle& handle,
        Component*& out) const noexcept
    {
        const RuntimeStatus status = [&]() noexcept -> RuntimeStatus
        {
            if (handle.IsEmpty()) return RuntimeStatus::InvalidHandle;

            GameObject* owner = nullptr;
            // 所有者側の失敗を Component 側の統計へ二重計上しないよう、
            // ここでは診断カウンタを持たない resolver で解決する。
            const HandleResolver silent(*world_, nullptr);
            const RuntimeStatus owner_status = silent.TryResolve(handle.owner, owner);
            if (Failed(owner_status)) return owner_status;

            const std::size_t count = owner->ComponentCount();
            for (std::size_t index = 0; index < count; ++index)
            {
                Component* component = owner->ComponentAt(index);
                if (component == nullptr) continue;
                if (component->InstanceID() != handle.instance) continue;

                // ここまで来れば同一実体。通し番号は再利用しないので取り違えは起きない。
                if (component->PendingDestroy()) return RuntimeStatus::ComponentDestroyed;

                if (handle.type_id != Core::invalid_component_type_id &&
                    component->TypeID() != handle.type_id)
                {
                    return RuntimeStatus::TypeMismatch;
                }

                out = component;
                return RuntimeStatus::Ok;
            }

            // 通し番号が見つからない = 既に破棄されて詰められたあと。
            return RuntimeStatus::ComponentDestroyed;
        }();

        CountComponent(status);
        return status;
    }

    bool HandleResolver::IsValid(const ObjectHandle& handle) const noexcept
    {
        GameObject* ignored = nullptr;
        return Succeeded(TryResolve(handle, ignored));
    }

    bool HandleResolver::IsValid(const ComponentHandle& handle) const noexcept
    {
        Component* ignored = nullptr;
        return Succeeded(TryResolve(handle, ignored));
    }

    GameObject* HandleResolver::Resolve(const ObjectHandle& handle) const noexcept
    {
        GameObject* result = nullptr;
        TryResolve(handle, result);
        return result;
    }

    Component* HandleResolver::Resolve(const ComponentHandle& handle) const noexcept
    {
        Component* result = nullptr;
        TryResolve(handle, result);
        return result;
    }

    // ---- 階層 -------------------------------------------------------------

    ObjectHandle HandleResolver::GetParentHandle(const ObjectHandle& handle) const noexcept
    {
        GameObject* object = Resolve(handle);
        if (object == nullptr) return ObjectHandle::None();
        return MakeHandle(object->Parent());
    }

    std::vector<ObjectHandle> HandleResolver::GetChildrenHandles(
        const ObjectHandle& handle) const
    {
        std::vector<ObjectHandle> result;

        GameObject* object = Resolve(handle);
        if (object == nullptr) return result;

        const std::vector<GameObject*>& children = object->Children();
        result.reserve(children.size());
        for (GameObject* child : children)
        {
            if (child == nullptr || child->PendingDestroy()) continue;
            const ObjectHandle child_handle = MakeHandle(child);
            if (!child_handle.IsEmpty()) result.push_back(child_handle);
        }
        return result;
    }

    // ---- 検索 -------------------------------------------------------------

    ObjectHandle HandleResolver::FindByObjectID(ObjectID id) const noexcept
    {
        GameObject* found = world_->FindGameObjectByID(id);
        if (found == nullptr || found->PendingDestroy()) return ObjectHandle::None();
        return MakeHandle(found);
    }

    ObjectHandle HandleResolver::FindByName(const std::string& name) const noexcept
    {
        return MakeHandle(world_->FindGameObjectByName(name));
    }

    ObjectHandle HandleResolver::GetControlledObjectHandle() const noexcept
    {
        const ObjectID controlled = world_->Services().ControlledObject();
        if (!controlled.Valid()) return ObjectHandle::None();
        return FindByObjectID(controlled);
    }

    // ---- Component 検索 ---------------------------------------------------

    ComponentHandle HandleResolver::FindComponent(const ObjectHandle& owner,
        Core::ComponentTypeID type_id) const noexcept
    {
        GameObject* object = Resolve(owner);
        if (object == nullptr) return ComponentHandle::None();
        return MakeHandle(object->FindComponent(type_id));
    }

    std::vector<ComponentHandle> HandleResolver::FindComponents(const ObjectHandle& owner,
        Core::ComponentTypeID type_id) const
    {
        std::vector<ComponentHandle> result;

        GameObject* object = Resolve(owner);
        if (object == nullptr) return result;

        const std::size_t count = object->ComponentCount();
        result.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            Component* component = object->ComponentAt(index);
            if (component == nullptr || component->PendingDestroy()) continue;
            if (type_id != Core::invalid_component_type_id &&
                component->TypeID() != type_id)
            {
                continue;
            }
            const ComponentHandle handle = MakeHandle(component);
            if (!handle.IsEmpty()) result.push_back(handle);
        }
        return result;
    }

    ComponentHandle HandleResolver::FindComponentByStableID(const ObjectHandle& owner,
        Core::ComponentStableID stable_id) const noexcept
    {
        if (stable_id == Core::invalid_component_stable_id) return ComponentHandle::None();

        GameObject* object = Resolve(owner);
        if (object == nullptr) return ComponentHandle::None();
        return MakeHandle(object->FindComponentByStableID(stable_id));
    }
}
