#pragma once

#include "../Component/ComponentTypeID.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ReplayEngine::Core
{
    class Component;
    class GameObject;

    enum class ComponentAvailabilityPolicy
    {
        Editor,
        Runtime,
    };

    enum class ComponentDependencyError
    {
        None,
        UnregisteredType,
        FactoryUnavailable,
        RuntimeUnavailable,
        Cycle,
        CreateFailed,
    };

    struct ComponentDependencyIssue
    {
        ComponentDependencyError error = ComponentDependencyError::None;
        ComponentTypeID type_id = invalid_component_type_id;
        ComponentTypeID required_by = invalid_component_type_id;

        bool Any() const noexcept { return error != ComponentDependencyError::None; }
    };

    struct ComponentDependencyPlan
    {
        ComponentTypeID requested_type = invalid_component_type_id;
        std::vector<ComponentTypeID> creation_order;
        ComponentDependencyIssue issue;

        bool Valid() const noexcept { return !issue.Any(); }
    };

    struct ComponentDependencyApplyResult
    {
        Component* requested_component = nullptr;
        std::vector<ComponentTypeID> added_types;
        std::size_t automatically_added = 0;
        ComponentDependencyIssue issue;

        bool Succeeded() const noexcept
        {
            return requested_component != nullptr && !issue.Any();
        }
    };

    // ComponentTypeInfo::required_components を正本として、追加と削除の入口が
    // 同じ依存判定を使うための読み取り・適用規則。
    class ComponentDependencyRules final
    {
    public:
        ComponentDependencyRules() = delete;

        static ComponentDependencyPlan PlanRequiredAdd(GameObject& owner,
            ComponentTypeID requested_type, ComponentAvailabilityPolicy policy);

        // Scene 復元では、まだ生成していない保存 Component も「後で存在する型」として渡す。
        // これを先に既定生成すると保存 StableID を復元できなくなるためである。
        static ComponentDependencyPlan PlanRequiredAdd(GameObject& owner,
            ComponentTypeID requested_type, ComponentAvailabilityPolicy policy,
            const std::vector<ComponentTypeID>& deferred_available);

        static ComponentDependencyApplyResult ApplyRequiredAddPlan(GameObject& owner,
            const ComponentDependencyPlan& plan);

        static std::vector<Component*> FindDirectDependents(const GameObject& owner,
            const Component& target, bool* had_unregistered = nullptr);

        static std::string DescribeIssue(const ComponentDependencyIssue& issue);
    };
}
