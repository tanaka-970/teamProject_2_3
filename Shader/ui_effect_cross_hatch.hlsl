Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // spacing, intensity, threshold, level count
    float4 effect_params1; // angle, progress, softness, speed
    float4 effect_params2;
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
static const int direction_count = 4;

float HatchLine(float2 pixel, float angle, float spacing, float softness)
{
    const float projected = dot(pixel, float2(cos(angle), sin(angle)));
    const float distance_to_line = abs(frac(projected / spacing) - 0.5) * spacing;
    const float aa = max(fwidth(projected), 0.5);
    const float half_width = 0.75 + softness * spacing * 0.25;
    return 1.0 - smoothstep(half_width, half_width + aa, distance_to_line);
}

float4 main(VSOutput input) : SV_TARGET
{
    const float4 source = source_texture.Sample(source_sampler, input.uv);
    const float luminance = dot(source.rgb, float3(0.2126, 0.7152, 0.0722));
    const float darkness = 1.0 - saturate(luminance);
    const float spacing = max(effect_params0.x, 2.0);
    const float requested_levels = clamp(effect_params0.w, 1.0, 4.0);
    const float base_angle = radians(effect_params1.x);
    const float2 pixel = input.uv * target_size.xy;
    float ink = 0.0;
    [unroll]
    for (int direction_index = 0; direction_index < direction_count; ++direction_index)
    {
        const float level_enabled = 1.0 - smoothstep(requested_levels - 0.25,
            requested_levels + 0.25, direction_index + 0.5);
        const float darkness_threshold = (direction_index + 1.0) / (direction_count + 1.0);
        const float tone_enabled = smoothstep(darkness_threshold - 0.08,
            darkness_threshold + 0.08, darkness);
        const float angle = base_angle + direction_index * 0.78539816339;
        ink = max(ink, HatchLine(pixel, angle, spacing, effect_params1.z) *
            level_enabled * tone_enabled);
    }
    float4 hatched = source;
    hatched.rgb = lerp(source.rgb, effect_color.rgb, ink * effect_color.a);
    return lerp(source, hatched, saturate(effect_params0.y));
}
