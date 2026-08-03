#pragma once

#include <DirectXMath.h>
#include <cstdint>

namespace ReplayEngine::Landscape
{
    struct LandscapeChunkCoord
    {
        int x = 0;
        int z = 0;
        bool operator==(const LandscapeChunkCoord& other) const noexcept
        { return x == other.x && z == other.z; }
    };

    struct LandscapeChunk
    {
        LandscapeChunkCoord coord;
        DirectX::XMFLOAT3 bounds_min{};
        DirectX::XMFLOAT3 bounds_max{};
        int lod = 0;
        std::uint64_t revision = 0;
        bool render_dirty = true;
        bool collision_dirty = true;
    };
}
