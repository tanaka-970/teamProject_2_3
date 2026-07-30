#include "MeshCollisionCooker.h"

#include <fstream>

namespace ReplayEngine::Physics
{
    namespace
    {
        constexpr std::uint32_t collision_magic = 0x4c4f4352;
        constexpr std::uint32_t collision_version = 1;

        struct Header
        {
            std::uint32_t magic = collision_magic;
            std::uint32_t version = collision_version;
            std::uint64_t triangle_count = 0;
            std::uint64_t fingerprint = 0;
        };

        std::uint64_t Fingerprint(const std::vector<Triangle>& triangles)
        {
            std::uint64_t hash = 1469598103934665603ull;
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(triangles.data());
            const std::size_t byte_count = triangles.size() * sizeof(Triangle);
            for (std::size_t index = 0; index < byte_count; ++index)
            {
                hash ^= bytes[index];
                hash *= 1099511628211ull;
            }
            return hash;
        }
    }

    MeshCollisionCooker::MeshCollisionCooker(std::filesystem::path root)
        : root_(std::move(root))
    {
    }

    bool MeshCollisionCooker::Cook(const std::string& asset_guid,
        const std::vector<Triangle>& triangles, CollisionCookResult& result,
        std::string& error) const
    {
        if (asset_guid.empty() || triangles.empty())
        {
            error = "GUIDまたは衝突三角形がありません";
            return false;
        }
        std::error_code filesystem_error;
        std::filesystem::create_directories(root_, filesystem_error);
        if (filesystem_error)
        {
            error = "衝突キャッシュフォルダーを作成できません";
            return false;
        }
        result.fingerprint = Fingerprint(triangles);
        result.triangle_count = triangles.size();
        result.cache_path = root_ / (asset_guid + "_v1.replaycollision");
        Header header{};
        header.triangle_count = result.triangle_count;
        header.fingerprint = result.fingerprint;
        std::ofstream stream(result.cache_path, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            error = "衝突キャッシュを作成できません";
            return false;
        }
        stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
        stream.write(reinterpret_cast<const char*>(triangles.data()),
            static_cast<std::streamsize>(triangles.size() * sizeof(Triangle)));
        if (!stream)
        {
            error = "衝突キャッシュの書き込みに失敗しました";
            return false;
        }
        return true;
    }

    bool MeshCollisionCooker::Load(const std::filesystem::path& cache_path,
        std::vector<Triangle>& triangles, CollisionCookResult& result,
        std::string& error) const
    {
        std::ifstream stream(cache_path, std::ios::binary);
        Header header{};
        if (!stream || !stream.read(reinterpret_cast<char*>(&header), sizeof(header)) ||
            header.magic != collision_magic || header.version != collision_version)
        {
            error = "衝突キャッシュの形式が不正です";
            return false;
        }
        triangles.resize(static_cast<std::size_t>(header.triangle_count));
        if (!triangles.empty())
            stream.read(reinterpret_cast<char*>(triangles.data()),
                static_cast<std::streamsize>(triangles.size() * sizeof(Triangle)));
        if (!stream || Fingerprint(triangles) != header.fingerprint)
        {
            triangles.clear();
            error = "衝突キャッシュが破損しています";
            return false;
        }
        result.cache_path = cache_path;
        result.triangle_count = header.triangle_count;
        result.fingerprint = header.fingerprint;
        return true;
    }
}
