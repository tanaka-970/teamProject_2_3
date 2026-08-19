#include "UIButtonPropertyToggleComponent.h"

#include "../../Object/GameObject/GameObject.h"
#include "../../Reflection/Property/PropertyDesc.h"
#include "../../Reflection/Property/PropertyValue.h"
#include "../../Reflection/Registry/PropertyRegistry.h"
#include "../../Runtime/API/RuntimeContext.h"
#include "../../Scene/Runtime/Scene.h"

#include <vector>

namespace ReplayEngine::Components
{
    namespace
    {
        Core::Component* ResolveComponent(Scene::Scene& scene,
            const Reflection::ComponentReference& reference) noexcept
        {
            if (!reference.IsAssigned()) return nullptr;
            Core::GameObject* owner = scene.FindGameObjectByID(reference.owner);
            if (owner == nullptr || owner->PendingDestroy()) return nullptr;
            return owner->FindComponentByStableID(reference.component);
        }

        const Reflection::PropertyDesc* FindProperty(
            Core::Component& component, const std::string& name) noexcept
        {
            if (const Reflection::PropertyDesc* desc =
                Reflection::PropertyRegistry::Find(component.TypeID(), name))
            {
                return desc;
            }
            const std::vector<Reflection::PropertyDesc>* dynamic =
                component.DynamicProperties();
            if (dynamic == nullptr) return nullptr;
            for (const Reflection::PropertyDesc& desc : *dynamic)
            {
                if (desc.name == name) return &desc;
            }
            return nullptr;
        }
    }

    void UIButtonPropertyToggleComponent::OnRuntimeAwake()
    {
        EnsureSubscription();
    }

    void UIButtonPropertyToggleComponent::OnEnable()
    {
        EnsureSubscription();
    }

    void UIButtonPropertyToggleComponent::OnDisable()
    {
        ReleaseSubscription();
    }

    void UIButtonPropertyToggleComponent::OnRuntimeDestroy()
    {
        ReleaseSubscription();
    }

    void UIButtonPropertyToggleComponent::EnsureSubscription()
    {
        if (button_state_subscription_.Valid()) return;
        Scene::Scene* scene = GetScene();
        Core::GameObject* owner = Owner();
        Runtime::RuntimeContext* runtime =
            scene != nullptr ? scene->Services().Runtime() : nullptr;
        if (scene == nullptr || owner == nullptr || runtime == nullptr) return;
        const Runtime::ObjectHandle handle = runtime->Resolver().MakeHandle(owner);
        if (handle.IsEmpty()) return;
        button_state_subscription_ = runtime->Events().Subscribe(
            Runtime::EngineEvents::ButtonStateChanged,
            [this](const Runtime::EventRecord& record)
            {
                HandleButtonStateChanged(record);
            }, handle);
    }

    void UIButtonPropertyToggleComponent::ReleaseSubscription() noexcept
    {
        button_state_subscription_.Release();
    }

    void UIButtonPropertyToggleComponent::HandleButtonStateChanged(
        const Runtime::EventRecord& record)
    {
        if (!ActiveInHierarchy() || target_property.empty()) return;
        Core::GameObject* owner = Owner();
        Scene::Scene* scene = GetScene();
        if (owner == nullptr || scene == nullptr || record.source.IsEmpty() ||
            record.source.world != scene->WorldInstanceID() ||
            record.source.object != owner->ID())
        {
            return;
        }

        const Reflection::PropertyValue* previous =
            record.payload.Find("previous_state");
        const Reflection::PropertyValue* current = record.payload.Find("state");
        if (previous == nullptr || current == nullptr ||
            previous->AsInt() != 2 || current->AsInt() == 2)
        {
            return;
        }

        Core::Component* target_component = ResolveComponent(*scene, target);
        if (target_component == nullptr) return;
        const Reflection::PropertyDesc* desc =
            FindProperty(*target_component, target_property);
        if (desc == nullptr || desc->read_only ||
            desc->type != Reflection::PropertyType::Bool)
        {
            return;
        }

        const bool value = desc->Capture(*target_component).AsBool(false);
        desc->Apply(*target_component, Reflection::PropertyValue::MakeBool(!value));
        target_component->OnPropertyChanged(target_property.c_str());
    }
}
