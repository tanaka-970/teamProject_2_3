Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // radius, intensity, threshold, amount
    float4 effect_params1; // angle, progress, softness, speed
    float4 effect_params2; // direction.xy, seed, time
    float4 target_size;    // width, height, 1 / width, 1 / height
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

static const int sector_count = 8;
static const int samples_per_sector = 8;
static const float two_pi = 6.28318530718;
static const float inverse_golden_ratio = 0.61803398875;

float4 main(VSOutput input) : SV_TARGET
{
    const float radius = max(effect_params0.x, 0.0);
    const float intensity = saturate(effect_params0.y);
    const float sigma = max(radius * 0.5, 0.0001);
    const float inverse_two_sigma_squared = 1.0 / (2.0 * sigma * sigma);
    const float sector_angle = two_pi / sector_count;
    const float4 center = source_texture.Sample(source_sampler, input.uv);

    float4 filtered_sum = float4(0.0, 0.0, 0.0, 0.0);
    float confidence_sum = 0.0;

    // Four axis-aligned quadrants create square patches. Eight circular sectors
    // avoid that bias; sixteen or more leave too few samples per sector for a
    // stable variance estimate. Eight sectors are the practical balance here.
    [unroll]
    for (int sector_index = 0; sector_index < sector_count; ++sector_index)
    {
        float4 weighted_color = float4(0.0, 0.0, 0.0, 0.0);
        float weighted_luminance = 0.0;
        float weighted_luminance_squared = 0.0;
        float gaussian_weight_sum = 0.0;

        [unroll]
        for (int sample_index = 0; sample_index < samples_per_sector; ++sample_index)
        {
            // sqrt distributes the fixed eight taps uniformly by disk area.
            // Radius changes only their spacing, so the shader always uses 64 taps.
            const float radial_fraction = sqrt(
                (sample_index + 0.5) / samples_per_sector);
            const float angular_fraction = frac(
                (sample_index + 0.5) * inverse_golden_ratio);
            const float sample_angle =
                (sector_index + angular_fraction) * sector_angle;
            const float sample_distance = radius * radial_fraction;
            float sin_angle;
            float cos_angle;
            sincos(sample_angle, sin_angle, cos_angle);
            const float2 sample_offset = float2(cos_angle, sin_angle) *
                sample_distance * target_size.zw;
            const float sample_weight = exp(-sample_distance * sample_distance *
                inverse_two_sigma_squared);
            const float4 sample_color = source_texture.Sample(
                source_sampler, input.uv + sample_offset);
            // Perceptual luminance keeps edge selection sensitive to visible
            // contrast without making chroma noise dominate the variance.
            const float sample_luminance = dot(sample_color.rgb,
                float3(0.2126, 0.7152, 0.0722));

            weighted_color += sample_color * sample_weight;
            weighted_luminance += sample_luminance * sample_weight;
            weighted_luminance_squared += sample_luminance *
                sample_luminance * sample_weight;
            gaussian_weight_sum += sample_weight;
        }

        const float inverse_weight = 1.0 / max(gaussian_weight_sum, 0.0001);
        const float4 sector_mean = weighted_color * inverse_weight;
        const float mean_luminance = weighted_luminance * inverse_weight;
        const float variance = max(weighted_luminance_squared * inverse_weight -
            mean_luminance * mean_luminance, 0.0);
        // 標準偏差で分散の小さいセクタを選ぶ。softness は「面の硬さ」で、
        // 高いほど選択を強める。最低重みを置くため、極端な設定でも
        // confidence_sum が 0 になって黒く抜けることはない。
        const float deviation = sqrt(variance);
        const float hardness = saturate(effect_params1.z);
        const float selectivity = lerp(8.0, 256.0, hardness);
        const float variance_weight = max(1.0e-5,
            1.0 / (1.0 + pow(deviation * selectivity, 8.0)));
        filtered_sum += sector_mean * variance_weight;
        confidence_sum += variance_weight;
    }

    const float4 filtered = filtered_sum / max(confidence_sum, 0.0001);
    const float4 result = lerp(center, filtered, intensity);
    return result;
}
