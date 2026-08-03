#pragma once

#include "SceneValidator.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ReplayEngine::Assets { class AssetDatabase; }
namespace ReplayEngine::Scene { class SceneCollisionWorld; }

namespace ReplayEngine::Editor
{
    class EditorContext;

    class ValidationPanel final
    {
    public:
        void Draw(EditorContext& context, const Assets::AssetDatabase* assets,
            const Scene::SceneCollisionWorld* collision_world, std::size_t render_item_count);
        void RequestValidation() noexcept { force_validation_ = true; }

        const std::vector<ValidationIssue>& Issues() const noexcept { return issues_; }

    private:
        void Revalidate(EditorContext& context, const Assets::AssetDatabase* assets);

        std::vector<ValidationIssue> issues_;
        std::uint32_t validated_generation_ = 0;
        bool force_validation_ = true;
    };
}
