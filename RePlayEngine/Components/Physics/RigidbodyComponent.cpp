#include "RigidbodyComponent.h"

#include "ColliderComponent.h"
#include "MeshColliderComponent.h"
#include "../Landscape/LandscapeColliderComponent.h"
#include "../../Object/GameObject/GameObject.h"

#include <cstddef>
#include <cmath>

namespace ReplayEngine::Components
{
    namespace
    {
        void AddTo(DirectX::XMFLOAT3& target,
            const DirectX::XMFLOAT3& value) noexcept
        {
            target.x += value.x;
            target.y += value.y;
            target.z += value.z;
        }

        const ColliderComponent* FirstCollider(const Core::GameObject* owner) noexcept
        {
            if (owner == nullptr) return nullptr;
            for (std::size_t index = 0; index < owner->ComponentCount(); ++index)
            {
                const auto* collider = dynamic_cast<const ColliderComponent*>(
                    owner->ComponentAt(index));
                if (collider != nullptr && !collider->PendingDestroy()) return collider;
            }
            return nullptr;
        }
    }

    void RigidbodyComponent::AddForce(const DirectX::XMFLOAT3& force) noexcept
    {
        AddTo(accumulated_force_, force);
        if (RuntimeInitialized())
        {
            is_sleeping = false;
            sleep_timer_ = 0.0f;
        }
    }

    void RigidbodyComponent::AddTorque(const DirectX::XMFLOAT3& torque) noexcept
    {
        AddTo(accumulated_torque_, torque);
        if (RuntimeInitialized())
        {
            is_sleeping = false;
            sleep_timer_ = 0.0f;
        }
    }

    void RigidbodyComponent::ClearForces() noexcept
    {
        ClearAccumulatedForces();
    }

    void RigidbodyComponent::ClearAccumulatedForces() noexcept
    {
        accumulated_force_ = DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
        accumulated_torque_ = DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
    }

    void RigidbodyComponent::Teleport(const DirectX::XMFLOAT3& position,
        const DirectX::XMFLOAT3& rotation_euler) noexcept
    {
        Core::GameObject* owner = Owner();
        if (owner == nullptr) return;

        owner->GetTransform().SetWorldPosition(position);
        owner->GetTransform().SetLocalRotationEuler(rotation_euler);
        is_sleeping = false;
        sleep_timer_ = 0.0f;
    }

    void RigidbodyComponent::ResetRuntimeState() noexcept
    {
        linear_velocity = DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
        angular_velocity = DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
        is_sleeping = start_asleep;
        sleep_timer_ = 0.0f;
        runtime_initialized_ = false;
        ClearAccumulatedForces();
    }

    std::string RigidbodyComponent::StatusMessage() const
    {
        const Core::GameObject* owner = Owner();
        const ColliderComponent* collider = FirstCollider(owner);
        if (collider == nullptr)
        {
            return "Collider がありません。衝突しない質点として扱います。";
        }

        if (collider->is_trigger)
        {
            return "Trigger Collider は Rigidbody の押し戻し対象になりません。";
        }

        if (body_type == BodyType_Dynamic &&
            (collider->Shape() == ColliderShape::Mesh ||
             collider->Shape() == ColliderShape::Landscape))
        {
            return "Mesh / Landscape は Dynamic に対応しないため、Kinematic として扱います。";
        }

        return std::string();
    }
}
