Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // jitter, noise, color shift, bleed
    float4 effect_params1; // angle, progress, noise amount, speed
    float4 effect_params2; // direction.xy, seed, time
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
static const int bleed_sample_count = 8;

float Hash(float2 value)
{
    return frac(sin(dot(value, float2(127.1, 311.7))) * 43758.5453);
}

float4 main(VSOutput input) : SV_TARGET
{
    const float time = effect_params2.w * max(effect_params1.w, 0.0) + effect_params2.z;
    const float row = floor(input.uv.y * target_size.y * 0.25);
    const float row_noise = Hash(float2(row, floor(time * 8.0)));
    const float jitter = (row_noise * 2.0 - 1.0) * effect_params0.x * target_size.z;
    const float2 base_uv = input.uv + float2(jitter, 0.0);
    float4 bled = 0.0;
    float weight_sum = 0.0;
    [unroll]
    for (int sample_index = 0; sample_index < bleed_sample_count; ++sample_index)
    {
        const float t = sample_index / (bleed_sample_count - 1.0);
        const float weight = 1.0 - t * 0.75;
        const float2 offset = float2(-effect_params0.w * t * target_size.z, 0.0);
        bled += source_texture.Sample(source_sampler, base_uv + offset) * weight;
        weight_sum += weight;
    }
    bled /= max(weight_sum, 0.0001);
    const float channel_offset = effect_params0.z * target_size.z;
    bled.r = source_texture.Sample(source_sampler,
        base_uv + float2(channel_offset, 0.0)).r;
    bled.b = source_texture.Sample(source_sampler,
        base_uv - float2(channel_offset, 0.0)).b;
    const float noise = Hash(input.uv * target_size.xy + time) * 2.0 - 1.0;
    bled.rgb += noise * saturate(effect_params1.z);
    // Scene Effect の RT は HDR なので 1.0 で丸めず、負値だけ止める。
    return float4(max(bled.rgb, 0.0f), saturate(bled.a));
}
