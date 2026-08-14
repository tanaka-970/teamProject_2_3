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

float4 main(VSOutput input) : SV_TARGET
{
    const float2 center = float2(0.5, 0.5);
    const float2 radial = input.uv - center;
    const float2 offset = normalize(radial + 0.0001) *
        effect_params0.w * effect_params0.y * target_size.zw;

    float4 color;
    color.r = source_texture.Sample(source_sampler, input.uv + offset).r;
    color.g = source_texture.Sample(source_sampler, input.uv).g;
    color.b = source_texture.Sample(source_sampler, input.uv - offset).b;
    color.a = source_texture.Sample(source_sampler, input.uv).a;
    return color;
}
