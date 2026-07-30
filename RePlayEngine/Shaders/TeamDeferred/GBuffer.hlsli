// G-Bufferの出力構造と読み書き方法を共通定義する。
#ifndef __GBUFFER_HLSLI__
#define __GBUFFER_HLSLI__


// G-Buffer 共通定義 しらべたことだけ。
//
// 画面と同じサイズのメモ帳4枚に、ライティングに必要な情報を書き込む。
//   RT0 : ベースカラー.rgb       + シェーディングモデルID(a)
//   RT1 : エミッシブ(自己発光).rgb
//   RT2 : ワールド法線.xyz       + NDC深度(a)  ※float32テクスチャ
//   RT3 : マテリアル情報 (x=metallic, y=roughness, z=トゥーン段数, w=予備)
//
// 書く係(GBufferPS)と読む係(DeferredXxxPS)が必ずこのヘッダーを使うことで、
// メモの書き方と読み方がズレないようにする。絶対に。

// シェーディングモデルID (「光の跳ね返り方レシピ」の番号)
static const int SHADING_MODEL_UNLIT = 0; // ライティング無し(そのままの色)
static const int SHADING_MODEL_PHONG = 1; // フォン(プラスチックっぽい)
static const int SHADING_MODEL_PBR   = 2; // 物理ベース(金属/ザラザラ表現)
static const int SHADING_MODEL_TOON_STAGE = 3;
static const int SHADING_MODEL_TOON_PLAYER = 4;
static const int SHADING_MODEL_TOON_ENEMY = 5;
static const int SHADING_MODEL_PBR_TOON_STAGE = 6;
static const int SHADING_MODEL_PBR_TOON_PLAYER = 7;
static const int SHADING_MODEL_PBR_TOON_ENEMY = 8;
static const int SHADING_MODEL_UNLIT_PLAYER = 9;
static const int SHADING_MODEL_PHONG_PLAYER = 10;
static const int SHADING_MODEL_PBR_PLAYER = 11;
static const int SHADING_MODEL_UNLIT_ENEMY = 12;
static const int SHADING_MODEL_PHONG_ENEMY = 13;
static const int SHADING_MODEL_PBR_ENEMY = 14;
static const int SHADING_MODEL_MAX = 15;

// ピクセルシェーダーからメモ帳4枚へ同時出力するための構造体 (MRT)
struct PSGBufferOut
{
    float4 baseColor   : SV_TARGET0;
    float4 emissive    : SV_TARGET1;
    float4 normalDepth : SV_TARGET2;
    float4 material    : SV_TARGET3;
};

// G-Bufferに書く/読む情報のまとまり
struct GBufferData
{
    float3 baseColor;    // 物そのものの色
    float3 emissive;     // 自分で光る色
    float3 worldNormal;  // 面の向き(ワールド空間)
    float3 worldPosition; // ワールド座標 (Decode時のみ。深度から復元)
    float  ndcDepth;     // 射影後の深度 (0=手前 1=奥)
    float  metallic;     // 金属っぽさ (PBR用)
    float  roughness;    // ザラザラ度 (PBR用)
    float  toonSteps;    // トゥーンの影の段数
    float  ambientOcclusion;
    float  exposure;
    int    shadingModel; // 上のSHADING_MODEL_xxx
};

// 書き込み: GBufferData → メモ帳4枚のレイアウトへ変換
PSGBufferOut EncodeGBuffer(in GBufferData data)
{
    PSGBufferOut ret = (PSGBufferOut) 0;

    ret.baseColor.rgb = data.baseColor;
    // シェーディングモデルIDは 0～1 に正規化してアルファに隠しておく
    ret.baseColor.a = (float) data.shadingModel / SHADING_MODEL_MAX;

    ret.emissive.rgb = data.emissive;
    ret.emissive.a = data.exposure;

    ret.normalDepth.xyz = normalize(data.worldNormal);
    ret.normalDepth.w = data.ndcDepth;

    ret.material = float4(data.metallic, data.roughness, data.toonSteps, data.ambientOcclusion);
    return ret;
}

// 読み取り: メモ帳4枚 + UV + 逆ビュー射影行列 → GBufferData に復元
GBufferData DecodeGBuffer(
    float4 rt0, float4 rt1, float4 rt2, float4 rt3,
    float2 uv, row_major float4x4 inverseViewProjection)
{
    GBufferData data = (GBufferData) 0;
    data.baseColor    = rt0.rgb;
    data.shadingModel = (int) round(rt0.a * SHADING_MODEL_MAX);
    data.emissive     = rt1.rgb;
    data.exposure     = max(rt1.a, 0.01f);
    data.worldNormal  = normalize(rt2.xyz);
    data.ndcDepth     = rt2.w;
    data.metallic     = rt3.x;
    data.roughness    = rt3.y;
    data.toonSteps    = rt3.z;
    data.ambientOcclusion = rt3.w;

    // 「画面のどこ(UV) + どれだけ奥(深度)」からワールド座標を逆算する
    float2 ndcXY = float2(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f);
    float4 position = mul(float4(ndcXY, data.ndcDepth, 1.0f), inverseViewProjection);
    data.worldPosition = position.xyz / position.w;
    return data;
}

#endif
