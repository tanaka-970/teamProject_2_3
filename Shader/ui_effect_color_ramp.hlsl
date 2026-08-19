Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0;
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
    const float luminance = saturate(dot(source.rgb,
        float3(0.2126, 0.7152, 0.0722)));
    const float stop2 = max(saturate(effect_color_stops.x), 0.0001);
    const float stop3 = max(saturate(effect_color_stops.y), stop2 + 0.0001);
    const float stop4 = max(saturate(effect_color_stops.z), stop3 + 0.0001);
    float4 ramp = lerp(effect_color, effect_color_2,
        smoothstep(0.0, stop2, luminance));
    ramp = lerp(ramp, effect_color_3,
        smoothstep(stop2, stop3, luminance));
    ramp = lerp(ramp, effect_color_4,
        smoothstep(stop3, stop4, luminance));
    ramp.a *= source.a;
    return lerp(source, ramp, saturate(effect_params0.y));
}
