#pragma once

#include "../../Rendering/ShaderStack/ShaderLayerStack.h"

namespace ReplayEngine::Assets { class AssetDatabase; }
namespace ReplayEngine::Rendering { class ShaderCatalog; }

#include <DirectXMath.h>

namespace ReplayEngine::Editor
{
    struct ShaderStackEditorResult
    {
        bool changed = false;
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
            float& pixelate_strength, bool show_surface_controls = true,
            const Rendering::ShaderCatalog* catalog = nullptr,
            const Assets::AssetDatabase* assets = nullptr);
    };
}
