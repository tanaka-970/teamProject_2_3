#include "PrefabSerializer.h"

#include "SceneSerializer.h"
#include "../Runtime/Scene.h"
#include "../../Object/GameObject/GameObject.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace ReplayEngine::Scene::Serialization
{
    namespace
    {
        struct PrefabBuildResult
        {
            SceneData data;
            std::unordered_map<Core::ObjectID, std::uint64_t> scene_to_local;
        };

        bool BuildPrefabData(const Scene& scene, Core::ObjectID root,
            const std::string& source_guid, PrefabBuildResult& output, std::string& error)
        {
            output = {};
            const Core::GameObject* root_object = scene.FindGameObjectByID(root);
            if (root_object == nullptr || root_object->PendingDestroy())
            {
                error = "Prefabとして扱うGameObjectが見つかりません。";
                return false;
            }

            if (!CaptureGameObjectSubtree(scene, root, output.data))
            {
                error = "Prefab subtreeを取得できません。";
                return false;
            }

            std::unordered_set<std::uint64_t> used_local_ids;
            std::uint64_t next_local_id = 1;
            for (const GameObjectData& object_data : output.data.objects)
            {
                const Core::GameObject* object = scene.FindGameObjectByID(object_data.id);
                if (object == nullptr) continue;

                // A child that is itself another instance root is a nested Prefab.
                // This build deliberately blocks it instead of silently flattening it.
                if (object->IsPrefabInstance() &&
                    object->PrefabInstanceRoot().Valid() && object->PrefabInstanceRoot() != root)
                {
                    error = "Nested Prefab未対応: " + object->Name();
                    return false;
                }

                std::uint64_t local_id = 0;
                if (object->PrefabInstanceRoot() == root && object->PrefabLocalID() != 0)
                    local_id = object->PrefabLocalID();
                if (local_id == 0 || used_local_ids.find(local_id) != used_local_ids.end())
                {
                    local_id = object->ID().Value();
                    if (local_id == 0 || used_local_ids.find(local_id) != used_local_ids.end())
                    {
                        while (used_local_ids.find(next_local_id) != used_local_ids.end()) ++next_local_id;
                        local_id = next_local_id++;
                    }
                }
                used_local_ids.insert(local_id);
                next_local_id = (std::max)(next_local_id, local_id + 1);
                output.scene_to_local.emplace(object_data.id, local_id);
            }

            for (GameObjectData& object_data : output.data.objects)
            {
                const Core::ObjectID old_id = object_data.id;
                const auto local = output.scene_to_local.find(old_id);
                if (local == output.scene_to_local.end()) continue;
                object_data.id = Core::ObjectID(local->second);

                const auto parent = output.scene_to_local.find(object_data.parent_id);
                object_data.parent_id = parent != output.scene_to_local.end()
                    ? Core::ObjectID(parent->second) : Core::ObjectID::Invalid();

                // Prefab assets contain local hierarchy data, not an instance of another
                // asset. Instance identity is written only by Scene serialization.
                object_data.prefab_source_guid.clear();
                object_data.prefab_local_id = 0;
                object_data.prefab_instance_root = Core::ObjectID::Invalid();

                for (ComponentData& component : object_data.components)
                {
                    for (const Reflection::PropertyBag::Entry& entry : component.properties.Entries())
                    {
                        if (entry.value.Type() != Reflection::PropertyType::ObjectReference) continue;
                        const Core::ObjectID scene_reference = entry.value.AsObjectReference();
                        const auto reference = output.scene_to_local.find(scene_reference);
                        component.properties.Set(entry.name,
                            Reflection::PropertyValue::MakeObjectReference(
                                reference != output.scene_to_local.end()
                                    ? Core::ObjectID(reference->second)
                                    : Core::ObjectID::Invalid()));
                    }
                }
            }
            output.data.controlled_object = Core::ObjectID::Invalid();
            output.data.scene_name = root_object->Name();
            (void)source_guid;
            return true;
        }

        // Transform の比較用。保存・読み込みの丸め差で override 扱いにしないための許容差。
        bool NearlyEqual(float a, float b) noexcept
        {
            return std::fabs(a - b) <= 0.00001f;
        }

        // 値の比較は Reflection::ValuesEqual へ集約した。
        //
        // 以前はここに専用の switch があったが、PropertyType を足すたびに
        // Inspector 側の同等関数と 2 か所直す必要があり、片方を忘れると
        // 「override が検出されない」という気づきにくい不具合になっていた。
        bool EqualValue(const Reflection::PropertyValue& a,
            const Reflection::PropertyValue& b)
        {
            return Reflection::ValuesEqual(a, b);
        }

        bool EqualObject(const GameObjectData& a, const GameObjectData& b,
            std::vector<std::string>& details)
        {
            bool equal = true;
            const auto difference = [&details, &a, &equal](const std::string& label)
            {
                equal = false;
                if (details.size() < 64) details.push_back(a.name + ": " + label);
            };
            if (a.name != b.name) difference("Name override");
            if (a.enabled != b.enabled) difference("Enabled override");
            if (!NearlyEqual(a.position.x, b.position.x) || !NearlyEqual(a.position.y, b.position.y) ||
                !NearlyEqual(a.position.z, b.position.z) || !NearlyEqual(a.rotation.x, b.rotation.x) ||
                !NearlyEqual(a.rotation.y, b.rotation.y) || !NearlyEqual(a.rotation.z, b.rotation.z) ||
                !NearlyEqual(a.scale.x, b.scale.x) || !NearlyEqual(a.scale.y, b.scale.y) ||
                !NearlyEqual(a.scale.z, b.scale.z)) difference("Transform override");
            if (a.parent_id != b.parent_id) difference("Parent override");
            if (a.components.size() != b.components.size()) difference("Component add/remove override");

            const std::size_t component_count = (std::min)(a.components.size(), b.components.size());
            for (std::size_t component_index = 0; component_index < component_count; ++component_index)
            {
                const ComponentData& x = a.components[component_index];
                const ComponentData& y = b.components[component_index];
                if (x.type_name != y.type_name || x.enabled != y.enabled ||
                    x.properties.Size() != y.properties.Size())
                {
                    difference("Component override");
                    continue;
                }
                for (const Reflection::PropertyBag::Entry& entry : x.properties.Entries())
                {
                    const Reflection::PropertyValue* value = y.properties.Find(entry.name);
                    if (value == nullptr || !EqualValue(entry.value, *value))
                    {
                        difference(x.type_name + "." + entry.name);
                    }
                }
            }
            return equal;
        }

        bool EquivalentPrefab(const SceneData& current, const SceneData& asset,
            std::vector<std::string>& details)
        {
            std::unordered_map<Core::ObjectID, const GameObjectData*> asset_objects;
            for (const GameObjectData& object : asset.objects) asset_objects.emplace(object.id, &object);
            bool equal = current.objects.size() == asset.objects.size();
            if (!equal) details.push_back("Child add/remove override");
            for (const GameObjectData& object : current.objects)
            {
                const auto found = asset_objects.find(object.id);
                if (found == asset_objects.end())
                {
                    if (details.size() < 64) details.push_back(object.name + ": Added child");
                    equal = false;
                    continue;
                }
                if (!EqualObject(object, *found->second, details)) equal = false;
            }
            return equal;
        }

        void LinkBuiltInstance(Scene& scene, Core::ObjectID root,
            const std::string& source_guid, const PrefabBuildResult& built)
        {
            for (const auto& entry : built.scene_to_local)
            {
                Core::GameObject* object = scene.FindGameObjectByID(entry.first);
                if (object != nullptr)
                    object->SetPrefabInstanceInfo(source_guid, entry.second, root);
            }
        }
    }

    bool PrefabSerializer::Save(const Scene& scene, Core::ObjectID root,
        const std::filesystem::path& path, std::string& error)
    {
        PrefabBuildResult built;
        if (!BuildPrefabData(scene, root, {}, built, error)) return false;
        return SceneSerializer::SaveToFile(built.data, path, error);
    }

    Core::ObjectID PrefabSerializer::Instantiate(Scene& scene,
        const std::filesystem::path& path, std::string& error,
        SceneLoadReport* report, const std::string& source_asset_guid)
    {
        SceneData data;
        if (!SceneSerializer::LoadFromFile(data, path, error))
            return Core::ObjectID::Invalid();

        SceneLoadReport local;
        SceneLoadReport& target = report != nullptr ? *report : local;
        Core::GameObject* root = InstantiateSceneData(data, scene, target, source_asset_guid);
        if (root == nullptr)
        {
            error = "Prefabに配置できるGameObjectがありません。";
            return Core::ObjectID::Invalid();
        }
        return root->ID();
    }

    bool PrefabSerializer::LinkInstance(Scene& scene, Core::ObjectID root,
        const std::string& source_asset_guid, std::string& error)
    {
        if (source_asset_guid.empty())
        {
            error = "Prefab AssetGUIDが空です。";
            return false;
        }
        PrefabBuildResult built;
        if (!BuildPrefabData(scene, root, source_asset_guid, built, error)) return false;
        LinkBuiltInstance(scene, root, source_asset_guid, built);
        return true;
    }

    bool PrefabSerializer::ApplyOverrides(Scene& scene, Core::ObjectID root,
        const std::filesystem::path& path, const std::string& source_asset_guid,
        std::string& error)
    {
        PrefabBuildResult built;
        if (!BuildPrefabData(scene, root, source_asset_guid, built, error)) return false;
        if (!SceneSerializer::SaveToFile(built.data, path, error)) return false;
        LinkBuiltInstance(scene, root, source_asset_guid, built);
        return true;
    }

    bool PrefabSerializer::RevertOverrides(Scene& scene, Core::ObjectID root,
        const std::filesystem::path& path, const std::string& source_asset_guid,
        std::string& error, SceneLoadReport* report)
    {
        SceneData data;
        if (!SceneSerializer::LoadFromFile(data, path, error)) return false;
        SceneLoadReport local;
        SceneLoadReport& target = report != nullptr ? *report : local;
        if (!ApplyPrefabInstanceData(data, scene, root, source_asset_guid, target))
        {
            error = "Prefab instanceをAsset状態へ戻せませんでした。";
            return false;
        }
        return true;
    }

    bool PrefabSerializer::Unpack(Scene& scene, Core::ObjectID root, std::string& error)
    {
        Core::GameObject* root_object = scene.FindGameObjectByID(root);
        if (root_object == nullptr || !root_object->IsPrefabRoot())
        {
            error = "Prefab Rootが選択されていません。";
            return false;
        }
        std::vector<Core::GameObject*> stack{ root_object };
        std::vector<Core::GameObject*> objects;
        while (!stack.empty())
        {
            Core::GameObject* object = stack.back();
            stack.pop_back();
            if (object == nullptr || object->PendingDestroy()) continue;
            if (object != root_object && object->IsPrefabRoot())
            {
                error = "Nested Prefab未対応のためUnpackを中止しました。";
                return false;
            }
            objects.push_back(object);
            for (Core::GameObject* child : object->Children()) stack.push_back(child);
        }
        for (Core::GameObject* object : objects) object->ClearPrefabInstanceInfo();
        return true;
    }

    PrefabOverrideSummary PrefabSerializer::InspectOverrides(const Scene& scene,
        Core::ObjectID root, const std::filesystem::path& path,
        const std::string& source_asset_guid)
    {
        PrefabOverrideSummary result;
        SceneData asset;
        std::string error;
        if (!SceneSerializer::LoadFromFile(asset, path, error))
        {
            result.missing_source = true;
            result.details.push_back(error);
            return result;
        }
        PrefabBuildResult current;
        if (!BuildPrefabData(scene, root, source_asset_guid, current, error))
        {
            result.unsupported_nested_prefab = error.find("Nested Prefab") != std::string::npos;
            result.details.push_back(error);
            result.has_overrides = true;
            return result;
        }
        result.has_overrides = !EquivalentPrefab(current.data, asset, result.details);
        return result;
    }
}
