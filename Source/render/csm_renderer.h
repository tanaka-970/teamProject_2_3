#pragma once

#include <DirectXMath.h>
#include <cstdint>

class csm_renderer
{
public:
    static constexpr std::uint32_t CASCADE_COUNT = 4;
    static constexpr std::uint32_t SHADOW_MAP_SIZE = 2048;

    struct csm_constants
    {
        DirectX::XMFLOAT4X4 view_projection[CASCADE_COUNT]{};
        DirectX::XMFLOAT4 split_distances{ 12.0f, 34.0f, 90.0f, 240.0f };
        DirectX::XMFLOAT4 params{ 0.02f, 1.4f, 3.0f, 1.0f };
        DirectX::XMFLOAT4 params2{ static_cast<float>(SHADOW_MAP_SIZE), 6.0f, 0.0035f, 1.0f };
        DirectX::XMFLOAT4 params3{ 3.0f, 0.25f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 texel_world{ 0.01f, 0.01f, 0.01f, 0.01f };
    };

    csm_constants constants{};
    DirectX::XMFLOAT3 shadow_volume_center{ 0.0f, 0.0f, 0.0f };
    float shadow_volume_radius{ 0.0f };
    DirectX::XMFLOAT3 shadow_light_direction{ 0.0f, -1.0f, 0.0f };
    float split_lambda{ 0.85f };
    float shadow_distance{ 120.0f };
    float caster_extrusion{ 60.0f };

    void update_cascades(const DirectX::XMFLOAT4& light_direction,
        const DirectX::XMFLOAT4X4& view, const DirectX::XMFLOAT4X4& projection,
        float scene_radius);
};
