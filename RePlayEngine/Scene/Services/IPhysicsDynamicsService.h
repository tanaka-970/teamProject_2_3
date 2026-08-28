#pragma once

#include "IPhysicsQueryService.h"

#include <DirectXMath.h>

#include <cstddef>
#include <vector>

namespace ReplayEngine::Scene
{
    class Scene;

    struct PhysicsContact final
    {
        Core::ObjectID object_a;
        ColliderID collider_a = invalid_collider_id;
        Core::ObjectID object_b;
        ColliderID collider_b = invalid_collider_id;
        DirectX::XMFLOAT3 point{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 normal{ 0.0f, 1.0f, 0.0f };
        DirectX::XMFLOAT3 relative_velocity{ 0.0f, 0.0f, 0.0f };
        float penetration = 0.0f;
    };

    // Rigidbody の時間積分・接触解決を Query API から分離した窓口。
    // Gameplay / Editor はこの型を知るだけで、Solver の実装型を参照しない。
    class IPhysicsDynamicsService
    {
    public:
        virtual ~IPhysicsDynamicsService() = default;

        virtual void AttachScene(Scene* scene) = 0;
        virtual void DetachScene() = 0;
        virtual const Scene* AttachedScene() const noexcept = 0;
        virtual void Step(float fixed_delta_time) = 0;

        virtual std::size_t BodyCount() const noexcept = 0;
        virtual std::size_t DynamicBodyCount() const noexcept = 0;
        virtual std::size_t SleepingBodyCount() const noexcept = 0;
        virtual int SolverIterations() const noexcept = 0;

        virtual const std::vector<PhysicsContact>& Contacts() const noexcept
        {
            static const std::vector<PhysicsContact> empty;
            return empty;
        }
    };
}
