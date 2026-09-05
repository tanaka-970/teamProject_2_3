#include "SpriteAtlasAsset.h"
#include "../Rendering/RenderStats.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace ReplayEngine::Assets
{
    const SpriteAtlasRegion* SpriteAtlasAsset::FindRegion(
        const std::string& region_name) const noexcept
    {
        for (const SpriteAtlasRegion& region : regions)
        {
            if (region.name == region_name) return &region;
        }
        return nullptr;
    }

    SpriteAtlasRegion* SpriteAtlasAsset::FindRegion(
        const std::string& region_name) noexcept
    {
        for (SpriteAtlasRegion& region : regions)
        {
            if (region.name == region_name) return &region;
        }
        return nullptr;
    }

    bool SpriteAtlasAsset::LoadFromFile(const std::filesystem::path& path,
        SpriteAtlasAsset& out, std::string& error)
    {
        REPLAY_PROFILE_SCOPE("Asset/SpriteAtlas");
        std::ifstream file(path);
        if (!file)
        {
            error = "Sprite Atlas を開けません: " + path.string();
            return false;
        }

        SpriteAtlasAsset asset;
        int version = 0;
        std::string line;
        while (std::getline(file, line))
        {
            std::istringstream input(line);
            std::string head;
            if (!(input >> head) || head.empty() || head[0] == '#') continue;

            if (head == "SPRITE_ATLAS_VERSION")
            {
                input >> version;
            }
            else if (head == "ATLAS")
            {
                input >> std::quoted(asset.name);
            }
            else if (head == "IMAGE")
            {
                input >> std::quoted(asset.image_guid);
            }
            else if (head == "TEXTURE_DDS")
            {
                input >> std::quoted(asset.embedded_texture_path);
            }
            else if (head == "REGION")
            {
                SpriteAtlasRegion region;
                int rotated = 0;
                input >> std::quoted(region.name)
                    >> region.uv_rect.x >> region.uv_rect.y
                    >> region.uv_rect.z >> region.uv_rect.w
                    >> region.pivot.x >> region.pivot.y
                    >> region.original_size.x >> region.original_size.y
                    >> region.trim_offset.x >> region.trim_offset.y
                    >> rotated;
                region.rotated = rotated != 0;
                if (!region.name.empty()) asset.regions.push_back(std::move(region));
            }
            else if (head == "REGION_PATH")
            {
                std::string region_name;
                std::size_t count = 0;
                input >> std::quoted(region_name) >> count;
                if (region_name.empty() || count < 3 || count > 256) continue;

                SpriteAtlasRegion* region = nullptr;
                for (SpriteAtlasRegion& candidate : asset.regions)
                {
                    if (candidate.name == region_name)
                    {
                        region = &candidate;
                        break;
                    }
                }
                if (region == nullptr) continue;

                region->path_points.clear();
                region->path_points.reserve(count);
                bool valid = true;
                for (std::size_t index = 0; index < count; ++index)
                {
                    DirectX::XMFLOAT2 point{};
                    if (!(input >> point.x >> point.y))
                    {
                        valid = false;
                        break;
                    }
                    point.x = (std::max)(0.0f, (std::min)(1.0f, point.x));
                    point.y = (std::max)(0.0f, (std::min)(1.0f, point.y));
                    region->path_points.push_back(point);
                }
                if (!valid) region->path_points.clear();
            }
        }

        if (version < 1 || version > current_version)
        {
            error = version < 1
                ? "Sprite Atlas の version が不正です"
                : "Sprite Atlas の version が新しすぎます";
            return false;
        }

        std::unordered_set<std::string> names;
        for (const SpriteAtlasRegion& region : asset.regions)
        {
            if (region.name.empty())
            {
                error = "Sprite Atlas に空の Region 名があります";
                return false;
            }
            if (!names.insert(region.name).second)
            {
                error = "Sprite Atlas の Region 名が重複しています: " + region.name;
                return false;
            }
        }
        out = std::move(asset);
        return true;
    }

    bool SpriteAtlasAsset::SaveToFile(const std::filesystem::path& path,
        const SpriteAtlasAsset& asset, std::string& error)
    {
        if (path.has_parent_path())
        {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec)
            {
                error = "Sprite Atlas 保存先を作成できません: " + ec.message();
                return false;
            }
        }

        std::unordered_set<std::string> names;
        for (const SpriteAtlasRegion& region : asset.regions)
        {
            if (region.name.empty())
            {
                error = "Sprite Atlas は空の Region 名を保存できません";
                return false;
            }
            if (!names.insert(region.name).second)
            {
                error = "Sprite Atlas の Region 名が重複しています: " + region.name;
                return false;
            }
        }

        std::ofstream file(path);
        if (!file)
        {
            error = "Sprite Atlas を書き込めません: " + path.string();
            return false;
        }

        file << "SPRITE_ATLAS_VERSION " << current_version << '\n';
        file << "ATLAS " << std::quoted(asset.name) << '\n';
        file << "IMAGE " << std::quoted(asset.image_guid) << '\n';
        if (!asset.embedded_texture_path.empty())
            file << "TEXTURE_DDS " << std::quoted(asset.embedded_texture_path) << '\n';
        file << std::setprecision(9);
        for (const SpriteAtlasRegion& region : asset.regions)
        {
            file << "REGION " << std::quoted(region.name) << ' '
                << region.uv_rect.x << ' ' << region.uv_rect.y << ' '
                << region.uv_rect.z << ' ' << region.uv_rect.w << ' '
                << region.pivot.x << ' ' << region.pivot.y << ' '
                << region.original_size.x << ' ' << region.original_size.y << ' '
                << region.trim_offset.x << ' ' << region.trim_offset.y << ' '
                << (region.rotated ? 1 : 0) << '\n';
            if (region.path_points.size() >= 3)
            {
                file << "REGION_PATH " << std::quoted(region.name) << ' '
                    << region.path_points.size();
                for (const DirectX::XMFLOAT2& point : region.path_points)
                    file << ' ' << point.x << ' ' << point.y;
                file << '\n';
            }
        }
        return static_cast<bool>(file);
    }
}
