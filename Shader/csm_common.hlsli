// カスケードシャドウの行列、分割距離、サンプリング処理を定義する。
#ifndef __CSM_COMMON_HLSLI__
#define __CSM_COMMON_HLSLI__

#define CSM_CASCADE_COUNT 4

cbuffer CSM_CONSTANT_BUFFER : register(b5)
{
    row_major float4x4 csm_view_projection[CSM_CASCADE_COUNT];
    float4 csm_split_distances;   // x..w = カスケード境界 (view-space z)
    float4 csm_params;            // x=bias, y=normal_bias, z=filter_radius, w=enable
};

Texture2DArray  csm_shadow_array : register(t12);
SamplerComparisonState csm_sampler : register(s5);

int csm_pick_cascade(float view_z)
{
    int idx = 0;
    [unroll] for (int i = 0; i < CSM_CASCADE_COUNT - 1; ++i)
    {
        if (view_z > csm_split_distances[i]) idx = i + 1;
    }
    return idx;
}

float csm_sample_shadow(float3 world_pos, float3 world_normal, float view_z)
{
    float visibility = 1.0f;
    if (csm_params.w >= 0.5f)
    {
        int idx = csm_pick_cascade(view_z);
        float4 lp = mul(float4(world_pos + world_normal * csm_params.y, 1.0f),
                        csm_view_projection[idx]);
        float safe_w = max(abs(lp.w), 1.0e-6f) * (lp.w < 0.0f ? -1.0f : 1.0f);
        lp.xyz /= safe_w;
        float2 uv = float2(lp.x * 0.5f + 0.5f, -lp.y * 0.5f + 0.5f);

        if (!any(uv < 0.0f) && !any(uv > 1.0f))
        {
            float ref = lp.z - csm_params.x;
            float sum = 0.0f;
            [unroll] for (int oy = -1; oy <= 1; ++oy)
            [unroll] for (int ox = -1; ox <= 1; ++ox)
            {
                float2 off = float2(ox, oy) * csm_params.z * 0.0009765625f;
                sum += csm_shadow_array.SampleCmpLevelZero(
                    csm_sampler, float3(uv + off, idx), ref);
            }
            visibility = sum / 9.0f;
        }
    }
    return visibility;
}

#endif
