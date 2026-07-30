#pragma once

#include "CharacterMaterialProfile.h"
#include "../ShaderStack/ShaderLayerStack.h"

#include <DirectXMath.h>

namespace ReplayEngine::Rendering
{
    struct alignas(16) ShaderLayerGpuData
    {
        float pixel_grid = 64.0f;
        float pixelate_strength = 1.0f;
        DirectX::XMFLOAT2 padding{ 0.0f, 0.0f };

        static ShaderLayerGpuData FromLayer(const ShaderLayer& layer) noexcept;
    };

    struct alignas(16) CharacterMaterialGpuData
    {
        DirectX::XMFLOAT4 skin_tint;
        DirectX::XMFLOAT4 skin_shadow_tint;
        DirectX::XMFLOAT4 face_shadow_tint;
        DirectX::XMFLOAT4 hair_highlight_color;
        DirectX::XMFLOAT4 rim_color;
        DirectX::XMFLOAT4 crystal_tint;
        DirectX::XMFLOAT4 general_params;
        DirectX::XMFLOAT4 skin_params;
        DirectX::XMFLOAT4 face_params;
        DirectX::XMFLOAT4 hair_params;
        DirectX::XMFLOAT4 rim_params;
        DirectX::XMFLOAT4 crystal_params;
        DirectX::XMFLOAT4 artistic_top_color;
        DirectX::XMFLOAT4 artistic_bottom_color;
        DirectX::XMFLOAT4 artistic_params;
        DirectX::XMFLOAT4 gradient_params;
        DirectX::XMFLOAT4 specular_color;
        DirectX::XMFLOAT4 specular_params;

        static CharacterMaterialGpuData FromProfile(
            const CharacterMaterialProfile& profile) noexcept;
    };

    static_assert(sizeof(ShaderLayerGpuData) % 16 == 0);
    static_assert(sizeof(CharacterMaterialGpuData) % 16 == 0);
}
