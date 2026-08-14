Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // band height, channel shift, frequency, horizontal shift
    float4 effect_params1; // angle, progress, softness, speed
    float4 effect_params2; // direction.xy, seed, time
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

float Hash(float2 value)
{
    return frac(sin(dot(value, float2(127.1, 311.7))) * 43758.5453);
}

float4 main(VSOutput input) : SV_TARGET
{
    const float band_height = max(effect_params0.x, 1.0);
    const float band = floor(input.uv.y * target_size.y / band_height);
    const float time_cell = floor(effect_params2.w * max(effect_params1.w, 0.0));
    const float random_value = Hash(float2(band, time_cell) + effect_params2.z);
    const float active = smoothstep(1.0 - saturate(effect_params0.z),
        1.0001 - saturate(effect_params0.z), random_value);
    const float signed_shift = (random_value * 2.0 - 1.0) * effect_params0.w * active;
    const float2 shifted_uv = input.uv + float2(signed_shift * target_size.z, 0.0);
    const float channel_shift = effect_params0.y * target_size.z * active;
    float4 result = source_texture.Sample(source_sampler, shifted_uv);
    result.r = source_texture.Sample(source_sampler,
        shifted_uv + float2(channel_shift, 0.0)).r;
    result.b = source_texture.Sample(source_sampler,
        shifted_uv - float2(channel_shift, 0.0)).b;
    return result;
}
