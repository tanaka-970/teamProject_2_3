#include "BuiltInShaderPresets.h"

#include <utility>

namespace ReplayEngine::Rendering
{
    namespace
    {
        ShaderPreset MakePreset(CharacterMaterialProfile profile, int base_shader,
            float character_opacity, bool outline)
        {
            ShaderPreset preset{};
            preset.name = profile.name;
            preset.base_shader = base_shader;
            preset.outline = outline;
            preset.character = std::move(profile);
            auto& character = preset.layers.Add(ShaderLayerType::StylizedCharacter);
            character.opacity = character_opacity;
            character.blend = ShaderLayerBlend::Alpha;
            if (outline) preset.layers.Add(ShaderLayerType::Outline);
            return preset;
        }

        CharacterMaterialProfile WutheringProfile()
        {
            CharacterMaterialProfile value{};
            value.name = "鳴潮風Stylized PBR";
            value.toon_threshold = 0.42f;
            value.toon_softness = 0.16f;
            value.shadow_strength = 0.48f;
            value.skin.wrap = 0.38f;
            value.skin.scatter = 0.16f;
            value.face.front_fill = 0.24f;
            value.hair.intensity = 0.42f;
            value.rim.intensity = 0.42f;
            value.rim.color = { 0.55f, 0.75f, 1.0f, 1.0f };
            value.artistic.shadow_bands = 3.0f;
            value.artistic.contrast = 1.08f;
            value.specular.intensity = 0.32f;
            return value;
        }

        CharacterMaterialProfile EndfieldProfile()
        {
            CharacterMaterialProfile value{};
            value.name = "エンドフィールド風Layered PBR";
            value.toon_threshold = 0.38f;
            value.toon_softness = 0.24f;
            value.shadow_strength = 0.38f;
            value.skin.wrap = 0.26f;
            value.skin.scatter = 0.08f;
            value.face.front_fill = 0.14f;
            value.hair.power = 72.0f;
            value.hair.intensity = 0.28f;
            value.rim.intensity = 0.18f;
            value.saturation = 0.92f;
            value.artistic.shadow_bands = 4.0f;
            value.artistic.contrast = 0.94f;
            value.artistic.gradient_strength = 0.12f;
            value.specular.power = 72.0f;
            value.specular.intensity = 0.20f;
            return value;
        }

        CharacterMaterialProfile CrystalToonProfile()
        {
            CharacterMaterialProfile value = WutheringProfile();
            value.name = "Crystal Toon";
            value.crystal.enabled = true;
            value.crystal.transparency = 0.58f;
            value.crystal.fresnel_power = 3.2f;
            value.crystal.dispersion = 0.18f;
            value.crystal.internal_emission = 0.20f;
            value.rim.intensity = 0.72f;
            value.artistic.gradient_strength = 0.30f;
            value.artistic.top_color = { 0.75f, 0.95f, 1.0f, 1.0f };
            value.artistic.bottom_color = { 0.30f, 0.42f, 0.92f, 1.0f };
            return value;
        }

        CharacterMaterialProfile SoftAnimeProfile()
        {
            CharacterMaterialProfile value{};
            value.name = "柔光アニメ";
            value.toon_threshold = 0.52f;
            value.toon_softness = 0.28f;
            value.shadow_strength = 0.34f;
            value.saturation = 1.08f;
            value.skin.tint = { 1.0f, 0.94f, 0.91f, 1.0f };
            value.skin.shadow_tint = { 0.78f, 0.58f, 0.68f, 1.0f };
            value.skin.wrap = 0.46f;
            value.skin.scatter = 0.24f;
            value.face.shadow_softness = 0.22f;
            value.face.front_fill = 0.32f;
            value.hair.power = 34.0f;
            value.hair.intensity = 0.36f;
            value.rim.color = { 1.0f, 0.72f, 0.76f, 1.0f };
            value.rim.power = 4.5f;
            value.rim.intensity = 0.26f;
            value.artistic.shadow_bands = 3.0f;
            value.artistic.contrast = 0.92f;
            value.specular.threshold = 0.78f;
            value.specular.intensity = 0.18f;
            return value;
        }

        CharacterMaterialProfile GraphicCelProfile()
        {
            CharacterMaterialProfile value{};
            value.name = "硬質グラフィックセル";
            value.toon_threshold = 0.48f;
            value.toon_softness = 0.018f;
            value.shadow_strength = 0.72f;
            value.saturation = 1.16f;
            value.skin.wrap = 0.08f;
            value.skin.scatter = 0.04f;
            value.face.shadow_softness = 0.025f;
            value.face.front_fill = 0.08f;
            value.hair.power = 92.0f;
            value.hair.intensity = 0.54f;
            value.rim.power = 2.2f;
            value.rim.threshold = 0.48f;
            value.rim.intensity = 0.46f;
            value.artistic.shadow_bands = 2.0f;
            value.artistic.contrast = 1.34f;
            value.specular.power = 96.0f;
            value.specular.threshold = 0.84f;
            value.specular.intensity = 0.50f;
            return value;
        }
    }

    ShaderPreset BuiltInShaderPresets::WutheringStylized()
    {
        return MakePreset(WutheringProfile(), 1, 0.68f, true);
    }

    ShaderPreset BuiltInShaderPresets::EndfieldLayered()
    {
        return MakePreset(EndfieldProfile(), 1, 0.52f, false);
    }

    ShaderPreset BuiltInShaderPresets::CrystalToon()
    {
        return MakePreset(CrystalToonProfile(), 2, 0.82f, true);
    }

    ShaderPreset BuiltInShaderPresets::SoftAnime()
    {
        return MakePreset(SoftAnimeProfile(), 2, 0.58f, true);
    }

    ShaderPreset BuiltInShaderPresets::GraphicCel()
    {
        return MakePreset(GraphicCelProfile(), 2, 0.88f, true);
    }

    ShaderPreset BuiltInShaderPresets::MoonlitCrystal()
    {
        CharacterMaterialProfile value = CrystalToonProfile();
        value.name = "月光クリスタル";
        value.saturation = 0.88f;
        value.crystal.tint = { 0.44f, 0.68f, 1.0f, 1.0f };
        value.crystal.transparency = 0.72f;
        value.crystal.fresnel_power = 2.4f;
        value.crystal.dispersion = 0.32f;
        value.crystal.internal_emission = 0.48f;
        value.rim.color = { 0.62f, 0.86f, 1.0f, 1.0f };
        value.rim.power = 2.0f;
        value.rim.intensity = 1.10f;
        value.artistic.top_color = { 0.82f, 0.96f, 1.0f, 1.0f };
        value.artistic.bottom_color = { 0.18f, 0.24f, 0.62f, 1.0f };
        value.artistic.gradient_strength = 0.48f;
        value.specular.color = { 0.72f, 0.92f, 1.0f, 1.0f };
        value.specular.intensity = 0.72f;
        return MakePreset(std::move(value), 2, 0.86f, true);
    }
}
