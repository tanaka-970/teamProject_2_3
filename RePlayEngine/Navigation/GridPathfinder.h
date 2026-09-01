#pragma once

#include "../Scene/Services/IPhysicsQueryService.h"

#include <DirectXMath.h>

#include <cstddef>
#include <vector>

namespace ReplayEngine::Navigation
{
    // GridPathfinder は NavAgent の公開 API を知らない。
    // 既存の Physics Query だけを受け取ることで、将来 NavMesh / Recast へ差し替えるときに
    // Component 側の MoveTo / Stop / Arrived を変更しなくて済む境界にしている。
    struct GridPathfinderSettings
    {
        float grid_size = 1.0f;
        float maximum_range = 24.0f;
        std::size_t maximum_search_cells = 4096;

        // CharacterMotor の Primary Collider から作る問い合わせ形状。
        float agent_radius = 0.38f;
        float walkable_normal_y = 0.25f;
        float ground_probe_up = 0.4f;
        float ground_probe_down = 1.4f;
        DirectX::XMFLOAT3 ground_offset{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 wall_offset{ 0.0f, 0.0f, 0.0f };
        Scene::CollisionQueryFilter filter;
    };

    // 成功時は start と goal を含む経路を返す。
    // 失敗時は out_path を空にして false。例外は外へ出さない。
    bool FindGridPath(const Scene::IPhysicsQueryService& physics,
        const DirectX::XMFLOAT3& start, const DirectX::XMFLOAT3& goal,
        const GridPathfinderSettings& settings,
        std::vector<DirectX::XMFLOAT3>& out_path) noexcept;
}
