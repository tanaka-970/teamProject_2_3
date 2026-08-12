Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // thickness, opacity, threshold, amount
    float4 effect_params1;
    float4 effect_params2;
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
static const int sample_count = 24;
static const float golden_angle = 2.39996323;

float4 main(VSOutput input) : SV_TARGET
{
    const float4 source = source_texture.Sample(source_sampler, input.uv);
    const float radius = max(effect_params0.x, 0.0);
    float surrounding_alpha = 0.0;
    [unroll]
    for (int sample_index = 0; sample_index < sample_count; ++sample_index)
    {
        const float sample_radius = radius * sqrt((sample_index + 0.5) / sample_count);
        const float sample_angle = sample_index * golden_angle;
        const float2 offset = float2(cos(sample_angle), sin(sample_angle)) *
            sample_radius * target_size.zw;
        surrounding_alpha = max(surrounding_alpha,
            source_texture.Sample(source_sampler, input.uv + offset).a);
    }
    const float outline_alpha = saturate(surrounding_alpha - source.a) *
        max(effect_params0.y, 0.0) * effect_color.a;
    float4 result = effect_color;
    result.a = outline_alpha;
    result.rgb = lerp(result.rgb, source.rgb, source.a);
    result.a = max(result.a, source.a);
    return result;
}
