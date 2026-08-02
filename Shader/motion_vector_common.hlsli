// TAA用モーションベクターの生成に必要な前フレーム情報。
//
// スロットの取り決め:
//   VS b6 = 前フレームのオブジェクト姿勢とビュー射影 (PSのb6はトゥーン材質なので無衝突)
//   VS b8 = 前フレームのボーン行列 (スキンメッシュのG-Bufferパスだけで使う)
// どちらもモーションベクターを書くパス以外では触らない。
#ifndef __MOTION_VECTOR_COMMON_HLSLI__
#define __MOTION_VECTOR_COMMON_HLSLI__

cbuffer PREVIOUS_OBJECT_CONSTANT_BUFFER : register(b6)
{
    row_major float4x4 previous_world;
    row_major float4x4 previous_view_projection;
    float4 motion_params;  // x=有効, y/z=現フレームのジッター(NDC), w=予約
    float4 motion_params2; // x/y=前フレームのジッター(NDC), z/w=予約
};

// クリップ座標のペアから、TAAジッターを除いた画面空間の移動量(UV)を求める。
// 「前フレームのUV = 現在のUV - motion」になる向きで返す。
float2 compute_motion_vector(float4 current_clip, float4 previous_clip)
{
    float2 result = float2(0.0f, 0.0f);
    if (motion_params.x >= 0.5f)
    {
        float2 current_ndc = current_clip.xy / max(abs(current_clip.w), 1.0e-6f);
        float2 previous_ndc = previous_clip.xy / max(abs(previous_clip.w), 1.0e-6f);

        // ジッターは幾何の動きではないので、両フレーム分を打ち消す。
        current_ndc -= motion_params.yz;
        previous_ndc -= motion_params2.xy;

        // NDC差をUV差へ。UVは上下が逆なのでyの符号を反転する。
        result = (current_ndc - previous_ndc) * float2(0.5f, -0.5f);
    }
    return result;
}

#endif
