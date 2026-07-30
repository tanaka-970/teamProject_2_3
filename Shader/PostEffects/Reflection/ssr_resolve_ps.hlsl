// 確率的SSRのノイズを落とすresolveパス。
//
// 隣接ピクセルは別方向のレイを飛ばしているので、深度と法線が近いタップだけを
// 集めれば同じ面の別サンプルとして再利用できる(spatial reuse)。
// ぼかし半径はラフネスに比例させ、鏡面はほぼそのまま、粗い面は大きく平均化する。

#include "../../fullscreen_quad.hlsli"
#include "ssr_common.hlsli"

Texture2D reflection_source : register(t0);
Texture2D scene_depth       : register(t1);
Texture2D gbuffer_normal    : register(t2);
Texture2D gbuffer_param     : register(t3);

static const int SSR_RESOLVE_MAX_TAP = 16;
static const float2 SSR_RESOLVE_DISK[SSR_RESOLVE_MAX_TAP] =
{
    float2( 0.0000f,  0.0000f), float2( 0.5417f,  0.1922f),
    float2(-0.2837f,  0.5116f), float2(-0.4746f, -0.3861f),
    float2( 0.2189f, -0.6032f), float2( 0.8562f, -0.2438f),
    float2(-0.7593f,  0.4392f), float2(-0.1128f, -0.9195f),
    float2( 0.6534f,  0.6837f), float2(-0.9231f, -0.1476f),
    float2( 0.3372f,  0.9013f), float2( 0.0724f, -0.3419f),
    float2(-0.5871f, -0.7315f), float2( 0.9564f,  0.2043f),
    float2(-0.3049f,  0.1834f), float2( 0.4382f, -0.8571f)
};

float4 main(VS_OUT pin) : SV_TARGET
{
    const float2 uv = pin.texcoord;
    const float4 center = reflection_source.SampleLevel(ssr_sampler_point, uv, 0);

    if (ssr_params2.x < 0.5f) return float4(0, 0, 0, 0);

    const float device_z = scene_depth.SampleLevel(ssr_sampler_point, uv, 0).r;
    if (device_z >= 0.999999f) return float4(0, 0, 0, 0);

    const float center_view_z = view_position_from_depth(uv, device_z).z;
    const float3 center_normal = normalize(
        gbuffer_normal.SampleLevel(ssr_sampler_point, uv, 0).xyz * 2.0f - 1.0f);
    const float roughness = saturate(gbuffer_param.SampleLevel(ssr_sampler_point, uv, 0).g);

    // ラフネスが低いほど半径を絞る。鏡面が滲まないようにするため。
    const float radius_pixels = max(ssr_params2.y, 0.0f) * saturate(roughness * 2.0f);
    const int tap_count = (int) clamp(ssr_params3.x, 1.0f, (float) SSR_RESOLVE_MAX_TAP);
    if (radius_pixels < 0.5f || tap_count <= 1)
        return center;

    const float noise = interleaved_gradient_noise(pin.position.xy, frame_params.x);
    float sin_rotation, cos_rotation;
    sincos(noise * 6.28318530718f, sin_rotation, cos_rotation);

    // 深度の許容差は距離に比例させる。遠景で過剰に弾かないため。
    const float depth_tolerance = max(center_view_z, 1.0f) * 0.03f;

    float4 accumulated = 0.0f;
    float weight_sum = 0.0f;

    [loop] for (int tap = 0; tap < tap_count; ++tap)
    {
        const float2 offset = SSR_RESOLVE_DISK[tap];
        const float2 rotated = float2(
            offset.x * cos_rotation - offset.y * sin_rotation,
            offset.x * sin_rotation + offset.y * cos_rotation);
        const float2 sample_uv = uv + rotated * radius_pixels * ssr_target_size.zw;
        if (any(sample_uv < 0.0f) || any(sample_uv > 1.0f)) continue;

        const float sample_device_z = scene_depth.SampleLevel(ssr_sampler_point, sample_uv, 0).r;
        if (sample_device_z >= 0.999999f) continue;

        const float sample_view_z = view_position_from_depth(sample_uv, sample_device_z).z;
        const float3 sample_normal = normalize(
            gbuffer_normal.SampleLevel(ssr_sampler_point, sample_uv, 0).xyz * 2.0f - 1.0f);

        // 同じ面のサンプルだけを採用する。深度と法線の両方で判定する。
        const float depth_weight =
            saturate(1.0f - abs(sample_view_z - center_view_z) / depth_tolerance);
        const float normal_weight = pow(saturate(dot(sample_normal, center_normal)), 8.0f);
        const float weight = depth_weight * normal_weight;
        if (weight <= 0.0f) continue;

        accumulated += reflection_source.SampleLevel(ssr_sampler_point, sample_uv, 0) * weight;
        weight_sum += weight;
    }

    if (weight_sum <= 0.0f) return center;
    return accumulated / weight_sum;
}
