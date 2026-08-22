#include "ui_effect_dynamic_common.hlsli"

float4 main(VSOutput input) : SV_TARGET
{
    const float2 p = aspect_delta(input.uv, effect_params2.xy);
    const float distance_from_center = length(p);
    const float radius = max(effect_params0.x, 0.01);
    const float falloff = 1.0 - smoothstep(radius, radius + max(effect_params1.z, 0.02),
        distance_from_center);
    const float base_angle = atan2(p.y, p.x);
    const float spiral = (effect_params1.x * 0.0174532925 +
        effect_params0.w * 5.5 + effect_params2.w * effect_params1.w) *
        falloff * falloff;
    const float angle = base_angle + spiral;
    const float convergence = saturate(effect_params0.z) * falloff;
    const float warped_radius = distance_from_center * (1.0 - convergence * 0.72);
    const float2 warped = from_aspect_delta(float2(cos(angle), sin(angle)) *
        warped_radius, effect_params2.xy);
    const float4 base = source_texture.Sample(source_sampler, input.uv);
    float4 vortex = sample_source_safe(warped);
    const float ring = exp(-abs(distance_from_center - radius * 0.82) *
        (24.0 / max(radius, 0.1)));
    const float core = exp(-distance_from_center * 18.0 / max(radius, 0.1));
    vortex.rgb = saturate(vortex.rgb + effect_color.rgb * effect_color.a *
        (ring * (0.3 + abs(effect_params0.w)) + core * 0.35));
    return lerp(base, vortex, saturate(effect_params0.y * falloff));
}
