// タイルドDeferredで使うライト配列と、タイル分割の定数。
//
// 定数バッファ配列(lights_common.hlsli)と違い StructuredBuffer に置くことで、
// シェーダーを再コンパイルせずにライト数を増やせる。
// タイル単位でライトを絞るので、ライト数が増えてもピクセルあたりの
// 評価回数はそのタイルに影響するものだけに留まる。
#ifndef __TILED_LIGHT_COMMON_HLSLI__
#define __TILED_LIGHT_COMMON_HLSLI__

// 1タイルの1辺(ピクセル)。16x16=256スレッドはGCN/NVIDIAどちらでも扱いやすい。
#define TILE_SIZE 16
// 1タイルが保持できるライト数の上限。超えた分は捨てる(実用上まず溢れない)。
#define TILE_MAX_LIGHT 256

#define TILED_LIGHT_TYPE_POINT 0
#define TILED_LIGHT_TYPE_SPOT  1

struct TiledLight
{
    float4 position_radius; // xyz=ワールド位置, w=影響半径
    float4 color_intensity; // rgb=色, a=強度
    float4 direction_cone;  // xyz=向き(スポット), w=内コーンcos
    float4 params;          // x=外コーンcos, y=種類, z/w=予約
};

StructuredBuffer<TiledLight> tiled_lights : register(t20);

cbuffer TILED_LIGHT_CONSTANT_BUFFER : register(b12)
{
    // x=ライト数, y=タイル数(横), z=タイル数(縦), w=デバッグ表示モード
    int4   tiled_counts;
    // x=デバッグ用のライト数正規化値, y/z/w=予約
    float4 tiled_params;
};

// 点光源の距離減衰。逆二乗に窓関数をかけ、半径で確実に0になるようにする
// (Karis の "Real Shading in Unreal Engine 4" と同じ形)。
float tiled_distance_attenuation(float distance_squared, float radius)
{
    float inverse_radius = 1.0f / max(radius, 1.0e-4f);
    float ratio = distance_squared * inverse_radius * inverse_radius;
    float window = saturate(1.0f - ratio * ratio);
    return (window * window) / max(distance_squared, 1.0e-4f);
}

// スポットのコーン減衰。内外コーンの間だけ滑らかに落とす。
float tiled_spot_attenuation(float3 light_to_surface, float3 spot_direction,
                             float inner_cosine, float outer_cosine)
{
    float cosine = dot(light_to_surface, spot_direction);
    return saturate((cosine - outer_cosine) /
        max(inner_cosine - outer_cosine, 1.0e-4f));
}

#endif
