#include "SceneData.h"
#include "SceneDataInternal.h"

#include "../Runtime/Scene.h"
#include "../../Object/Component/MissingComponent.h"
#include "../../Object/Registry/ComponentDependencyRules.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Reflection/Registry/PropertyRegistry.h"

#include <algorithm>
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

        // 影の深度バイアスは type_version 2 で NDC からワールドメートルへ単位を変えた。
        // 旧既定値が新既定値へ乗るよう比率で移行し、手で詰めた相対値も保つ。
        float LegacyShadowDepthBiasScale(const std::string& type_name) noexcept
        {
            if (type_name == "DirectionalLightComponent") return 0.02f / 0.0016f;
            if (type_name == "PointLightComponent") return 0.02f / 0.0025f;
            if (type_name == "SpotLightComponent") return 0.02f / 0.0018f;
            return 0.0f;
        }

        // 保存データの型バージョンを見て、現在の型が期待する形へ揃える。
        void MigrateSavedProperties(const ComponentTypeInfo& info,
            int saved_type_version, Reflection::PropertyBag& properties)
        {
            if (saved_type_version >= 2) return;
            const float scale = LegacyShadowDepthBiasScale(info.type_name);
            if (scale <= 0.0f) return;
            const Reflection::PropertyValue* saved = properties.Find("shadow_depth_bias");
            if (saved == nullptr) return;

            const float migrated = saved->AsFloat(0.0f) * scale;
            properties.Set("shadow_depth_bias", Reflection::PropertyValue::MakeFloat(
                (std::max)(0.0f, (std::min)(0.5f, migrated))));
        }
        }

        // GameObject 1 体ぶんの Component を SceneData から作り直す。
        // ApplySceneData と InstantiateSceneData で共通に使う。
        //
        // clear_unresolved_references:
        //   Prefab 配置・複製のように ObjectID を採番し直す経路では true。
        //   Scene 読み込みのように保存 ID をそのまま復元する経路では false。
        void BuildComponents(const GameObjectData& source, GameObject& target,
            SceneLoadReport& report,
            const ObjectRemap* object_remap,
            bool clear_unresolved_references)
        {
            const ReplayEngine::Scene::Scene* owner_scene = target.GetScene();
            const bool runtime_world = owner_scene != nullptr &&
                owner_scene->Services().Runtime() != nullptr;
            const Core::ComponentAvailabilityPolicy availability = runtime_world
                ? Core::ComponentAvailabilityPolicy::Runtime
                : Core::ComponentAvailabilityPolicy::Editor;

            // 欠落依存の自動追加が、後続の保存 Component の StableID を
            // 先取りしないようにする。保存 ID 自体は結線時に明示復元できるので、
            // 自動採番だけを保存範囲より上へ進めておく。
            for (const ComponentData& component_data : source.components)
            {
                if (component_data.stable_id != Core::invalid_component_stable_id)
                    target.EnsureComponentStableIDAbove(component_data.stable_id);
            }

            // 保存されている型は StableID 付きの生成まで既定値で先取りしない。
            // Component 同士の保存順も変えず、欠落している依存だけを途中へ差し込む。
            std::vector<Core::ComponentTypeID> saved_types;
            for (const ComponentData& component_data : source.components)
            {
                const ComponentTypeInfo* info = ComponentRegistry::Resolve(
                    component_data.type_guid, component_data.type_name);
                if (info == nullptr || (runtime_world && !info->runtime_available)) continue;
                if (std::find(saved_types.begin(), saved_types.end(), info->type_id) ==
                    saved_types.end())
                {
                    saved_types.push_back(info->type_id);
                }
            }

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

                // EditorOnly Component はファイルには残すが Runtime World へは持ち込まない。
                // RuntimeSceneService は ApplySceneData より前に Services().Runtime() を
                // 接続するため、Editor 読み込みとの区別を型ごとの if なしで行える。
                if (!info->runtime_available && owner_scene != nullptr &&
                    owner_scene->Services().Runtime() != nullptr)
                {
                    continue;
                }

                for (Core::ComponentTypeID dependency_id : info->required_components)
                {
                    if (target.FindComponent(dependency_id) != nullptr) continue;
                    if (std::find(saved_types.begin(), saved_types.end(), dependency_id) !=
                        saved_types.end())
                    {
                        continue;
                    }

                    const Core::ComponentDependencyPlan plan =
                        Core::ComponentDependencyRules::PlanRequiredAdd(target,
                            dependency_id, availability, saved_types);
                    if (!plan.Valid())
                    {
                        report.warnings.push_back(
                            "必須 Component の補完計画に失敗しました: " +
                            ComponentRegistry::DisplayNameOf(info->type_id) + " -> " +
                            Core::ComponentDependencyRules::DescribeIssue(plan.issue) +
                            " (" + source.name + ")");
                        continue;
                    }

                    const Core::ComponentDependencyApplyResult added =
                        Core::ComponentDependencyRules::ApplyRequiredAddPlan(target, plan);
                    if (!added.Succeeded())
                    {
                        report.warnings.push_back(
                            "必須 Component を補完できませんでした: " +
                            ComponentRegistry::DisplayNameOf(info->type_id) + " -> " +
                            Core::ComponentDependencyRules::DescribeIssue(added.issue) +
                            " (" + source.name + ")");
                        continue;
                    }
                    report.automatically_added_components +=
                        static_cast<int>(added.added_types.size());
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
                Reflection::PropertyBag saved_properties =
                    ApplyPropertyAliases(*info, remapped_properties);
                MigrateSavedProperties(*info, component_data.type_version, saved_properties);
                PropertyRegistry::Apply(*component, saved_properties, &unknown);
                for (const std::string& name : unknown)
                {
                    ++report.unknown_properties;
                    report.warnings.push_back(
                        "この型が知らないプロパティを保持しました（保存時に書き戻します）: " +
                        component_data.type_name + "." + name);
                }
            }

            // 生成を止めない方針なので、最後に残った不足を独立した件数で報告する。
            // 上位は Prefab の root を受け取りつつ、この値から不完全な構成を検知できる。
            for (std::size_t index = 0; index < target.ComponentCount(); ++index)
            {
                Core::Component* component = target.ComponentAt(index);
                if (component == nullptr || component->PendingDestroy()) continue;
                const ComponentTypeInfo* component_info =
                    ComponentRegistry::Find(component->TypeID());
                if (component_info == nullptr) continue;
                for (Core::ComponentTypeID dependency_id :
                    component_info->required_components)
                {
                    if (target.FindComponent(dependency_id) != nullptr) continue;
                    ++report.unresolved_component_dependencies;
                    report.warnings.push_back(
                        "必須 Component が未解決のままです: " +
                        ComponentRegistry::DisplayNameOf(component->TypeID()) + " -> " +
                        ComponentRegistry::DisplayNameOf(dependency_id) +
                        " (" + source.name + ")");
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

    using Detail::ApplyObjectBasics;
    using Detail::BuildComponents;

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
}
