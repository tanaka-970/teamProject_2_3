#include "CharacterMaterialGpuData.h"

namespace ReplayEngine::Rendering
{
    ShaderLayerGpuData ShaderLayerGpuData::FromLayer(const ShaderLayer& layer) noexcept
    {
        ShaderLayerGpuData value{};
        value.pixel_grid = layer.parameter;
        value.pixelate_strength = layer.strength;
        return value;
    }

    CharacterMaterialGpuData CharacterMaterialGpuData::FromProfile(
        const CharacterMaterialProfile& profile) noexcept
    {
        CharacterMaterialGpuData value{};
        value.skin_tint = profile.skin.tint;
        value.skin_shadow_tint = profile.skin.shadow_tint;
        value.face_shadow_tint = profile.face.shadow_tint;
        value.hair_highlight_color = profile.hair.highlight_color;
        value.rim_color = profile.rim.color;
        value.crystal_tint = profile.crystal.tint;
        value.crystal_tint.w = profile.crystal.enabled ? 1.0f : 0.0f;
        value.general_params = { profile.toon_threshold, profile.toon_softness,
            profile.shadow_strength, profile.saturation };
        value.skin_params = { profile.skin.wrap, profile.skin.scatter,
            profile.skin.softness, profile.skin.enabled ? 1.0f : 0.0f };
        value.face_params = { profile.face.light_bias, profile.face.shadow_softness,
            profile.face.front_fill, profile.face.enabled ? 1.0f : 0.0f };
        value.hair_params = { profile.hair.power, profile.hair.intensity,
            profile.hair.anisotropy, profile.hair.enabled ? 1.0f : 0.0f };
        value.rim_params = { profile.rim.power, profile.rim.threshold,
            profile.rim.intensity, profile.rim.enabled ? 1.0f : 0.0f };
        value.crystal_params = { profile.crystal.transparency,
            profile.crystal.fresnel_power, profile.crystal.dispersion,
            profile.crystal.internal_emission };
        value.artistic_top_color = profile.artistic.top_color;
        value.artistic_bottom_color = profile.artistic.bottom_color;
        value.artistic_params = { profile.artistic.shadow_bands,
            profile.artistic.contrast, profile.artistic.hue_shift,
            profile.artistic.gradient_strength };
        value.gradient_params = { profile.artistic.gradient_scale,
            profile.artistic.gradient_offset, 0.0f, 0.0f };
        value.specular_color = profile.specular.color;
        value.specular_params = { profile.specular.power,
            profile.specular.threshold, profile.specular.intensity,
            profile.specular.enabled ? 1.0f : 0.0f };
        return value;
    }
}
