#include "ui_effect_dynamic_common.hlsli"

float3 foil_color(float t)
{
    t = frac(t);
    float3 result = effect_color.rgb;
    if (t < 0.25)
        result = lerp(effect_color.rgb, effect_color_2.rgb,
            smoothstep(0.0, 0.25, t));
    else if (t < 0.50)
        result = lerp(effect_color_2.rgb, effect_color_3.rgb,
            smoothstep(0.25, 0.50, t));
    else if (t < 0.75)
        result = lerp(effect_color_3.rgb, effect_color_4.rgb,
            smoothstep(0.50, 0.75, t));
    else
        result = lerp(effect_color_4.rgb, effect_color.rgb,
            smoothstep(0.75, 1.0, t));
    return result;
}

float4 main(VSOutput input) : SV_TARGET
{
    const float4 base = source_texture.Sample(source_sampler, input.uv);
    const float2 p = centered_uv(input.uv);
    const float scale = max(effect_params0.x, 0.25);
    const float noise = fbm21(p * (4.0 / scale) + effect_params2.z * 11.0);
    const float ripple = sin(length(p) * 26.0 / scale - noise * 5.0) * 0.10;
    const float phase = atan2(p.y, p.x) / 6.2831853 + luminance(base.rgb) * 0.42 +
        noise * 0.52 + ripple + effect_params1.y + effect_params2.w * effect_params1.w;
    const float edge = abs(base.a - sample_source_safe(input.uv +
        float2(target_size.z, 0.0)).a) + abs(base.a - sample_source_safe(input.uv +
        float2(0.0, target_size.w)).a);
    const float grazing = pow(saturate(1.0 - abs(noise * 2.0 - 1.0)), 2.0);
    const float alpha = (effect_color.a + effect_color_2.a + effect_color_3.a +
        effect_color_4.a) * 0.25;
    const float3 foil = foil_color(phase) * (0.18 + grazing * 0.42 + edge * 1.6) * alpha;
    return float4(lerp(base.rgb, saturate(base.rgb + foil * effect_params0.w),
        saturate(effect_params0.y)), base.a);
}
