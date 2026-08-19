Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0;
    float4 effect_params1; // rotation, transform amount, softness, speed
    float4 effect_params2; // center.xy, seed, time
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
static const float inverse_two_pi = 0.15915494309;

float2 SafeCenter()
{
    const bool valid = effect_params2.x >= 0.0 && effect_params2.x <= 1.0 &&
        effect_params2.y >= 0.0 && effect_params2.y <= 1.0;
    return valid ? effect_params2.xy : float2(0.5, 0.5);
}

float4 main(VSOutput input) : SV_TARGET
{
    const float2 center = SafeCenter();
    const float2 aspect = float2(target_size.x / max(target_size.y, 1.0), 1.0);
    const float2 centered = (input.uv - center) * aspect;
    const float angle_u = frac(atan2(centered.y, centered.x) * inverse_two_pi +
        0.5 + effect_params1.x / 360.0);
    const float radius_v = saturate(length(centered) * 2.0);
    const float2 polar_uv = float2(angle_u, radius_v);
    const float4 source = source_texture.Sample(source_sampler, input.uv);
    const float4 polar = source_texture.Sample(source_sampler, polar_uv);
    return lerp(source, polar, saturate(effect_params1.y));
}
