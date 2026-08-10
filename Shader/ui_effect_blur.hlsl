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
    const float radius = max(effect_params0.x, 0.0);
    const float intensity = max(effect_params0.y, 0.0);
    const float2 texel = target_size.zw * max(radius, 1.0);

    float4 color = source_texture.Sample(source_sampler, input.uv) * 0.227027;
    color += source_texture.Sample(source_sampler, input.uv + float2(texel.x, 0.0)) * 0.1945946;
    color += source_texture.Sample(source_sampler, input.uv - float2(texel.x, 0.0)) * 0.1945946;
    color += source_texture.Sample(source_sampler, input.uv + float2(0.0, texel.y)) * 0.1945946;
    color += source_texture.Sample(source_sampler, input.uv - float2(0.0, texel.y)) * 0.1945946;
    return lerp(source_texture.Sample(source_sampler, input.uv), color, saturate(intensity));
}
