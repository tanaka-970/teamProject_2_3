#pragma once

#include <DirectXMath.h>
#include <cstdint>
#include <vector>

namespace ReplayEngine::Landscape
{
    struct LandscapeChunkCoord
    {
        int x = 0;
        int z = 0;
        bool operator==(const LandscapeChunkCoord& other) const noexcept
        { return x == other.x && z == other.z; }
    };

    // 1 チャンクが描く範囲。頂点は境界で重複させ、チャンク単位で独立して更新できるようにする。
    struct LandscapeChunk
    {
        LandscapeChunkCoord coord;
        DirectX::XMFLOAT3 bounds_min{};
        DirectX::XMFLOAT3 bounds_max{};
        int lod = 0;
        std::uint64_t revision = 0;
        bool render_dirty = true;
        bool collision_dirty = true;

        // このチャンクが持つ三角形。頂点は LandscapeData 側の並びを指す。
        std::vector<std::uint32_t> indices;

        // 描画へ渡す実体。indices を 0 起点へ詰め直したもの。
        std::vector<std::uint32_t> local_indices;
        std::vector<std::uint32_t> vertex_map;
    };
}
