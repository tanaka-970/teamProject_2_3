#pragma once

#include "SceneDocument.h"

#include <filesystem>
#include <string>

namespace ReplayEngine::Scene
{
    class SceneSerializer final
    {
    public:
        static bool Save(const SceneDocument& scene,
            const std::filesystem::path& path, std::string& error);
        static bool Load(SceneDocument& scene,
            const std::filesystem::path& path, std::string& error);
    };
}
