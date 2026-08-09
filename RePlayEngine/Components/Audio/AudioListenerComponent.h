#pragma once

#include "../Common/PriorityComponentSelection.h"
#include "../../Object/Component/Component.h"
#include "../../Object/GameObject/GameObject.h"

#include <DirectXMath.h>

#include <cmath>

namespace ReplayEngine::Components
{
    class AudioListenerComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(AudioListenerComponent)

    public:
        AudioListenerComponent() = default;

        int priority = 0;

        DirectX::XMFLOAT3 Position() const noexcept
        {
            const Core::GameObject* owner = Owner();
            return owner != nullptr
                ? owner->GetTransform().WorldPosition()
                : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
        }

        DirectX::XMFLOAT3 Forward() const noexcept
        {
            return RotateAxis(0.0f, 0.0f, 1.0f);
        }

        DirectX::XMFLOAT3 Up() const noexcept
        {
            return RotateAxis(0.0f, 1.0f, 0.0f);
        }

    private:
        DirectX::XMFLOAT3 RotateAxis(float x, float y, float z) const noexcept
        {
            const Core::GameObject* owner = Owner();
            if (owner == nullptr) return DirectX::XMFLOAT3{ x, y, z };

            const DirectX::XMFLOAT4 rotation =
                owner->GetTransform().WorldRotationQuaternion();
            DirectX::XMVECTOR axis = DirectX::XMVectorSet(x, y, z, 0.0f);
            axis = DirectX::XMVector3Rotate(axis, DirectX::XMLoadFloat4(&rotation));
            axis = DirectX::XMVector3Normalize(axis);

            DirectX::XMFLOAT3 result{ x, y, z };
            DirectX::XMFLOAT3 rotated{};
            DirectX::XMStoreFloat3(&rotated, axis);
            if (std::isfinite(rotated.x) && std::isfinite(rotated.y) &&
                std::isfinite(rotated.z))
            {
                result = rotated;
            }
            return result;
        }
    };

    using AudioListenerSelection =
        PriorityComponentSelection<AudioListenerComponent>;

    inline AudioListenerSelection ResolveAudioListenerSelection(
        const Scene::Scene& scene, Core::ObjectID controlled = Core::ObjectID::Invalid())
    {
        return ResolvePriorityComponentSelection<AudioListenerComponent>(
            scene, controlled);
    }
}
