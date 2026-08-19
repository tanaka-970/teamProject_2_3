Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // spacing, intensity, threshold, amount
    float4 effect_params1; // angle, progress, softness, speed
    float4 effect_params2; // direction.xy, seed, time
    float4 target_size;    // width, height, 1 / width, 1 / height
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float2 RotatePoint(float2 value, float angle)
{
    float sin_angle;
    float cos_angle;
    sincos(angle, sin_angle, cos_angle);
    return float2(
        value.x * cos_angle - value.y * sin_angle,
        value.x * sin_angle + value.y * cos_angle);
}

float PlateCoverage(float2 centered_pixel, float angle, int channel,
    float spacing, float softness)
{
    const float2 grid_pixel = RotatePoint(centered_pixel, -angle);
    const float2 cell_center = floor(grid_pixel / spacing + 0.5) * spacing;
    const float2 local = grid_pixel - cell_center;
    const float2 sample_pixel = RotatePoint(cell_center, angle) +
        target_size.xy * 0.5;
    const float2 sample_uv = saturate(sample_pixel * target_size.zw);
    const float4 cell_color = source_texture.Sample(source_sampler, sample_uv);
    const float darkness = 1.0 - saturate(cell_color[channel]);
    // sqrt makes dot area, rather than radius, proportional to ink coverage.
    const float dot_radius = spacing * 0.70710678 * sqrt(darkness);
    const float distance_to_center = length(local);
    const float antialias_width = max(fwidth(distance_to_center), 0.5);
    const float softness_width = max(softness, 0.0) * spacing * 0.5;
    const float edge_width = max(antialias_width, softness_width);
    float coverage = 1.0 - smoothstep(dot_radius - edge_width,
        dot_radius + edge_width, distance_to_center);
    coverage *= saturate(dot_radius / max(edge_width, 0.0001));
    return coverage;
}

float4 main(VSOutput input) : SV_TARGET
{
    const float spacing = max(effect_params0.x, 1.0);
    const float intensity = saturate(effect_params0.y);
    const float base_angle = radians(effect_params1.x);
    const float softness = effect_params1.z;
    const float2 centered_pixel = input.uv * target_size.xy -
        target_size.xy * 0.5;
    const float4 center = source_texture.Sample(source_sampler, input.uv);

    // Separate RGB screen angles prevent the three plates from stacking into
    // the same grid, which would produce muddy moire patterns.
    const float red_coverage = PlateCoverage(centered_pixel,
        base_angle + radians(15.0), 0, spacing, softness);
    const float green_coverage = PlateCoverage(centered_pixel,
        base_angle + radians(75.0), 1, spacing, softness);
    const float blue_coverage = PlateCoverage(centered_pixel,
        base_angle, 2, spacing, softness);

    const float3 dot_color = center.rgb * effect_color.rgb;
    float4 halftone = center;
    halftone.r = lerp(1.0, dot_color.r, red_coverage);
    halftone.g = lerp(1.0, dot_color.g, green_coverage);
    halftone.b = lerp(1.0, dot_color.b, blue_coverage);
    const float4 result = lerp(center, halftone, intensity);
    return result;
}
