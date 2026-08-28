#pragma once

#include <DirectXMath.h>

class lights_manager
{
public:
    static constexpr int POINT_LIGHT_MAX = 8;
    static constexpr int SPOT_LIGHT_MAX = 4;

    struct point_light
    {
        DirectX::XMFLOAT4 position{ 0, 0, 0, 5.0f };
        DirectX::XMFLOAT4 color{ 1, 1, 1, 1.0f };
        DirectX::XMFLOAT4 shadow{ -1.0f, 1.0f, 0, 0 };
    };

    struct spot_light
    {
        DirectX::XMFLOAT4 position{ 0, 5, 0, 10.0f };
        DirectX::XMFLOAT4 direction{ 0, -1, 0, 0.95f };
        DirectX::XMFLOAT4 color{ 1, 1, 1, 0.85f };
        DirectX::XMFLOAT4 params{ 1.0f, -1.0f, 1.0f, 0 };
    };

    struct lights_cb
    {
        point_light point_lights[POINT_LIGHT_MAX];
        spot_light spot_lights[SPOT_LIGHT_MAX];
        DirectX::XMINT4 light_counts{ 0, 0, 0, 0 };
    };

    lights_cb data{};
};
