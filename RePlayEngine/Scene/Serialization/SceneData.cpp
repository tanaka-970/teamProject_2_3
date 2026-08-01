#include "SceneData.h"

#include "../Runtime/Scene.h"
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
    using Core::ObjectID;
    using Reflection::PropertyRegistry;

    void CaptureScene(const Scene& scene, SceneData& output)
    {
        output.Clear();
        output.scene_name = scene.Name();

        // Scene 単位の状態。Component の有無とは独立して保存する。
        output.controlled_object = scene.Services().ControlledObject();

        // v9。衝突の問い合わせ先と、旧 Stage の移行済み集合。
        output.collision_backend_mode = scene.Services().CollisionBackendMode();
        output.migrated_legacy_sources =
            scene.Services().LegacyStageMigration().MigratedSources();

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
            data.prefab_source_guid = object->PrefabSourceGUID();
            data.prefab_local_id = object->PrefabLocalID();
            data.prefab_instance_root = object->PrefabInstanceRoot();

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

    namespace
    {
        // GameObject 1 体ぶんの Component を SceneData から作り直す。
        // ApplySceneData と InstantiateSceneData で共通に使う。
        void BuildComponents(const GameObjectData& source, GameObject& target,
            SceneLoadReport& report,
            const std::unordered_map<ObjectID, GameObject*>* object_remap = nullptr)
        {
            for (const ComponentData& component_data : source.components)
            {
                const ComponentTypeInfo* info = ComponentRegistry::Find(component_data.type_name);
                if (info == nullptr)
                {
                    // 未登録の型。Scene 全体を諦めず、この Component だけ飛ばす。
                    ++report.skipped_components;
                    report.warnings.push_back(
                        "未登録の Component のため読み飛ばしました: " +
                        component_data.type_name + " (" + source.name + ")");
                    continue;
                }

                Core::Component* component = ComponentRegistry::Create(info->type_id, target);
                if (component == nullptr)
                {
                    ++report.skipped_components;
                    report.warnings.push_back(
                        "Component を生成できませんでした: " +
                        component_data.type_name + " (" + source.name + ")");
                    continue;
                }

                component->SetEnabled(component_data.enabled);

                Reflection::PropertyBag remapped_properties = component_data.properties;
                if (object_remap != nullptr)
                {
                    for (const Reflection::PropertyBag::Entry& entry : component_data.properties.Entries())
                    {
                        if (entry.value.Type() != Reflection::PropertyType::ObjectReference) continue;
                        const ObjectID saved_reference = entry.value.AsObjectReference();
                        ObjectID actual_reference = ObjectID::Invalid();
                        const auto found_reference = object_remap->find(saved_reference);
                        if (found_reference != object_remap->end() && found_reference->second != nullptr)
                            actual_reference = found_reference->second->ID();
                        remapped_properties.Set(entry.name,
                            Reflection::PropertyValue::MakeObjectReference(actual_reference));
                    }
                }

                std::vector<std::string> unknown;
                PropertyRegistry::Apply(*component, remapped_properties, &unknown);
                for (const std::string& name : unknown)
                {
                    ++report.unknown_properties;
                    report.warnings.push_back(
                        "定義が見つからないプロパティを無視しました: " +
                        component_data.type_name + "." + name);
                }
            }
        }

        // 保存されていた Transform と有効状態を反映する。
        void ApplyObjectBasics(const GameObjectData& source, GameObject& target)
        {
            target.SetEnabled(source.enabled);
            target.GetTransform().SetLocal(source.position, source.rotation, source.scale);
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

            ApplyObjectBasics(object_data, *created);

            // 同じ保存 ID が 2 回出てきた場合は最初の 1 つを採用する。
            saved_to_object.emplace(object_data.id, created);
        }

        // v10 Prefab metadata. Scene ObjectID may have been repaired, so the
        // instance-root reference must pass through the same remap table.
        for (const GameObjectData& object_data : data.objects)
        {
            const auto object_found = saved_to_object.find(object_data.id);
            if (object_found == saved_to_object.end()) continue;
            if (object_data.prefab_source_guid.empty())
            {
                object_found->second->ClearPrefabInstanceInfo();
                continue;
            }
            const auto root_found = saved_to_object.find(object_data.prefab_instance_root);
            if (root_found == saved_to_object.end())
            {
                object_found->second->ClearPrefabInstanceInfo();
                report.warnings.push_back("Prefab instance rootが見つからないためUnpack扱いにしました: " + object_data.name);
                continue;
            }
            object_found->second->SetPrefabInstanceInfo(object_data.prefab_source_guid,
                object_data.prefab_local_id, root_found->second->ID());
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
            BuildComponents(object_data, *found->second, report, &saved_to_object);
        }

        // Scene 単位の状態を復元する。
        //
        // 操作対象 ObjectID は、ID が採番し直された場合に備えて必ず対応表を通す。
        // これを忘れると Play 開始時に操作対象を見失う。
        const auto remap = [&saved_to_object](Core::ObjectID saved) -> Core::ObjectID
        {
            if (!saved.Valid()) return Core::ObjectID::Invalid();
            const auto found = saved_to_object.find(saved);
            return found != saved_to_object.end()
                ? found->second->ID() : Core::ObjectID::Invalid();
        };

        // 対応表を通した結果が無効になる場合（保存された操作対象が
        // 実在しなかった場合）は、無効のまま残す。別の GameObject へ
        // 勝手に乗り移らせない。
        scene.Services().SetControlledObject(remap(data.controlled_object));

        // 衝突の問い合わせ先。v8 以前のファイルは 1 (Hybrid) のまま入ってくる。
        scene.Services().SetCollisionBackendMode(data.collision_backend_mode);

        // 移行元 ID は Scene 内の ObjectID ではなく旧 Stage 側の番号なので、
        // ここでは remap しない。remap すると別のものを指してしまう。
        scene.Services().LegacyStageMigration().SetMigratedSources(
            data.migrated_legacy_sources);

        // 予約状態が残っていないことを保証してから読み込みを終える。
        scene.ProcessPendingOperations();

        // 8) 読み込み完了。OnStart / OnEnable は呼び出し側の Scene::Start() で走る。
        scene.EndLoad();
        return true;
    }

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
            BuildComponents(object_data, *found->second, report, &saved_to_object);
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
                BuildComponents(object_data, *found->second, report, &local_to_object);
        }
        scene.ProcessPendingOperations();
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
