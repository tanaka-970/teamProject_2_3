// SSAO用の深度考慮バイラテラルブラー(分離型)。
// ssao_params2.zw に (1,0) / (0,1) を入れて横→縦の2パスで呼ぶ。
// Rの可視性だけを平滑化し、Gのビュー空間深度は中心値を保つ。
// 深度差の大きい隣接ピクセルを弾くので、輪郭のAOが滲まない。

#include "../../fullscreen_quad.hlsli"
#include "ssao_common.hlsli"

Texture2D ssao_source : register(t0);

static const int   SSAO_BLUR_RADIUS = 4;
static const float SSAO_BLUR_WEIGHTS[SSAO_BLUR_RADIUS + 1] =
{
    0.2270270270f, 0.1945945946f, 0.1216216216f, 0.0540540541f, 0.0162162162f
};

float4 main(VS_OUT pin) : SV_TARGET
{
    const float2 uv = pin.texcoord;
    const float2 direction = ssao_params2.zw * frame_screen_size.zw;

    const float2 center = ssao_source.SampleLevel(ssao_sampler_point, uv, 0).rg;
    const float center_visibility = center.r;
    const float center_depth = center.g;

    if (ssao_params3.z < 0.5f) return float4(center_visibility, center_depth, 0.0f, 0.0f);

    // 深度差の許容量は距離に比例させる(遠くほど1ピクセルあたりの奥行きが伸びる)。
    const float sharpness = max(ssao_params3.x, 1.0e-3f);
    const float depth_tolerance = max(center_depth, 1.0f) * 0.02f / sharpness;

    float visibility_sum = center_visibility * SSAO_BLUR_WEIGHTS[0];
    float weight_sum = SSAO_BLUR_WEIGHTS[0];

    [unroll] for (int offset = 1; offset <= SSAO_BLUR_RADIUS; ++offset)
    {
        [unroll] for (int side = 0; side < 2; ++side)
        {
            const float2 sample_uv =
                uv + direction * float(offset) * (side == 0 ? 1.0f : -1.0f);
            if (any(sample_uv < 0.0f) || any(sample_uv > 1.0f)) continue;

            const float2 tap = ssao_source.SampleLevel(ssao_sampler_point, sample_uv, 0).rg;
            const float depth_difference = abs(tap.g - center_depth);
            // 深度が離れているタップは重みを0へ落として輪郭を守る。
            const float depth_weight = saturate(1.0f - depth_difference / depth_tolerance);
            const float weight = SSAO_BLUR_WEIGHTS[offset] * depth_weight;

            visibility_sum += tap.r * weight;
            weight_sum += weight;
        }
    }

    const float visibility = weight_sum > 0.0f
        ? visibility_sum / weight_sum : center_visibility;
    return float4(visibility, center_depth, 0.0f, 0.0f);
}
