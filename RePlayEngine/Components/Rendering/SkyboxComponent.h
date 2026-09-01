#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/References.h"

namespace ReplayEngine::Components
{
    class SkyboxComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(SkyboxComponent)

    public:
        SkyboxComponent() = default;

        Reflection::AssetReference cubemap;
        int priority = 0;
        bool sky_enabled = true;
        float rotation_degrees = 0.0f;
        float intensity = 1.0f;
    };
}
