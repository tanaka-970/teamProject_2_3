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
    const float threshold = saturate(effect_params0.z);
    const float2 texel = target_size.zw * max(radius, 1.0);

    float4 center = source_texture.Sample(source_sampler, input.uv);
    float4 glow = center;
    glow += source_texture.Sample(source_sampler, input.uv + float2(texel.x, 0.0));
    glow += source_texture.Sample(source_sampler, input.uv - float2(texel.x, 0.0));
    glow += source_texture.Sample(source_sampler, input.uv + float2(0.0, texel.y));
    glow += source_texture.Sample(source_sampler, input.uv - float2(0.0, texel.y));
    glow *= 0.2;

    const float luminance = dot(glow.rgb, float3(0.2126, 0.7152, 0.0722));
    const float mask = saturate((luminance - threshold) * 8.0) * intensity;
    center.rgb += effect_color.rgb * glow.a * mask;
    center.a = saturate(center.a + glow.a * mask * effect_color.a);
    return center;
}
