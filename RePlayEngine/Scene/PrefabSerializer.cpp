#include "PrefabSerializer.h"
#include "SceneSerializer.h"

namespace ReplayEngine::Scene
{
    bool PrefabSerializer::Save(const SceneEntity& entity,
        const std::filesystem::path& path, std::string& error)
    {
        SceneDocument prefab;
        prefab.Entities().push_back(entity);
        prefab.Entities().front().id = 1;
        prefab.RebuildNextId();
        return SceneSerializer::Save(prefab, path, error);
    }

    EntityId PrefabSerializer::Instantiate(SceneDocument& scene,
        const std::filesystem::path& path, std::string& error)
    {
        SceneDocument prefab;
        if (!SceneSerializer::Load(prefab, path, error) || prefab.Entities().empty()) return 0;
        return scene.ImportEntity(prefab.Entities().front()).id;
    }
}
