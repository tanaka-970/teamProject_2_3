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
        // 空なら従来の矩形。3点以上なら uv_rect 内の自由形状として扱う。
        // 座標は Atlas 全体の正規化 UV (0..1) で保持する。
        std::vector<DirectX::XMFLOAT2> path_points;
        DirectX::XMFLOAT2 pivot{ 0.5f, 0.5f };
        DirectX::XMFLOAT2 original_size{ 0.0f, 0.0f };
        DirectX::XMFLOAT2 trim_offset{ 0.0f, 0.0f };
        bool rotated = false;
    };

    // 不均一な身体パーツや装飾素材を 1 枚の画像へまとめる Atlas。
    // 画像そのものは既存 Image Asset が所有し、この Asset は名前付き UV、
    // 必要なら自由形状の輪郭、Pivot を持つ。
    class SpriteAtlasAsset final
    {
    public:
        static constexpr const char* file_extension = ".replayatlas";
        static constexpr int current_version = 2;

        std::string name{ "Sprite Atlas" };
        std::string image_guid;
        // Atlas と同じフォルダに置く、BC 圧縮済みの自立テクスチャキャッシュ。
        // 空なら従来どおり image_guid の元画像へフォールバックする。
        std::string embedded_texture_path;
        std::vector<SpriteAtlasRegion> regions;

        const SpriteAtlasRegion* FindRegion(const std::string& region_name) const noexcept;
        SpriteAtlasRegion* FindRegion(const std::string& region_name) noexcept;

        static bool LoadFromFile(const std::filesystem::path& path,
            SpriteAtlasAsset& out, std::string& error);
        static bool SaveToFile(const std::filesystem::path& path,
            const SpriteAtlasAsset& asset, std::string& error);
    };
}
