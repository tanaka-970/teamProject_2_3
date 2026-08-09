#pragma once

#include "ShaderLayerStack.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ReplayEngine::Rendering
{
    class ShaderCatalog;

    enum class ShaderExecutionStepKind : std::uint8_t
    {
        LayerMain = 0,
        ShaderPass,
    };

    struct ShaderExecutionStep final
    {
        ShaderExecutionStepKind kind = ShaderExecutionStepKind::LayerMain;
        std::size_t layer_index = 0;
        std::size_t pass_index = static_cast<std::size_t>(-1);
        std::uint64_t layer_id = 0;
        ShaderID shader;
        std::string entry_point = "main";
        ShaderLayerBlend blend = ShaderLayerBlend::Alpha;
    };

    // Phase 16 の線引きを純粋データで表す。
    // Material が持つ Layer 順 -> 各 Shader が持つ固定 Pass 順、の順で展開する。
    // Editor は Layer だけを並べ替え、Pass はこの計画から触れない。
    class ShaderExecutionPlan final
    {
    public:
        static std::vector<ShaderExecutionStep> Build(
            const ShaderLayerStack& layers, const ShaderCatalog& catalog);
    };
}
