#pragma once

#include "IComponent.h"

#include <DirectXMath.h>

namespace ReplayEngine::Core
{
    class TransformComponent final : public IComponent
    {
    public:
        DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 rotation{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };

        DirectX::XMMATRIX WorldMatrix() const noexcept
        {
            using namespace DirectX;
            return XMMatrixScaling(scale.x, scale.y, scale.z)
                * XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z)
                * XMMatrixTranslation(position.x, position.y, position.z);
        }
    };
}
