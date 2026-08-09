#include "CompositionAsset.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace ReplayEngine::Motion
{
    bool CompositionAsset::LoadFromFile(const std::filesystem::path& path,
        CompositionAsset& out, std::string& error)
    {
        std::ifstream file(path);
        if (!file)
        {
            error = "Composition Assetを開けません: " + path.string();
            return false;
        }

        CompositionAsset asset;
        std::string line;
        while (std::getline(file, line))
        {
            std::istringstream input(line);
            std::string head;
            if (!(input >> head) || head.empty() || head[0] == '#') continue;

            if (head == "COMPOSITION")
            {
                input >> std::quoted(asset.name);
            }
            else if (head == "LAYER")
            {
                CompositionMotionLayer layer;
                int enabled = 1;
                input >> std::quoted(layer.motion_guid) >> layer.start_offset >> enabled;
                layer.enabled = enabled != 0;
                if (!layer.motion_guid.empty()) asset.layers.push_back(std::move(layer));
            }
        }

        out = std::move(asset);
        return true;
    }

    bool CompositionAsset::SaveToFile(const std::filesystem::path& path,
        const CompositionAsset& asset, std::string& error)
    {
        if (path.has_parent_path())
        {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec)
            {
                error = "Composition Assetの保存先を作成できません: " + ec.message();
                return false;
            }
        }

        std::ofstream file(path);
        if (!file)
        {
            error = "Composition Assetを書き込めません: " + path.string();
            return false;
        }

        file << "COMPOSITION " << std::quoted(asset.name) << '\n';
        for (const CompositionMotionLayer& layer : asset.layers)
        {
            file << "LAYER " << std::quoted(layer.motion_guid) << ' ' <<
                layer.start_offset << ' ' << (layer.enabled ? 1 : 0) << '\n';
        }
        return static_cast<bool>(file);
    }
}
