Texture2D source_texture : register(t0);
Texture2D mask_texture : register(t1);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // radius, intensity, threshold, operation
    float4 effect_params1;
    float4 effect_params2;
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
    float4 effect_params3; // z = luma mode
    float4 brush_pattern_settings;
    float4 brush_pattern_weights[4];
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float matte_value(float4 sample_value)
{
    return effect_params3.z > 0.5f
        ? dot(sample_value.rgb, float3(0.2126f, 0.7152f, 0.0722f))
        : sample_value.a;
}

float4 main(VSOutput input) : SV_TARGET
{
    const float a = saturate(matte_value(source_texture.Sample(source_sampler, input.uv)));
    const float b = saturate(matte_value(mask_texture.Sample(source_sampler, input.uv)));
    const int operation = (int)round(effect_params0.w);
    float value = saturate(a + b);
    if (operation == 1) value = saturate(a - b);
    else if (operation == 2) value = min(a, b);
    return float4(value, value, value, value);
}
