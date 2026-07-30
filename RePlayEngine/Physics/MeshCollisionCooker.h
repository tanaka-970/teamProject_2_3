#pragma once

#include "SphereCast.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Physics
{
    struct CollisionCookResult
    {
        std::filesystem::path cache_path;
        std::uint64_t triangle_count = 0;
        std::uint64_t fingerprint = 0;
    };

    class MeshCollisionCooker final
    {
    public:
        explicit MeshCollisionCooker(std::filesystem::path root =
            std::filesystem::path("resources") / ".replay_cache" / "collisions");

        bool Cook(const std::string& asset_guid, const std::vector<Triangle>& triangles,
            CollisionCookResult& result, std::string& error) const;
        bool Load(const std::filesystem::path& cache_path,
            std::vector<Triangle>& triangles, CollisionCookResult& result,
            std::string& error) const;

    private:
        std::filesystem::path root_;
    };
}
