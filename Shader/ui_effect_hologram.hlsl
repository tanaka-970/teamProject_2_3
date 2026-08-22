#include "ui_effect_dynamic_common.hlsli"

float4 main(VSOutput input) : SV_TARGET
{
    const float4 base = source_texture.Sample(source_sampler, input.uv);
    const float spacing = max(effect_params0.x, 2.0);
    const float time = effect_params2.w * effect_params1.w;
    const float scan_phase = input.uv.y * target_size.y / spacing + time * 2.0;
    const float scan = pow(0.5 + 0.5 * sin(scan_phase * 6.2831853), 5.0);
    const float signal_noise = fbm21(input.uv * target_size.xy /
        max(spacing * 1.7, 1.0) + effect_params2.z * 19.0 + time);
    const float dropout = smoothstep(1.0 - effect_params0.z * 0.55, 1.0,
        signal_noise) * effect_params0.w;
    const float moving_band = exp(-abs(frac(input.uv.y - time * 0.12) - 0.5) * 18.0);
    const float flicker = max(0.0, 1.0 + (signal_noise - 0.5) * effect_params0.z * 1.6 +
        sin(effect_params2.w * (9.0 + abs(effect_params1.w))) * effect_params1.z);
    const float2 direction = length(effect_params2.xy) > 0.0001
        ? normalize(effect_params2.xy) : float2(1.0, 0.0);
    const float2 chroma = direction * effect_params0.w * 2.5 * target_size.zw;
    const float red = sample_source_safe(input.uv + chroma).r;
    const float blue = sample_source_safe(input.uv - chroma).b;
    const float edge = abs(base.a - sample_source_safe(input.uv +
        float2(target_size.z, 0.0)).a) + abs(base.a - sample_source_safe(input.uv +
        float2(0.0, target_size.w)).a);
    const float3 rgb = float3(red, base.g, blue);
    const float glow_mask = (scan * 0.35 + moving_band * 0.45 +
        signal_noise * effect_params0.z * 0.5 + edge * 1.8) * effect_params0.w;
    const float3 hologram = saturate(rgb * (1.0 - dropout * 0.35) +
        effect_color.rgb * glow_mask * flicker * effect_color.a);
    return float4(lerp(base.rgb, hologram, saturate(effect_params0.y)), base.a);
}
