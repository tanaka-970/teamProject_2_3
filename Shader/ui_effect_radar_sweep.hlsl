#include "ui_effect_dynamic_common.hlsli"

float4 main(VSOutput input) : SV_TARGET
{
    const float4 base = source_texture.Sample(source_sampler, input.uv);
    const float2 delta = aspect_delta(input.uv, effect_params2.xy);
    const float distance_from_center = length(delta);
    const float angle = atan2(delta.y, delta.x);
    const float phase = frac(effect_params1.y + effect_params2.w * effect_params1.w);
    const float sweep = phase * 6.2831853;
    const float behind = atan2(sin(sweep - angle), cos(sweep - angle));
    const float beam_width = max(effect_params0.x, 0.01) * 0.75;
    const float beam = 1.0 - smoothstep(0.0, beam_width, abs(behind));
    const float tail = step(0.0, behind) * exp(-behind / max(beam_width * 4.5, 0.01));
    const float range_falloff = 1.0 - smoothstep(0.82, 1.25, distance_from_center);
    const float range_ring = pow(0.5 + 0.5 * cos(distance_from_center * 55.0), 18.0) * 0.18;
    const float ping_ring = exp(-abs(distance_from_center - phase * 1.15) * 34.0);
    const float glow = ((beam * 0.8 + tail * 0.48) * range_falloff +
        range_ring + ping_ring * 0.65) * effect_params0.w * effect_color.a;
    return float4(lerp(base.rgb, saturate(base.rgb + effect_color.rgb * glow),
        saturate(effect_params0.y)), base.a);
}
