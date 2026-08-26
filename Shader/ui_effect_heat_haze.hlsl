#include "ui_effect_dynamic_common.hlsli"

float4 main(VSOutput input) : SV_TARGET
{
    const float scale = max(effect_params0.x, 2.0);
    const float time = effect_params2.w * effect_params1.w;
    const float rise = effect_params0.z;
    const float2 flow_uv = input.uv * target_size.xy / scale +
        float2(effect_params2.z * 17.0 + sin(time * 0.7) * 0.2, -time * (0.45 + rise));
    const float n0 = fbm21(flow_uv);
    const float n1 = fbm21(flow_uv + float2(0.73, 2.17));
    const float n2 = fbm21(flow_uv * 1.83 + 11.3);
    const float2 offset = float2(n1 - n0, (n2 - 0.5) * (0.35 + rise * 0.3)) *
        effect_params0.w * target_size.zw;
    const float4 base = source_texture.Sample(source_sampler, input.uv);
    const float4 warped = source_texture.Sample(source_sampler, saturate(input.uv + offset));
    const float heat = saturate((n0 + n1) * 0.5);
    return float4(lerp(base.rgb, warped.rgb + float3(heat * 0.045, heat * 0.012, 0.0),
        saturate(effect_params0.y)), base.a);
}
