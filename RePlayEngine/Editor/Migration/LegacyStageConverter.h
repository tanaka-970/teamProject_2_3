#pragma once

#include "../../Core/ObjectID/ObjectID.h"
#include "../../Scene/SceneDocument.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Scene { class Scene; }

namespace ReplayEngine::Editor
{
    class LegacyStageConverter final
    {
    public:
        struct Mapping
        {
            Scene::EntityId source_id = 0;
            Core::ObjectID object_id;
        };

        struct Result
        {
            Core::ObjectID stage_root;
            std::vector<Mapping> mappings;
            std::vector<std::string> warnings;
            int renderer_count = 0;
            int collider_count = 0;
            bool already_converted = false;
        };

        LegacyStageConverter() = delete;

        // Editor 専用の一方向変換。旧データは変更しない。
        static bool Convert(const Scene::SceneDocument& source, Scene::Scene& destination,
            Result& result, std::string& error);

        // Scene v9としてAtomic保存し、直後に再読込して最低限の整合を確認する。
        static bool SaveAndVerify(const Scene::Scene& scene,
            const std::filesystem::path& path, std::string& error);
    };
}
