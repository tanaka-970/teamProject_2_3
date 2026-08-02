#include "SceneData.h"

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

    namespace
    {
        // 保存 ObjectID -> 実際に作られた GameObject の対応表。
        using ObjectRemap = std::unordered_map<ObjectID, GameObject*>;

        // 参照値を配置先の ObjectID へ付け替える。
        //
        // clear_unresolved の意味:
        //   false … 対応表に無ければ元の値をそのまま残す（Scene 読み込み）
        //           ファイルに書かれている全 Object が対応表に載っているので、
        //           見つからない＝本当に存在しない参照。値を消すと
        //           「参照が壊れている」ことすら分からなくなるため残す。
        //           Validation が Missing 参照として報告する。
        //
        //   true  … 対応表に無ければ無効値にする（Prefab 配置・複製）
        //           ObjectID を採番し直しているので、元の ID を残すと
        //           たまたま同じ ID を持つ無関係な GameObject を指してしまう。
        //           これは黙って壊れるので、必ず切る。
        Reflection::PropertyValue RemapReferenceValue(const Reflection::PropertyValue& value,
            const ObjectRemap& remap, bool clear_unresolved)
        {
            const auto translate = [&remap, clear_unresolved](ObjectID saved, bool& resolved)
            {
                resolved = false;
                if (!saved.Valid()) return ObjectID::Invalid();

                const auto found = remap.find(saved);
                if (found != remap.end() && found->second != nullptr)
                {
                    resolved = true;
                    return found->second->ID();
                }
                return clear_unresolved ? ObjectID::Invalid() : saved;
            };

            switch (value.Type())
            {
            case Reflection::PropertyType::ObjectReference:
            {
                bool resolved = false;
                return Reflection::PropertyValue::MakeObjectReference(
                    translate(value.AsObjectReference(), resolved));
            }
            case Reflection::PropertyType::ComponentReference:
            {
                // 付け替えるのは所有 ObjectID だけ。
                // ComponentStableID は GameObject の中で閉じた番号なので、
                // 配置先でもそのまま通用する。ここが GameObject 単位にした利点。
                Reflection::ComponentReference reference = value.AsComponentReference();
                bool resolved = false;
                reference.owner = translate(reference.owner, resolved);
                if (!resolved && clear_unresolved) reference.Clear();
                return Reflection::PropertyValue::MakeComponentReference(reference);
            }
            case Reflection::PropertyType::Array:
            {
                // 参照を含まない配列でも通るが、要素ごとに素通りするだけなので害はない。
                std::vector<Reflection::PropertyValue> elements;
                elements.reserve(value.ArrayElements().size());
                for (const Reflection::PropertyValue& element : value.ArrayElements())
                {
                    elements.push_back(RemapReferenceValue(element, remap, clear_unresolved));
                }
                return Reflection::PropertyValue::MakeArray(
                    value.ArrayElementType(), std::move(elements));
            }
            default:
                // AssetReference / SceneReference は AssetGUID なので付け替え不要。
                // Asset は Scene の配置とは無関係に存在する。
                return value;
            }
        }

        Reflection::PropertyBag RemapReferences(const Reflection::PropertyBag& source,
            const ObjectRemap* remap, bool clear_unresolved)
        {
            if (remap == nullptr) return source;

            Reflection::PropertyBag result = source;
            for (const Reflection::PropertyBag::Entry& entry : source.Entries())
            {
                result.Set(entry.name, RemapReferenceValue(entry.value, *remap, clear_unresolved));
            }
            return result;
        }
    }

    void CaptureScene(const Scene& scene, SceneData& output)
    {
        output.Clear();
        output.scene_name = scene.Name();

        // Scene 単位の状態。Component の有無とは独立して保存する。
        output.controlled_object = scene.Services().ControlledObject();

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
                component_data.enabled = component->Enabled();
                component_data.stable_id = component->StableID();

                if (component->TypeID() == MissingComponent::StaticTypeID())
                {
                    // 読み込めなかった Component。
                    //
                    // "MissingComponent" として書き出さず、預かっている「元の型」として
                    // 書き戻す。こうしておくと、
                    //   - 型が使えない環境で開いて保存してもファイルの内容が変わらない
                    //   - 型が使えるようになった環境では、次の読み込みで自動的に復元される
                    // という往復になる。Missing であること自体はファイルに残さない。
                    const MissingComponent::Record& record =
                        static_cast<const MissingComponent*>(component)->Original();

                    component_data.type_name = record.type_name;
                    component_data.type_id = Core::MakeComponentTypeID(record.type_name);
                    component_data.type_guid = record.type_guid;
                    component_data.module_id = record.module_id;
                    component_data.type_version = record.type_version;
                    component_data.properties = record.properties;
                }
                else
                {
                    component_data.type_id = component->TypeID();
                    component_data.type_name = component->TypeName();

                    // 型の永続情報は Registry が持つ。Component 実体は持たない。
                    if (const ComponentTypeInfo* info = ComponentRegistry::Find(component->TypeID()))
                    {
                        component_data.type_guid = info->type_guid;
                        component_data.module_id = info->module_id;
                        component_data.type_version = info->type_version;
                    }

                    // Capture の中で、預かっている未知プロパティも合流する。
                    PropertyRegistry::Capture(*component, component_data.properties);
                }

                data.components.push_back(std::move(component_data));
            }

            output.objects.push_back(std::move(data));
        }
    }

    namespace
    {
        // 保存済みのプロパティ名を、型が宣言した別名に従って読み替える。
        // Field を Rename しても保存値を失わないための入口。
        Reflection::PropertyBag ApplyPropertyAliases(const ComponentTypeInfo& info,
            const Reflection::PropertyBag& source)
        {
            if (info.property_aliases.empty()) return source;

            Reflection::PropertyBag result;
            for (const Reflection::PropertyBag::Entry& entry : source.Entries())
            {
                const std::string* renamed = nullptr;
                for (const auto& alias : info.property_aliases)
                {
                    if (alias.first == entry.name) { renamed = &alias.second; break; }
                }
                result.Set(renamed != nullptr ? *renamed : entry.name, entry.value);
            }
            return result;
        }

        // GameObject 1 体ぶんの Component を SceneData から作り直す。
        // ApplySceneData と InstantiateSceneData で共通に使う。
        //
        // clear_unresolved_references:
        //   Prefab 配置・複製のように ObjectID を採番し直す経路では true。
        //   Scene 読み込みのように保存 ID をそのまま復元する経路では false。
        void BuildComponents(const GameObjectData& source, GameObject& target,
            SceneLoadReport& report,
            const ObjectRemap* object_remap = nullptr,
            bool clear_unresolved_references = false)
        {
            for (const ComponentData& component_data : source.components)
            {
                // 型の解決は必ず Resolve を通す。
                //   type_guid -> alias_guids -> type_name の順で引く。
                const ComponentTypeInfo* info =
                    ComponentRegistry::Resolve(component_data.type_guid, component_data.type_name);

                const Reflection::PropertyBag remapped_properties =
                    RemapReferences(component_data.properties, object_remap,
                        clear_unresolved_references);

                if (info == nullptr)
                {
                    // 型が見つからない。
                    //
                    // ここで読み飛ばすと、この Scene を保存し直した時点で
                    // 保存されていた値がファイルから消える。
                    // Script の Compile が通っていないだけ、Asset が未取得なだけ、
                    // といった一時的な状態でも同じことが起きるため、実質のデータ破壊になる。
                    //
                    // 代わりに MissingComponent へ丸ごと預ける。
                    // 動作はしないが、値は次の保存でそのまま書き戻される。
                    Core::Component* placeholder = ComponentRegistry::CreateWithStableID(
                        MissingComponent::StaticTypeID(), target, component_data.stable_id);
                    if (placeholder == nullptr)
                    {
                        ++report.skipped_components;
                        report.warnings.push_back(
                            "Missing Component の預かり先を作れませんでした: " +
                            component_data.type_name + " (" + source.name + ")");
                        continue;
                    }

                    MissingComponent::Record record;
                    record.type_name = component_data.type_name;
                    record.type_guid = component_data.type_guid;
                    record.module_id = component_data.module_id;
                    record.type_version = component_data.type_version;
                    // 参照は預かるデータに対しても付け替える。
                    // Prefab として配置された Missing Component が、
                    // 配置元 Scene の GameObject を指したままにならないようにする。
                    record.properties = remapped_properties;

                    static_cast<MissingComponent*>(placeholder)->SetOriginal(std::move(record));
                    placeholder->SetEnabled(component_data.enabled);

                    // 採番し直しの報告は Missing 経路でも行う。
                    // 「型が読めなかった」ことと「番号が衝突した」ことは別の問題であり、
                    // 片方だけ黙っていると、参照が別の Component へ向いた原因を追えなくなる。
                    if (component_data.stable_id != Core::invalid_component_stable_id &&
                        placeholder->StableID() != component_data.stable_id)
                    {
                        ++report.repaired_component_ids;
                        report.warnings.push_back(
                            "Component StableID が重複していたため採番し直しました: " +
                            component_data.type_name + " (" + source.name + ")");
                    }

                    ++report.missing_components;
                    report.warnings.push_back(
                        "型が見つからないため Missing Component として保持しました: " +
                        component_data.type_name +
                        (component_data.module_id.empty()
                            ? std::string{} : " [" + component_data.module_id + "]") +
                        " (" + source.name + ")");
                    continue;
                }

                Core::Component* component = ComponentRegistry::CreateWithStableID(
                    info->type_id, target, component_data.stable_id);
                if (component == nullptr)
                {
                    ++report.skipped_components;
                    report.warnings.push_back(
                        "Component を生成できませんでした: " +
                        component_data.type_name + " (" + source.name + ")");
                    continue;
                }

                if (component_data.stable_id != Core::invalid_component_stable_id &&
                    component->StableID() != component_data.stable_id)
                {
                    ++report.repaired_component_ids;
                    report.warnings.push_back(
                        "Component StableID が重複していたため採番し直しました: " +
                        component_data.type_name + " (" + source.name + ")");
                }

                component->SetEnabled(component_data.enabled);

                std::vector<std::string> unknown;
                PropertyRegistry::Apply(*component,
                    ApplyPropertyAliases(*info, remapped_properties), &unknown);
                for (const std::string& name : unknown)
                {
                    ++report.unknown_properties;
                    report.warnings.push_back(
                        "この型が知らないプロパティを保持しました（保存時に書き戻します）: " +
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

    namespace
    {
        // 複製元の部分木を、親→子の順で集める。
        // 深さに上限を設けて、壊れた階層でも無限再帰しない。
        void CollectSubtree(const GameObject& root, bool include_children,
            std::vector<const GameObject*>& output)
        {
            output.push_back(&root);
            if (!include_children) return;

            for (std::size_t index = 0; index < output.size(); ++index)
            {
                if (output.size() > 100000) break;

                const GameObject* current = output[index];
                for (const GameObject* child : current->Children())
                {
                    if (child == nullptr || child->PendingDestroy()) continue;
                    output.push_back(child);
                }
            }
        }

        // Component を 1 つ複製する。StableID は元と同じ値を引き継ぐ。
        //
        // 引き継ぐ理由:
        //   複製した部分木の中で ComponentReference が閉じている場合、
        //   所有 ObjectID さえ付け替えれば参照がそのまま成立する。
        //   StableID を振り直すと、その対応表まで作らなければならなくなる。
        void DuplicateComponent(const Core::Component& original, GameObject& clone,
            const ObjectRemap& remap)
        {
            const ComponentTypeInfo* info = ComponentRegistry::Find(original.TypeID());

            // 組み込み Component は GameObject 生成時に自動で付いている。
            // Transform の値は呼び出し側でコピー済みなので、ここでは触らない。
            if (info != nullptr && info->built_in) return;

            Core::Component* copy = ComponentRegistry::CreateWithStableID(
                original.TypeID(), clone, original.StableID());
            if (copy == nullptr) return;

            copy->SetEnabled(original.Enabled());

            // 値をいったん取り出し、参照だけ付け替えてから流し込む。
            //
            // PropertyRegistry::CopyValues を直接使わないのは、あれが
            // 「値をそのまま写す」ためのものであり、ObjectID の付け替えを行わないため。
            // 複製先は必ず新しい ObjectID を持つので、付け替えないと
            // 複製した側が元のオブジェクトを指したままになる。
            Reflection::PropertyBag values;
            PropertyRegistry::Capture(original, values);
            PropertyRegistry::Apply(*copy, RemapReferences(values, &remap, true));

            // MissingComponent は PropertyRegistry の対象外なので、預かり内容を直接写す。
            if (original.TypeID() == MissingComponent::StaticTypeID() &&
                copy->TypeID() == MissingComponent::StaticTypeID())
            {
                MissingComponent::Record record =
                    static_cast<const MissingComponent&>(original).Original();
                record.properties = RemapReferences(record.properties, &remap, true);
                static_cast<MissingComponent*>(copy)->SetOriginal(std::move(record));
            }
        }
    }

    GameObject* DuplicateGameObject(Scene& scene, const GameObject& source,
        bool include_children)
    {
        // 3 段階に分ける。
        //   1) 複製する GameObject をすべて先に作る
        //   2) 保存 ObjectID -> 複製先 の対応表を完成させる
        //   3) その対応表を使って Component を写す
        //
        // 以前は 1 体ずつ完結させていたため、まだ作られていない兄弟を指す
        // ObjectReference / ComponentReference を付け替えられなかった。
        std::vector<const GameObject*> originals;
        CollectSubtree(source, include_children, originals);

        ObjectRemap remap;
        remap.reserve(originals.size());

        std::vector<GameObject*> clones;
        clones.reserve(originals.size());

        for (const GameObject* original : originals)
        {
            // 名前に「コピー」を付けるのは複製の起点だけでよい。
            GameObject* clone = scene.CreateGameObject(
                original == &source ? original->Name() + " コピー" : original->Name());
            if (clone == nullptr)
            {
                clones.push_back(nullptr);
                continue;
            }

            clone->SetEnabled(original->Enabled());

            const Core::Transform& source_transform = original->GetTransform();
            clone->GetTransform().SetLocal(
                source_transform.LocalPosition(),
                source_transform.LocalRotationEuler(),
                source_transform.LocalScale());

            // Prefab instance の情報も引き継ぐ。instance root は下で張り替える。
            if (original->IsPrefabInstance())
            {
                clone->SetPrefabInstanceInfo(original->PrefabSourceGUID(),
                    original->PrefabLocalID(), clone->ID());
            }

            clones.push_back(clone);
            remap.emplace(original->ID(), clone);
        }

        GameObject* root_clone = clones.empty() ? nullptr : clones.front();
        if (root_clone == nullptr) return nullptr;

        // 階層を張り直す。複製元の親が部分木の中にいれば複製先の親へ、
        // 外にいれば（＝起点）元と同じ親へぶら下げる。
        for (std::size_t index = 0; index < originals.size(); ++index)
        {
            GameObject* clone = clones[index];
            if (clone == nullptr) continue;

            const GameObject* original_parent = originals[index]->Parent();
            if (original_parent == nullptr)
            {
                continue;
            }

            const auto found = remap.find(original_parent->ID());
            clone->SetParent(found != remap.end() ? found->second
                : const_cast<GameObject*>(original_parent), false);
        }

        // Prefab instance root を複製先の起点へ揃える。
        for (GameObject* clone : clones)
        {
            if (clone == nullptr || !clone->IsPrefabInstance()) continue;
            clone->SetPrefabInstanceInfo(clone->PrefabSourceGUID(),
                clone->PrefabLocalID(), root_clone->ID());
        }

        for (std::size_t index = 0; index < originals.size(); ++index)
        {
            GameObject* clone = clones[index];
            if (clone == nullptr) continue;

            const GameObject& original = *originals[index];
            for (std::size_t slot = 0; slot < original.ComponentCount(); ++slot)
            {
                const Core::Component* component = original.ComponentAt(slot);
                if (component == nullptr || component->PendingDestroy()) continue;
                DuplicateComponent(*component, *clone, remap);
            }
        }

        return root_clone;
    }
}
