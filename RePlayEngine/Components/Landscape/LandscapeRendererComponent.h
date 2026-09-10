#pragma once

#include "../../Object/Component/Component.h"
#include <DirectXMath.h>
#include <string>

namespace ReplayEngine::Components
{
    // Landscape の描画設定だけを持つ。GPU resource は framework Renderer が所有する。
    class LandscapeRendererComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(LandscapeRendererComponent)
    public:
        static constexpr float unload_hysteresis_scale = 1.1f;

        bool ShouldSubmitChunk(const DirectX::XMFLOAT3& camera_position,
            const DirectX::XMFLOAT3& bounds_min, const DirectX::XMFLOAT3& bounds_max,
            bool resident) const noexcept;

        DirectX::XMFLOAT4 tint{ 0.34f, 0.48f, 0.30f, 1.0f };
        std::string base_color_texture;
        float uv_tiling = 1.0f;
        float load_range = 0.0f;
        bool visible = true;
        bool cast_shadow = true;
        bool receive_shadow = true;
        bool double_sided = true;
    };
}
