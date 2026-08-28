#pragma once

#include <DirectXMath.h>

class pbr_renderer
{
public:
    struct light_constants
    {
        DirectX::XMFLOAT4 directional_color{ 1.0f, 1.0f, 1.0f, 3.598f };
        DirectX::XMFLOAT4 ibl_params{ 1.372f, 1.021f, 0.791f, 1.188f };
        DirectX::XMFLOAT4 shadow_params{ 0.741f, 0.00092f, 1.500f, 1.0f };
    };

    struct shadow_constants
    {
        DirectX::XMFLOAT4X4 light_view_projection{};
    };

    struct stage_material_constants
    {
        DirectX::XMFLOAT4 options{ 0.0f, 0.0f, 1.0f, 0.0f };
        DirectX::XMFLOAT4 base_tint{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    light_constants light{};
    shadow_constants shadow{};
    stage_material_constants stage_material{};
};
