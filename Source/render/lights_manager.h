#pragma once

#include <d3d11.h>
#include <wrl.h>
#include <DirectXMath.h>

class lights_manager
{
public:
    static constexpr int POINT_LIGHT_MAX = 8;
    static constexpr int SPOT_LIGHT_MAX  = 4;

    struct point_light
    {
        DirectX::XMFLOAT4 position{ 0, 0, 0, 5.0f };  // xyz pos, w radius
        DirectX::XMFLOAT4 color   { 1, 1, 1, 1.0f };  // rgb, w intensity
    };
    struct spot_light
    {
        DirectX::XMFLOAT4 position { 0, 5, 0, 10.0f };
        DirectX::XMFLOAT4 direction{ 0,-1, 0, 0.95f };  // w = inner cos
        DirectX::XMFLOAT4 color    { 1, 1, 1, 0.85f };  // w = outer cos
        DirectX::XMFLOAT4 params   { 1.0f, 0, 0, 0 };   // x intensity
    };

    struct lights_cb
    {
        point_light point_lights[POINT_LIGHT_MAX];
        spot_light  spot_lights [SPOT_LIGHT_MAX];
        DirectX::XMINT4 light_counts{ 0, 0, 0, 0 };
    };

    Microsoft::WRL::ComPtr<ID3D11Buffer> cb;
    lights_cb data{};

    bool initialize(ID3D11Device* device);
    void update_constants(ID3D11DeviceContext* ctx);
};
