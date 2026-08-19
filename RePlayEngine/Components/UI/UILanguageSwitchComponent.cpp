#include "UILanguageSwitchComponent.h"

#include "../../Localization/LocalizationService.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Runtime/API/RuntimeContext.h"
#include "../../Scene/Runtime/Scene.h"

namespace ReplayEngine::Components
{
    void UILanguageSwitchComponent::OnRuntimeAwake()
    {
        EnsureSubscription();
    }

    void UILanguageSwitchComponent::OnEnable()
    {
        EnsureSubscription();
    }

    void UILanguageSwitchComponent::OnDisable()
    {
        ReleaseSubscription();
    }

    void UILanguageSwitchComponent::OnRuntimeDestroy()
    {
        ReleaseSubscription();
    }

    void UILanguageSwitchComponent::EnsureSubscription()
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

    void UILanguageSwitchComponent::ReleaseSubscription() noexcept
    {
        button_state_subscription_.Release();
    }

    void UILanguageSwitchComponent::HandleButtonStateChanged(
        const Runtime::EventRecord& record)
    {
        if (!ActiveInHierarchy() || language.empty()) return;
        Core::GameObject* owner = Owner();
        Scene::Scene* scene = GetScene();
        if (owner == nullptr || scene == nullptr || record.source.IsEmpty() ||
            record.source.world != scene->WorldInstanceID() ||
            record.source.object != owner->ID())
        {
            return;
        }
        const Reflection::PropertyValue* previous = record.payload.Find("previous_state");
        const Reflection::PropertyValue* current = record.payload.Find("state");
        if (previous == nullptr || current == nullptr) return;
        if (previous->AsInt() == 2 && current->AsInt() != 2)
            Localization::LocalizationService::Global().SetLanguage(language);
    }
}
