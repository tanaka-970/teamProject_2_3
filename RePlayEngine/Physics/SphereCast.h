#pragma once

#include <DirectXMath.h>
#include <cstddef>
#include <cstdint>

namespace ReplayEngine::Physics
{
    struct Triangle
    {
        DirectX::XMFLOAT3 vertices[3]{};
        int material_index = -1;
    };

    struct SphereCastQuery
    {
        DirectX::XMFLOAT3 start{};
        DirectX::XMFLOAT3 end{};
        float radius = 0.5f;
        float minimum_normal_y = -1.0f;
        float maximum_normal_y = 1.0f;
    };

    struct SphereCastHit
    {
        DirectX::XMFLOAT3 position{}; // Contact point on the target surface.
        DirectX::XMFLOAT3 normal{ 0.0f, 1.0f, 0.0f };
        DirectX::XMFLOAT3 center{};   // Sphere center at time of impact.
        float distance = 0.0f;
        float fraction = 0.0f;
        std::uint32_t triangle_index = UINT32_MAX;
        int material_index = -1;
        bool started_overlapping = false;
    };

// チーム制作の球対三角形スイープを基にした、外部依存のない衝突判定核。
// 面・辺・頂点・初期重なりを調べ、最初に起きる有効な衝突を返す。
    bool CastSphereAgainstTriangle(const SphereCastQuery& query,
        const Triangle& triangle, SphereCastHit& hit) noexcept;

    bool CastSphereAgainstTriangles(const SphereCastQuery& query,
        const Triangle* triangles, std::size_t triangle_count,
        SphereCastHit& hit) noexcept;
}
