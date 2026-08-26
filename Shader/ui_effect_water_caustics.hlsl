#include "ui_effect_dynamic_common.hlsli"

float4 main(VSOutput input) : SV_TARGET
{
    const float scale = max(effect_params0.x, 4.0);
    const float2 seed_offset = float2(effect_params2.z * 19.7,
        effect_params2.z * -13.1);
    const float2 p = input.uv * target_size.xy / scale + seed_offset;
    const float time = effect_params2.w * effect_params1.w;
    const float wave_a = sin(p.x * 3.7 + p.y * 1.8 + time * 1.35);
    const float wave_b = sin(p.y * 4.9 - p.x * 2.4 - time * 1.08);
    const float wave_c = sin((p.x + wave_b * 0.28) * 6.1 +
        (p.y + wave_a * 0.22) * 2.7 + time * 0.72);
    const float ridge = 1.0 - abs(wave_a * wave_b * 0.58 + wave_c * 0.42);
    const float caustic = pow(saturate(ridge), 5.0);
    const float2 offset = float2(wave_b + wave_c * 0.35, wave_a - wave_c * 0.25) *
        effect_params0.w * 0.45 * target_size.zw;
    const float4 base = source_texture.Sample(source_sampler, input.uv);
    const float4 warped = source_texture.Sample(source_sampler, saturate(input.uv + offset));
    return float4(lerp(base.rgb, saturate(warped.rgb + effect_color.rgb *
        caustic * effect_params0.w * effect_color.a), saturate(effect_params0.y)), base.a);
}
