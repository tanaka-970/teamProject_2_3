Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // radius, intensity, threshold, distortion
    float4 effect_params1;
    float4 effect_params2; // center.xy, seed, time
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

float4 main(VSOutput input) : SV_TARGET
{
    const bool center_valid = effect_params2.x >= 0.0 && effect_params2.x <= 1.0 &&
        effect_params2.y >= 0.0 && effect_params2.y <= 1.0;
    const float2 center = center_valid ? effect_params2.xy : float2(0.5, 0.5);
    const float2 aspect = float2(target_size.x / max(target_size.y, 1.0), 1.0);
    const float2 centered = (input.uv - center) * aspect;
    const float radius_squared = dot(centered, centered);
    const float scale = 1.0 + effect_params0.w * radius_squared;
    const float2 distorted_uv = center + centered * scale / aspect;
    const float4 source = source_texture.Sample(source_sampler, input.uv);
    const float4 distorted = source_texture.Sample(source_sampler, distorted_uv);
    return lerp(source, distorted, saturate(effect_params0.y));
}
