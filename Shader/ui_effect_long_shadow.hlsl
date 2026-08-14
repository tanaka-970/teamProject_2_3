Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // radius, opacity, threshold, length
    float4 effect_params1; // angle, progress, softness, speed
    float4 effect_params2;
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
static const int sample_count = 32;

float4 main(VSOutput input) : SV_TARGET
{
    const float angle = radians(effect_params1.x);
    const float2 direction = float2(cos(angle), sin(angle));
    float shadow_alpha = 0.0;
    [unroll]
    for (int sample_index = 1; sample_index <= sample_count; ++sample_index)
    {
        const float t = sample_index / (float)sample_count;
        const float2 offset = direction * (effect_params0.w * t) * target_size.zw;
        const float alpha = source_texture.Sample(source_sampler, input.uv - offset).a;
        shadow_alpha = max(shadow_alpha, alpha * (1.0 - t * 0.35));
    }
    const float4 source = source_texture.Sample(source_sampler, input.uv);
    float4 result = effect_color;
    result.a = shadow_alpha * effect_color.a * max(effect_params0.y, 0.0);
    result.rgb = lerp(result.rgb, source.rgb, source.a);
    result.a = max(result.a, source.a);
    return result;
}
