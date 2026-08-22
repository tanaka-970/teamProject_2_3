#include "ui_effect_dynamic_common.hlsli"

float4 main(VSOutput input) : SV_TARGET
{
    const float4 base = source_texture.Sample(source_sampler, input.uv);
    const float2 delta = aspect_delta(input.uv, effect_params2.xy);
    const float distance_from_center = length(delta);
    const float progress = abs(effect_params1.w) > 0.00001
        ? frac(effect_params1.y + effect_params2.w * effect_params1.w)
        : saturate(effect_params1.y);
    const float width = max(effect_params0.x, 0.005);
    const float distance_to_ring = abs(distance_from_center - progress * 1.15);
    const float core = 1.0 - smoothstep(width * 0.12, width * 0.48, distance_to_ring);
    const float halo_width = width * (1.0 + effect_params1.z * 4.0);
    const float halo = 1.0 - smoothstep(width * 0.30, halo_width, distance_to_ring);
    const float edge = abs(base.a - sample_source_safe(input.uv +
        float2(target_size.z, 0.0)).a) + abs(base.a - sample_source_safe(input.uv +
        float2(0.0, target_size.w)).a);
    const float pulse = (core + halo * 0.42) * (0.6 + edge * 1.5) *
        effect_params0.w * effect_color.a;
    return float4(lerp(base.rgb, saturate(base.rgb + effect_color.rgb * pulse),
        saturate(effect_params0.y)), base.a);
}
