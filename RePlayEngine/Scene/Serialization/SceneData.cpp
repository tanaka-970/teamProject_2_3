// SceneData のうち「参照の付け替え」と「Scene の取り込み」だけを持つ。
//
//   SceneData.cpp           … 参照の付け替えと Scene の取り込み（このファイル）
//   SceneDataInternal.h     … 分割内部で共有する適用ヘルパの宣言
//   SceneDataApply.cpp      … Scene 全体の再構築
//   SceneDataPrefab.cpp     … Prefab の取り込み・再適用
//   SceneDataDuplicate.cpp  … GameObject 部分木の複製

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
    namespace Detail
    {
        namespace
        {
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

    using Detail::RemapReferences;

    void RemapLiveObjectReferences(const std::vector<GameObject*>& objects,
        const ObjectRemap& remap)
    {
        if (remap.empty())
        {
            // 衝突がない通常の Scene 遷移では、Persistent 階層の全 Component に
            // Capture/Apply を行う必要がない。setter の clamp や OnSerialize の
            // 副作用を不要な場面で発生させないため、ここで往復を止める。
            return;
        }

        for (GameObject* object : objects)
        {
            if (object == nullptr || object->PendingDestroy()) continue;

            for (std::size_t index = 0; index < object->ComponentCount(); ++index)
            {
                Core::Component* component = object->ComponentAt(index);
                if (component == nullptr || component->PendingDestroy()) continue;

                Reflection::PropertyBag values;
                PropertyRegistry::Capture(*component, values);

                // 対応表に載るのは今回振り直した ID だけなので、
                // 載っていない参照は持ち越し先で有効な値として残す。
                // true にすると、振り直していない Persistent への参照まで切れてしまう。
                PropertyRegistry::Apply(*component,
                    RemapReferences(values, &remap, false));
            }
        }
    }

    void CaptureScene(const Scene& scene, SceneData& output)
    {
        output.Clear();
        output.scene_name = scene.Name();

        // Scene 単位の状態。Component の有無とは独立して保存する。
        output.controlled_object = scene.Services().ControlledObject();

        output.objects.reserve(scene.GameObjectCount());

        // Hierarchy の兄弟順をファイル形式を変えずに保存する。Apply 側は親を
        // SceneData の順に SetParent して children_ の末尾へ足すため、preorder で
        // 書けば既存 v7 形式のまま root/child の順序が往復する。
        std::vector<const GameObject*> ordered;
        ordered.reserve(scene.GameObjectCount());
        std::unordered_set<ObjectID> visited;
        const auto append_preorder = [&](const GameObject* root, const auto& self) -> void
        {
            if (root == nullptr || root->PendingDestroy() || !visited.insert(root->ID()).second) return;
            ordered.push_back(root);
            for (const GameObject* child : root->Children()) self(child, self);
        };
        for (const GameObject* root : scene.RootGameObjects()) append_preorder(root, append_preorder);
        // 壊れた階層などで root から到達できない Object も失わない。
        for (std::size_t index = 0; index < scene.GameObjectCount(); ++index)
            append_preorder(scene.GameObjectAt(index), append_preorder);

        for (const GameObject* object : ordered)
        {
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
}
