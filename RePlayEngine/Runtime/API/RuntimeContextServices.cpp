#include "RuntimeContext.h"

#include "../Events/EventBus.h"
#include "../../Components/Motion/MotionPlayerComponent.h"
#include "../../Object/Component/Component.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Scene/Runtime/Scene.h"

namespace ReplayEngine::Runtime
{
    using Core::Component;
    using Core::GameObject;
    using Core::ObjectID;
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

    RuntimeStatus RuntimeContext::Raycast(const DirectX::XMFLOAT3& origin,
        const DirectX::XMFLOAT3& direction, float max_distance,
        int layer, int mask, const ObjectHandle& ignore, Scene::RaycastHit& out) const
    {
        out = Scene::RaycastHit{};
        if (max_distance <= 0.0f) return RuntimeStatus::InvalidArgument;

        const Scene::IPhysicsQueryService* physics = world_->Services().Physics();
        if (physics == nullptr || !physics->CollisionAvailable())
            return RuntimeStatus::ServiceUnavailable;

        Scene::CollisionQueryFilter filter;
        filter.layer = layer;
        filter.mask = mask;
        if (!ignore.IsEmpty()) filter.ignore_object = ignore.object;

        physics->RaycastFiltered(origin, direction, max_distance, filter, out);
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
