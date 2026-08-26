Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // radius, intensity, threshold, amount
    float4 effect_params1; // angle, progress, softness, speed
    float4 effect_params2; // direction.xy, seed, time
    float4 target_size;    // width, height, 1 / width, 1 / height
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
    float4 effect_params3;
    float4 brush_pattern_settings;
    float4 brush_pattern_weights[4];
    float4 effect_region_params;
    float4 effect_region_settings;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float hash21(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float noise21(float2 p)
{
    const float2 cell = floor(p);
    const float2 local = frac(p);
    const float2 smooth_local = local * local * (3.0 - 2.0 * local);
    const float a = hash21(cell);
    const float b = hash21(cell + float2(1.0, 0.0));
    const float c = hash21(cell + float2(0.0, 1.0));
    const float d = hash21(cell + float2(1.0, 1.0));
    return lerp(lerp(a, b, smooth_local.x), lerp(c, d, smooth_local.x), smooth_local.y);
}

float luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float2 centered_uv(float2 uv)
{
    return (uv - 0.5) * float2(target_size.x / max(target_size.y, 1.0), 1.0);
}

float2 aspect_delta(float2 uv, float2 center)
{
    return (uv - center) * float2(target_size.x / max(target_size.y, 1.0), 1.0);
}

float2 from_aspect_delta(float2 delta, float2 center)
{
    return center + delta / float2(target_size.x / max(target_size.y, 1.0), 1.0);
}

float4 sample_source_safe(float2 uv)
{
    const float inside = step(0.0, uv.x) * step(uv.x, 1.0) *
        step(0.0, uv.y) * step(uv.y, 1.0);
    return source_texture.Sample(source_sampler, saturate(uv)) * inside;
}

float fbm21(float2 p)
{
    float value = 0.0;
    float amplitude = 0.5;
    [unroll]
    for (int octave = 0; octave < 4; ++octave)
    {
        value += noise21(p) * amplitude;
        p = mul(float2x2(0.80, -0.60, 0.60, 0.80), p) * 2.03 + 13.17;
        amplitude *= 0.5;
    }
    return value / 0.9375;
}

void voronoi21(float2 p, float seed, out float nearest_distance,
    out float second_distance, out float2 nearest_cell)
{
    const float2 base_cell = floor(p);
    const float2 local = frac(p);
    nearest_distance = 1e6;
    second_distance = 1e6;
    nearest_cell = base_cell;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float2 neighbor = float2((float)x, (float)y);
            const float2 cell = base_cell + neighbor;
            const float2 feature = float2(
                hash21(cell + float2(seed * 17.3, seed * 7.1)),
                hash21(cell + float2(31.7 + seed * 5.3, 11.9 - seed * 13.1)));
            const float distance_squared = dot(neighbor + feature - local,
                neighbor + feature - local);
            if (distance_squared < nearest_distance)
            {
                second_distance = nearest_distance;
                nearest_distance = distance_squared;
                nearest_cell = cell;
            }
            else if (distance_squared < second_distance)
            {
                second_distance = distance_squared;
            }
        }
    }
    nearest_distance = sqrt(nearest_distance);
    second_distance = sqrt(second_distance);
}
