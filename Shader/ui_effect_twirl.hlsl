Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // radius, intensity, threshold, amount
    float4 effect_params1; // angle, progress, softness, speed
    float4 effect_params2; // center.xy, seed, time
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

float2 SafeCenter()
{
    const bool valid = effect_params2.x >= 0.0 && effect_params2.x <= 1.0 &&
        effect_params2.y >= 0.0 && effect_params2.y <= 1.0;
    return valid ? effect_params2.xy : float2(0.5, 0.5);
}

float2 Rotate(float2 value, float angle)
{
    float sine;
    float cosine;
    sincos(angle, sine, cosine);
    return float2(value.x * cosine - value.y * sine,
        value.x * sine + value.y * cosine);
}

float4 main(VSOutput input) : SV_TARGET
{
    const float2 center = SafeCenter();
    const float2 aspect = float2(target_size.x / max(target_size.y, 1.0), 1.0);
    const float2 centered = (input.uv - center) * aspect;
    const float distance_from_center = length(centered);
    const float radius = max(effect_params0.x, 0.0001);
    const float falloff = 1.0 - smoothstep(0.0, radius, distance_from_center);
    const float2 twirled_uv = center + Rotate(centered,
        radians(effect_params1.x) * falloff) / aspect;
    const float4 source = source_texture.Sample(source_sampler, input.uv);
    const float4 twirled = source_texture.Sample(source_sampler, twirled_uv);
    return lerp(source, twirled, saturate(effect_params0.y));
}
