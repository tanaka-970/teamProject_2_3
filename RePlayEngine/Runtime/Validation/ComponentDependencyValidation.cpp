#include "BehaviourValidationInternal.h"

#include "../../Object/Registry/ComponentDependencyRules.h"
#include "../../Scene/Serialization/SceneData.h"

namespace ReplayEngine::Runtime::Validation::Detail::BehaviourValidation
{
    int RunComponentDependencyValidation()
    {
        Checker check(940);

        // Planner は依存を全部収集してから適用する。循環や未登録では何も作らない。
        {
            Scene::Scene world("DependencyRules");
            Core::GameObject* object = world.CreateGameObject("Target");
            check.Expect(object != nullptr, "依存規則の検証用 GameObject を作れる");
            if (object != nullptr)
            {
                const Core::ComponentDependencyPlan plan =
                    Core::ComponentDependencyRules::PlanRequiredAdd(*object,
                        DependencyLeafComponent::StaticTypeID(),
                        Core::ComponentAvailabilityPolicy::Editor);
                const std::vector<Core::ComponentTypeID> expected_order = {
                    DependencyBaseComponent::StaticTypeID(),
                    DependencyMiddleComponent::StaticTypeID(),
                    DependencyLeafComponent::StaticTypeID(),
                };
                check.Expect(plan.Valid() && plan.creation_order == expected_order,
                    "2 段依存を生成前に Base -> Middle -> Leaf の順で収集する");
                check.Expect(object->GetComponent<DependencyBaseComponent>() == nullptr,
                    "計画だけでは部分追加を残さない");

                const Core::ComponentDependencyApplyResult result =
                    Core::ComponentDependencyRules::ApplyRequiredAddPlan(*object, plan);
                check.Expect(result.Succeeded() && result.automatically_added == 2 &&
                    result.added_types.size() == 3,
                    "共通 plan で必須 2 件と要求型 1 件を追加する");

                auto* base = object->GetComponent<DependencyBaseComponent>();
                auto* middle = object->GetComponent<DependencyMiddleComponent>();
                const std::vector<Core::Component*> dependents = base != nullptr
                    ? Core::ComponentDependencyRules::FindDirectDependents(*object, *base)
                    : std::vector<Core::Component*>{};
                check.Expect(base != nullptr && middle != nullptr &&
                    dependents.size() == 1 && dependents.front() == middle,
                    "直接依存元だけを同じ owner から返す");
                if (middle != nullptr) object->RemoveComponent(middle);
                const std::vector<Core::Component*> after_pending = base != nullptr
                    ? Core::ComponentDependencyRules::FindDirectDependents(*object, *base)
                    : std::vector<Core::Component*>{};
                check.Expect(after_pending.empty(),
                    "削除予約済み Component は依存元に数えない");
            }

            Core::GameObject* cycle_object = world.CreateGameObject("Cycle");
            if (cycle_object != nullptr)
            {
                const Core::ComponentDependencyPlan cycle =
                    Core::ComponentDependencyRules::PlanRequiredAdd(*cycle_object,
                        DependencyCycleAComponent::StaticTypeID(),
                        Core::ComponentAvailabilityPolicy::Editor);
                check.Expect(!cycle.Valid() &&
                    cycle.issue.error == Core::ComponentDependencyError::Cycle &&
                    cycle_object->GetComponent<DependencyCycleAComponent>() == nullptr &&
                    cycle_object->GetComponent<DependencyCycleBComponent>() == nullptr,
                    "循環依存はクラッシュせず、部分追加なしで診断する");

                const Core::ComponentDependencyPlan broken =
                    Core::ComponentDependencyRules::PlanRequiredAdd(*cycle_object,
                        DependencyBrokenComponent::StaticTypeID(),
                        Core::ComponentAvailabilityPolicy::Editor);
                check.Expect(!broken.Valid() &&
                    broken.issue.error == Core::ComponentDependencyError::UnregisteredType &&
                    cycle_object->GetComponent<DependencyBrokenComponent>() == nullptr,
                    "未登録依存はクラッシュせず、部分追加なしで診断する");
            }
            else
            {
                check.Expect(false, "循環依存の検証用 GameObject を作れる");
                check.Expect(false, "未登録依存の検証用 GameObject を作れる");
            }
        }

        namespace Serialization = ReplayEngine::Scene::Serialization;
        Serialization::SceneData data;
        data.scene_name = "DependencyRestore";
        Serialization::GameObjectData object_data;
        object_data.id = ObjectID{ 1 };
        object_data.name = "Restored";

        Serialization::ComponentData leaf_data;
        leaf_data.type_name = DependencyLeafComponent::StaticTypeName();
        leaf_data.type_id = DependencyLeafComponent::StaticTypeID();
        leaf_data.stable_id = 7;
        object_data.components.push_back(leaf_data);

        Serialization::ComponentData middle_data;
        middle_data.type_name = DependencyMiddleComponent::StaticTypeName();
        middle_data.type_id = DependencyMiddleComponent::StaticTypeID();
        middle_data.stable_id = 8;
        object_data.components.push_back(middle_data);
        data.objects.push_back(object_data);

        // Scene 読み込み: 保存型の相対順と StableID を守り、Awake より前に補完する。
        {
            Scene::Scene world("DependencyRestoreTarget");
            RuntimeContext runtime(world);
            world.Services().SetRuntime(&runtime);
            Serialization::SceneLoadReport report;
            const bool applied = Serialization::ApplySceneData(data, world, report);
            Core::GameObject* restored = world.FindGameObjectByID(ObjectID{ 1 });
            auto* leaf = restored != nullptr
                ? restored->GetComponent<DependencyLeafComponent>() : nullptr;
            auto* middle = restored != nullptr
                ? restored->GetComponent<DependencyMiddleComponent>() : nullptr;
            auto* base = restored != nullptr
                ? restored->GetComponent<DependencyBaseComponent>() : nullptr;
            check.Expect(applied && restored != nullptr && leaf != nullptr &&
                middle != nullptr && base != nullptr,
                "Scene 読み込みで保存外の必須 Component を補完する");
            check.Expect(report.automatically_added_components == 1 &&
                report.unresolved_component_dependencies == 0,
                "自動追加数と未解決数を分けて報告する");
            check.Expect(leaf != nullptr && middle != nullptr && base != nullptr &&
                leaf->StableID() == 7 && middle->StableID() == 8 &&
                base->StableID() > 8,
                "自動追加が保存 StableID 7/8 を先取りしない");

            std::size_t leaf_index = restored != nullptr ? restored->ComponentCount() : 0;
            std::size_t base_index = leaf_index;
            std::size_t middle_index = leaf_index;
            if (restored != nullptr)
            {
                for (std::size_t index = 0; index < restored->ComponentCount(); ++index)
                {
                    Core::Component* component = restored->ComponentAt(index);
                    if (component == leaf) leaf_index = index;
                    if (component == base) base_index = index;
                    if (component == middle) middle_index = index;
                }
            }
            check.Expect(leaf_index < base_index && base_index < middle_index,
                "保存 Component の相対順を保ち、欠落依存だけ要求元の前へ入れる");

            world.Start();
            check.Expect(leaf != nullptr && leaf->dependencies_ready_at_runtime_awake,
                "OnRuntimeAwake より前に依存補完が完了する");
            world.Services().SetRuntime(nullptr);
        }

        // Runtime Prefab も同じ BuildComponents を通り、不完全でも root は返す。
        {
            Scene::Scene world("DependencyPrefabTarget");
            RuntimeContext runtime(world);
            world.Services().SetRuntime(&runtime);
            world.Start();
            Serialization::SceneLoadReport report;
            Core::GameObject* root = Serialization::InstantiateSceneData(
                data, world, report, "dependency-validation");
            auto* leaf = root != nullptr
                ? root->GetComponent<DependencyLeafComponent>() : nullptr;
            check.Expect(root != nullptr && leaf != nullptr &&
                root->GetComponent<DependencyBaseComponent>() != nullptr &&
                report.automatically_added_components == 1 &&
                report.unresolved_component_dependencies == 0,
                "Runtime Prefab 配置も共通 BuildComponents で依存を補完する");
            world.Update(0.016f);
            check.Expect(leaf != nullptr && leaf->dependencies_ready_at_runtime_awake,
                "Runtime Prefab でも次の Awake より前に依存が揃う");

            Serialization::SceneData broken_data;
            broken_data.scene_name = "BrokenDependencyPrefab";
            Serialization::GameObjectData broken_object;
            broken_object.id = ObjectID{ 2 };
            broken_object.name = "Broken";
            Serialization::ComponentData broken_component;
            broken_component.type_name = DependencyBrokenComponent::StaticTypeName();
            broken_component.type_id = DependencyBrokenComponent::StaticTypeID();
            broken_component.stable_id = 4;
            broken_object.components.push_back(broken_component);
            broken_data.objects.push_back(broken_object);
            Serialization::SceneLoadReport broken_report;
            Core::GameObject* broken_root = Serialization::InstantiateSceneData(
                broken_data, world, broken_report, "broken-validation");
            check.Expect(broken_root != nullptr &&
                broken_root->GetComponent<DependencyBrokenComponent>() != nullptr &&
                broken_report.unresolved_component_dependencies == 1 &&
                !broken_report.warnings.empty(),
                "未解決依存でも Prefab を失敗させず診断を残す");
            world.Services().SetRuntime(nullptr);
        }

        return check.Report("Component dependency validation");
    }
}
