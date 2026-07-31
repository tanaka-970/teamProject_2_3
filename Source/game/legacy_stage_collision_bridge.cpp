#include "legacy_stage_collision_bridge.h"

#include "raycast.h"
#include "stage.h"

bool LegacyStageCollisionBridge::CollisionAvailable() const
{
    return active_ && stage_ != nullptr && stage_->GetCollisionMesh().Valid();
}

bool LegacyStageCollisionBridge::QueryGround(const DirectX::XMFLOAT3& origin, float radius,
    float up_offset, float down_distance, float walkable_normal_y,
    ReplayEngine::Scene::GroundHit& hit) const
{
    hit = ReplayEngine::Scene::GroundHit{};
    if (!CollisionAvailable()) return false;

    GameRaycast::SphereCastHit result{};
    if (!GameRaycast::SphereCastStageDown(*stage_, origin, radius,
        up_offset, down_distance, result, walkable_normal_y))
    {
        return false;
    }

    hit.position = result.position;
    hit.normal = result.normal;
    hit.valid = true;
    return true;
}

bool LegacyStageCollisionBridge::SweepSphere(const DirectX::XMFLOAT3& start,
    const DirectX::XMFLOAT3& end, float radius, float maximum_normal_y,
    ReplayEngine::Scene::SphereSweepHit& hit) const
{
    hit = ReplayEngine::Scene::SphereSweepHit{};
    if (!CollisionAvailable()) return false;

    GameRaycast::SphereCastHit result{};
    // 下限は -1（真下向きの面まで含む）。上限で床を除外し、壁だけを拾う。
    if (!GameRaycast::SphereCastStage(*stage_, start, end, radius,
        result, -1.0f, maximum_normal_y))
    {
        return false;
    }

    hit.center = result.center;
    hit.normal = result.normal;
    hit.fraction = result.fraction;
    hit.valid = true;
    return true;
}
