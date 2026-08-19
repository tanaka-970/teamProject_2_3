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

static const int spiral_sample_count = 32;
static const float golden_angle = 2.39996323;

float4 main(VSOutput input) : SV_TARGET
{
    const float radius = max(effect_params0.x, 0.0);
    const float intensity = max(effect_params0.y, 0.0);
    const float threshold = saturate(effect_params0.z);
    const float sigma = max(radius * 0.5, 0.0001);
    const float inverse_two_sigma_squared = 1.0 / (2.0 * sigma * sigma);

    float4 center = source_texture.Sample(source_sampler, input.uv);
    float4 glow = center;
    float total_weight = 1.0;

    // 上下左右は十字になり、8・16 方向でも大半径では多角形の輪郭になるため使わない。
    // 黄金角で方向をずらし、sqrt で半径を配ることで円板上の面積あたりを均等にする。
    // Effect Stack は対象要素の小さな RT だけを通り、通常 UI は直接バッチされるため 32 タップは実用範囲に収まる。
    [unroll]
    for (int sample_index = 0; sample_index < spiral_sample_count; ++sample_index)
    {
        const float sample_radius = radius * sqrt((sample_index + 0.5) / spiral_sample_count);
        const float sample_angle = sample_index * golden_angle;
        float sin_angle;
        float cos_angle;
        sincos(sample_angle, sin_angle, cos_angle);
        const float2 sample_offset = float2(cos_angle, sin_angle) * sample_radius * target_size.zw;
        const float sample_weight = exp(-sample_radius * sample_radius * inverse_two_sigma_squared);
        glow += source_texture.Sample(source_sampler, input.uv + sample_offset) * sample_weight;
        total_weight += sample_weight;
    }
    glow /= total_weight;

    const float luminance = dot(glow.rgb, float3(0.2126, 0.7152, 0.0722));
    const float mask = saturate((luminance - threshold) * 8.0) * intensity;
    center.rgb += effect_color.rgb * glow.a * mask;
    center.a = saturate(center.a + glow.a * mask * effect_color.a);
    return center;
}
