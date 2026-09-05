// RuntimeContext のうち「World Transform」「Component プロパティ」「Rigidbody」を持つ。
//
//   RuntimeContext.cpp             … Context 接続と Object・Transform・Component API
//   RuntimeContextComponentApi.cpp … World Transform / 汎用プロパティ / Rigidbody（このファイル）
//   RuntimeContextMotion.cpp       … Motion Player API
//   RuntimeContextScene.cpp        … Scene 遷移と Prefab 生成 API
//   RuntimeContextServices.cpp     … Physics と Log の Service API

#include "RuntimeContext.h"

#include "../../Components/Audio/AudioSourceComponent.h"
#include "../../Components/Physics/RigidbodyComponent.h"
#include "../../Components/Rendering/AnimatorComponent.h"
#include "../../Components/Rendering/ParticleEmitterComponent.h"
#include "../../Object/Component/Component.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Reflection/Property/PropertyDesc.h"
#include "../../Reflection/Registry/PropertyRegistry.h"
#include "../../Scene/Runtime/Scene.h"

#include <cmath>

namespace ReplayEngine::Runtime
{
    using Core::Component;
    using Core::GameObject;
    using namespace DirectX;

    namespace
    {
        bool FiniteVector(const XMFLOAT3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                std::isfinite(value.z);
        }

        // Scale * Rotation * Translation を組み直して Transform へ書き戻す。
        void StoreWorld(Core::Transform& transform, const XMFLOAT3& scale,
            FXMVECTOR rotation, const XMFLOAT3& position) noexcept
        {
            const XMMATRIX world =
                XMMatrixScaling(scale.x, scale.y, scale.z) *
                XMMatrixRotationQuaternion(rotation) *
                XMMatrixTranslation(position.x, position.y, position.z);
            transform.SetFromWorldMatrix(world);
        }
    }

    // ---- World Transform ----------------------------------------------------

