#include "UIButtonComponent.h"

#include "RectTransformComponent.h"
#include "UIImageComponent.h"
#include "UISelectableComponent.h"
#include "../../Object/GameObject/GameObject.h"

namespace ReplayEngine::Components
{
    void UIButtonComponent::OnAttach()
    {
        Core::GameObject* owner = Owner();
        if (owner == nullptr) return;

        owner->AddComponent<RectTransformComponent>();
        UISelectableComponent* selectable = owner->AddComponent<UISelectableComponent>();
        if (selectable != nullptr)
        {
            selectable->interactable = interactable;
            selectable->navigation_enabled = navigation_enabled;
            selectable->navigation_order = navigation_order;
        }
        if (!target_image.IsAssigned())
        {
            if (UIImageComponent* image = owner->GetComponent<UIImageComponent>())
            {
                target_image.owner = owner->ID();
                target_image.component = image->StableID();
            }
        }
    }
    void UIButtonComponent::OnPropertyChanged(const char* /*property_name*/)
    {
        Core::GameObject* owner = Owner();
        if (owner == nullptr) return;
        UISelectableComponent* selectable = owner->GetComponent<UISelectableComponent>();
        if (selectable == nullptr) selectable = owner->AddComponent<UISelectableComponent>();
        if (selectable == nullptr) return;
        selectable->interactable = interactable;
        selectable->navigation_enabled = navigation_enabled;
        selectable->navigation_order = navigation_order;
        selectable->focused = focused;
    }

}
