Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // radius, intensity, threshold, amount
    float4 effect_params1; // angle, progress, softness, speed
    float4 effect_params2; // direction.xy, seed, time
    float4 target_size;    // width, height, 1 / width, 1 / height
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float Hash(float value)
{
    return frac(sin(value * 12.9898) * 43758.5453);
}

float4 main(VSOutput input) : SV_TARGET
{
    const float t = effect_params2.w * effect_params1.w + effect_params2.z;
    const float2 jitter = float2(Hash(t) - 0.5, Hash(t + 7.31) - 0.5) *
        effect_params0.w * effect_params0.y * target_size.zw;
    return source_texture.Sample(source_sampler, input.uv + jitter);
}
