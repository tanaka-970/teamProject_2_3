#pragma once

#include "SceneDocument.h"

#include <filesystem>
#include <string>

namespace ReplayEngine::Scene
{
    class PrefabSerializer final
    {
    public:
        static bool Save(const SceneEntity& entity,
            const std::filesystem::path& path, std::string& error);
        static EntityId Instantiate(SceneDocument& scene,
            const std::filesystem::path& path, std::string& error);
    };
}
