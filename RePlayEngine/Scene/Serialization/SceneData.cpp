#include "SceneData.h"

#include "../Runtime/Scene.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Reflection/Registry/PropertyRegistry.h"

#include <unordered_map>

namespace ReplayEngine::Scene::Serialization
{
    using Core::ComponentRegistry;
    using Core::ComponentTypeInfo;
    using Core::GameObject;
    using Core::ObjectID;
    using Reflection::PropertyRegistry;

    void CaptureScene(const Scene& scene, SceneData& output)
    {
        output.Clear();
        output.scene_name = scene.Name();
        output.objects.reserve(scene.GameObjectCount());

        for (std::size_t index = 0; index < scene.GameObjectCount(); ++index)
        {
            const GameObject* object = scene.GameObjectAt(index);
            if (object == nullptr || object->PendingDestroy()) continue;

            GameObjectData data;
            data.id = object->ID();
            data.name = object->Name();
            data.enabled = object->Enabled();
            data.parent_id = object->Parent() != nullptr
                ? object->Parent()->ID() : ObjectID::Invalid();

            const Core::Transform& transform = object->GetTransform();
            data.position = transform.LocalPosition();
            data.rotation = transform.LocalRotationEuler();
            data.scale = transform.LocalScale();

            for (std::size_t slot = 0; slot < object->ComponentCount(); ++slot)
            {
                const Core::Component* component = object->ComponentAt(slot);
                if (component == nullptr || component->PendingDestroy()) continue;

                // 保存対象外の型はここで落とす。
                // TransformComponent は GameObject の transform として既に保存されており、
                // 重ねて保存すると同じ値がファイル内へ 2 度出てしまう。
                if (!ComponentRegistry::IsSerializable(component->TypeID())) continue;

                ComponentData component_data;
                component_data.type_id = component->TypeID();
                component_data.type_name = component->TypeName();
                component_data.enabled = component->Enabled();
                PropertyRegistry::Capture(*component, component_data.properties);

                data.components.push_back(std::move(component_data));
            }

            output.objects.push_back(std::move(data));
        }
    }

    bool ApplySceneData(const SceneData& data, Scene& scene, SceneLoadReport& report)
    {
        report.Clear();

        // 1) 読み込み中は Update を止める。
        //    途中まで構築された Scene が動かないようにするため。
        scene.BeginLoad();

        // 2) 既存の内容を捨てる。
        scene.Clear();
        scene.SetName(data.scene_name);

        // 3) 全 GameObject を生成する。Component と親子はまだ触らない。
        // 4) 保存 ID -> 実 ID の対応表。ID が重複していて採番し直した場合に備える。
        std::unordered_map<ObjectID, GameObject*> saved_to_object;
        saved_to_object.reserve(data.objects.size());

        for (const GameObjectData& object_data : data.objects)
        {
            GameObject* created = scene.CreateGameObjectWithID(object_data.id, object_data.name);
            if (created == nullptr) continue;

            if (created->ID() != object_data.id)
            {
                ++report.repaired_ids;
                report.warnings.push_back(
                    "ObjectID が重複していたため採番し直しました: " +
                    object_data.id.ToString() + " -> " + created->ID().ToString() +
                    " (" + object_data.name + ")");
            }

            created->SetEnabled(object_data.enabled);
            created->GetTransform().SetLocal(
                object_data.position, object_data.rotation, object_data.scale);

            // 同じ保存 ID が 2 回出てきた場合は最初の 1 つを採用する。
            saved_to_object.emplace(object_data.id, created);
        }

        // 5) 親子関係を復元する。
        //    ワールド姿勢の維持はしない。保存されているのはローカル値であり、
        //    そのまま親へぶら下げるのが元の見た目と一致する。
        for (const GameObjectData& object_data : data.objects)
        {
            if (!object_data.parent_id.Valid()) continue;

            const auto child_found = saved_to_object.find(object_data.id);
            if (child_found == saved_to_object.end()) continue;
            GameObject* child = child_found->second;

            const auto parent_found = saved_to_object.find(object_data.parent_id);
            if (parent_found == saved_to_object.end())
            {
                ++report.repaired_parents;
                report.warnings.push_back(
                    "親 GameObject が見つからないため Scene 直下へ移しました: " +
                    object_data.name + " (親 ID " + object_data.parent_id.ToString() + ")");
                continue;
            }

            // SetParent は循環を検出すると false を返す。壊れたファイル対策。
            if (!child->SetParent(parent_found->second, false))
            {
                ++report.repaired_parents;
                report.warnings.push_back(
                    "親子関係が循環しているため Scene 直下へ移しました: " + object_data.name);
            }
        }

        // 6) Component を生成する。ここで OnAttach が呼ばれる。
        // 7) プロパティを反映する。
        for (const GameObjectData& object_data : data.objects)
        {
            const auto found = saved_to_object.find(object_data.id);
            if (found == saved_to_object.end()) continue;
            GameObject* object = found->second;

            for (const ComponentData& component_data : object_data.components)
            {
                const ComponentTypeInfo* info = ComponentRegistry::Find(component_data.type_name);
                if (info == nullptr)
                {
                    // 未登録の型。Scene 全体を諦めず、この Component だけ飛ばす。
                    ++report.skipped_components;
                    report.warnings.push_back(
                        "未登録の Component のため読み飛ばしました: " +
                        component_data.type_name + " (" + object_data.name + ")");
                    continue;
                }

                Core::Component* component = ComponentRegistry::Create(info->type_id, *object);
                if (component == nullptr)
                {
                    ++report.skipped_components;
                    report.warnings.push_back(
                        "Component を生成できませんでした: " +
                        component_data.type_name + " (" + object_data.name + ")");
                    continue;
                }

                component->SetEnabled(component_data.enabled);

                std::vector<std::string> unknown;
                PropertyRegistry::Apply(*component, component_data.properties, &unknown);
                for (const std::string& name : unknown)
                {
                    ++report.unknown_properties;
                    report.warnings.push_back(
                        "定義が見つからないプロパティを無視しました: " +
                        component_data.type_name + "." + name);
                }
            }
        }

        // 予約状態が残っていないことを保証してから読み込みを終える。
        scene.ProcessPendingOperations();

        // 8) 読み込み完了。OnStart / OnEnable は呼び出し側の Scene::Start() で走る。
        scene.EndLoad();
        return true;
    }

