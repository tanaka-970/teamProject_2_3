#pragma once

#include "../../Object/Component/Component.h"

#include <DirectXMath.h>

namespace ReplayEngine::Components
{
    // Light data lives on ordinary GameObjects. Transform supplies position and
    // orientation; these serializable properties supply photometric settings.
    class DirectionalLightComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(DirectionalLightComponent)
    public:
        DirectX::XMFLOAT4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float intensity = 3.0f;
        bool cast_shadows = true;
    };

    class PointLightComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(PointLightComponent)
    public:
        DirectX::XMFLOAT4 color{ 1.0f, 0.9f, 0.75f, 1.0f };
        float intensity = 2.0f;
        float range = 10.0f;
    };

    class SpotLightComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(SpotLightComponent)
    public:
        DirectX::XMFLOAT4 color{ 1.0f, 0.95f, 0.85f, 1.0f };
        float intensity = 2.0f;
        float range = 12.0f;
        float inner_angle_degrees = 25.0f;
        float outer_angle_degrees = 40.0f;
    };
}
