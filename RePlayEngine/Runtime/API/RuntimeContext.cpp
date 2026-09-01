// RuntimeContext のうち「Context の接続」と「Object API」だけを持つ。
//
//   RuntimeContext.cpp          … Context 接続と Object・Transform・Component API（このファイル）
//   RuntimeContextMotion.cpp    … Motion Player API
//   RuntimeContextScene.cpp     … Scene 遷移と Prefab 生成 API
//   RuntimeContextServices.cpp  … Physics と Log の Service API

#include "RuntimeContext.h"

#include "../Events/EventBus.h"
#include "../../Components/Motion/MotionPlayerComponent.h"
#include "../../Object/Component/Component.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Scripting/Core/ScriptComponent.h"
#include "../../Components/UI/UISelectableComponent.h"
#include "../../UI/UIFocusManager.h"

#include <algorithm>
#include <functional>
#include <unordered_set>
#include <utility>
#include <vector>

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

    RuntimeContext::~RuntimeContext()
    {
        // 自分が結ばれている World から back-pointer を外してから消える。
        //
        // なぜ必要か:
        //   RuntimeSceneService が World を所有し、RuntimeContext はその World を
        //   参照する。両方を同じスコープに置くと、宣言順によっては
        //   Context の方が先に消える。
        //   そのあと World が破棄されると、Component の OnRuntimeDestroy が
        //   Services().Runtime() 経由で破棄済みの Context を触りにいく。
        //   （実際に EventBus::UnsubscribeOwner でクラッシュした。）
        //
        //   ここで参照を外しておけば、World 側は「Runtime 未接続」として
        //   破棄されるだけになる。未接続は元から正常な状態なので、
        //   破棄経路に特別な分岐を足さずに済む。
        //
        // 前提となる寿命の約束:
        //   RuntimeContext は World の view であり、World より長生きしない。
        //   つまり「World（または World を所有する RuntimeSceneService）を先に宣言し、
        //   RuntimeContext を後に宣言する」。この順なら Context の方が先に消え、
        //   ここで触る World は必ず生きている。
        if (world_ != nullptr && world_->Services().Runtime() == this)
        {
            world_->Services().SetRuntime(nullptr);
        }
    }

    Core::WorldInstanceID RuntimeContext::CurrentWorldID() const noexcept
    {
        return world_->WorldInstanceID();
    }

    void RuntimeContext::Rebind(Scene::Scene& world)
    {
        // 旧 World には触れない。
        //
        // Rebind は「旧 World を解放したあと」に呼ばれることがある
        // （RuntimeSceneService の入れ替えがまさにそれ）。
        // ここで旧 World を読みにいくと解放済みメモリへ触る。
        // 旧 World 側の後始末は、World をまだ持っている側の責任にする。
        world_ = &world;

        // resolver_ は Scene への参照を値で持つ view なので作り直す。
        // 診断カウンタも同時に 0 へ戻り、World ごとの統計として読める。
        diagnostics_.Reset();
        resolver_ = HandleResolver(world, &diagnostics_);

        // 旧 World に紐づいていたものを捨てる。
        // 持ち越すと、消えた Object を指す購読と生成要求が残る。
        events_->Clear();
        pending_instantiations_.clear();
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

    ObjectHandle RuntimeContext::FindByName(const std::string& name) const noexcept
    {
        return resolver_.FindByName(name);
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
        out = ComponentHandle::None();
        RuntimeStatus status = RuntimeStatus::Ok;
        GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;

        const Core::ComponentTypeInfo* requested = Core::ComponentRegistry::Find(type_id);
        if (requested == nullptr || !requested->runtime_available)
        {
            return RuntimeStatus::TypeMismatch;
        }

        // EditorのAddComponentPanelが持つ依存解決と同じメタデータをRuntimeでも使う。
        // 依存の型ごとに分岐を書かず、ComponentTypeInfo.required_componentsだけを見る。
        std::vector<Core::ComponentTypeID> creation_order;
        std::unordered_set<Core::ComponentTypeID> visiting;
        std::unordered_set<Core::ComponentTypeID> planned;
        std::function<bool(Core::ComponentTypeID)> collect;
        collect = [&](Core::ComponentTypeID current) -> bool
        {
            const Core::ComponentTypeInfo* info =
                Core::ComponentRegistry::Find(current);
            if (info == nullptr || !info->runtime_available || !info->factory)
                return false;
            if (!visiting.insert(current).second) return false;

            for (const Core::ComponentTypeID required_id : info->required_components)
            {
                if (object->FindComponent(required_id) != nullptr) continue;
                if (!collect(required_id))
                {
                    visiting.erase(current);
                    return false;
                }
            }
            visiting.erase(current);

            const bool already_present = object->FindComponent(current) != nullptr;
            if ((!already_present || info->allow_multiple) && planned.insert(current).second)
                creation_order.push_back(current);
            return true;
        };

        if (!collect(type_id)) return RuntimeStatus::ComponentDependencyMissing;

        Component* requested_component = object->FindComponent(type_id);
        for (const Core::ComponentTypeID create_id : creation_order)
        {
            Component* created = Core::ComponentRegistry::Create(create_id, *object);
            if (created == nullptr) return RuntimeStatus::UnsupportedOperation;
            if (create_id == type_id) requested_component = created;
        }
        if (requested_component == nullptr) return RuntimeStatus::UnsupportedOperation;

        out = resolver_.MakeHandle(requested_component);
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
    RuntimeStatus RuntimeContext::GetScriptField(const ComponentHandle& handle,
        const std::string& field_name, Reflection::PropertyValue& out) const
    {
        out = Reflection::PropertyValue{};
        if (field_name.empty()) return RuntimeStatus::InvalidArgument;

        Component* component = nullptr;
        const RuntimeStatus status = resolver_.TryResolve(handle, component);
        if (status != RuntimeStatus::Ok) return status;

        const Scripting::ScriptComponent* script =
            component != nullptr ? Scripting::ScriptComponent::From(*component) : nullptr;
        if (script == nullptr) return RuntimeStatus::TypeMismatch;
        if (!script->Schema()) return RuntimeStatus::ServiceUnavailable;

        const std::string saved_name = Scripting::ScriptNames::IsFieldSavedName(field_name)
            ? field_name : Scripting::ScriptNames::MakeFieldSavedName(field_name);
        if (script->Schema()->FindBySavedName(saved_name) == nullptr)
            return RuntimeStatus::InvalidArgument;

        out = script->ReadField(saved_name);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::SetScriptField(const ComponentHandle& handle,
        const std::string& field_name, const Reflection::PropertyValue& value)
    {
        if (field_name.empty() || !value.IsFinite()) return RuntimeStatus::InvalidArgument;

        Component* component = nullptr;
        const RuntimeStatus status = resolver_.TryResolve(handle, component);
        if (status != RuntimeStatus::Ok) return status;

        Scripting::ScriptComponent* script =
            component != nullptr ? Scripting::ScriptComponent::From(*component) : nullptr;
        if (script == nullptr) return RuntimeStatus::TypeMismatch;
        if (!script->Schema()) return RuntimeStatus::ServiceUnavailable;

        const std::string saved_name = Scripting::ScriptNames::IsFieldSavedName(field_name)
            ? field_name : Scripting::ScriptNames::MakeFieldSavedName(field_name);
        if (script->Schema()->FindBySavedName(saved_name) == nullptr)
            return RuntimeStatus::InvalidArgument;
        if (script->Schema()->FindBySavedName(saved_name)->read_only)
            return RuntimeStatus::UnsupportedOperation;
        return script->TryWriteRuntimeField(saved_name, value)
            ? RuntimeStatus::Ok : RuntimeStatus::TypeMismatch;
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

        // 必須依存を壊してComponentだけが残る状態をRuntime APIから作らない。
        for (std::size_t index = 0; index < owner->ComponentCount(); ++index)
        {
            Component* other = owner->ComponentAt(index);
            if (other == nullptr || other == component || other->PendingDestroy()) continue;
            const Core::ComponentTypeInfo* info =
                Core::ComponentRegistry::Find(other->TypeID());
            if (info == nullptr) continue;
            if (std::find(info->required_components.begin(),
                info->required_components.end(), component->TypeID()) !=
                info->required_components.end())
            {
                return RuntimeStatus::ComponentHasDependents;
            }
        }

        return owner->RemoveComponent(component)
            ? RuntimeStatus::Ok : RuntimeStatus::UnsupportedOperation;
    }
    RuntimeStatus RuntimeContext::GetUIFocus(ObjectHandle& out)
    {
        out = ObjectHandle::None();
        if (world_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        Components::UISelectableComponent* current = UI::UIFocusManager::Current(*world_);
        if (current == nullptr || current->Owner() == nullptr) return RuntimeStatus::Ok;
        out = resolver_.MakeHandle(current->Owner());
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::SetUIFocus(const ObjectHandle& object)
    {
        if (world_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        if (object.IsEmpty())
        {
            UI::UIFocusManager::SetFocus(*world_, nullptr);
            return RuntimeStatus::Ok;
        }

        RuntimeStatus status = RuntimeStatus::Ok;
        GameObject* owner = ResolveObject(object, status);
        if (owner == nullptr) return status;
        Components::UISelectableComponent* selectable =
            owner->GetComponent<Components::UISelectableComponent>();
        if (selectable == nullptr) return RuntimeStatus::ComponentNotFound;
        UI::UIFocusManager::SetFocus(*world_, selectable);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::FindUIFocusInDirection(const ObjectHandle& from,
        int direction, ObjectHandle& out)
    {
        out = ObjectHandle::None();
        if (world_ == nullptr) return RuntimeStatus::ServiceUnavailable;
        if (direction < 0 || direction > 3) return RuntimeStatus::InvalidArgument;

        RuntimeStatus status = RuntimeStatus::Ok;
        GameObject* owner = ResolveObject(from, status);
        if (owner == nullptr) return status;
        Components::UISelectableComponent* selectable =
            owner->GetComponent<Components::UISelectableComponent>();
        if (selectable == nullptr) return RuntimeStatus::ComponentNotFound;

        Components::UISelectableComponent* next = UI::UIFocusManager::FindInDirection(
            *world_, *selectable, static_cast<UI::UIFocusManager::Direction>(direction));
        if (next != nullptr && next->Owner() != nullptr) out = resolver_.MakeHandle(next->Owner());
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::PublishEvent(EventRecord record)
    {
        if (!record.type.IsValid()) return RuntimeStatus::InvalidArgument;
        record.frame_index = time_.frame_index;
        events_->Publish(std::move(record));
        return RuntimeStatus::Ok;
    }

}
