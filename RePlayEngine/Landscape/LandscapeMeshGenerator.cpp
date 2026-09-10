#include "LandscapeMeshGenerator.h"
#include <unordered_map>

namespace ReplayEngine::Landscape
{
    bool LandscapeMeshGenerator::Generate(const LandscapeData& data,
        const LandscapeChunk& chunk, int lod, LandscapeMeshData& output)
    {
        if (data.VertexCount() == 0 || lod < 0 || lod > 6) return false;

        // v2 は任意 topology。規則格子を前提にした「1<<lod で x/z を間引く」方式は
        // 洞窟の壁を壊すため使わない。LOD は将来の mesh simplifier に委ね、
        // 現段階では topology を完全に保った LOD0 として提出する。
        output = {};
        output.coord = chunk.coord;
        output.lod = 0;
        output.source_revision = data.Revision();
        std::unordered_map<std::uint32_t, std::uint32_t> remap;
        for (const auto global : chunk.indices)
        {
            if (global >= data.VertexCount()) { output = {}; return false; }
            auto found = remap.find(global);
            if (found == remap.end())
            {
                const auto local = static_cast<std::uint32_t>(output.vertices.size());
                output.vertices.push_back(data.Vertices()[global]);
                found = remap.emplace(global,local).first;
            }
            output.indices.push_back(found->second);
        }
        return !output.vertices.empty() && !output.indices.empty();
    }
}
