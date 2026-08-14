#include "ShaderExecutionPlan.h"

#include "../Shaders/ShaderCatalog.h"

namespace ReplayEngine::Rendering
{
    namespace
    {
        ShaderLayerBlend ResolveBlend(ShaderPassBlend pass_blend,
            ShaderLayerBlend layer_blend) noexcept
        {
            switch (pass_blend)
            {
            case ShaderPassBlend::Alpha:    return ShaderLayerBlend::Alpha;
            case ShaderPassBlend::Additive: return ShaderLayerBlend::Additive;
            case ShaderPassBlend::Multiply: return ShaderLayerBlend::Multiply;
            case ShaderPassBlend::Inherit:
            default:                        return layer_blend;
            }
        }
    }

    std::vector<ShaderExecutionStep> ShaderExecutionPlan::Build(
        const ShaderLayerStack& layers, const ShaderCatalog& catalog)
    {
        std::vector<ShaderExecutionStep> result;
        for (std::size_t layer_index = 0;
            layer_index < layers.Layers().size(); ++layer_index)
        {
            const ShaderLayer& layer = layers.Layers()[layer_index];
            if (!layer.enabled) continue;

            const ShaderID shader = layer.EffectiveShader();
            if (!shader.IsValid()) continue;
            const ShaderCatalog::Entry* entry = catalog.Find(shader);
            if (entry == nullptr || entry->info.domain != ShaderDomain::Layer) continue;

            ShaderExecutionStep main;
            main.kind = ShaderExecutionStepKind::LayerMain;
            main.layer_index = layer_index;
            main.layer_id = layer.id;
            main.shader = shader;
            main.entry_point = "main";
            main.blend = layer.blend;
            result.push_back(std::move(main));

            for (std::size_t pass_index = 0;
                pass_index < entry->passes.size(); ++pass_index)
            {
                ShaderExecutionStep pass;
                pass.kind = ShaderExecutionStepKind::ShaderPass;
                pass.layer_index = layer_index;
                pass.pass_index = pass_index;
                pass.layer_id = layer.id;
                pass.shader = shader;
                pass.entry_point = entry->passes[pass_index].info.entry_point;
                pass.blend = ResolveBlend(entry->passes[pass_index].info.blend,
                    layer.blend);
                result.push_back(std::move(pass));
            }
        }
        return result;
    }
}
