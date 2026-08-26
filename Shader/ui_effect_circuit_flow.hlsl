#include "ui_effect_dynamic_common.hlsli"

float4 main(VSOutput input) : SV_TARGET
{
    const float cell_size = max(effect_params0.x, 4.0);
    const float2 grid_uv = input.uv * target_size.xy / cell_size;
    const float2 local = frac(grid_uv) - 0.5;
    const float2 cell_id = floor(grid_uv);
    const float horizontal_gate = step(0.28, hash21(cell_id + effect_params2.z * 13.0));
    const float vertical_gate = step(0.28, hash21(cell_id.yx + 37.1 + effect_params2.z * 7.0));
    const float thickness = lerp(0.018, 0.16, saturate(effect_params0.z / 0.49));
    const float horizontal = (1.0 - smoothstep(thickness, thickness + 0.018,
        abs(local.y))) * horizontal_gate;
    const float vertical = (1.0 - smoothstep(thickness, thickness + 0.018,
        abs(local.x))) * vertical_gate;
    const float trace = saturate(horizontal + vertical);
    const float node = 1.0 - smoothstep(thickness * 1.3, thickness * 2.8, length(local));
    const float path_phase = (grid_uv.x * horizontal + grid_uv.y * vertical) * 2.4 +
        hash21(cell_id + 91.7) * 6.2831853 - effect_params2.w * effect_params1.w * 4.0;
    const float pulse = pow(0.5 + 0.5 * cos(path_phase), 18.0);
    const float signal = trace * (0.16 + pulse * 1.25) + node * 0.45 *
        step(0.62, hash21(cell_id + 17.0));
    const float4 base = source_texture.Sample(source_sampler, input.uv);
    return float4(lerp(base.rgb, saturate(base.rgb + effect_color.rgb *
        signal * effect_params0.w * effect_color.a), saturate(effect_params0.y)), base.a);
}
