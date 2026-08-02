#include "RuntimeContext.h"

#include "../Events/EventBus.h"
#include "../../Object/Component/Component.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Scene/Runtime/Scene.h"

namespace ReplayEngine::Runtime
{
    using Core::Component;
    using Core::GameObject;
    using Core::ObjectID;

    RuntimeContext::RuntimeContext(Scene::Scene& world) noexcept
        : world_(&world),
        resolver_(world, &diagnostics_),
        events_(std::make_unique<EventBus>())
    {
    }

    RuntimeContext::~RuntimeContext() = default;

    Core::WorldInstanceID RuntimeContext::CurrentWorldID() const noexcept
    {
        return world_->WorldInstanceID();
    }

    GameObject* RuntimeContext::ResolveObject(const ObjectHandle& handle,
        RuntimeStatus& status) const
    {
        GameObject* object = nullptr;
        status = resolver_.TryResolve(handle, object);
        return object;
    }

    // ---- Object -----------------------------------------------------------

    bool RuntimeContext::IsValid(const ObjectHandle& handle) const noexcept
    {
        return resolver_.IsValid(handle);
    }

    ObjectHandle RuntimeContext::FindByObjectID(ObjectID id) const noexcept
    {
        return resolver_.FindByObjectID(id);
    }

    ObjectHandle RuntimeContext::ControlledObject() const noexcept
    {
        return resolver_.GetControlledObjectHandle();
    }

    RuntimeStatus RuntimeContext::GetName(const ObjectHandle& handle, std::string& out) const
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        const GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;
        out = object->Name();
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::SetName(const ObjectHandle& handle, const std::string& name)
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;
        object->SetName(name);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::SetEnabled(const ObjectHandle& handle, bool enabled)
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;
        object->SetEnabled(enabled);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::IsEnabled(const ObjectHandle& handle, bool& out) const
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        const GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;
        out = object->Enabled();
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::GetParent(const ObjectHandle& handle,
        ObjectHandle& out) const
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        const GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;
        out = resolver_.MakeHandle(object->Parent());
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::GetChildren(const ObjectHandle& handle,
        std::vector<ObjectHandle>& out) const
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        if (ResolveObject(handle, status) == nullptr) return status;
        out = resolver_.GetChildrenHandles(handle);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::SetParent(const ObjectHandle& child,
        const ObjectHandle& parent, bool preserve_world_transform)
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        GameObject* child_object = ResolveObject(child, status);
        if (child_object == nullptr) return status;

        // 空 Handle は「Scene 直下へ移す」意味。無効 Handle とは区別する。
        GameObject* parent_object = nullptr;
        if (!parent.IsEmpty())
        {
            parent_object = ResolveObject(parent, status);
            if (parent_object == nullptr) return status;
        }

