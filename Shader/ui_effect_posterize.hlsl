Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // radius, intensity, threshold, levels
    float4 effect_params1;
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
    const float levels = max(floor(effect_params0.w + 0.5), 2.0);
    float4 posterized = source;
    posterized.rgb = floor(saturate(source.rgb) * (levels - 1.0) + 0.5) /
        (levels - 1.0);
    return lerp(source, posterized, saturate(effect_params0.y));
}
