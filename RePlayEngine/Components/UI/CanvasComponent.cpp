#include "CanvasComponent.h"

#include "RectTransformComponent.h"
#include "../../Object/GameObject/GameObject.h"

namespace ReplayEngine::Components
{
    void CanvasComponent::OnAttach()
    {
        if (Core::GameObject* owner = Owner())
            owner->AddComponent<RectTransformComponent>();
    }
}
