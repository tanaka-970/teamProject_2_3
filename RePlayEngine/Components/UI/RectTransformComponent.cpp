#include "RectTransformComponent.h"

namespace ReplayEngine::Components
{
    RectTransformComponent::RectTransformComponent()
    {
        DirectX::XMStoreFloat4x4(&resolved_matrix_, DirectX::XMMatrixIdentity());
    }
}
