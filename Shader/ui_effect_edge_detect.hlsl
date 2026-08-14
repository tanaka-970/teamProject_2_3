Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // thickness, strength, threshold, amount
    float4 effect_params1;
    float4 effect_params2;
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

float LuminanceAlpha(float2 uv)
{
    const float4 sample_color = source_texture.Sample(source_sampler, uv);
    return dot(sample_color.rgb, float3(0.2126, 0.7152, 0.0722)) * sample_color.a;
}

float4 main(VSOutput input) : SV_TARGET
{
    const float2 texel = target_size.zw * max(effect_params0.x, 0.25);
    const float tl = LuminanceAlpha(input.uv + texel * float2(-1.0, -1.0));
    const float tc = LuminanceAlpha(input.uv + texel * float2(0.0, -1.0));
    const float tr = LuminanceAlpha(input.uv + texel * float2(1.0, -1.0));
    const float ml = LuminanceAlpha(input.uv + texel * float2(-1.0, 0.0));
    const float mr = LuminanceAlpha(input.uv + texel * float2(1.0, 0.0));
    const float bl = LuminanceAlpha(input.uv + texel * float2(-1.0, 1.0));
    const float bc = LuminanceAlpha(input.uv + texel * float2(0.0, 1.0));
    const float br = LuminanceAlpha(input.uv + texel * float2(1.0, 1.0));
    const float gx = -tl - 2.0 * ml - bl + tr + 2.0 * mr + br;
    const float gy = -tl - 2.0 * tc - tr + bl + 2.0 * bc + br;
    const float edge = saturate(length(float2(gx, gy)) * effect_params0.y);
    return float4(effect_color.rgb * edge, effect_color.a * edge);
}
