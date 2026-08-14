#pragma once

#include "../../Object/Component/Component.h"
#include <DirectXMath.h>

namespace ReplayEngine::Components
{
    // Landscape の描画設定だけを持つ。GPU resource は framework Renderer が所有する。
    class LandscapeRendererComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(LandscapeRendererComponent)
    public:
        DirectX::XMFLOAT4 tint{ 0.34f, 0.48f, 0.30f, 1.0f };
        bool visible = true;
        bool cast_shadow = true;
        bool receive_shadow = true;
        bool double_sided = true;
    };
}
