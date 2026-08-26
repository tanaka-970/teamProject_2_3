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
    float4 effect_params3; // x = tile mode
    float4 brush_pattern_settings;
    float4 brush_pattern_weights[4];
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

static const float pi = 3.14159265359;
static const float two_pi = 6.28318530718;

float2 safe_center()
{
    const bool valid = effect_params2.x >= 0.0 && effect_params2.x <= 1.0 &&
        effect_params2.y >= 0.0 && effect_params2.y <= 1.0;
    return valid ? effect_params2.xy : float2(0.5, 0.5);
}

float4 main(VSOutput input) : SV_TARGET
{
    const float2 center = safe_center();
    const float2 aspect = float2(target_size.x / max(target_size.y, 1.0), 1.0);
    const float2 centered = (input.uv - center) * aspect;
    const float radial_distance = length(centered);
    const float source_angle = atan2(centered.y, centered.x) + pi +
        radians(effect_params1.x);
    const float segment_count = max(round(effect_params0.x), 2.0);
    const float segment_angle = two_pi / segment_count;
    float wrapped_angle = fmod(source_angle, segment_angle);
    if (wrapped_angle < 0.0) wrapped_angle += segment_angle;
    const float local_angle = effect_params3.x < 0.5
        ? min(wrapped_angle, segment_angle - wrapped_angle)
        : wrapped_angle - segment_angle * 0.5;

    const float scale = max(effect_params0.w, 0.01);
    const float2 sample_centered = float2(cos(local_angle), sin(local_angle)) *
        radial_distance / scale;
    float2 sample_uv = center + sample_centered / aspect;
    // Keep scaled/offset centers useful instead of stretching the last border
    // texel over an entire sector.
    sample_uv = 1.0 - abs(frac(sample_uv * 0.5) * 2.0 - 1.0);
    const float4 source = source_texture.Sample(source_sampler, input.uv);
    const float4 tiled = source_texture.Sample(source_sampler, sample_uv);
    return lerp(source, tiled, saturate(effect_params0.y));
}
