#include "SceneData.h"
#include "SceneDataInternal.h"

#include "../Runtime/Scene.h"
#include "../../Object/Component/MissingComponent.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Reflection/Registry/PropertyRegistry.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ReplayEngine::Scene::Serialization
{
    using Core::ComponentRegistry;
    using Core::ComponentTypeInfo;
    using Core::GameObject;
    using Core::MissingComponent;
    using Core::ObjectID;
    using Reflection::PropertyRegistry;
    using Detail::ApplyObjectBasics;
    using Detail::BuildComponents;

    bool CaptureGameObjectSubtree(const Scene& scene, Core::ObjectID root, SceneData& output)
    {
        output.Clear();

        const GameObject* root_object = scene.FindGameObjectByID(root);
        if (root_object == nullptr || root_object->PendingDestroy()) return false;

        // 起点とその子孫の ID を集める。深さに上限を設けて、
        // 万一データが壊れていても無限再帰しないようにする。
        std::unordered_set<Core::ObjectID> subtree;
        std::vector<const GameObject*> stack{ root_object };
        while (!stack.empty())
        {
            const GameObject* current = stack.back();
            stack.pop_back();
            if (current == nullptr || current->PendingDestroy()) continue;
            if (!subtree.insert(current->ID()).second) continue;   // 既出なら打ち切り
            if (subtree.size() > 100000) break;
            for (const GameObject* child : current->Children()) stack.push_back(child);
        }

        SceneData whole;
        CaptureScene(scene, whole);

        output.scene_name = root_object->Name();
        for (GameObjectData& object_data : whole.objects)
        {
            if (subtree.find(object_data.id) == subtree.end()) continue;

            // 起点は必ず親なしにする。どの階層を保存しても独立した部分木になる。
            if (object_data.id == root) object_data.parent_id = Core::ObjectID::Invalid();
            output.objects.push_back(std::move(object_data));
        }
        return !output.objects.empty();
    }

    GameObject* InstantiateSceneData(const SceneData& data, Scene& scene,
        SceneLoadReport& report, const std::string& prefab_source_guid)
    {
        report.Clear();
        if (data.objects.empty()) return nullptr;

        // Prefab の中に入っていた操作対象 ID は「配置先 Scene の情報」なので
        // 一切持ち込まない。配置しただけで操作対象が入れ替わらないようにするため。
        // 操作対象を変えるのは、ユーザーが Inspector で「操作対象に設定」を
        // 押したときだけ。

        // Scene は消さない。既存の内容へ追加する。
        // ID は必ず採番し直すので、同じ Prefab を何度置いても衝突しない。
        std::unordered_map<Core::ObjectID, GameObject*> saved_to_object;
        saved_to_object.reserve(data.objects.size());

        for (const GameObjectData& object_data : data.objects)
        {
            GameObject* created = scene.CreateGameObject(object_data.name);
            if (created == nullptr) continue;
            ApplyObjectBasics(object_data, *created);
            saved_to_object.emplace(object_data.id, created);
        }

        GameObject* root = nullptr;
        for (const GameObjectData& object_data : data.objects)
        {
            const auto child_found = saved_to_object.find(object_data.id);
            if (child_found == saved_to_object.end()) continue;
            GameObject* child = child_found->second;

            if (!object_data.parent_id.Valid())
            {
                // 親なし = 部分木の起点。複数ある場合は最初の 1 つを代表とする。
                if (root == nullptr) root = child;
                continue;
            }

            const auto parent_found = saved_to_object.find(object_data.parent_id);
            if (parent_found == saved_to_object.end())
            {
                // Prefab の外を指す親。Scene 直下へ置く。
                ++report.repaired_parents;
                report.warnings.push_back(
                    "Prefab 外の親を参照していたためシーン直下へ置きました: " + object_data.name);
                if (root == nullptr) root = child;
                continue;
            }
            child->SetParent(parent_found->second, false);
        }

        for (const GameObjectData& object_data : data.objects)
        {
            const auto found = saved_to_object.find(object_data.id);
            if (found == saved_to_object.end()) continue;
            // Prefab 配置では ObjectID を採番し直しているため、対応表に無い参照は必ず切る。
            // 残すと、たまたま同じ ID の無関係な GameObject を指してしまう。
            BuildComponents(object_data, *found->second, report, &saved_to_object, true);
        }

        if (root != nullptr && !prefab_source_guid.empty())
        {
            for (const GameObjectData& object_data : data.objects)
            {
                const auto found = saved_to_object.find(object_data.id);
                if (found == saved_to_object.end()) continue;
                found->second->SetPrefabInstanceInfo(prefab_source_guid,
                    object_data.id.Value(), root->ID());
            }
        }

        scene.ProcessPendingOperations();
        return root;
    }

    bool ApplyPrefabInstanceData(const SceneData& data, Scene& scene,
        Core::ObjectID instance_root, const std::string& prefab_source_guid,
        SceneLoadReport& report)
    {
        report.Clear();
        GameObject* root = scene.FindGameObjectByID(instance_root);
        if (root == nullptr || root->PendingDestroy() || data.objects.empty() ||
            prefab_source_guid.empty()) return false;

        std::vector<GameObject*> current_subtree;
        std::vector<GameObject*> stack{ root };
        while (!stack.empty())
        {
            GameObject* current = stack.back();
            stack.pop_back();
            if (current == nullptr || current->PendingDestroy()) continue;
            current_subtree.push_back(current);
            for (GameObject* child : current->Children()) stack.push_back(child);
        }

        std::unordered_map<std::uint64_t, GameObject*> existing_by_local;
        for (GameObject* current : current_subtree)
        {
            if (current->PrefabSourceGUID() == prefab_source_guid &&
                current->PrefabInstanceRoot() == instance_root && current->PrefabLocalID() != 0)
                existing_by_local.emplace(current->PrefabLocalID(), current);
        }

        const GameObjectData* asset_root = nullptr;
        for (const GameObjectData& object_data : data.objects)
        {
            if (!object_data.parent_id.Valid()) { asset_root = &object_data; break; }
        }
        if (asset_root == nullptr) return false;

        std::unordered_map<ObjectID, GameObject*> local_to_object;
        local_to_object.reserve(data.objects.size());
        for (const GameObjectData& object_data : data.objects)
        {
            GameObject* target = nullptr;
            if (object_data.id == asset_root->id)
            {
                target = root;
            }
            else
            {
                const auto existing = existing_by_local.find(object_data.id.Value());
                if (existing != existing_by_local.end()) target = existing->second;
            }
            if (target == nullptr) target = scene.CreateGameObject(object_data.name);
            if (target == nullptr) continue;

            target->SetName(object_data.name);
            ApplyObjectBasics(object_data, *target);
            target->SetPrefabInstanceInfo(prefab_source_guid, object_data.id.Value(), instance_root);
            local_to_object.emplace(object_data.id, target);
        }

        // Move matching children to their asset parents before deleting override-only
        // parents, otherwise recursive destruction could remove a valid matching child.
        for (const GameObjectData& object_data : data.objects)
        {
            const auto child = local_to_object.find(object_data.id);
            if (child == local_to_object.end()) continue;
            if (!object_data.parent_id.Valid()) continue; // root keeps its external parent
            const auto parent = local_to_object.find(object_data.parent_id);
            if (parent != local_to_object.end()) child->second->SetParent(parent->second, false);
        }

        std::unordered_set<GameObject*> target_objects;
        for (const auto& entry : local_to_object) target_objects.insert(entry.second);
        for (GameObject* current : current_subtree)
        {
            if (target_objects.find(current) == target_objects.end()) current->Destroy();
        }

        // Replace every serializable, non-built-in Component with the asset version.
        for (const GameObjectData& object_data : data.objects)
        {
            const auto found = local_to_object.find(object_data.id);
            if (found == local_to_object.end()) continue;
            GameObject* target = found->second;
            std::vector<Core::Component*> removal;
            for (std::size_t slot = 0; slot < target->ComponentCount(); ++slot)
            {
                Core::Component* component = target->ComponentAt(slot);
                if (component == nullptr || component->PendingDestroy()) continue;
                const ComponentTypeInfo* info = ComponentRegistry::Find(component->TypeID());
                if (info != nullptr && !info->built_in && info->serializable)
                    removal.push_back(component);
            }
            for (Core::Component* component : removal) target->RemoveComponent(component);
        }
        scene.ProcessPendingOperations();

        for (const GameObjectData& object_data : data.objects)
        {
            const auto found = local_to_object.find(object_data.id);
            if (found != local_to_object.end())
                BuildComponents(object_data, *found->second, report, &local_to_object, true);
        }
        scene.ProcessPendingOperations();
        return true;
    }
}
