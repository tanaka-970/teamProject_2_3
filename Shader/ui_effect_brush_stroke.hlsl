Texture2D source_texture : register(t0);
Texture2D mask_texture : register(t1);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // brush length, intensity, jitter, brush width
    float4 effect_params1; // angle, progress = stamp size, softness = variation, speed
    float4 effect_params2; // direction.xy, seed, time
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
    float4 effect_params3; // x = waveform, y = optional texture is bound
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
static const int tensor_neighborhood_width = 3;
static const int brush_sample_count = 24;
static const float golden_angle = 2.39996323;

float Luminance(float2 uv)
{
    return dot(source_texture.Sample(source_sampler, uv).rgb,
        float3(0.2126, 0.7152, 0.0722));
}

float Hash(float2 value)
{
    return frac(sin(dot(value, float2(127.1, 311.7))) * 43758.5453);
}

float4 main(VSOutput input) : SV_TARGET
{
    float jxx = 0.0;
    float jxy = 0.0;
    float jyy = 0.0;
    [unroll]
    for (int y = 0; y < tensor_neighborhood_width; ++y)
    {
        [unroll]
        for (int x = 0; x < tensor_neighborhood_width; ++x)
        {
            const float2 neighborhood = float2(x - 1, y - 1) * target_size.zw;
            const float gx = Luminance(input.uv + neighborhood + float2(target_size.z, 0.0)) -
                Luminance(input.uv + neighborhood - float2(target_size.z, 0.0));
            const float gy = Luminance(input.uv + neighborhood + float2(0.0, target_size.w)) -
                Luminance(input.uv + neighborhood - float2(0.0, target_size.w));
            jxx += gx * gx;
            jxy += gx * gy;
            jyy += gy * gy;
        }
    }

    // The dominant eigenvector is the strongest luminance change. Brush marks
    // use its perpendicular, so they follow structure instead of crossing it.
    const float gradient_angle = 0.5 * atan2(2.0 * jxy, jxx - jyy);
    const float2 jitter_cell = floor(input.uv * target_size.xy /
        max(effect_params0.x, 1.0));
    const float jitter = (Hash(jitter_cell + effect_params2.z) - 0.5) *
        effect_params0.z;
    const float tangent_angle = gradient_angle + 1.57079632679 + jitter;
    const float2 tangent = float2(cos(tangent_angle), sin(tangent_angle));
    const float2 normal = float2(-tangent.y, tangent.x);
    float4 brushed = 0.0;
    float weight_sum = 0.0;
    [unroll]
    for (int sample_index = 0; sample_index < brush_sample_count; ++sample_index)
    {
        const float radius_fraction = sqrt((sample_index + 0.5) / brush_sample_count);
        const float angle = sample_index * golden_angle;
        const float2 ellipse_offset = tangent * (cos(angle) * radius_fraction * effect_params0.x) +
            normal * (sin(angle) * radius_fraction * max(effect_params0.w, 0.5));
        const float weight = exp(-radius_fraction * radius_fraction * 2.0);
        brushed += source_texture.Sample(source_sampler,
            input.uv + ellipse_offset * target_size.zw) * weight;
        weight_sum += weight;
    }
    brushed /= max(weight_sum, 0.0001);
    const float4 source = source_texture.Sample(source_sampler, input.uv);
    float brush_amount = saturate(effect_params0.y);
    if (effect_params3.y > 0.5)
    {
        // 筆跡はセルごとに貼る。seed から作る角度と大きさの揺れは、同じ seed
        // なら同じになる。threshold の輪郭方向 jitter とは別の役割である。
        const float stamp_size = max(effect_params1.y, 1.0);
        const float2 stamp_grid = input.uv * target_size.xy / stamp_size;
        const float2 stamp_cell = floor(stamp_grid);
        const float variation = saturate(effect_params1.z);
        const float rotation = (Hash(stamp_cell + effect_params2.z) - 0.5) *
            6.28318530718 * variation;
        const float stamp_scale = 1.0 +
            (Hash(stamp_cell.yx + effect_params2.z + 17.0) - 0.5) *
            0.7 * variation;
        float sine;
        float cosine;
        sincos(rotation, sine, cosine);
        const float2 local = (frac(stamp_grid) - 0.5) / max(stamp_scale, 0.1);
        const float2 stamp_uv = frac(float2(
            local.x * cosine - local.y * sine,
            local.x * sine + local.y * cosine) + 0.5);
        brush_amount *= mask_texture.Sample(source_sampler, stamp_uv).a;
    }
    return lerp(source, brushed, brush_amount);
}
