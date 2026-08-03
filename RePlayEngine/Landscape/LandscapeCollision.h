#pragma once

#include "LandscapeData.h"
#include "../Physics/SphereCast.h"

#include <vector>

namespace ReplayEngine::Landscape
{
    struct LandscapeCollisionChunk
    {
        LandscapeChunkCoord coord;
        std::uint64_t source_revision = 0;
        std::vector<Physics::Triangle> triangles;
    };

    // CPU collision cache. It deliberately exposes triangles so SceneCollisionWorld
    // can adopt them later without coupling LandscapeData to the current physics backend.
    class LandscapeCollision final
    {
    public:
        int UpdateDirtyChunks(LandscapeData& data);
        const LandscapeCollisionChunk* FindChunk(LandscapeChunkCoord coord) const noexcept;
        void Clear() noexcept { chunks_.clear(); }

    private:
        std::vector<LandscapeCollisionChunk> chunks_;
    };
}
