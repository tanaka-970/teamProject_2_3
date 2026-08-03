#include "LandscapeMeshGenerator.h"

#include <algorithm>
#include <cmath>

namespace ReplayEngine::Landscape
{
    bool LandscapeMeshGenerator::Generate(const LandscapeData& data,
        const LandscapeChunk& chunk, int lod, LandscapeMeshData& output)
    {
        if (!data.Valid() || lod < 0 || lod > 6) return false;
        const int step = 1 << lod;
        const int start_x = chunk.coord.x * LandscapeData::chunk_cell_count;
        const int start_z = chunk.coord.z * LandscapeData::chunk_cell_count;
        const int end_x = (std::min)(data.Width() - 1,
            start_x + LandscapeData::chunk_cell_count);
        const int end_z = (std::min)(data.Height() - 1,
            start_z + LandscapeData::chunk_cell_count);
        if (start_x >= end_x || start_z >= end_z) return false;

        std::vector<int> xs;
        std::vector<int> zs;
        for (int x = start_x; x < end_x; x += step) xs.push_back(x);
        for (int z = start_z; z < end_z; z += step) zs.push_back(z);
        if (xs.empty() || xs.back() != end_x) xs.push_back(end_x);
        if (zs.empty() || zs.back() != end_z) zs.push_back(end_z);

        output = {};
        output.coord = chunk.coord;
        output.lod = lod;
        output.source_revision = chunk.revision;
        output.vertices.reserve(xs.size() * zs.size());

        const float cell = data.CellSize();
        for (int z : zs)
        {
            for (int x : xs)
            {
                const int left = (std::max)(0, x - 1);
                const int right = (std::min)(data.Width() - 1, x + 1);
                const int back = (std::max)(0, z - 1);
                const int front = (std::min)(data.Height() - 1, z + 1);
                DirectX::XMFLOAT3 normal{
                    data.HeightAt(left, z) - data.HeightAt(right, z),
                    2.0f * cell,
                    data.HeightAt(x, back) - data.HeightAt(x, front) };
                const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
                if (length > 0.00001f)
                {
                    normal.x /= length; normal.y /= length; normal.z /= length;
                }
                output.vertices.push_back({
                    { x * cell, data.HeightAt(x, z), z * cell }, normal,
                    { static_cast<float>(x) / (data.Width() - 1),
                      static_cast<float>(z) / (data.Height() - 1) } });
            }
        }

        const std::uint32_t row = static_cast<std::uint32_t>(xs.size());
        for (std::uint32_t z = 0; z + 1 < static_cast<std::uint32_t>(zs.size()); ++z)
        {
            for (std::uint32_t x = 0; x + 1 < row; ++x)
            {
                const std::uint32_t a = z * row + x;
                const std::uint32_t b = a + 1;
                const std::uint32_t c = a + row;
                const std::uint32_t d = c + 1;
                output.indices.insert(output.indices.end(), { a, c, b, b, c, d });
            }
        }
        return true;
    }
}
