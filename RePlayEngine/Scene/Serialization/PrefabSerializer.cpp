#include "PrefabSerializer.h"

#include "SceneSerializer.h"
#include "../Runtime/Scene.h"

namespace ReplayEngine::Scene::Serialization
{
    bool PrefabSerializer::Save(const Scene& scene, Core::ObjectID root,
        const std::filesystem::path& path, std::string& error)
    {
        SceneData data;
        if (!CaptureGameObjectSubtree(scene, root, data))
        {
            error = "Prefab として保存する GameObject が見つかりません。";
            return false;
        }

        // 書き出しは Scene と同じ v7 ライターをそのまま使う。
        // 形式を分けないので、Prefab をそのまま Scene として開くこともできる。
        return SceneSerializer::SaveToFile(data, path, error);
    }

    Core::ObjectID PrefabSerializer::Instantiate(Scene& scene,
        const std::filesystem::path& path, std::string& error,
        SceneLoadReport* report)
    {
        SceneData data;
        if (!SceneSerializer::LoadFromFile(data, path, error))
        {
            return Core::ObjectID::Invalid();
        }

        SceneLoadReport local;
        SceneLoadReport& target = report != nullptr ? *report : local;

        // Scene は消さずに追加する。ObjectID は採番し直される。
        Core::GameObject* root = InstantiateSceneData(data, scene, target);
        if (root == nullptr)
        {
            error = "Prefab に配置できる GameObject がありません。";
            return Core::ObjectID::Invalid();
        }
        return root->ID();
    }
}
