#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Assets
{
    enum class AssetKind : std::uint32_t
    {
        Unknown,
        Model,
        Image,
        Audio,
        Shader,
        Scene,
        Material
    };

    struct AssetCacheEntry
    {
        std::filesystem::path source_path;
        std::filesystem::path cache_path;
        std::uint64_t source_fingerprint = 0;
        std::uint32_t format_version = 0;
        AssetKind kind = AssetKind::Unknown;
    };

    // 各インポーターが生成したバイナリを共通形式で保存するキャッシュ。
    // FBX、glTF、画像などの個別ローダーには依存しない。
    class AssetCache final
    {
    public:
        explicit AssetCache(std::filesystem::path root =
            std::filesystem::path("resources") / ".replay_cache");

        bool Store(const std::filesystem::path& source, AssetKind kind,
            std::uint32_t format_version, const std::vector<std::uint8_t>& payload,
            AssetCacheEntry& entry, std::string& error) const;
        bool Load(const std::filesystem::path& source, AssetKind kind,
            std::uint32_t format_version, std::vector<std::uint8_t>& payload,
            AssetCacheEntry& entry, std::string& error) const;
        bool StoreSourceFile(const std::filesystem::path& source, AssetKind kind,
            AssetCacheEntry& entry, std::string& error) const;

    private:
        bool Describe(const std::filesystem::path& source, AssetKind kind,
            std::uint32_t format_version, AssetCacheEntry& entry,
            std::string& error) const;

        std::filesystem::path root_;
    };
}
