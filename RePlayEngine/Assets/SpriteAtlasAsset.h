#pragma once

#include <DirectXMath.h>

#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Assets
{
    struct SpriteAtlasRegion
    {
        std::string name;
        DirectX::XMFLOAT4 uv_rect{ 0.0f, 0.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT2 pivot{ 0.5f, 0.5f };
        DirectX::XMFLOAT2 original_size{ 0.0f, 0.0f };
        DirectX::XMFLOAT2 trim_offset{ 0.0f, 0.0f };
        bool rotated = false;
    };

    // 不均一な身体パーツや装飾素材を 1 枚の画像へまとめる Atlas。
    // 画像そのものは既存 Image Asset が所有し、この Asset は名前付き UV と Pivot だけを持つ。
    class SpriteAtlasAsset final
    {
    public:
        static constexpr const char* file_extension = ".replayatlas";
        static constexpr int current_version = 1;

        std::string name{ "Sprite Atlas" };
        std::string image_guid;
        std::vector<SpriteAtlasRegion> regions;

        const SpriteAtlasRegion* FindRegion(const std::string& region_name) const noexcept;
        SpriteAtlasRegion* FindRegion(const std::string& region_name) noexcept;

        static bool LoadFromFile(const std::filesystem::path& path,
            SpriteAtlasAsset& out, std::string& error);
        static bool SaveToFile(const std::filesystem::path& path,
            const SpriteAtlasAsset& asset, std::string& error);
    };
}
