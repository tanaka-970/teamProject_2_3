#pragma once

#include "LandscapeMeshGenerator.h"
#include "LandscapeMaterial.h"

#include <vector>

namespace ReplayEngine::Landscape
{
    // CPU-side render cache/foundation. D3D resource upload remains renderer-owned.
    // Only chunks marked render_dirty are regenerated.
    class LandscapeRenderer final
    {
    public:
        int UpdateDirtyChunks(LandscapeData& data);
        const LandscapeMeshData* FindMesh(LandscapeChunkCoord coord) const noexcept;
        void Clear() noexcept { meshes_.clear(); }
        LandscapeMaterial& Material() noexcept { return material_; }
        const LandscapeMaterial& Material() const noexcept { return material_; }

    private:
        std::vector<LandscapeMeshData> meshes_;
        LandscapeMaterial material_;
    };
}
