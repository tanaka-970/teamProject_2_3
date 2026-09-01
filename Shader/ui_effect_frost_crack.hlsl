#include "ui_effect_dynamic_common.hlsli"

float4 main(VSOutput input) : SV_TARGET
{
    const float scale = max(effect_params0.x, 4.0);
    const float2 grid = input.uv * target_size.xy / scale;
    float nearest_distance;
    float second_distance;
    float2 cell_id;
    voronoi21(grid, effect_params2.z, nearest_distance, second_distance, cell_id);
    const float growth = abs(effect_params1.w) > 0.00001
        ? saturate(effect_params1.y + effect_params2.w * effect_params1.w * 0.08)
        : saturate(effect_params1.y);
    const float boundary = second_distance - nearest_distance;
    const float crack_width = max(effect_params1.z, 0.004) * 0.42;
    const float density = step(effect_params0.z,
        hash21(cell_id * 1.7 + effect_params2.z * 29.0));
    const float crack = (1.0 - smoothstep(0.0, crack_width, boundary)) * density;
    const float frost_noise = fbm21(grid * 0.42 + effect_params2.z * 13.0);
    const float growth_field = frost_noise * 0.72 +
        (1.0 - length(centered_uv(input.uv))) * 0.28;
    const float reveal = smoothstep(1.0 - growth - effect_params1.z,
        1.0 - growth + effect_params1.z, growth_field);
    const float crystal = reveal * (0.18 + crack * 0.95 +
        pow(saturate(frost_noise), 5.0) * 0.28);
    const float4 base = source_texture.Sample(source_sampler, input.uv);
    const float gray = luminance(base.rgb);
    const float3 chilled = lerp(base.rgb, gray.xxx, crystal * 0.38);
    const float3 frosted = saturate(chilled + effect_color.rgb * effect_color.a *
        crystal * effect_params0.w);
    return float4(lerp(base.rgb, frosted, saturate(effect_params0.y)), base.a);
}
