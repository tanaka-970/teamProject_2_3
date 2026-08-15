Texture2D source_texture : register(t0);
Texture2D mask_texture : register(t1);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0;
    float4 effect_params1;
    float4 effect_params2;
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
    float4 effect_params3;
    float4 brush_pattern_settings;
    float4 brush_pattern_weights[4];
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 local_uv : TEXCOORD0;
    float2 center_uv : TEXCOORD1;
    float2 tangent : TEXCOORD2;
    float2 stroke_size : TEXCOORD3;
    nointerpolation uint pattern : TEXCOORD4;
};

float4 main(PSInput input) : SV_TARGET
{
    const uint pattern = min(input.pattern, 15u);
    const float2 atlas_cell = float2(pattern % 4u, pattern / 4u);
    const float mask = dot(mask_texture.Sample(source_sampler,
        (atlas_cell + input.local_uv) * 0.25).rgb,
        float3(0.2126, 0.7152, 0.0722));
    const float coverage = smoothstep(0.16, 0.70, mask);
    clip(coverage - 0.001);

    float4 pigment = 0.0;
    [unroll]
    for (int sample_index = 0; sample_index < 3; ++sample_index)
    {
        const float along = (sample_index - 1.0) * input.stroke_size.x * 0.16;
        pigment += source_texture.Sample(source_sampler,
            saturate(input.center_uv + input.tangent * along * target_size.zw));
    }
    pigment *= 1.0 / 3.0;
    const float steps = lerp(15.0, 8.0, saturate(effect_params1.x));
    pigment.rgb = floor(saturate(pigment.rgb) * steps + 0.5) / steps;
    return float4(pigment.rgb, coverage * saturate(effect_params0.y));
}
