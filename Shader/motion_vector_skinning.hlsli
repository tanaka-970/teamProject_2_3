// スキンメッシュのモーションベクター用、前フレームのボーン行列。
// 16KBあるのでG-Bufferパス(モーションベクターを書く描画)だけでバインドする。
#ifndef __MOTION_VECTOR_SKINNING_HLSLI__
#define __MOTION_VECTOR_SKINNING_HLSLI__

#include "motion_vector_common.hlsli"

static const int MOTION_MAX_BONES = 256;

cbuffer PREVIOUS_BONE_CONSTANT_BUFFER : register(b8)
{
    row_major float4x4 previous_bone_transforms[MOTION_MAX_BONES];
};

// 前フレームのボーン姿勢で同じ頂点をスキニングする。
// これを現フレームの結果と比べることで、アニメーション由来の動きも
// 正しくモーションベクターへ乗る(カメラ再投影だけでは取れない成分)。
float4 skin_previous_position(float4 local_position,
                              float4 bone_weights, uint4 bone_indices)
{
    float4 blended = float4(0.0f, 0.0f, 0.0f, 0.0f);
    [unroll] for (int i = 0; i < 4; ++i)
    {
        blended += bone_weights[i] *
            mul(local_position, previous_bone_transforms[bone_indices[i]]);
    }
    return float4(blended.xyz, 1.0f);
}

#endif
