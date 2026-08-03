#include "LandscapeCollision.h"

#include "LandscapeMeshGenerator.h"

namespace ReplayEngine::Landscape
{
    const LandscapeCollisionChunk* LandscapeCollision::FindChunk(
        LandscapeChunkCoord coord) const noexcept
    {
        for (const LandscapeCollisionChunk& chunk : chunks_)
            if (chunk.coord == coord) return &chunk;
        return nullptr;
    }

    int LandscapeCollision::UpdateDirtyChunks(LandscapeData& data)
    {
        int updated = 0;
        for (LandscapeChunk& chunk : data.Chunks())
        {
            if (!chunk.collision_dirty) continue;
            LandscapeMeshData mesh;
            if (!LandscapeMeshGenerator::Generate(data, chunk, 0, mesh)) continue;
            LandscapeCollisionChunk generated;
            generated.coord = chunk.coord;
            generated.source_revision = chunk.revision;
            generated.triangles.reserve(mesh.indices.size() / 3);
            for (std::size_t index = 0; index + 2 < mesh.indices.size(); index += 3)
            {
                Physics::Triangle triangle;
                triangle.vertices[0] = mesh.vertices[mesh.indices[index]].position;
                triangle.vertices[1] = mesh.vertices[mesh.indices[index + 1]].position;
                triangle.vertices[2] = mesh.vertices[mesh.indices[index + 2]].position;
                triangle.material_index = 0;
                generated.triangles.push_back(triangle);
            }
            bool replaced = false;
            for (LandscapeCollisionChunk& current : chunks_)
            {
                if (!(current.coord == chunk.coord)) continue;
                current = std::move(generated);
                replaced = true;
                break;
            }
            if (!replaced) chunks_.push_back(std::move(generated));
            chunk.collision_dirty = false;
            ++updated;
        }
        return updated;
    }
}
