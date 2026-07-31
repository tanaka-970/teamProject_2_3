#include "SphereColliderComponent.h"

#include "../../Object/GameObject/GameObject.h"

namespace ReplayEngine::Components
{
    DirectX::XMFLOAT3 SphereColliderComponent::WorldCenter() const noexcept
    {
        const Core::GameObject* owner = Owner();
        if (owner == nullptr) return DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };

        // 中心は Owner の Transform から毎回求める。Collider は座標を持たない。
        const DirectX::XMFLOAT3 position = owner->GetTransform().WorldPosition();
        return DirectX::XMFLOAT3{
            position.x + center_offset.x,
            position.y + center_offset.y,
            position.z + center_offset.z };
    }
}
