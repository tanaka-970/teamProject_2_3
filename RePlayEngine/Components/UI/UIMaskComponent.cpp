#include "UIMaskComponent.h"

#include "RectTransformComponent.h"
#include "../../Object/GameObject/GameObject.h"

namespace ReplayEngine::Components
{
    void UIMaskComponent::OnAttach()
    {
        if (Core::GameObject* owner = Owner())
        {
            owner->AddComponent<RectTransformComponent>();
        }
    }
}
