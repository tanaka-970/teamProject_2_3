#include "PropertyLinkComponent.h"

#include "../../Motion/MotionEasing.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Reflection/Property/PropertyDesc.h"
#include "../../Reflection/Property/PropertyValue.h"
#include "../../Reflection/Registry/PropertyRegistry.h"
#include "../../Scene/Runtime/Scene.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ReplayEngine::Components
{
    namespace
    {
        using Core::Component;
        using Reflection::PropertyDesc;
        using Reflection::PropertyType;
        using Reflection::PropertyValue;

        bool IsNumeric(PropertyType type) noexcept
        {
            switch (type)
            {
            case PropertyType::Int:
            case PropertyType::Float:
            case PropertyType::Double:
            case PropertyType::Enum:
            case PropertyType::CollisionLayer:
            case PropertyType::CollisionMask:
            case PropertyType::ColliderReference:
            case PropertyType::Int64:
            case PropertyType::UInt64:
                return true;
            default:
                return false;
            }
        }

        const PropertyDesc* FindProperty(const Component& component,
            const std::string& name) noexcept
        {
            if (const PropertyDesc* found =
                Reflection::PropertyRegistry::Find(component.TypeID(), name))
            {
                return found;
            }

            const std::vector<PropertyDesc>* dynamic = component.DynamicProperties();
            if (dynamic != nullptr)
            {
                for (const PropertyDesc& desc : *dynamic)
                {
                    if (desc.name == name) return &desc;
                }
            }
            return nullptr;
        }

        Component* ResolveComponent(Scene::Scene& scene,
            const Reflection::ComponentReference& reference) noexcept
        {
            if (!reference.IsAssigned()) return nullptr;
            Core::GameObject* owner = scene.FindGameObjectByID(reference.owner);
            if (owner == nullptr || owner->PendingDestroy()) return nullptr;
            return owner->FindComponentByStableID(reference.component);
        }

        bool CanReach(Component* from, Component* destination,
            const std::unordered_map<Component*, std::vector<Component*>>& graph,
            std::unordered_set<Component*>& visited) noexcept
        {
            if (from == nullptr || !visited.insert(from).second) return false;

            const auto found = graph.find(from);
            if (found == graph.end()) return false;
            for (Component* next : found->second)
            {
                if (next == destination || CanReach(next, destination, graph, visited))
                    return true;
            }
            return false;
        }

        float Normalized(float value, float low, float high) noexcept
        {
            const float span = high - low;
            if (std::fabs(span) <= 0.000001f) return 0.0f;
            return (value - low) / span;
        }

        float MapValue(const PropertyLinkComponent& link, float source) noexcept
        {
            float normalized = Normalized(source, link.source_min, link.source_max);
            if (link.clamp)
            {
                normalized = (std::max)(0.0f, (std::min)(1.0f, normalized));
            }
            if (link.invert) normalized = 1.0f - normalized;

            const Motion::MotionEasing easing =
                static_cast<Motion::MotionEasing>(link.easing);
            normalized = Motion::ApplyEasing(easing, normalized);
            return link.target_min +
                (link.target_max - link.target_min) * normalized;
        }

        void WarnCycle() noexcept
        {
            OutputDebugStringA("PropertyLinkComponent: cyclic link disabled.\n");
        }
    }

    void PropertyLinkComponent::OnRuntimeAwake()
    {
        smoothing_initialized_ = false;
        smoothed_value_ = 0.0f;
        cycle_disabled_ = false;
    }

    void PropertyLinkComponent::OnRuntimeDestroy()
    {
        smoothing_initialized_ = false;
        smoothed_value_ = 0.0f;
        cycle_disabled_ = false;
    }

    void PropertyLinkComponent::OnPropertyChanged(const char* /*property_name*/)
    {
        // 接続先・範囲・イージングが変わったら、循環判定を次の評価でやり直す。
        cycle_disabled_ = false;
    }

    void PropertyLinkComponent::EvaluateAll(Scene::Scene& scene, float delta_time)
    {
        std::vector<PropertyLinkComponent*> links;
        std::unordered_map<Component*, std::vector<Component*>> graph;

        for (std::size_t object_index = 0;
            object_index < scene.GameObjectCount(); ++object_index)
        {
            Core::GameObject* object = scene.GameObjectAt(object_index);
            if (object == nullptr || object->PendingDestroy()) continue;

            for (std::size_t component_index = 0;
                component_index < object->ComponentCount(); ++component_index)
            {
                Component* component = object->ComponentAt(component_index);
                if (component == nullptr || component->PendingDestroy() ||
                    !component->ActiveInHierarchy())
                {
                    continue;
                }

                if (component->TypeID() != StaticTypeID()) continue;
                auto* link = static_cast<PropertyLinkComponent*>(component);
                links.push_back(link);

                Component* source = ResolveComponent(scene, link->source_object);
                Component* target = ResolveComponent(scene, link->target_object);
                if (source != nullptr && target != nullptr)
                    graph[source].push_back(target);
            }
        }

        const float safe_delta_time = (std::max)(0.0f, delta_time);
        for (PropertyLinkComponent* link : links)
        {
            Component* source = ResolveComponent(scene, link->source_object);
            Component* target = ResolveComponent(scene, link->target_object);
            if (source == nullptr || target == nullptr) continue;

            std::unordered_map<Component*, std::vector<Component*>> graph_without_link = graph;
            auto source_edges = graph_without_link.find(source);
            if (source_edges != graph_without_link.end())
            {
                auto& edges = source_edges->second;
                for (auto edge = edges.begin(); edge != edges.end(); ++edge)
                {
                    if (*edge == target)
                    {
                        edges.erase(edge);
                        break;
                    }
                }
            }
            std::unordered_set<Component*> visited;
            const bool self_link = source == target;
            if (self_link || CanReach(target, source, graph_without_link, visited))
            {
                if (!link->cycle_disabled_)
                {
                    WarnCycle();
                    link->cycle_disabled_ = true;
                }
                continue;
            }
            link->cycle_disabled_ = false;

            const PropertyDesc* source_desc = FindProperty(*source, link->source_property);
            const PropertyDesc* target_desc = FindProperty(*target, link->target_property);
            if (source_desc == nullptr || target_desc == nullptr ||
                !IsNumeric(source_desc->type) || !IsNumeric(target_desc->type) ||
                target_desc->read_only)
            {
                continue;
            }

            const float source_value = source_desc->Capture(*source).AsFloat(0.0f);
            if (!std::isfinite(source_value))
            {
                OutputDebugStringA("PropertyLinkComponent: source value is not finite; link skipped.\n");
                continue;
            }
            const float mapped = MapValue(*link, source_value);
            if (!std::isfinite(mapped))
            {
                OutputDebugStringA("PropertyLinkComponent: mapped value is not finite; link skipped.\n");
                continue;
            }
            float output = mapped;
            if (link->smoothing > 0.0f && std::isfinite(link->smoothing))
            {
                if (!link->smoothing_initialized_)
                {
                    link->smoothed_value_ = mapped;
                    link->smoothing_initialized_ = true;
                }
                else
                {
                    const float alpha = safe_delta_time /
                        (link->smoothing + safe_delta_time);
                    link->smoothed_value_ +=
                        (mapped - link->smoothed_value_) * alpha;
                }
                output = link->smoothed_value_;
            }
            else
            {
                link->smoothing_initialized_ = false;
            }

            PropertyValue value = PropertyValue::MakeFloat(output);
            PropertyValue converted;
            if (value.Type() != target_desc->type &&
                value.ConvertTo(target_desc->type, converted))
            {
                value = std::move(converted);
            }
            target_desc->Apply(*target, value);
            target->OnPropertyChanged(link->target_property.c_str());
        }
    }
}