    namespace
    {
        // 1 体ぶんの複製。親の指定は呼び出し側が行う。
        GameObject* DuplicateSingle(Scene& scene, const GameObject& source,
            const std::string& name_override)
        {
            GameObject* clone = scene.CreateGameObject(
                name_override.empty() ? source.Name() : name_override);
            if (clone == nullptr) return nullptr;

            clone->SetEnabled(source.Enabled());

            const Core::Transform& source_transform = source.GetTransform();
            clone->GetTransform().SetLocal(
                source_transform.LocalPosition(),
                source_transform.LocalRotationEuler(),
                source_transform.LocalScale());

            for (std::size_t slot = 0; slot < source.ComponentCount(); ++slot)
            {
                const Core::Component* original = source.ComponentAt(slot);
                if (original == nullptr || original->PendingDestroy()) continue;

                const ComponentTypeInfo* info = ComponentRegistry::Find(original->TypeID());

                // 組み込み Component は GameObject 生成時に自動で付いている。
                // Transform の値は上でコピー済みなので、ここでは触らない。
                if (info != nullptr && info->built_in) continue;

                Core::Component* copy = ComponentRegistry::Create(original->TypeID(), *clone);
                if (copy == nullptr) continue;

                copy->SetEnabled(original->Enabled());
                PropertyRegistry::CopyValues(*original, *copy);
            }
            return clone;
        }
    }

    GameObject* DuplicateGameObject(Scene& scene, const GameObject& source,
        bool include_children)
    {
        GameObject* clone = DuplicateSingle(scene, source, source.Name() + " コピー");
        if (clone == nullptr) return nullptr;

        // 元と同じ親へぶら下げる。ローカル値のまま付けるので見た目の相対位置は変わらない。
        if (source.Parent() != nullptr) clone->SetParent(source.Parent(), false);

        if (!include_children) return clone;

        // 子リストは複製中に変化しないよう控えを取ってから回す。
        const std::vector<GameObject*> children = source.Children();
        for (const GameObject* child : children)
        {
            if (child == nullptr || child->PendingDestroy()) continue;

            GameObject* child_clone = DuplicateGameObject(scene, *child, true);
            if (child_clone == nullptr) continue;

            // 子の名前には「コピー」を付けない。付くのは複製の起点だけでよい。
            child_clone->SetName(child->Name());
            child_clone->SetParent(clone, false);
        }
        return clone;
    }
}
