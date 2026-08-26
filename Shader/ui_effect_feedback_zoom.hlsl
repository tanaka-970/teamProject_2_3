Texture2D source_texture : register(t0);
Texture2D history_texture : register(t1);
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
    float4 effect_params3; // y = history is valid
    float4 brush_pattern_settings;
    float4 brush_pattern_weights[4];
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

float2 safe_center()
{
    const bool valid = effect_params2.x >= 0.0 && effect_params2.x <= 1.0 &&
        effect_params2.y >= 0.0 && effect_params2.y <= 1.0;
    return valid ? effect_params2.xy : float2(0.5, 0.5);
}

float2 rotate_pixels(float2 value, float angle)
{
    float sine;
    float cosine;
    sincos(angle, sine, cosine);
    return float2(value.x * cosine - value.y * sine,
        value.x * sine + value.y * cosine);
}

float4 history_at(float2 uv)
{
    if (any(uv < 0.0) || any(uv > 1.0)) return 0.0;
    return history_texture.Sample(source_sampler, uv);
}

float4 main(VSOutput input) : SV_TARGET
{
    const float4 current = source_texture.Sample(source_sampler, input.uv);
    if (effect_params3.y < 0.5) return current;

    const float2 center = safe_center();
    const float2 centered_pixels = (input.uv - center) * target_size.xy;
    const float zoom = max(1.0 + effect_params0.w, 0.1);
    // Positive values are documented as zoom-in, so the history lookup moves
    // toward the center (division), not away from it.
    const float2 rotated_pixels = rotate_pixels(centered_pixels / zoom,
        -radians(effect_params1.x));
    const float2 history_uv = center + rotated_pixels * target_size.zw;
    const float2 spread = target_size.zw * max(effect_params0.x, 0.0);
    float4 previous = history_at(history_uv) * 4.0;
    previous += history_at(history_uv + float2(spread.x, 0.0));
    previous += history_at(history_uv - float2(spread.x, 0.0));
    previous += history_at(history_uv + float2(0.0, spread.y));
    previous += history_at(history_uv - float2(0.0, spread.y));
    previous += history_at(history_uv + spread);
    previous += history_at(history_uv - spread);
    previous += history_at(history_uv + float2(spread.x, -spread.y));
    previous += history_at(history_uv + float2(-spread.x, spread.y));
    previous *= 1.0 / 12.0;

    const float distance_from_center = length(centered_pixels) /
        max(length(target_size.xy) * 0.5, 1.0);
    const float center_falloff = lerp(1.0, 1.0 - saturate(distance_from_center),
        saturate(effect_params1.z));
    const float feedback = saturate(effect_params0.y) * center_falloff;
    float4 result = lerp(current, previous, feedback);
    result.a = max(current.a, previous.a * feedback);
    return result;
}
