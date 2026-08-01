#pragma once

#include "LandscapeData.h"

#include <DirectXMath.h>
#include <cstdint>
#include <vector>

namespace ReplayEngine::Landscape
{
    struct LandscapeVertex
    {
        DirectX::XMFLOAT3 position{};
        DirectX::XMFLOAT3 normal{ 0, 1, 0 };
        DirectX::XMFLOAT2 uv{};
    };

    struct LandscapeMeshData
    {
        LandscapeChunkCoord coord;
        int lod = 0;
        std::uint64_t source_revision = 0;
        std::vector<LandscapeVertex> vertices;
        std::vector<std::uint32_t> indices;
    };

    class LandscapeMeshGenerator final
    {
    public:
        static bool Generate(const LandscapeData& data, const LandscapeChunk& chunk,
            int lod, LandscapeMeshData& output);
    };
}
