#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Motion
{
    struct CompositionMotionLayer
    {
        std::string motion_guid;
        float start_offset = 0.0f;
        bool enabled = true;
    };

    class CompositionAsset final
    {
    public:
        static constexpr const char* file_extension = ".replaycomp";

        std::string name{ "Motion Composition" };
        std::vector<CompositionMotionLayer> layers;

        static bool LoadFromFile(const std::filesystem::path& path,
            CompositionAsset& out, std::string& error);
        static bool SaveToFile(const std::filesystem::path& path,
            const CompositionAsset& asset, std::string& error);
    };
}
