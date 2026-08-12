Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // length, intensity, threshold, streak count
    float4 effect_params1; // angle, progress, softness, speed
    float4 effect_params2;
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
static const int maximum_streak_count = 8;
static const int samples_per_streak = 12;
static const float two_pi = 6.28318530718;

float4 main(VSOutput input) : SV_TARGET
{
    const float streak_count = clamp(effect_params0.w, 1.0, 8.0);
    const float base_angle = radians(effect_params1.x);
    const float threshold = saturate(effect_params0.z);
    float4 streak_sum = 0.0;
    float sample_weight_sum = 0.0;
    [unroll]
    for (int streak_index = 0; streak_index < maximum_streak_count; ++streak_index)
    {
        const float active = 1.0 - smoothstep(streak_count - 0.25,
            streak_count + 0.25, streak_index + 0.5);
        const float angle = base_angle + two_pi * streak_index / streak_count;
        const float2 direction = float2(cos(angle), sin(angle));
        [unroll]
        for (int sample_index = 1; sample_index <= samples_per_streak; ++sample_index)
        {
            const float t = sample_index / (float)samples_per_streak;
            const float falloff = (1.0 - t) * active;
            const float2 offset = direction * (effect_params0.x * t) * target_size.zw;
            const float4 positive = source_texture.Sample(source_sampler, input.uv + offset);
            const float4 negative = source_texture.Sample(source_sampler, input.uv - offset);
            const float positive_luma = dot(positive.rgb, float3(0.2126, 0.7152, 0.0722));
            const float negative_luma = dot(negative.rgb, float3(0.2126, 0.7152, 0.0722));
            const float positive_mask = smoothstep(threshold, threshold + 0.05, positive_luma);
            const float negative_mask = smoothstep(threshold, threshold + 0.05, negative_luma);
            streak_sum += (positive * positive_mask + negative * negative_mask) * falloff;
            sample_weight_sum += 2.0 * falloff;
        }
    }
    const float4 source = source_texture.Sample(source_sampler, input.uv);
    const float4 streaks = streak_sum / max(sample_weight_sum, 0.0001);
    float4 result = source;
    result.rgb += streaks.rgb * effect_color.rgb * max(effect_params0.y, 0.0);
    result.a = saturate(result.a + streaks.a * effect_color.a * max(effect_params0.y, 0.0));
    return result;
}
