#pragma once

#include "../Shaders/ShaderAsset.h"

#include <cstdint>
#include <vector>

namespace ReplayEngine::Rendering::BuiltInShaderLayers
{
    // Layer の固定 GUID。
    // MaterialAsset v4 がこの値を永続保存するので、一度公開した値は変更禁止。
    // Pixelate は Phase 4/10 の先行実装で既に 0101 を使っていたため維持する。
    inline constexpr ShaderID Pixelate =
        Reflection::MakeTypeGUID("00000000000000000000000000000101");
    inline constexpr ShaderID Pbr =
        Reflection::MakeTypeGUID("00000000000000000000000000000102");
    inline constexpr ShaderID Toon =
        Reflection::MakeTypeGUID("00000000000000000000000000000103");
    inline constexpr ShaderID Unlit =
        Reflection::MakeTypeGUID("00000000000000000000000000000104");
    inline constexpr ShaderID Wireframe =
        Reflection::MakeTypeGUID("00000000000000000000000000000105");
    inline constexpr ShaderID Outline =
        Reflection::MakeTypeGUID("00000000000000000000000000000106");
    inline constexpr ShaderID StylizedCharacter =
        Reflection::MakeTypeGUID("00000000000000000000000000000107");

    struct Definition final
    {
        ShaderID id;
        std::uint32_t legacy_type = 0;
        const char* display_name = "";
        const char* relative_path = "";
    };

    const std::vector<Definition>& All();

    // Material v2/v3 の enum 値から固定 GUID へ移行するためだけの関数。
    // 新規コードは GUID を直接使うこと。
    ShaderID FromLegacyType(std::uint32_t legacy_type) noexcept;
    bool TryGetLegacyType(ShaderID id, std::uint32_t& out) noexcept;
    bool IsBuiltIn(ShaderID id) noexcept;
}
