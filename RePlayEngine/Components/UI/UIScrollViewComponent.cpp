#include "UIScrollViewComponent.h"

#include "RectTransformComponent.h"
#include "UISelectableComponent.h"
#include "UIMaskComponent.h"
#include "../../Object/GameObject/GameObject.h"

namespace ReplayEngine::Components
{
    void UIScrollViewComponent::OnAttach()
    {
        Core::GameObject* owner = Owner();
        if (owner == nullptr) return;
        owner->AddComponent<RectTransformComponent>();
        owner->AddComponent<UISelectableComponent>();
        UIMaskComponent* mask = owner->AddComponent<UIMaskComponent>();
        if (mask != nullptr)
        {
            mask->enabled_mask = true;
            mask->mask_mode = UIMaskComponent::Rectangle;
            mask->show_mask_graphic = true;
        }
    }
}
