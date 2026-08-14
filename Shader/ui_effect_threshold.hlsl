Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // radius, intensity, threshold, amount
    float4 effect_params1; // angle, progress, softness, speed
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
    const float luminance = dot(source.rgb, float3(0.2126, 0.7152, 0.0722));
    const float softness = max(effect_params1.z, fwidth(luminance));
    const float white = smoothstep(effect_params0.z - softness,
        effect_params0.z + softness, luminance);
    const float4 binary = float4(effect_color.rgb * white, source.a);
    return lerp(source, binary, saturate(effect_params0.y));
}
