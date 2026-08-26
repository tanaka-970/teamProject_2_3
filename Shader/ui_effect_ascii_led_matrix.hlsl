Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // radius, intensity, threshold, amount
    float4 effect_params1;
    float4 effect_params2;
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

static const uint glyph_rows[70] = {
    0, 0, 0, 0, 0, 0, 0,                         // space
    0, 0, 0, 0, 0, 4, 4,                         // .
    0, 4, 4, 0, 4, 4, 0,                         // :
    0, 0, 0, 31, 0, 0, 0,                        // -
    0, 0, 31, 0, 31, 0, 0,                       // =
    0, 4, 4, 31, 4, 4, 0,                        // +
    0, 21, 14, 31, 14, 21, 0,                    // *
    10, 31, 10, 10, 31, 10, 0,                   // #
    25, 26, 4, 8, 22, 6, 0,                      // %
    14, 17, 23, 21, 23, 16, 14                   // @
};

float cell_shape(float2 local, float brightness, int mode)
{
    const float size = 0.06 + brightness * 0.43 * saturate(effect_params0.w);
    const float distance_to_dot = mode == 1 ? length(local) : max(abs(local.x), abs(local.y));
    const float aa = max(fwidth(distance_to_dot), 0.002) +
        saturate(effect_params1.z) * 0.035;
    return 1.0 - smoothstep(size - aa, size + aa, distance_to_dot);
}

float4 main(VSOutput input) : SV_TARGET
{
    const float4 source = source_texture.Sample(source_sampler, input.uv);
    const float threshold = saturate(effect_params0.z);
    const float cell_size = max(effect_params0.x, 1.0);
    const float2 cell_uv = input.uv * target_size.xy / cell_size;
    const float2 local = frac(cell_uv) - 0.5;
    const float2 cell_center_pixels = (floor(cell_uv) + 0.5) * cell_size;
    const float2 cell_center_uv = saturate(cell_center_pixels * target_size.zw);
    const float4 cell_source = source_texture.SampleLevel(source_sampler, cell_center_uv, 0.0);
    const float luminance = dot(cell_source.rgb, float3(0.2126, 0.7152, 0.0722));
    const float brightness = saturate((luminance - threshold) /
        max(1.0 - threshold, 0.001));
    const int mode = clamp((int)round(effect_params3.x), 0, 2);
    float coverage = 0.0;
    if (mode == 0)
    {
        // A real 5x7 density glyph selected once per cell. Keep the glyph's
        // pixels square inside a square cell instead of stretching it wide.
        const float2 glyph_uv = float2((local.x + 0.5 - 0.5) * 1.4 + 0.5,
            local.y + 0.5);
        if (all(glyph_uv >= 0.0) && all(glyph_uv < 1.0))
        {
            const int column = clamp((int)floor(glyph_uv.x * 5.0), 0, 4);
            const int row = clamp((int)floor(glyph_uv.y * 7.0), 0, 6);
            const int glyph = clamp((int)floor(brightness * 9.999), 0, 9);
            const uint row_bits = glyph_rows[glyph * 7 + row];
            const bool active = (row_bits & (1u << (4 - column))) != 0u;
            const float2 pixel_local = frac(glyph_uv * float2(5.0, 7.0)) - 0.5;
            const float distance_to_pixel = max(abs(pixel_local.x), abs(pixel_local.y));
            const float half_size = 0.18 + 0.30 * saturate(effect_params0.w);
            const float aa = max(fwidth(distance_to_pixel), 0.01) +
                saturate(effect_params1.z) * 0.04;
            coverage = active ? 1.0 - smoothstep(half_size - aa,
                half_size + aa, distance_to_pixel) : 0.0;
        }
    }
    else
    {
        coverage = cell_shape(local, brightness, mode);
    }
    const float3 converted = effect_color.rgb * coverage *
        lerp(0.35, 1.0, brightness) * effect_color.a;

    float4 result = source;
    result.rgb = lerp(source.rgb, converted, saturate(effect_params0.y));
    return result;
}
