Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // radius, intensity, threshold, strength
    float4 effect_params1;
    float4 effect_params2; // center.xy, seed, time
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
static const int sample_count = 20;

float4 main(VSOutput input) : SV_TARGET
{
    const bool center_valid = effect_params2.x >= 0.0 && effect_params2.x <= 1.0 &&
        effect_params2.y >= 0.0 && effect_params2.y <= 1.0;
    const float2 center = center_valid ? effect_params2.xy : float2(0.5, 0.5);
    const float2 pixel_delta = (input.uv - center) * target_size.xy;
    const float pixel_distance = length(pixel_delta);
    const float2 ray = pixel_distance > 0.0001 ? pixel_delta / pixel_distance : float2(0.0, 0.0);
    const float distance_scale = saturate(pixel_distance / max(target_size.x, target_size.y));
    float4 blurred = 0.0;
    float weight_sum = 0.0;
    [unroll]
    for (int sample_index = 0; sample_index < sample_count; ++sample_index)
    {
        const float t = sample_index / (sample_count - 1.0) - 0.5;
        const float weight = exp(-t * t * 8.0);
        const float2 offset = ray * effect_params0.w * distance_scale * t * target_size.zw;
        blurred += source_texture.Sample(source_sampler, input.uv + offset) * weight;
        weight_sum += weight;
    }
    blurred /= max(weight_sum, 0.0001);
    const float4 source = source_texture.Sample(source_sampler, input.uv);
    return lerp(source, blurred, saturate(effect_params0.y));
}
