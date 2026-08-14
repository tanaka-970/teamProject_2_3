#pragma once

#include "../../Assets/AssetDatabase.h"
#include "../../Reflection/Property/PropertyBag.h"
#include "../../Rendering/Shaders/ShaderAsset.h"

namespace ReplayEngine::Editor
{
    // ShaderPropertySchema を PropertyBag へ描く共通 UI。
    // Material / ShaderLayer / 将来 Shader Composer preview が同じ操作感を使う。
    class ShaderPropertyInspector final
    {
    public:
        static bool Draw(const char* id,
            Reflection::PropertyBag& properties,
            const Rendering::ShaderPropertySchema& schema,
            const Assets::AssetDatabase& assets);
    };
}
