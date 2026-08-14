#include "UIButtonComponent.h"

#include "RectTransformComponent.h"
#include "UIImageComponent.h"
#include "../../Object/GameObject/GameObject.h"

namespace ReplayEngine::Components
{
    void UIButtonComponent::OnAttach()
    {
        Core::GameObject* owner = Owner();
        if (owner == nullptr) return;

        owner->AddComponent<RectTransformComponent>();
        if (!target_image.IsAssigned())
        {
            if (UIImageComponent* image = owner->GetComponent<UIImageComponent>())
            {
                target_image.owner = owner->ID();
                target_image.component = image->StableID();
            }
        }
    }
}
