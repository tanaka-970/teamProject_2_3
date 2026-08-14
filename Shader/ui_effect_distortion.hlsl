Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // radius, intensity, frequency, amount
    float4 effect_params1; // angle, progress, softness, speed
    float4 effect_params2; // direction.xy, seed, time
    float4 target_size;    // width, height, 1 / width, 1 / height
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(VSOutput input) : SV_TARGET
{
    const float frequency = max(effect_params0.z, 0.001);
    const float amount = effect_params0.w * effect_params0.y;
    const float time = effect_params2.w * effect_params1.w + effect_params2.z;
    float2 offset;
    offset.x = sin((input.uv.y + time) * frequency) * amount * target_size.z;
    offset.y = cos((input.uv.x + time) * frequency) * amount * target_size.w;
    return source_texture.Sample(source_sampler, input.uv + offset);
}