    RuntimeStatus RuntimeContext::SetWorldPosition(const ObjectHandle& handle,
        const XMFLOAT3& value)
    {
        if (!FiniteVector(value)) return RuntimeStatus::InvalidArgument;
        RuntimeStatus status = RuntimeStatus::Ok;
        GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;
        object->GetTransform().SetWorldPosition(value);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::GetWorldRotationQuaternion(const ObjectHandle& handle,
        XMFLOAT4& out) const
    {
        out = XMFLOAT4{ 0.0f, 0.0f, 0.0f, 1.0f };
        RuntimeStatus status = RuntimeStatus::Ok;
        const GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;
        out = object->GetTransform().WorldRotationQuaternion();
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::SetWorldRotationQuaternion(const ObjectHandle& handle,
        const XMFLOAT4& value)
    {
        if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
            !std::isfinite(value.z) || !std::isfinite(value.w))
        {
            return RuntimeStatus::InvalidArgument;
        }
        const XMVECTOR quaternion = XMLoadFloat4(&value);
        // 長さ 0 の四元数は回転を表せない。既存の姿勢を壊さずに弾く。
        if (XMVector4Less(XMVector4LengthSq(quaternion), XMVectorReplicate(1.0e-12f)))
            return RuntimeStatus::InvalidArgument;

        RuntimeStatus status = RuntimeStatus::Ok;
        GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;

        Core::Transform& transform = object->GetTransform();
        StoreWorld(transform, transform.WorldScale(),
            XMQuaternionNormalize(quaternion), transform.WorldPosition());
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::GetWorldScale(const ObjectHandle& handle,
        XMFLOAT3& out) const
    {
        out = XMFLOAT3{ 1.0f, 1.0f, 1.0f };
        RuntimeStatus status = RuntimeStatus::Ok;
        const GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;
        out = object->GetTransform().WorldScale();
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::SetWorldScale(const ObjectHandle& handle,
        const XMFLOAT3& value)
    {
        if (!FiniteVector(value)) return RuntimeStatus::InvalidArgument;
        RuntimeStatus status = RuntimeStatus::Ok;
        GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;

        Core::Transform& transform = object->GetTransform();
        const XMFLOAT4 rotation = transform.WorldRotationQuaternion();
        StoreWorld(transform, value, XMLoadFloat4(&rotation), transform.WorldPosition());
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::GetWorldAxes(const ObjectHandle& handle,
        XMFLOAT3& forward, XMFLOAT3& right, XMFLOAT3& up) const
    {
        forward = XMFLOAT3{ 0.0f, 0.0f, 1.0f };
        right = XMFLOAT3{ 1.0f, 0.0f, 0.0f };
        up = XMFLOAT3{ 0.0f, 1.0f, 0.0f };

        RuntimeStatus status = RuntimeStatus::Ok;
        const GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;

        const XMFLOAT4 quaternion = object->GetTransform().WorldRotationQuaternion();
        const XMMATRIX rotation = XMMatrixRotationQuaternion(XMLoadFloat4(&quaternion));
        XMStoreFloat3(&right, XMVector3Normalize(rotation.r[0]));
        XMStoreFloat3(&up, XMVector3Normalize(rotation.r[1]));
        XMStoreFloat3(&forward, XMVector3Normalize(rotation.r[2]));
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::LookAt(const ObjectHandle& handle,
        const XMFLOAT3& target, const XMFLOAT3& world_up)
    {
        if (!FiniteVector(target) || !FiniteVector(world_up))
            return RuntimeStatus::InvalidArgument;

        RuntimeStatus status = RuntimeStatus::Ok;
        GameObject* object = ResolveObject(handle, status);
        if (object == nullptr) return status;

        Core::Transform& transform = object->GetTransform();
        const XMFLOAT3 position = transform.WorldPosition();
        const XMVECTOR eye = XMLoadFloat3(&position);
        const XMVECTOR focus = XMLoadFloat3(&target);
        const XMVECTOR direction = XMVectorSubtract(focus, eye);
        if (XMVector3Less(XMVector3LengthSq(direction), XMVectorReplicate(1.0e-12f)))
            return RuntimeStatus::InvalidArgument;

        const XMVECTOR forward = XMVector3Normalize(direction);
        XMVECTOR up = XMVector3Normalize(XMLoadFloat3(&world_up));
        XMVECTOR right = XMVector3Cross(up, forward);
        // 上方向と前方向が平行だと基底が作れない。別の上方向へ倒して続ける。
        if (XMVector3Less(XMVector3LengthSq(right), XMVectorReplicate(1.0e-8f)))
        {
            up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
            right = XMVector3Cross(up, forward);
            if (XMVector3Less(XMVector3LengthSq(right), XMVectorReplicate(1.0e-8f)))
                return RuntimeStatus::InvalidArgument;
        }
        right = XMVector3Normalize(right);
        up = XMVector3Cross(forward, right);

        XMMATRIX basis = XMMatrixIdentity();
        basis.r[0] = right;
        basis.r[1] = up;
        basis.r[2] = forward;
        StoreWorld(transform, transform.WorldScale(),
            XMQuaternionRotationMatrix(basis), position);
        return RuntimeStatus::Ok;
    }

    // ---- Component の型とプロパティ -------------------------------------------

    Core::ComponentTypeID RuntimeContext::FindComponentTypeId(
        const std::string& type_name) const noexcept
    {
        if (type_name.empty()) return Core::invalid_component_type_id;
        const Core::ComponentTypeInfo* info = Core::ComponentRegistry::Find(type_name);
        return info != nullptr ? info->type_id : Core::invalid_component_type_id;
    }

    RuntimeStatus RuntimeContext::GetComponentTypeName(const ComponentHandle& handle,
        std::string& out) const
    {
        out.clear();
        Component* component = nullptr;
        const RuntimeStatus status = resolver_.TryResolve(handle, component);
        if (status != RuntimeStatus::Ok) return status;
        if (component == nullptr) return RuntimeStatus::ComponentDestroyed;

        const Core::ComponentTypeInfo* info =
            Core::ComponentRegistry::Find(component->TypeID());
        if (info == nullptr) return RuntimeStatus::ComponentNotFound;
        out = info->type_name;
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::GetComponentProperty(const ComponentHandle& handle,
        const std::string& property_name, Reflection::PropertyValue& out) const
    {
        out = Reflection::PropertyValue{};
        if (property_name.empty()) return RuntimeStatus::InvalidArgument;

        Component* component = nullptr;
        const RuntimeStatus status = resolver_.TryResolve(handle, component);
        if (status != RuntimeStatus::Ok) return status;
        if (component == nullptr) return RuntimeStatus::ComponentDestroyed;

        // 動的プロパティも見る。Effect Stack の effects[i].* はこちらにしか無い。
        const Reflection::PropertyDesc* desc =
            Reflection::PropertyRegistry::FindForComponent(*component, property_name);
        if (desc == nullptr) return RuntimeStatus::InvalidArgument;
        if (!desc->getter) return RuntimeStatus::UnsupportedOperation;

        out = desc->getter(*component);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::SetComponentProperty(const ComponentHandle& handle,
        const std::string& property_name, const Reflection::PropertyValue& value)
    {
        if (property_name.empty() || !value.IsFinite())
            return RuntimeStatus::InvalidArgument;

        Component* component = nullptr;
        const RuntimeStatus status = resolver_.TryResolve(handle, component);
        if (status != RuntimeStatus::Ok) return status;
        if (component == nullptr) return RuntimeStatus::ComponentDestroyed;

        // 動的プロパティも見る。Effect Stack の effects[i].* はこちらにしか無い。
        const Reflection::PropertyDesc* desc =
            Reflection::PropertyRegistry::FindForComponent(*component, property_name);
        if (desc == nullptr) return RuntimeStatus::InvalidArgument;
        if (desc->read_only) return RuntimeStatus::UnsupportedOperation;
        if (!desc->setter) return RuntimeStatus::UnsupportedOperation;

        desc->setter(*component, value);
        return RuntimeStatus::Ok;
    }

    // ---- Rigidbody ------------------------------------------------------------

    namespace
    {
        RuntimeStatus ResolveRigidbody(const HandleResolver& resolver,
            const ComponentHandle& handle, Components::RigidbodyComponent*& out)
        {
            out = nullptr;
            Component* component = nullptr;
            const RuntimeStatus status = resolver.TryResolve(handle, component);
            if (status != RuntimeStatus::Ok) return status;
            if (component == nullptr) return RuntimeStatus::ComponentDestroyed;
            if (component->TypeID() != Components::RigidbodyComponent::StaticTypeID())
                return RuntimeStatus::TypeMismatch;
            out = static_cast<Components::RigidbodyComponent*>(component);
            return RuntimeStatus::Ok;
        }
    }

    RuntimeStatus RuntimeContext::RigidbodyAddForce(const ComponentHandle& handle,
        const XMFLOAT3& force)
    {
        if (!FiniteVector(force)) return RuntimeStatus::InvalidArgument;
        Components::RigidbodyComponent* body = nullptr;
        const RuntimeStatus status = ResolveRigidbody(resolver_, handle, body);
        if (status != RuntimeStatus::Ok) return status;
        body->AddForce(force);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::RigidbodyAddTorque(const ComponentHandle& handle,
        const XMFLOAT3& torque)
    {
        if (!FiniteVector(torque)) return RuntimeStatus::InvalidArgument;
        Components::RigidbodyComponent* body = nullptr;
        const RuntimeStatus status = ResolveRigidbody(resolver_, handle, body);
        if (status != RuntimeStatus::Ok) return status;
        body->AddTorque(torque);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::RigidbodyClearForces(const ComponentHandle& handle)
    {
        Components::RigidbodyComponent* body = nullptr;
        const RuntimeStatus status = ResolveRigidbody(resolver_, handle, body);
        if (status != RuntimeStatus::Ok) return status;
        body->ClearForces();
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::RigidbodyTeleport(const ComponentHandle& handle,
        const XMFLOAT3& position, const XMFLOAT3& rotation_euler)
    {
        if (!FiniteVector(position) || !FiniteVector(rotation_euler))
            return RuntimeStatus::InvalidArgument;
        Components::RigidbodyComponent* body = nullptr;
        const RuntimeStatus status = ResolveRigidbody(resolver_, handle, body);
        if (status != RuntimeStatus::Ok) return status;
        body->Teleport(position, rotation_euler);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::RigidbodyGetLinearVelocity(const ComponentHandle& handle,
        XMFLOAT3& out) const
    {
        out = XMFLOAT3{ 0.0f, 0.0f, 0.0f };
        Components::RigidbodyComponent* body = nullptr;
        const RuntimeStatus status = ResolveRigidbody(resolver_, handle, body);
        if (status != RuntimeStatus::Ok) return status;
        out = body->linear_velocity;
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::RigidbodySetLinearVelocity(const ComponentHandle& handle,
        const XMFLOAT3& value)
    {
        if (!FiniteVector(value)) return RuntimeStatus::InvalidArgument;
        Components::RigidbodyComponent* body = nullptr;
        const RuntimeStatus status = ResolveRigidbody(resolver_, handle, body);
        if (status != RuntimeStatus::Ok) return status;
        body->linear_velocity = value;
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::RigidbodyGetAngularVelocity(const ComponentHandle& handle,
        XMFLOAT3& out) const
    {
        out = XMFLOAT3{ 0.0f, 0.0f, 0.0f };
        Components::RigidbodyComponent* body = nullptr;
        const RuntimeStatus status = ResolveRigidbody(resolver_, handle, body);
        if (status != RuntimeStatus::Ok) return status;
        out = body->angular_velocity;
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::RigidbodySetAngularVelocity(const ComponentHandle& handle,
        const XMFLOAT3& value)
    {
        if (!FiniteVector(value)) return RuntimeStatus::InvalidArgument;
        Components::RigidbodyComponent* body = nullptr;
        const RuntimeStatus status = ResolveRigidbody(resolver_, handle, body);
        if (status != RuntimeStatus::Ok) return status;
        body->angular_velocity = value;
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::InvokeComponentCommand(const ComponentHandle& handle,
        ComponentCommand command, const std::string& text, float scalar,
        float secondary_scalar, int integer)
    {
        Component* component = nullptr;
        const RuntimeStatus status = resolver_.TryResolve(handle, component);
        if (component == nullptr) return status;

        if (command >= ComponentCommand::AnimatorPlayState &&
            command <= ComponentCommand::AnimatorResetTrigger)
        {
            auto* animator = dynamic_cast<Components::AnimatorComponent*>(component);
            if (animator == nullptr) return RuntimeStatus::TypeMismatch;
            switch (command)
            {
            case ComponentCommand::AnimatorPlayState:
                return animator->PlayState(text, scalar, secondary_scalar)
                    ? RuntimeStatus::Ok : RuntimeStatus::InvalidArgument;
            case ComponentCommand::AnimatorPause: animator->Pause(); break;
            case ComponentCommand::AnimatorResume: animator->Resume(); break;
            case ComponentCommand::AnimatorStop: animator->Stop(); break;
            case ComponentCommand::AnimatorSetBool: animator->SetBool(text, integer != 0); break;
            case ComponentCommand::AnimatorSetFloat: animator->SetFloat(text, scalar); break;
            case ComponentCommand::AnimatorSetTrigger: animator->SetTrigger(text); break;
            case ComponentCommand::AnimatorResetTrigger: animator->ResetTrigger(text); break;
            default: return RuntimeStatus::UnsupportedOperation;
            }
            return RuntimeStatus::Ok;
        }

        if (command == ComponentCommand::AudioPlay ||
            command == ComponentCommand::AudioStop)
        {
            auto* source = dynamic_cast<Components::AudioSourceComponent*>(component);
            if (source == nullptr) return RuntimeStatus::TypeMismatch;
            if (command == ComponentCommand::AudioPlay)
            {
                Scene::Scene* scene = source->GetScene();
                Audio::IAudioPlaybackService* audio = scene != nullptr
                    ? scene->Services().Audio() : nullptr;
                if (audio == nullptr || !audio->Available())
                    return RuntimeStatus::ServiceUnavailable;
                if (source->clip_path.empty()) return RuntimeStatus::InvalidArgument;
                source->Play();
                if (!source->IsPlaying()) return RuntimeStatus::AssetMissing;
            }
            else source->Stop();
            return RuntimeStatus::Ok;
        }

        auto* emitter = dynamic_cast<Components::ParticleEmitterComponent*>(component);
        if (emitter == nullptr) return RuntimeStatus::TypeMismatch;
        switch (command)
        {
        case ComponentCommand::ParticlePlay: emitter->emitting = true; break;
        case ComponentCommand::ParticleStop: emitter->emitting = false; break;
        case ComponentCommand::ParticleEmit:
            if (integer <= 0) return RuntimeStatus::InvalidArgument;
            emitter->Emit(integer);
            break;
        case ComponentCommand::ParticleClear: emitter->Clear(); break;
        default: return RuntimeStatus::UnsupportedOperation;
        }
        return RuntimeStatus::Ok;
    }
}
