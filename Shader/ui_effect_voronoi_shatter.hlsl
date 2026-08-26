#include "ui_effect_dynamic_common.hlsli"

float4 main(VSOutput input) : SV_TARGET
{
    const float cell_size = max(effect_params0.x, 4.0);
    const float2 grid = input.uv * target_size.xy / cell_size;
    float nearest_distance;
    float second_distance;
    float2 cell_id;
    voronoi21(grid, effect_params2.z, nearest_distance, second_distance, cell_id);
    const float progress = abs(effect_params1.w) > 0.00001
        ? frac(effect_params1.y + effect_params2.w * effect_params1.w)
        : saturate(effect_params1.y);
    const float2 random_direction = float2(hash21(cell_id + effect_params2.z * 23.0),
        hash21(cell_id + 31.7 + effect_params2.z * 11.0)) * 2.0 - 1.0;
    const float2 cell_center_uv = (cell_id + 0.5) * cell_size * target_size.zw;
    const float2 outward = normalize(cell_center_uv - 0.5 + float2(0.0001, 0.0001));
    const float2 direction = normalize(random_direction + outward * 0.65 +
        float2(0.0001, 0.0001));
    const float eased = progress * progress * (3.0 - 2.0 * progress);
    const float2 offset = direction * effect_params0.w * eased * target_size.zw;
    const float4 base = source_texture.Sample(source_sampler, input.uv);
    float4 shattered = sample_source_safe(input.uv - offset);
    const float border_distance = second_distance - nearest_distance;
    const float border = 1.0 - smoothstep(0.0,
        max(effect_params1.z, 0.002) * 0.75, border_distance);
    const float gap = border * smoothstep(0.02, 0.45, progress);
    shattered.rgb *= 1.0 - gap * 0.82;
    shattered.a *= 1.0 - gap;
    return lerp(base, shattered, saturate(effect_params0.y));
}
