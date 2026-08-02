// カスケードシャドウの行列、分割距離、サンプリング処理を定義する。
//
// 品質面でやっていること:
//   - 回転ポアソンディスクPCF: 固定3x3の格子状ノイズを消し、少ないタップ数でも
//     境界が滑らかになる。回転角はピクセルごとに変えてTAAで平均化させる。
//   - PCSS: ブロッカー探索で遮蔽物までの距離を測り、半影幅を可変にする。
//     接地部が硬く、離れるほど柔らかい物理的に妥当な影になる。
//   - 法線オフセット + 傾斜対応バイアス: シャドウアクネとピーターパンを同時に抑える。
//   - カスケード間クロスフェード: 分割境界で影の解像度が切り替わる継ぎ目を消す。
#ifndef __CSM_COMMON_HLSLI__
#define __CSM_COMMON_HLSLI__

#define CSM_CASCADE_COUNT 4
#define CSM_POISSON_TAP_COUNT 16
#define CSM_BLOCKER_TAP_COUNT 8

cbuffer CSM_CONSTANT_BUFFER : register(b5)
{
    row_major float4x4 csm_view_projection[CSM_CASCADE_COUNT];
    float4 csm_split_distances;   // x..w = カスケード境界 (view-space z)
    float4 csm_params;            // x=depth_bias, y=normal_bias, z=filter_radius, w=enable
    float4 csm_params2;           // x=shadow_map_size, y=cascade_blend, z=light_size_uv, w=pcss_enable
    float4 csm_params3;           // x=slope_bias_scale, y=max_bias, z=strength, w=tap_scale
    float4 csm_texel_world;       // カスケードごとの1テクセルのワールド長
};

Texture2DArray  csm_shadow_array : register(t12);
SamplerComparisonState csm_sampler : register(s5);
SamplerState csm_point_sampler : register(s6);

// 半径1の円内に低食い違いで散らしたタップ。ポアソンディスクの定番配置。
static const float2 CSM_POISSON_DISK[CSM_POISSON_TAP_COUNT] =
{
    float2(-0.94201624f, -0.39906216f), float2( 0.94558609f, -0.76890725f),
    float2(-0.09418410f, -0.92938870f), float2( 0.34495938f,  0.29387760f),
    float2(-0.91588581f,  0.45771432f), float2(-0.81544232f, -0.87912464f),
    float2(-0.38277543f,  0.27676845f), float2( 0.97484398f,  0.75648379f),
    float2( 0.44323325f, -0.97511554f), float2( 0.53742981f, -0.47373420f),
    float2(-0.26496911f, -0.41893023f), float2( 0.79197514f,  0.19090188f),
    float2(-0.24188840f,  0.99706507f), float2(-0.81409955f,  0.91437590f),
    float2( 0.19984126f,  0.78641367f), float2( 0.14383161f, -0.14100790f)
};

int csm_pick_cascade(float view_z)
{
    int index = 0;
    [unroll] for (int i = 0; i < CSM_CASCADE_COUNT - 1; ++i)
    {
        if (view_z > csm_split_distances[i]) index = i + 1;
    }
    return index;
}

float2 csm_shadow_texel_size()
{
    return 1.0f / max(csm_params2.x, 1.0f);
}

// ワールド位置をカスケードのシャドウUVと参照深度へ変換する。
// 戻り値: xy=UV, z=参照深度, w=範囲内なら1
float4 csm_project(float3 world_position, float3 world_normal, int cascade, float NoL)
{
    // 法線オフセットはテクセルのワールド長に比例させる。カスケードごとに
    // 解像度が違うので、固定値ではなく実サイズ基準にするのが重要。
    float texel_world = max(csm_texel_world[cascade], 1.0e-4f);
    float slope_scale = saturate(1.0f - NoL);
    float normal_offset = csm_params.y * texel_world * (1.0f + slope_scale * 2.0f);

    float4 light_position = mul(float4(world_position + world_normal * normal_offset, 1.0f),
        csm_view_projection[cascade]);
    float safe_w = max(abs(light_position.w), 1.0e-6f) * (light_position.w < 0.0f ? -1.0f : 1.0f);
    light_position.xyz /= safe_w;

    float2 uv = float2(light_position.x * 0.5f + 0.5f, -light_position.y * 0.5f + 0.5f);
    bool inside = !any(uv < 0.0f) && !any(uv > 1.0f) &&
        light_position.z > 0.0f && light_position.z < 1.0f;

    // 傾斜に応じてバイアスを増やし、斜面のアクネを消す。上限を設けて
    // 影が浮く(ピーターパニング)のを防ぐ。
    float slope_bias = csm_params.x * (1.0f + slope_scale * csm_params3.x);
    slope_bias = min(slope_bias, max(csm_params3.y, csm_params.x));

    return float4(uv, light_position.z - slope_bias, inside ? 1.0f : 0.0f);
}

