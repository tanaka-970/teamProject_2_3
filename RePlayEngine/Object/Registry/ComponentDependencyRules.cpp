#include "ComponentDependencyRules.h"

#include "ComponentRegistry.h"
#include "../Component/Component.h"
#include "../GameObject/GameObject.h"

#include <algorithm>
#include <functional>
#include <unordered_set>

namespace ReplayEngine::Core
{
    ComponentDependencyPlan ComponentDependencyRules::PlanRequiredAdd(GameObject& owner,
        ComponentTypeID requested_type, ComponentAvailabilityPolicy policy)
    {
        const std::vector<ComponentTypeID> none;
        return PlanRequiredAdd(owner, requested_type, policy, none);
    }

    ComponentDependencyPlan ComponentDependencyRules::PlanRequiredAdd(GameObject& owner,
        ComponentTypeID requested_type, ComponentAvailabilityPolicy policy,
        const std::vector<ComponentTypeID>& deferred_available)
    {
        ComponentDependencyPlan plan;
        plan.requested_type = requested_type;

        std::unordered_set<ComponentTypeID> visiting;
        std::unordered_set<ComponentTypeID> planned;
        const auto deferred = [&deferred_available](ComponentTypeID type_id)
        {
            return std::find(deferred_available.begin(), deferred_available.end(), type_id) !=
                deferred_available.end();
        };

        std::function<bool(ComponentTypeID, ComponentTypeID, bool)> collect;
        collect = [&](ComponentTypeID type_id, ComponentTypeID required_by,
            bool requested) -> bool
        {
            const ComponentTypeInfo* info = ComponentRegistry::Find(type_id);

            // 依存先は 1 個でも既存なら成立する。要求型だけは allow_multiple に従う。
            Component* existing = owner.FindComponent(type_id);
            if (!requested && (existing != nullptr || deferred(type_id))) return true;
            if (requested && existing != nullptr && info != nullptr && !info->allow_multiple)
                return true;
            if (planned.find(type_id) != planned.end()) return true;

            if (info == nullptr)
            {
                plan.issue = { ComponentDependencyError::UnregisteredType, type_id, required_by };
                return false;
            }
            if (!info->factory)
            {
                plan.issue = { ComponentDependencyError::FactoryUnavailable, type_id, required_by };
                return false;
            }
            if (policy == ComponentAvailabilityPolicy::Runtime && !info->runtime_available)
            {
                plan.issue = { ComponentDependencyError::RuntimeUnavailable, type_id, required_by };
                return false;
            }
            if (!visiting.insert(type_id).second)
            {
                plan.issue = { ComponentDependencyError::Cycle, type_id, required_by };
                return false;
            }

            for (ComponentTypeID dependency_id : info->required_components)
            {
                if (!collect(dependency_id, info->type_id, false))
                {
                    visiting.erase(type_id);
                    return false;
                }
            }

            visiting.erase(type_id);
            if (planned.insert(type_id).second) plan.creation_order.push_back(type_id);
            return true;
        };

        collect(requested_type, invalid_component_type_id, true);
        return plan;
    }

    ComponentDependencyApplyResult ComponentDependencyRules::ApplyRequiredAddPlan(
        GameObject& owner, const ComponentDependencyPlan& plan)
    {
        ComponentDependencyApplyResult result;
        if (!plan.Valid())
        {
            result.issue = plan.issue;
            return result;
        }

        for (ComponentTypeID type_id : plan.creation_order)
        {
            const ComponentTypeInfo* info = ComponentRegistry::Find(type_id);
            if (info == nullptr || !info->factory)
            {
                result.issue = { info == nullptr
                    ? ComponentDependencyError::UnregisteredType
                    : ComponentDependencyError::FactoryUnavailable,
                    type_id, invalid_component_type_id };
                return result;
            }

            const bool requested = type_id == plan.requested_type;
            Component* existing = owner.FindComponent(type_id);
            if ((!requested && existing != nullptr) ||
                (requested && existing != nullptr && !info->allow_multiple))
            {
                if (requested) result.requested_component = existing;
                continue;
            }

            Component* created = ComponentRegistry::Create(type_id, owner);
            if (created == nullptr)
            {
                result.issue = { ComponentDependencyError::CreateFailed,
                    type_id, invalid_component_type_id };
                return result;
            }
            result.added_types.push_back(type_id);
            if (requested) result.requested_component = created;
            else ++result.automatically_added;
        }

        if (result.requested_component == nullptr)
        {
            result.requested_component = owner.FindComponent(plan.requested_type);
        }
        return result;
    }

    std::vector<Component*> ComponentDependencyRules::FindDirectDependents(
        const GameObject& owner, const Component& target, bool* had_unregistered)
    {
        if (had_unregistered != nullptr) *had_unregistered = false;
        std::vector<Component*> dependents;
        if (target.Owner() != &owner) return dependents;

        for (std::size_t index = 0; index < owner.ComponentCount(); ++index)
        {
            Component* candidate = owner.ComponentAt(index);
            if (candidate == nullptr || candidate == &target || candidate->PendingDestroy())
                continue;

            const ComponentTypeInfo* info = ComponentRegistry::Find(candidate->TypeID());
            if (info == nullptr)
            {
                if (had_unregistered != nullptr) *had_unregistered = true;
                continue;
            }
            if (std::find(info->required_components.begin(),
                info->required_components.end(), target.TypeID()) !=
                info->required_components.end())
            {
                dependents.push_back(candidate);
            }
        }
        return dependents;
    }

    std::string ComponentDependencyRules::DescribeIssue(
        const ComponentDependencyIssue& issue)
    {
        if (!issue.Any()) return {};

        std::string reason;
        switch (issue.error)
        {
        case ComponentDependencyError::UnregisteredType:
            reason = "未登録";
            break;
        case ComponentDependencyError::FactoryUnavailable:
            reason = "生成関数なし";
            break;
        case ComponentDependencyError::RuntimeUnavailable:
            reason = "Runtime では利用不可";
            break;
        case ComponentDependencyError::Cycle:
            reason = "循環依存";
            break;
        case ComponentDependencyError::CreateFailed:
            reason = "生成失敗";
            break;
        default:
            reason = "不明な依存エラー";
            break;
        }

        std::string text = ComponentRegistry::DisplayNameOf(issue.type_id) + "（" + reason + "）";
        if (issue.required_by != invalid_component_type_id)
        {
            text += " / 要求元: " + ComponentRegistry::DisplayNameOf(issue.required_by);
        }
        return text;
    }
}
