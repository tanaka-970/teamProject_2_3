#pragma once

#include "../../Rendering/ShaderStack/ShaderLayerStack.h"

#include <DirectXMath.h>

namespace ReplayEngine::Editor
{
    struct ShaderStackEditorResult
    {
        bool requires_pbr = false;
        bool requires_toon = false;
        bool requires_unlit = false;
        bool requires_outline = false;
    };

    class ShaderStackEditor final
    {
    public:
        static ShaderStackEditorResult Draw(const char* id, int& base_shader,
            bool& outline_pass, Rendering::ShaderLayerStack& layers,
            bool& advanced_mode, DirectX::XMFLOAT4& outline_color,
            DirectX::XMFLOAT4& outline_parameters, float& pixel_grid,
            float& pixelate_strength);
    };
}
