#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Motion
{
    struct CompositionMotionLayer
    {
        std::string name{ "Layer" };
        std::string motion_guid;
        std::string composition_guid;
        float start_offset = 0.0f;
        float in_time = 0.0f;
        float out_time = -1.0f;
        float time_scale = 1.0f;
        float weight = 1.0f;
        bool enabled = true;
    };

    struct CompositionMarker
    {
        std::string name;
        float time = 0.0f;
    };

    class CompositionAsset final
    {
    public:
        static constexpr int current_version = 2;
        static constexpr const char* file_extension = ".replaycomp";

        std::string name{ "Motion Composition" };
        float duration = 1.0f;
        std::vector<CompositionMotionLayer> layers;
        std::vector<CompositionMarker> markers;

        static bool LoadFromFile(const std::filesystem::path& path,
            CompositionAsset& out, std::string& error);
        static bool SaveToFile(const std::filesystem::path& path,
            const CompositionAsset& asset, std::string& error);
    };
}
