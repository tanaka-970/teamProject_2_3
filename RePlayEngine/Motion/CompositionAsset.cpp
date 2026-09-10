#include "CompositionAsset.h"
#include "../Rendering/RenderStats.h"

#include <algorithm>
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
        REPLAY_PROFILE_SCOPE("Asset/Composition");
        std::ifstream file(path);
        if (!file)
        {
            error = "Composition Assetを開けません: " + path.string();
            return false;
        }

        CompositionAsset asset;
        int version = 1;
        std::string line;
        while (std::getline(file, line))
        {
            std::istringstream input(line);
            std::string head;
            if (!(input >> head) || head.empty() || head[0] == '#') continue;

            if (head == "COMPOSITION_VERSION")
            {
                input >> version;
                if (version < 1 || version > current_version)
                {
                    error = "未対応のComposition Assetバージョンです: " +
                        std::to_string(version);
                    return false;
                }
            }
            else if (head == "COMPOSITION")
            {
                input >> std::quoted(asset.name);
            }
            else if (head == "DURATION")
            {
                input >> asset.duration;
            }
            else if (head == "MARKER")
            {
                CompositionMarker marker;
                input >> std::quoted(marker.name) >> marker.time;
                if (!marker.name.empty()) asset.markers.push_back(std::move(marker));
            }
            else if (head == "LAYER")
            {
                CompositionMotionLayer layer;
                int enabled = 1;
                if (version >= 2)
                {
                    input >> std::quoted(layer.name)
                        >> std::quoted(layer.motion_guid)
                        >> std::quoted(layer.composition_guid)
                        >> layer.start_offset >> layer.in_time >> layer.out_time
                        >> layer.time_scale >> layer.weight >> enabled;
                }
                else
                {
                    input >> std::quoted(layer.motion_guid) >> layer.start_offset >> enabled;
                    layer.name = "Layer " + std::to_string(asset.layers.size() + 1);
                }
                layer.enabled = enabled != 0;
                if (!layer.motion_guid.empty() || !layer.composition_guid.empty())
                    asset.layers.push_back(std::move(layer));
            }
        }

        asset.duration = (std::max)(0.0f, asset.duration);
        for (CompositionMarker& marker : asset.markers)
        {
            marker.time = (std::max)(0.0f, marker.time);
            if (asset.duration > 0.0f)
                marker.time = (std::min)(asset.duration, marker.time);
        }
        for (CompositionMotionLayer& layer : asset.layers)
        {
            layer.start_offset = (std::max)(0.0f, layer.start_offset);
            layer.in_time = (std::max)(0.0f, layer.in_time);
            if (layer.out_time >= 0.0f)
                layer.out_time = (std::max)(layer.in_time, layer.out_time);
            layer.time_scale = layer.time_scale == 0.0f ? 1.0f : layer.time_scale;
            layer.weight = (std::max)(0.0f, layer.weight);
        }
        out = std::move(asset);
        error.clear();
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

        file << "COMPOSITION_VERSION " << current_version << '\n';
        file << "COMPOSITION " << std::quoted(asset.name) << '\n';
        file << "DURATION " << (std::max)(0.0f, asset.duration) << '\n';
        for (const CompositionMarker& marker : asset.markers)
            file << "MARKER " << std::quoted(marker.name) << ' ' << marker.time << '\n';
        for (const CompositionMotionLayer& layer : asset.layers)
        {
            file << "LAYER " << std::quoted(layer.name) << ' '
                << std::quoted(layer.motion_guid) << ' '
                << std::quoted(layer.composition_guid) << ' '
                << layer.start_offset << ' ' << layer.in_time << ' '
                << layer.out_time << ' ' << layer.time_scale << ' '
                << layer.weight << ' ' << (layer.enabled ? 1 : 0) << '\n';
        }
        error.clear();
        return static_cast<bool>(file);
    }
}
