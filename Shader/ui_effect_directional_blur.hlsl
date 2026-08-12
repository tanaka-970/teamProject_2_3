Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // radius, intensity, threshold, distance
    float4 effect_params1; // angle, progress, softness, speed
    float4 effect_params2; // direction.xy, seed, time
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
static const int sample_count = 24;

float4 main(VSOutput input) : SV_TARGET
{
    const float distance_pixels = max(effect_params0.w, 0.0);
    const float angle = radians(effect_params1.x);
    const float2 direction = float2(cos(angle), sin(angle));
    float4 blurred = 0.0;
    float weight_sum = 0.0;
    [unroll]
    for (int sample_index = 0; sample_index < sample_count; ++sample_index)
    {
        const float t = sample_index / (sample_count - 1.0) - 0.5;
        const float weight = exp(-t * t * 8.0);
        const float2 offset = direction * (distance_pixels * t) * target_size.zw;
        blurred += source_texture.Sample(source_sampler, input.uv + offset) * weight;
        weight_sum += weight;
    }
    blurred /= max(weight_sum, 0.0001);
    const float4 source = source_texture.Sample(source_sampler, input.uv);
    return lerp(source, blurred, saturate(effect_params0.y));
}
