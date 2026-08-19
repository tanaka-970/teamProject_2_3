Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // radius, intensity, threshold, amount
    float4 effect_params1; // temperature, tint, softness, speed
    float4 effect_params2;
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

float4 main(VSOutput input) : SV_TARGET
{
    const float4 source = source_texture.Sample(source_sampler, input.uv);
    const float temperature = clamp(effect_params1.x, -1.0, 1.0);
    const float tint = clamp(effect_params1.y, -1.0, 1.0);
    float4 adjusted = source;
    adjusted.rgb += float3(temperature * 0.18 + tint * 0.06,
        -tint * 0.12, -temperature * 0.18 + tint * 0.06);
    adjusted.rgb = saturate(adjusted.rgb);
    return lerp(source, adjusted, saturate(effect_params0.y));
}
