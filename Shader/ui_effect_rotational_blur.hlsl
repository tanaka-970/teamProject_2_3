Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0;
    float4 effect_params1; // angular amount, progress, softness, speed
    float4 effect_params2; // center.xy, seed, time
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
static const int sample_count = 20;

float2 Rotate(float2 value, float angle)
{
    float sine;
    float cosine;
    sincos(angle, sine, cosine);
    return float2(value.x * cosine - value.y * sine,
        value.x * sine + value.y * cosine);
}

float4 main(VSOutput input) : SV_TARGET
{
    const float2 center = effect_params2.xy;
    const float2 aspect = float2(target_size.x / max(target_size.y, 1.0), 1.0);
    const float2 centered = (input.uv - center) * aspect;
    const float angle_amount = radians(effect_params1.x);
    float4 blurred = 0.0;
    float weight_sum = 0.0;
    [unroll]
    for (int sample_index = 0; sample_index < sample_count; ++sample_index)
    {
        const float t = sample_index / (sample_count - 1.0) - 0.5;
        const float weight = exp(-t * t * 8.0);
        const float2 sample_uv = center + Rotate(centered, angle_amount * t) / aspect;
        blurred += source_texture.Sample(source_sampler, sample_uv) * weight;
        weight_sum += weight;
    }
    blurred /= max(weight_sum, 0.0001);
    const float4 source = source_texture.Sample(source_sampler, input.uv);
    return lerp(source, blurred, saturate(effect_params0.y));
}
