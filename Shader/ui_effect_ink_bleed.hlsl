#include "ui_effect_dynamic_common.hlsli"

void choose_ink_sample(float4 candidate, inout float4 selected)
{
    if (candidate.a > selected.a) selected = candidate;
}

float4 main(VSOutput input) : SV_TARGET
{
    const float scale = max(effect_params0.x, 2.0);
    const float growth = abs(effect_params1.w) > 0.00001
        ? saturate(effect_params1.y + effect_params2.w * effect_params1.w * 0.08)
        : saturate(effect_params1.y);
    const float noise = fbm21(input.uv * target_size.xy / scale +
        effect_params2.z * 17.0 + effect_params2.w * effect_params1.w * 0.05);
    const float bleed_pixels = effect_params0.w * 14.0 * growth *
        lerp(0.65, 1.35, noise);
    const float2 dx = float2(bleed_pixels * target_size.z, 0.0);
    const float2 dy = float2(0.0, bleed_pixels * target_size.w);
    const float2 diagonal = float2(dx.x, dy.y) * 0.70710678;
    const float4 base = source_texture.Sample(source_sampler, input.uv);
    float4 selected = base;
    choose_ink_sample(sample_source_safe(input.uv + dx), selected);
    choose_ink_sample(sample_source_safe(input.uv - dx), selected);
    choose_ink_sample(sample_source_safe(input.uv + dy), selected);
    choose_ink_sample(sample_source_safe(input.uv - dy), selected);
    choose_ink_sample(sample_source_safe(input.uv + diagonal), selected);
    choose_ink_sample(sample_source_safe(input.uv - diagonal), selected);
    choose_ink_sample(sample_source_safe(input.uv + float2(diagonal.x, -diagonal.y)), selected);
    choose_ink_sample(sample_source_safe(input.uv + float2(-diagonal.x, diagonal.y)), selected);
    const float feather = lerp(0.58, 1.0, saturate(effect_params1.z * 2.0));
    const float expanded_alpha = selected.a * smoothstep(0.28 - effect_params1.z,
        0.72 + effect_params1.z, noise + feather * 0.42);
    const float new_alpha = max(base.a, expanded_alpha * effect_color.a);
    const float expansion = saturate((new_alpha - base.a) * 4.0);
    float4 inked = selected;
    inked.rgb = lerp(selected.rgb, effect_color.rgb, saturate(0.35 + expansion * 0.65));
    inked.a = new_alpha;
    return lerp(base, inked, saturate(effect_params0.y));
}
