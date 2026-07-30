#pragma once

#include <DirectXMath.h>

class Stage;

namespace GameRaycast
{
    struct RaycastHit
    {
        DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 normal{ 0.0f, 1.0f, 0.0f };
        float distance = 0.0f;
    };

    struct SphereCastHit
    {
        DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 normal{ 0.0f, 1.0f, 0.0f };
        DirectX::XMFLOAT3 center{ 0.0f, 0.0f, 0.0f };
        float distance = 0.0f;
        float fraction = 0.0f;
        bool started_overlapping = false;
    };

    bool RaycastStage(const Stage& stage,
                      const DirectX::XMFLOAT3& origin,
                      const DirectX::XMFLOAT3& direction,
                      float max_distance,
                      RaycastHit& hit,
                      float min_normal_y = 0.0f);

    bool RaycastStageDown(const Stage& stage,
                          const DirectX::XMFLOAT3& position,
                          float up_offset,
                          float down_distance,
                          RaycastHit& hit,
                          float min_normal_y = 0.25f);

    bool SphereCastStage(const Stage& stage,
                         const DirectX::XMFLOAT3& start,
                         const DirectX::XMFLOAT3& end,
                         float radius,
                         SphereCastHit& hit,
                         float minimum_normal_y = -1.0f,
                         float maximum_normal_y = 1.0f);

    bool SphereCastStageDown(const Stage& stage,
                             const DirectX::XMFLOAT3& position,
                             float radius,
                             float up_offset,
                             float down_distance,
                             SphereCastHit& hit,
                             float minimum_normal_y = 0.25f);
}
