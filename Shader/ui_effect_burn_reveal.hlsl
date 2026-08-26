#include "ui_effect_dynamic_common.hlsli"

float4 main(VSOutput input) : SV_TARGET
{
    const float scale = max(effect_params0.x, 2.0);
    const float time = effect_params2.w * effect_params1.w;
    const float n = fbm21(input.uv * target_size.xy / scale +
        effect_params2.z * 17.0 + float2(time * 0.17, -time * 0.11));
    const float width = max(effect_params1.z, 0.002) *
        lerp(0.7, 1.8, saturate(effect_params0.w * 0.5));
    const float reveal = smoothstep(n - width, n + width, effect_params1.y);
    const float distance_to_edge = abs(effect_params1.y - n);
    const float edge = 1.0 - smoothstep(width * 0.18, width * 1.25, distance_to_edge);
    const float hot_core = 1.0 - smoothstep(0.0, width * 0.42, distance_to_edge);
    const float4 base = source_texture.Sample(source_sampler, input.uv);
    const float3 burn = lerp(effect_color.rgb * effect_color.a,
        effect_color_2.rgb * effect_color_2.a, hot_core);
    float4 revealed = base;
    revealed.rgb = lerp(base.rgb, saturate(base.rgb + burn * effect_params0.w), edge);
    revealed.a = base.a * reveal;
    revealed.rgb *= reveal;
    return lerp(base, revealed, saturate(effect_params0.y));
}
