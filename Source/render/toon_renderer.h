#pragma once

#include <DirectXMath.h>

class toon_renderer
{
public:
    struct toon_material_constants
    {
        DirectX::XMFLOAT4 shadow_tint{ 0.55f, 0.40f, 0.65f, 0.65f };
        DirectX::XMFLOAT4 rim_color{ 1.00f, 0.85f, 0.60f, 0.75f };
        DirectX::XMFLOAT4 specular_tint{ 1.00f, 1.00f, 0.95f, 0.80f };
        DirectX::XMFLOAT4 toon_params{ 3.0f, 0.55f, 1.0f, 0.0f };
        DirectX::XMFLOAT4 specular_params{ 32.0f, 0.60f, 0.8f, 0.4f };
    };

    struct outline_constants
    {
        DirectX::XMFLOAT4 outline_color{ 0.05f, 0.05f, 0.08f, 1.0f };
        DirectX::XMFLOAT4 outline_params{ 0.020f, 0.020f, 0.0f, 0.0f };
    };

    toon_material_constants material{};
    outline_constants outline{};
};
