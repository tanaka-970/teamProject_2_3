#include "LandscapeRenderer.h"

namespace ReplayEngine::Landscape
{
    const LandscapeMeshData* LandscapeRenderer::FindMesh(LandscapeChunkCoord coord) const noexcept
    {
        for (const LandscapeMeshData& mesh : meshes_) if (mesh.coord == coord) return &mesh;
        return nullptr;
    }

    int LandscapeRenderer::UpdateDirtyChunks(LandscapeData& data)
    {
        int updated = 0;
        for (LandscapeChunk& chunk : data.Chunks())
        {
            if (!chunk.render_dirty) continue;
            LandscapeMeshData generated;
            if (!LandscapeMeshGenerator::Generate(data, chunk, chunk.lod, generated)) continue;
            bool replaced = false;
            for (LandscapeMeshData& mesh : meshes_)
            {
                if (!(mesh.coord == chunk.coord)) continue;
                mesh = std::move(generated);
                replaced = true;
                break;
            }
            if (!replaced) meshes_.push_back(std::move(generated));
            chunk.render_dirty = false;
            ++updated;
        }
        return updated;
    }
}
