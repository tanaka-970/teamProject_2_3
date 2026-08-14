#pragma once

#include <cstddef>

namespace ReplayEngine::Scene
{
    class Scene;

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
    };
}
