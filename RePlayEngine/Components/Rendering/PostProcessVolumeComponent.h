#pragma once

#include "../Common/PriorityComponentSelection.h"
#include "../../Object/Component/Component.h"
#include "../../Scene/Runtime/Scene.h"

#include <DirectXMath.h>

namespace ReplayEngine::Components
{
    class PostProcessVolumeComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(PostProcessVolumeComponent)

    public:
        PostProcessVolumeComponent() = default;

        int priority = 0;
        bool bloom_enabled = true;
        float bloom_threshold = 1.0f;
        float bloom_intensity = 1.0f;
        bool vignette_enabled = false;
        float vignette_intensity = 0.0f;
        bool ssao_enabled = true;
        float ssao_radius = 0.75f;
        float ssao_intensity = 1.0f;
        bool ssr_enabled = false;
        float ssr_intensity = 1.0f;
        bool taa_enabled = true;
        float exposure = 1.0f;
        DirectX::XMFLOAT4 color_filter{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    using PostProcessVolumeSelection =
        PriorityComponentSelection<PostProcessVolumeComponent>;

    inline PostProcessVolumeSelection ResolvePostProcessVolumeSelection(
        const Scene::Scene& scene)
    {
        return ResolvePriorityComponentSelection<PostProcessVolumeComponent>(
            scene, Core::ObjectID::Invalid());
    }
}
