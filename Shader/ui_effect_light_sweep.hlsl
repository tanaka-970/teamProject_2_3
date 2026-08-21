Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // band width, amount, threshold, unused
    float4 effect_params1; // angle, progress, softness, speed
    float4 effect_params2; // direction.xy, seed, time
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
    const float angle = radians(effect_params1.x);
    const float2 axis = normalize(float2(cos(angle), sin(angle)));
    const float2 pixel = (input.uv - 0.5) * target_size.xy;
    const float coordinate = dot(pixel, axis);
    const float extent = dot(0.5 * target_size.xy, abs(axis));
    const float width = max(effect_params0.x, 1.0);
    const float softness = max(effect_params1.z * width, 1.0);
    const float motion = abs(effect_params1.w) < 0.000001
        ? saturate(effect_params1.y)
        : frac(effect_params1.y + effect_params2.w * effect_params1.w);
    const float center = lerp(-extent - width, extent + width, motion);
    const float distance_to_band = abs(coordinate - center);
    const float band = 1.0 - smoothstep(width * 0.5,
        width * 0.5 + softness, distance_to_band);
    const float halo = 1.0 - smoothstep(width * 1.2,
        width * 1.2 + softness * 2.0, distance_to_band);
    const float luma = dot(source.rgb, float3(0.2126, 0.7152, 0.0722));
    const float threshold = saturate(effect_params0.z);
    const float luma_mask = threshold <= 0.0001
        ? 1.0 : (threshold >= 0.9999 ? step(threshold, luma)
            : smoothstep(threshold, min(threshold + 0.12, 1.0), luma));
    const float mask = source.a * luma_mask;
    const float strength = max(effect_params0.w, 0.0);
    const float main_light = band * mask * strength * effect_color.a;
    const float edge_light = max(halo - band, 0.0) * mask * strength * effect_color_2.a;

    float4 result = source;
    const float3 swept = source.rgb + effect_color.rgb * main_light +
        effect_color_2.rgb * edge_light;
    result.rgb = lerp(source.rgb, swept, saturate(effect_params0.y));
    return result;
}
