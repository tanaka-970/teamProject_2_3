#pragma once

#include "../../Assets/AssetDatabase.h"
#include "../../Rendering/Materials/MaterialAsset.h"
#include "../../Rendering/Shaders/ShaderCatalog.h"

namespace ReplayEngine::Editor
{
    // MaterialAsset v3 用の Schema-driven Inspector。
    //
    // Shader 固有の if/switch はここへ追加しない。
    // ShaderSource の #pragma property -> ShaderPropertySchema を唯一の UI 定義にする。
    class MaterialShaderInspector final
    {
    public:
        struct Result final
        {
            bool changed = false;
            bool shader_changed = false;
            bool properties_changed = false;
            bool rendering_changed = false;
            bool missing_shader = false;
        };

        static Result Draw(const char* id,
            Rendering::MaterialAsset& material,
            const Rendering::ShaderCatalog& catalog,
            const Assets::AssetDatabase& assets);
    };
}
