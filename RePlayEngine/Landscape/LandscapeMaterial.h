#pragma once

#include <DirectXMath.h>
#include <array>
#include <string>

namespace ReplayEngine::Landscape
{
    struct LandscapeMaterialLayer
    {
        std::string material_asset_guid;
        float tiling = 1.0f;
        float minimum_height = -10000.0f;
        float maximum_height = 10000.0f;
        float minimum_slope = 0.0f;
        float maximum_slope = 90.0f;
    };

    struct LandscapeMaterial
    {
        static constexpr std::size_t maximum_layers = 8;
        std::array<LandscapeMaterialLayer, maximum_layers> layers{};
        std::size_t layer_count = 0;
        DirectX::XMFLOAT4 base_tint{ 1, 1, 1, 1 };
    };
}
