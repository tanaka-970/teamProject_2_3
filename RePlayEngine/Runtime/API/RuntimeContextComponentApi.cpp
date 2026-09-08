// RuntimeContext のうち「World Transform」「Component プロパティ」「Rigidbody」を持つ。
//
//   RuntimeContext.cpp             … Context 接続と Object・Transform・Component API
//   RuntimeContextComponentApi.cpp … World Transform / 汎用プロパティ / Rigidbody（このファイル）
//   RuntimeContextMotion.cpp       … Motion Player API
//   RuntimeContextScene.cpp        … Scene 遷移と Prefab 生成 API
//   RuntimeContextServices.cpp     … Physics と Log の Service API

#include "RuntimeContext.h"

#include "../../Components/Audio/AudioSourceComponent.h"
#include "../../Components/Landscape/LandscapeComponent.h"
#include "../../Components/Physics/ColliderComponent.h"
#include "../../Scripting/Core/ScriptComponent.h"
#include "../../Scripting/Core/ScriptServices.h"
#include "../../Scripting/Core/ScriptTypeCatalog.h"
#include "../../Landscape/LandscapeEditorTool.h"
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

    namespace
    {
        RuntimeStatus ResolveLandscape(const HandleResolver& resolver,
            const ComponentHandle& handle, Components::LandscapeComponent*& out)
        {
            out = nullptr;
            Component* component = nullptr;
            const RuntimeStatus status = resolver.TryResolve(handle, component);
            if (status != RuntimeStatus::Ok) return status;
            if (component == nullptr) return RuntimeStatus::ComponentDestroyed;
            if (component->TypeID() != Components::LandscapeComponent::StaticTypeID())
                return RuntimeStatus::TypeMismatch;
            out = static_cast<Components::LandscapeComponent*>(component);
            return RuntimeStatus::Ok;
        }
    }

    RuntimeStatus RuntimeContext::AddScriptComponent(const ObjectHandle& owner,
        std::uint64_t type_high, std::uint64_t type_low, ComponentHandle& out)
    {
        out = {};
        GameObject* object = nullptr;
        const RuntimeStatus status = resolver_.TryResolve(owner, object);
        if (status != RuntimeStatus::Ok) return status;
        if (object == nullptr) return RuntimeStatus::ObjectDestroyed;

        // 型の正体は Catalog が持っている。ここで型表を作り直さない。
        Scripting::IScriptServices* scripts = object->GetScene() != nullptr
            ? object->GetScene()->Services().Scripts() : nullptr;
        if (scripts == nullptr) return RuntimeStatus::ServiceUnavailable;

        Reflection::TypeGUID type_id;
        type_id.high = type_high;
        type_id.low = type_low;
        const Scripting::ScriptTypeDescriptor* descriptor = scripts->Catalog().Find(type_id);
        if (descriptor == nullptr) return RuntimeStatus::ComponentNotFound;

        auto* script = object->AddComponent<Scripting::ScriptComponent>();
        if (script == nullptr) return RuntimeStatus::UnsupportedOperation;
        script->AssignScriptType(*descriptor);

        // 実体だけこの場で作る。Awake は Scene の同期点のまま。
        //
        // 【なぜここで作るか】
        //   AddComponent<T>() は Unity と同じく、戻り値をその行で使える必要がある。
        //   Awake を待つと、追加した直後は必ず null になり
        //   controller.speed = 5 が書けない。
        //   Awake まで一緒に走らせないのは、Awake 中に足した Component の
        //   Start が先に走らないようにした 2 パス構成を壊さないため。
        script->ResolveSchema();
        if (!script->EnsureInstance())
        {
            LogError("AddScriptComponent: " + script->LastError(), owner);
            // Update 走査中にも呼ばれるため、既存の同期点で安全に破棄する。
            script->Destroy();
            return RuntimeStatus::UnsupportedOperation;
        }

        out = resolver_.MakeHandle(script);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::IsComponentAlive(const ComponentHandle& handle,
        bool& out) const
    {
        out = false;
        Component* component = nullptr;
        const RuntimeStatus status = resolver_.TryResolve(handle, component);
        if (status != RuntimeStatus::Ok) return status;
        out = component != nullptr && !component->PendingDestroy();
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::FindColliderComponent(const ObjectHandle& owner,
        std::uint32_t collider_id, ComponentHandle& out) const
    {
        out = {};
        GameObject* object = nullptr;
        const RuntimeStatus status = resolver_.TryResolve(owner, object);
        if (status != RuntimeStatus::Ok) return status;
        if (object == nullptr) return RuntimeStatus::ObjectDestroyed;

        // 既存の解決をそのまま使う。ここで探索を書き直さない。
        Components::ColliderComponent* collider = Components::FindColliderByID(
            *object, static_cast<Scene::ColliderID>(collider_id));
        if (collider == nullptr) return RuntimeStatus::ComponentNotFound;

        out = resolver_.MakeHandle(collider);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::LandscapeInfo(const ComponentHandle& handle,
        int& out_width, int& out_height, float& out_cell_size) const
    {
        out_width = 0;
        out_height = 0;
        out_cell_size = 0.0f;
        Components::LandscapeComponent* landscape = nullptr;
        const RuntimeStatus status = ResolveLandscape(resolver_, handle, landscape);
        if (status != RuntimeStatus::Ok) return status;

        const Landscape::LandscapeData& data = landscape->Data();
        if (!data.Valid()) return RuntimeStatus::UnsupportedOperation;
        out_width = data.Width();
        out_height = data.Height();
        out_cell_size = data.CellSize();
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::LandscapeGetHeight(const ComponentHandle& handle,
        int x, int z, float& out_height) const
    {
        out_height = 0.0f;
        Components::LandscapeComponent* landscape = nullptr;
        const RuntimeStatus status = ResolveLandscape(resolver_, handle, landscape);
        if (status != RuntimeStatus::Ok) return status;

        const Landscape::LandscapeData& data = landscape->Data();
        if (!data.Contains(x, z)) return RuntimeStatus::InvalidArgument;
        out_height = data.HeightAt(x, z);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::LandscapeSetHeight(const ComponentHandle& handle,
        int x, int z, float value)
    {
        if (!std::isfinite(value)) return RuntimeStatus::InvalidArgument;
        Components::LandscapeComponent* landscape = nullptr;
        const RuntimeStatus status = ResolveLandscape(resolver_, handle, landscape);
        if (status != RuntimeStatus::Ok) return status;

        Landscape::LandscapeData& data = landscape->Data();
        if (!data.Contains(x, z)) return RuntimeStatus::InvalidArgument;
        return data.SetHeight(x, z, value) ? RuntimeStatus::Ok : RuntimeStatus::UnsupportedOperation;
    }

    RuntimeStatus RuntimeContext::LandscapeSampleHeight(const ComponentHandle& handle,
        float local_x, float local_z, float& out_height) const
    {
        out_height = 0.0f;
        if (!std::isfinite(local_x) || !std::isfinite(local_z))
            return RuntimeStatus::InvalidArgument;

        Components::LandscapeComponent* landscape = nullptr;
        const RuntimeStatus status = ResolveLandscape(resolver_, handle, landscape);
        if (status != RuntimeStatus::Ok) return status;

        const Landscape::LandscapeData& data = landscape->Data();
        if (!data.Valid()) return RuntimeStatus::UnsupportedOperation;
        const float cell = data.CellSize();
        if (!(cell > 0.0f)) return RuntimeStatus::UnsupportedOperation;

        // 地形は原点が中心。格子座標へ移してから双一次補間する。
        const float grid_x = local_x / cell + (data.Width() - 1) * 0.5f;
        const float grid_z = local_z / cell + (data.Height() - 1) * 0.5f;
        const int x0 = static_cast<int>(std::floor(grid_x));
        const int z0 = static_cast<int>(std::floor(grid_z));
        const float tx = grid_x - static_cast<float>(x0);
        const float tz = grid_z - static_cast<float>(z0);

        const auto sample = [&data](int x, int z)
        {
            const int cx = x < 0 ? 0 : (x >= data.Width() ? data.Width() - 1 : x);
            const int cz = z < 0 ? 0 : (z >= data.Height() ? data.Height() - 1 : z);
            return data.HeightAt(cx, cz);
        };

        const float h00 = sample(x0, z0);
        const float h10 = sample(x0 + 1, z0);
        const float h01 = sample(x0, z0 + 1);
        const float h11 = sample(x0 + 1, z0 + 1);
        const float top = h00 + (h10 - h00) * tx;
        const float bottom = h01 + (h11 - h01) * tx;
        out_height = top + (bottom - top) * tz;
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::LandscapeSculpt(const ComponentHandle& handle,
        const DirectX::XMFLOAT3& local_center, int mode, int direction,
        float radius, float strength, float falloff, float flatten_height,
        float noise_scale, float delta_time)
    {
        if (!FiniteVector(local_center)) return RuntimeStatus::InvalidArgument;
        if (!std::isfinite(radius) || !std::isfinite(strength) ||
            !std::isfinite(falloff) || !std::isfinite(flatten_height) ||
            !std::isfinite(noise_scale) || !std::isfinite(delta_time))
            return RuntimeStatus::InvalidArgument;
        if (!(radius > 0.0f) || !(delta_time > 0.0f)) return RuntimeStatus::InvalidArgument;
        if (mode < 0 || mode > static_cast<int>(Landscape::LandscapeBrushMode::Subdivide))
            return RuntimeStatus::InvalidArgument;
        if (direction < 0 ||
            direction > static_cast<int>(Landscape::LandscapeSculptDirection::VertexNormal))
            return RuntimeStatus::InvalidArgument;

        Components::LandscapeComponent* landscape = nullptr;
        const RuntimeStatus status = ResolveLandscape(resolver_, handle, landscape);
        if (status != RuntimeStatus::Ok) return status;

        Landscape::LandscapeData& data = landscape->Data();
        if (!data.Valid()) return RuntimeStatus::UnsupportedOperation;

        Landscape::LandscapeBrush brush;
        brush.radius = radius;
        brush.strength = strength;
        brush.falloff = falloff;
        brush.flatten_height = flatten_height;
        brush.noise_scale = noise_scale;
        brush.direction = static_cast<Landscape::LandscapeSculptDirection>(direction);

        // Editor と同じ 1 ストロークを、この呼び出しの中で開いて閉じる。
        // Undo コマンドは Editor の履歴へ積むものなので Runtime では捨てる。
        // Script から地形を変えたことを Undo で戻せてしまうと、
        // ゲーム進行と編集履歴が混ざる。
        Landscape::LandscapeEditorTool tool;
        if (!tool.BeginStroke(data, static_cast<Landscape::LandscapeBrushMode>(mode), brush))
            return RuntimeStatus::UnsupportedOperation;

        const bool applied = tool.ApplySample(local_center, delta_time);
        tool.EndStroke();
        return applied ? RuntimeStatus::Ok : RuntimeStatus::InvalidArgument;
    }

    RuntimeStatus RuntimeContext::LandscapeRaycast(const ComponentHandle& handle,
        const DirectX::XMFLOAT3& local_origin, const DirectX::XMFLOAT3& local_direction,
        float max_distance, DirectX::XMFLOAT3& out_position,
        DirectX::XMFLOAT3& out_normal, float& out_distance) const
    {
        out_position = {};
        out_normal = { 0.0f, 1.0f, 0.0f };
        out_distance = 0.0f;
        if (!FiniteVector(local_origin) || !FiniteVector(local_direction))
            return RuntimeStatus::InvalidArgument;
        if (!std::isfinite(max_distance) || !(max_distance > 0.0f))
            return RuntimeStatus::InvalidArgument;

        Components::LandscapeComponent* landscape = nullptr;
        const RuntimeStatus status = ResolveLandscape(resolver_, handle, landscape);
        if (status != RuntimeStatus::Ok) return status;

        const Landscape::LandscapeData& data = landscape->Data();
        if (!data.Valid()) return RuntimeStatus::UnsupportedOperation;

        Landscape::LandscapeRayHit hit{};
        if (!data.Raycast(local_origin, local_direction, max_distance, hit) || !hit.hit)
            return RuntimeStatus::ComponentNotFound;

        out_position = hit.position;
        out_normal = hit.normal;
        out_distance = hit.distance;
        return RuntimeStatus::Ok;
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
