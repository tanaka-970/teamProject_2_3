#include "gltf_model.h"

#include <filesystem>
#include <utility>

namespace
{
    std::filesystem::path& GltfCacheRootStorage()
    {
        static std::filesystem::path root = std::filesystem::path("resources") / ".replay_cache";
        return root;
    }
}

void gltf_model::SetCacheRoot(std::filesystem::path root)
{
    if (root.empty()) root = std::filesystem::path("resources") / ".replay_cache";
    GltfCacheRootStorage() = std::move(root);
}

const std::filesystem::path& gltf_model::CacheRoot()
{
    return GltfCacheRootStorage();
}