// 遮蔽物の平均深度を探し、半影幅を推定する(PCSSのブロッカー探索)。
// 戻り値は フィルタ半径のスケール。遮蔽物が近いほど小さく(硬い影)なる。
float csm_estimate_penumbra(float2 uv, float reference_depth, int cascade, float rotation)
{
    float search_radius = max(csm_params2.z, 1.0e-4f);
    float2 texel = csm_shadow_texel_size();

    float sin_rotation, cos_rotation;
    sincos(rotation, sin_rotation, cos_rotation);

    float blocker_sum = 0.0f;
    float blocker_count = 0.0f;

    [unroll] for (int i = 0; i < CSM_BLOCKER_TAP_COUNT; ++i)
    {
        float2 tap = CSM_POISSON_DISK[i];
        float2 rotated = float2(tap.x * cos_rotation - tap.y * sin_rotation,
                                tap.x * sin_rotation + tap.y * cos_rotation);
        float2 sample_uv = uv + rotated * search_radius;
        if (any(sample_uv < 0.0f) || any(sample_uv > 1.0f)) continue;

        float depth = csm_shadow_array.SampleLevel(csm_point_sampler,
            float3(sample_uv, cascade), 0).r;
        // 参照より手前にあるものだけが遮蔽物。
        if (depth < reference_depth)
        {
            blocker_sum += depth;
            blocker_count += 1.0f;
        }
    }

    float result = 0.0f; // 遮蔽物なし = 完全に明るい
    if (blocker_count >= 0.5f)
    {
        float average_blocker_depth = blocker_sum / blocker_count;
        // 相似三角形で半影幅を求める。receiver と blocker が離れるほど広がる。
        float penumbra = (reference_depth - average_blocker_depth) /
            max(average_blocker_depth, 1.0e-5f);
        result = clamp(penumbra * csm_params2.z * csm_params2.x, 0.5f, 24.0f);
    }
    return result;
}

// 回転ポアソンディスクPCF。filter_radius はテクセル単位。
float csm_filter_pcf(float2 uv, float reference_depth, int cascade,
                     float filter_radius_texels, float rotation)
{
    float2 texel = csm_shadow_texel_size();
    float2 radius = filter_radius_texels * texel;

    float sin_rotation, cos_rotation;
    sincos(rotation, sin_rotation, cos_rotation);

    float sum = 0.0f;
    float weight = 0.0f;

    [unroll] for (int i = 0; i < CSM_POISSON_TAP_COUNT; ++i)
    {
        float2 tap = CSM_POISSON_DISK[i];
        float2 rotated = float2(tap.x * cos_rotation - tap.y * sin_rotation,
                                tap.x * sin_rotation + tap.y * cos_rotation);
        float2 sample_uv = uv + rotated * radius;
        sum += csm_shadow_array.SampleCmpLevelZero(csm_sampler,
            float3(sample_uv, cascade), reference_depth);
        weight += 1.0f;
    }

    return weight > 0.0f ? sum / weight : 1.0f;
}

float csm_sample_cascade(float3 world_position, float3 world_normal,
                         int cascade, float NoL, float rotation)
{
    float4 projected = csm_project(world_position, world_normal, cascade, NoL);
    float visibility = 1.0f;
    if (projected.w >= 0.5f)
    {
        float filter_radius = max(csm_params.z, 0.5f) * max(csm_params3.w, 0.1f);
        bool should_filter = true;

        // PCSSが有効なら、ブロッカー距離からフィルタ半径を可変にする。
        if (csm_params2.w >= 0.5f)
        {
            float penumbra = csm_estimate_penumbra(
                projected.xy, projected.z, cascade, rotation);
            should_filter = penumbra > 0.0f;
            filter_radius = clamp(penumbra, 0.5f, 24.0f);
        }
        if (should_filter)
            visibility = csm_filter_pcf(
                projected.xy, projected.z, cascade, filter_radius, rotation);
    }
    return visibility;
}

// 高品質版。view_z でカスケードを選び、境界ではひとつ先のカスケードと混ぜる。
// rotation_seed には interleaved gradient noise 等のピクセル毎の乱数を渡す。
float csm_sample_shadow_hq(float3 world_position, float3 world_normal,
                           float view_z, float NoL, float rotation_seed)
{
    float result = 1.0f;
    if (csm_params.w >= 0.5f)
    {
        int cascade = csm_pick_cascade(view_z);
        float rotation = rotation_seed * 6.28318530718f;

        float visibility = csm_sample_cascade(world_position, world_normal,
            cascade, NoL, rotation);

        // 分割境界の手前からクロスフェードして、解像度切り替えの継ぎ目を消す。
        float blend_width = max(csm_params2.y, 0.0f);
        if (blend_width > 0.0f && cascade < CSM_CASCADE_COUNT - 1)
        {
            float split = csm_split_distances[cascade];
            float blend_start = split - blend_width;
            if (view_z > blend_start)
            {
                float blend = saturate((view_z - blend_start) / max(blend_width, 1.0e-4f));
                float next_visibility = csm_sample_cascade(world_position, world_normal,
                    cascade + 1, NoL, rotation);
                visibility = lerp(visibility, next_visibility, blend);
            }
        }
        result = lerp(1.0f, visibility, saturate(csm_params3.z));
    }
    return result;
}

// 既存シェーダー互換の入口。回転シードとNoLが無い呼び出しでも動くようにする。
float csm_sample_shadow(float3 world_pos, float3 world_normal, float view_z)
{
    return csm_sample_shadow_hq(world_pos, world_normal, view_z, 1.0f, 0.0f);
}

#endif
