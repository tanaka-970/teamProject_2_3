#include "UIImageComponent.h"

#include "RectTransformComponent.h"
#include "../../Object/GameObject/GameObject.h"

namespace ReplayEngine::Components
{
    void UIImageComponent::OnAttach()
    {
        if (Core::GameObject* owner = Owner())
        {
            owner->AddComponent<RectTransformComponent>();
        }
    }
}
