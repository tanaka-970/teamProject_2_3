#include "UITextComponent.h"

#include "RectTransformComponent.h"
#include "../../Object/GameObject/GameObject.h"

namespace ReplayEngine::Components
{
    void UITextComponent::OnAttach()
    {
        if (Core::GameObject* owner = Owner())
        {
            owner->AddComponent<RectTransformComponent>();
        }
    }
}
