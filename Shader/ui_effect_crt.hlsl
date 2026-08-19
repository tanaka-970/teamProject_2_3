Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // scan spacing, scan darkness, vignette, amount
    float4 effect_params1; // angle, curvature, edge softness, speed
    float4 effect_params2;
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
static const float two_pi = 6.28318530718;

float4 main(VSOutput input) : SV_TARGET
{
    const float2 centered = input.uv * 2.0 - 1.0;
    const float radius_squared = dot(centered, centered);
    const float curvature = max(effect_params1.y, 0.0);
    const float2 curved_uv = 0.5 + centered * (1.0 + curvature * radius_squared) * 0.5;
    float4 crt = source_texture.Sample(source_sampler, curved_uv);
    const float spacing = max(effect_params0.x, 1.0);
    const float scan_wave = 0.5 + 0.5 * cos(curved_uv.y * target_size.y /
        spacing * two_pi);
    const float scanline = smoothstep(0.45, 0.75, scan_wave);
    crt.rgb *= 1.0 - (1.0 - scanline) * saturate(effect_params0.y);
    const float vignette = smoothstep(0.35,
        0.35 + max(effect_params1.z, 0.0001), radius_squared) *
        saturate(effect_params0.z);
    crt.rgb *= 1.0 - vignette;
    const float2 outside_distance = max(abs(curved_uv - 0.5) - 0.5, 0.0);
    const float inside_mask = 1.0 - smoothstep(0.0, 0.005,
        max(outside_distance.x, outside_distance.y));
    crt *= inside_mask;
    return crt;
}
