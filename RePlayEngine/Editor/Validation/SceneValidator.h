#pragma once

#include "../../Core/ObjectID/ObjectID.h"

#include <string>
#include <vector>

namespace ReplayEngine::Assets { class AssetDatabase; }
namespace ReplayEngine::Scene { class Scene; }

namespace ReplayEngine::Editor
{
    enum class ValidationSeverity
    {
        Info,
        Warning,
        Error
    };

    struct ValidationIssue
    {
        ValidationSeverity severity = ValidationSeverity::Warning;
        std::string code;
        std::string message;
        std::string suggestion;
        Core::ObjectID object;
    };

    class SceneValidator final
    {
    public:
        SceneValidator() = delete;

        static std::vector<ValidationIssue> Validate(const Scene::Scene& scene,
            const Assets::AssetDatabase* assets);
    };
}
