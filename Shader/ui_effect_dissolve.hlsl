Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // radius, intensity, edge_width, amount
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
    value = frac(value * float2(127.1, 311.7));
    value += dot(value, value + 19.19);
    return frac(value.x * value.y);
}

float4 main(VSOutput input) : SV_TARGET
{
    float4 color = source_texture.Sample(source_sampler, input.uv);
    const float noise = Hash(floor(input.uv * target_size.xy * 0.25) + effect_params2.z);
    const float progress = saturate(effect_params1.y);
    const float edge = max(effect_params0.z, 0.0001);
    const float keep = smoothstep(progress - edge, progress + edge, noise);
    const float border = 1.0 - abs(keep * 2.0 - 1.0);
    color.rgb = lerp(color.rgb, effect_color.rgb, border * effect_color.a);
    color.a *= keep;
    return color;
}
