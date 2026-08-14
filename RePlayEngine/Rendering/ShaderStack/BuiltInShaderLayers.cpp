#include "BuiltInShaderLayers.h"

namespace ReplayEngine::Rendering::BuiltInShaderLayers
{
    const std::vector<Definition>& All()
    {
        static const std::vector<Definition> definitions = {
            { Pbr,               0u, "PBR補助",           "Layers/BuiltIn/Pbr.hlsl" },
            { Toon,              1u, "Toon補助",          "Layers/BuiltIn/Toon.hlsl" },
            { Unlit,             2u, "Unlit発光",         "Layers/BuiltIn/Unlit.hlsl" },
            { Pixelate,          3u, "ピクセレーション",   "Layers/Pixelate.hlsl" },
            { Wireframe,         4u, "ワイヤーフレーム",   "Layers/BuiltIn/Wireframe.hlsl" },
            { Outline,           5u, "輪郭線",             "Layers/BuiltIn/Outline.hlsl" },
            { StylizedCharacter, 6u, "キャラクター材質",   "Layers/BuiltIn/StylizedCharacter.hlsl" },
        };
        return definitions;
    }

    ShaderID FromLegacyType(std::uint32_t legacy_type) noexcept
    {
        for (const Definition& definition : All())
        {
            if (definition.legacy_type == legacy_type) return definition.id;
        }
        return ShaderID{};
    }

    bool TryGetLegacyType(ShaderID id, std::uint32_t& out) noexcept
    {
        for (const Definition& definition : All())
        {
            if (definition.id == id)
            {
                out = definition.legacy_type;
                return true;
            }
        }
        return false;
    }

    bool IsBuiltIn(ShaderID id) noexcept
    {
        std::uint32_t unused = 0;
        return TryGetLegacyType(id, unused);
    }
}
