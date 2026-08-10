Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // radius, intensity, threshold, grain_size
    float4 effect_params1; // angle, progress, softness, speed
    float4 effect_params2; // direction.xy, seed, time
    float4 target_size;    // width, height, 1 / width, 1 / height
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float Hash(float2 value)
{
    value = frac(value * float2(123.34, 456.21));
    value += dot(value, value + 45.32);
    return frac(value.x * value.y);
}

float4 main(VSOutput input) : SV_TARGET
{
    float4 color = source_texture.Sample(source_sampler, input.uv);
    const float grain = max(effect_params0.w, 1.0);
    const float2 cell = floor(input.uv * target_size.xy / grain);
    const float n = Hash(cell + effect_params2.z + effect_params1.w * effect_params2.w) * 2.0 - 1.0;
    color.rgb += n * effect_params0.y * effect_color.rgb;
    return saturate(color);
}
