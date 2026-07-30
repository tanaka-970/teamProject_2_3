#pragma once

#include <DirectXMath.h>

#include <string>

namespace ReplayEngine::Rendering
{
    struct CharacterMaterialProfile
    {
        struct SkinSettings
        {
            DirectX::XMFLOAT4 tint{ 1.0f, 0.98f, 0.96f, 1.0f };
            DirectX::XMFLOAT4 shadow_tint{ 0.72f, 0.50f, 0.55f, 1.0f };
            float wrap = 0.32f;
            float scatter = 0.12f;
            float softness = 0.10f;
            bool enabled = true;
        } skin;

        struct FaceSettings
        {
            DirectX::XMFLOAT4 shadow_tint{ 0.72f, 0.48f, 0.52f, 1.0f };
            float light_bias = 0.12f;
            float shadow_softness = 0.12f;
            float front_fill = 0.18f;
            bool enabled = true;
        } face;

        struct HairSettings
        {
            DirectX::XMFLOAT4 highlight_color{ 0.75f, 0.86f, 1.0f, 1.0f };
            float power = 48.0f;
            float intensity = 0.32f;
            float anisotropy = 0.75f;
            bool enabled = true;
        } hair;

        struct RimSettings
        {
            DirectX::XMFLOAT4 color{ 0.62f, 0.78f, 1.0f, 1.0f };
            float power = 3.5f;
            float threshold = 0.35f;
            float intensity = 0.30f;
            bool enabled = true;
        } rim;

        struct CrystalSettings
        {
            DirectX::XMFLOAT4 tint{ 0.42f, 0.82f, 1.0f, 1.0f };
            float transparency = 0.0f;
            float fresnel_power = 4.0f;
            float dispersion = 0.0f;
            float internal_emission = 0.0f;
            bool enabled = false;
        } crystal;

        struct ArtisticSettings
        {
            DirectX::XMFLOAT4 top_color{ 1.0f, 1.0f, 1.0f, 1.0f };
            DirectX::XMFLOAT4 bottom_color{ 0.82f, 0.88f, 1.0f, 1.0f };
            float shadow_bands = 3.0f;
            float contrast = 1.0f;
            float hue_shift = 0.0f;
            float gradient_strength = 0.0f;
            float gradient_scale = 0.10f;
            float gradient_offset = 0.50f;
        } artistic;

        struct SpecularSettings
        {
            DirectX::XMFLOAT4 color{ 1.0f, 0.96f, 0.90f, 1.0f };
            float power = 48.0f;
            float threshold = 0.72f;
            float intensity = 0.24f;
            bool enabled = true;
        } specular;

        std::string name{ "カスタム" };
        float toon_threshold = 0.46f;
        float toon_softness = 0.10f;
        float shadow_strength = 0.52f;
        float saturation = 1.0f;

    };
}