        // 循環や Scene 跨ぎは GameObject 側が弾く。
        return child_object->SetParent(parent_object, preserve_world_transform)
            ? RuntimeStatus::Ok : RuntimeStatus::InvalidArgument;
    }

    // ---- Transform ---------------------------------------------------------

    RuntimeStatus RuntimeContext::GetLocalPosition(const ObjectHandle& handle,
        DirectX::XMFLOAT3& out) const
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        const GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;
        out = object->GetTransform().LocalPosition();
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::SetLocalPosition(const ObjectHandle& handle,
        const DirectX::XMFLOAT3& value)
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;
        object->GetTransform().SetLocalPosition(value);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::GetLocalRotationEuler(const ObjectHandle& handle,
        DirectX::XMFLOAT3& out) const
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        const GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;
        out = object->GetTransform().LocalRotationEuler();
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::SetLocalRotationEuler(const ObjectHandle& handle,
        const DirectX::XMFLOAT3& value)
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;
        object->GetTransform().SetLocalRotationEuler(value);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::GetLocalScale(const ObjectHandle& handle,
        DirectX::XMFLOAT3& out) const
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        const GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;
        out = object->GetTransform().LocalScale();
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::SetLocalScale(const ObjectHandle& handle,
        const DirectX::XMFLOAT3& value)
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;
        object->GetTransform().SetLocalScale(value);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::GetWorldPosition(const ObjectHandle& handle,
        DirectX::XMFLOAT3& out) const
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        const GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;
        out = object->GetTransform().WorldPosition();
        return RuntimeStatus::Ok;
    }

    // ---- Component ---------------------------------------------------------

    bool RuntimeContext::HasComponent(const ObjectHandle& handle,
        Core::ComponentTypeID type_id) const noexcept
    {
        return !resolver_.FindComponent(handle, type_id).IsEmpty();
    }

    ComponentHandle RuntimeContext::GetComponent(const ObjectHandle& handle,
        Core::ComponentTypeID type_id) const noexcept
    {
        return resolver_.FindComponent(handle, type_id);
    }

    std::vector<ComponentHandle> RuntimeContext::GetComponents(const ObjectHandle& handle,
        Core::ComponentTypeID type_id) const
    {
        return resolver_.FindComponents(handle, type_id);
    }

    RuntimeStatus RuntimeContext::AddComponent(const ObjectHandle& handle,
        Core::ComponentTypeID type_id, ComponentHandle& out)
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;

        if (Core::ComponentRegistry::Find(type_id) == nullptr)
        {
            return RuntimeStatus::TypeMismatch;
        }

        Component* created = Core::ComponentRegistry::Create(type_id, *object);
        if (created == nullptr) return RuntimeStatus::UnsupportedOperation;

        out = resolver_.MakeHandle(created);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::SetComponentEnabled(const ComponentHandle& handle,
        bool enabled)
    {
        Component* component = nullptr;
        const RuntimeStatus status = resolver_.TryResolve(handle, component);
        if (component == nullptr) return status;
        component->SetEnabled(enabled);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::IsComponentEnabled(const ComponentHandle& handle,
        bool& out) const
    {
        Component* component = nullptr;
        const RuntimeStatus status = resolver_.TryResolve(handle, component);
        if (component == nullptr) return status;
        out = component->Enabled();
        return RuntimeStatus::Ok;
    }

    // ---- 生成・破棄 ---------------------------------------------------------

    RuntimeStatus RuntimeContext::CreateGameObject(const std::string& name,
        ObjectHandle& out)
    {
        GameObject* created = world_->CreateGameObject(name);
        if (created == nullptr) return RuntimeStatus::UnsupportedOperation;
        out = resolver_.MakeHandle(created);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::DestroyGameObject(const ObjectHandle& handle)
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;

        // 予約だけ。実際の破棄は Scene の同期点。
        world_->DestroyGameObject(object);

        // この Object に紐づく購読も一緒に落とす。
        // Behaviour 側の OnDestroy でも解除されるが、
        // 生ポインタ以外の経路で購読していた場合の取りこぼしを防ぐ。
        events_->UnsubscribeOwner(handle);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::DestroyComponent(const ComponentHandle& handle)
    {
        Component* component = nullptr;
        const RuntimeStatus status = resolver_.TryResolve(handle, component);
        if (component == nullptr) return status;

        GameObject* owner = component->Owner();
        if (owner == nullptr) return RuntimeStatus::ComponentDestroyed;

        return owner->RemoveComponent(component)
            ? RuntimeStatus::Ok : RuntimeStatus::UnsupportedOperation;
    }

    // ---- Prefab -------------------------------------------------------------

    RuntimeStatus RuntimeContext::InstantiatePrefab(const std::string& asset_guid,
        const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& rotation_euler,
        const DirectX::XMFLOAT3& scale, const ObjectHandle& parent, ObjectHandle& out)
    {
        if (asset_guid.empty()) return RuntimeStatus::InvalidArgument;

        // 未接続なら「できません」と返す。何もせず成功を返さない。
        if (prefab_instantiator_ == nullptr) return RuntimeStatus::ServiceUnavailable;

        ObjectID parent_id = ObjectID::Invalid();
        if (!parent.IsEmpty())
        {
            RuntimeStatus status = RuntimeStatus::Ok;
            const GameObject* parent_object = ResolveObject(parent, status);
            if (parent_object == nullptr) return status;
            parent_id = parent_object->ID();
        }

        ObjectID created = ObjectID::Invalid();
        const RuntimeStatus status = prefab_instantiator_->InstantiatePrefab(
            asset_guid, *world_, position, rotation_euler, scale, parent_id, created);
        if (Failed(status)) return status;

        out = resolver_.FindByObjectID(created);
        return out.IsEmpty() ? RuntimeStatus::SceneLoadFailed : RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::InstantiatePrefabDeferred(const std::string& asset_guid,
        const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& rotation_euler,
        const DirectX::XMFLOAT3& scale, const ObjectHandle& parent)
    {
        if (asset_guid.empty()) return RuntimeStatus::InvalidArgument;
        if (prefab_instantiator_ == nullptr) return RuntimeStatus::ServiceUnavailable;

        PendingInstantiation pending;
        pending.asset_guid = asset_guid;
        pending.position = position;
        pending.rotation = rotation_euler;
        pending.scale = scale;

        // 親は ObjectID で覚える。Handle のまま持つと、
        // 実行までの間に World が入れ替わった場合の判定が二重になる。
        // World が変われば ObjectID の解決自体が失敗するので、これで足りる。
        if (!parent.IsEmpty())
        {
            RuntimeStatus status = RuntimeStatus::Ok;
            const GameObject* parent_object = ResolveObject(parent, status);
            if (parent_object == nullptr) return status;
            pending.parent = parent_object->ID();
        }

        pending_instantiations_.push_back(std::move(pending));
        return RuntimeStatus::Ok;
    }

    void RuntimeContext::FlushDeferredOperations()
    {
        if (pending_instantiations_.empty()) return;
        if (prefab_instantiator_ == nullptr)
        {
            // 実行できないまま溜め続けない。捨てたことはログへ残す。
            LogWarning("Prefab の生成要求を破棄しました（Instantiator が未接続）。");
            pending_instantiations_.clear();
            return;
        }

        // 引き取ってから実行する。実行中に新しい要求が積まれても、
        // それは次の同期点で処理される（同じフレームで無限に増えない）。
        std::vector<PendingInstantiation> batch;
        batch.swap(pending_instantiations_);

        for (const PendingInstantiation& pending : batch)
        {
            ObjectID created = ObjectID::Invalid();
            const RuntimeStatus status = prefab_instantiator_->InstantiatePrefab(
                pending.asset_guid, *world_, pending.position, pending.rotation,
                pending.scale, pending.parent, created);
            if (Failed(status))
            {
                LogWarning(std::string("Prefab の生成に失敗しました: ") +
                    ToString(status) + " (" + pending.asset_guid + ")");
            }
        }
    }

    // ---- Physics ------------------------------------------------------------

    bool RuntimeContext::PhysicsAvailable() const noexcept
    {
        const Scene::IPhysicsQueryService* physics = world_->Services().Physics();
        return physics != nullptr && physics->CollisionAvailable();
    }

    RuntimeStatus RuntimeContext::QueryGround(const DirectX::XMFLOAT3& origin, float radius,
        float up_offset, float down_distance, float walkable_normal_y,
        const ObjectHandle& ignore, Scene::GroundHit& out) const
    {
        const Scene::IPhysicsQueryService* physics = world_->Services().Physics();
        if (physics == nullptr || !physics->CollisionAvailable())
        {
            return RuntimeStatus::ServiceUnavailable;
        }

        Scene::CollisionQueryFilter filter;
        if (!ignore.IsEmpty()) filter.ignore_object = ignore.object;

        physics->QueryGroundFiltered(origin, radius, up_offset, down_distance,
            walkable_normal_y, filter, out);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::SweepSphere(const DirectX::XMFLOAT3& start,
        const DirectX::XMFLOAT3& end, float radius, float maximum_normal_y,
        const ObjectHandle& ignore, Scene::SphereSweepHit& out) const
    {
        const Scene::IPhysicsQueryService* physics = world_->Services().Physics();
        if (physics == nullptr || !physics->CollisionAvailable())
        {
            return RuntimeStatus::ServiceUnavailable;
        }

        Scene::CollisionQueryFilter filter;
        if (!ignore.IsEmpty()) filter.ignore_object = ignore.object;

        physics->SweepSphereFiltered(start, end, radius, maximum_normal_y, filter, out);
        return RuntimeStatus::Ok;
    }

    // ---- Log ----------------------------------------------------------------

    void RuntimeContext::Log(LogLevel level, const std::string& message,
        const ObjectHandle& source) const
    {
        // 出力先が無ければ捨てる。ここで OutputDebugString を直接呼ばない。
        // Runtime 層が Windows API と Editor の表示方法へ依存しないようにするため。
        if (log_sink_ != nullptr) log_sink_->Write(level, message, source);
    }

    void RuntimeContext::LogInfo(const std::string& message, const ObjectHandle& source) const
    {
        Log(LogLevel::Info, message, source);
    }

    void RuntimeContext::LogWarning(const std::string& message,
        const ObjectHandle& source) const
    {
        Log(LogLevel::Warning, message, source);
    }

    void RuntimeContext::LogError(const std::string& message,
        const ObjectHandle& source) const
    {
        Log(LogLevel::Error, message, source);
    }
}
