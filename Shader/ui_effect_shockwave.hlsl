Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // ring width, distortion amount, wave count, unused
    float4 effect_params1; // angle, progress, softness, speed
    float4 effect_params2; // center.xy, seed, time
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
    float4 effect_params3;
    float4 brush_pattern_settings;
    float4 brush_pattern_weights[4];
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

float4 main(VSOutput input) : SV_TARGET
{
    const float4 source = source_texture.Sample(source_sampler, input.uv);
    const float2 center = clamp(effect_params2.xy, 0.0, 1.0);
    const float2 delta_pixels = (input.uv - center) * target_size.xy;
    const float distance_pixels = length(delta_pixels);
    const float2 radial = distance_pixels > 0.0001
        ? delta_pixels / distance_pixels : float2(0.0, 0.0);
    const float2 center_pixels = center * target_size.xy;
    const float max_radius = max(max(
        length(center_pixels),
        length(float2(target_size.x - center_pixels.x, center_pixels.y))),
        max(length(float2(center_pixels.x, target_size.y - center_pixels.y)),
            length(target_size.xy - center_pixels)));
    const float wave_progress = effect_params1.w == 0.0
        ? saturate(effect_params1.y)
        : frac(effect_params1.y + effect_params2.w * effect_params1.w);
    const float wave_radius = wave_progress * max_radius * 1.15;
    const float ring_width = max(effect_params0.x, 1.0);
    const float ring_distance = abs(distance_pixels - wave_radius);
    const float ring_softness = max(effect_params1.z * ring_width, 1.0);
    const float ring = 1.0 - smoothstep(ring_width * 0.35,
        ring_width * 0.35 + ring_softness, ring_distance);
    const float wave_count = max(effect_params0.z, 0.0);
    const float phase = (distance_pixels - wave_radius) / ring_width *
        6.28318530718 * max(wave_count, 1.0);
    const float oscillation = wave_count < 0.5 ? 1.0 : cos(phase);
    const float crest = 0.5 + 0.5 * oscillation;
    const float application = max(effect_params0.y, 0.0);
    const float displacement = effect_params0.w * ring * oscillation * application;
    const float2 sample_uv = saturate(input.uv - radial * displacement * target_size.zw);
    const float4 warped = source_texture.Sample(source_sampler, sample_uv);
    const float mix_amount = ring * application;
    const float glow = ring * (0.35 + 0.65 * crest) *
        application * effect_color.a;

    float4 result = lerp(source, warped, saturate(mix_amount));
    result.rgb += effect_color.rgb * glow;
    result.a = max(source.a, warped.a * saturate(mix_amount));
    return result;
}
