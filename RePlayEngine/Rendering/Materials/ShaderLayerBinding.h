#pragma once

#include "MaterialBinding.h"
#include "../ShaderStack/ShaderLayerStack.h"

namespace ReplayEngine::Rendering
{
    class ShaderCatalog;

    // Asset-driven ShaderLayer を Phase 6/12 の GPU binding 形式へ変換する。
    // ResolvedMaterialBinding を再利用することで b9 / t40+ / hot reload cache を
    // surface と layer で二重実装しない。
    class ShaderLayerBindingResolver final
    {
    public:
        // Layer domain の Shader を解決する。
        // Missing / compile failure の layer は Material 本体を壊さず false。
        // 追加効果なので base surface の magenta fallback にはしない。
        static bool Resolve(const ShaderLayer& layer,
            const ShaderCatalog& catalog, ShaderVariant variant,
            ResolvedMaterialBinding& out);
    };
}
