// モデルからG-Bufferへ渡す頂点情報と材質定数を共通定義する。
#ifndef __GBUFFER_MODEL_HLSLI__
#define __GBUFFER_MODEL_HLSLI__

// ジオメトリパス(モデル→G-Buffer書き込み)の共通定義
// Scene.hlsli の CbScene(b0) / Skinning.hlsli の CbSkeleton(b1/VS) と併用する

#include "Scene.hlsli"
#include "GBuffer.hlsli"

struct VS_OUT
{
    float4 vertex      : SV_POSITION;
    float2 texcoord    : TEXCOORD0;
    float3 normal      : TEXCOORD1;
    float4 ndcPosition : TEXCOORD2; // 射影後座標(深度の計算用)
    float3 tangent     : TEXCOORD3;
    float3 positionWS  : TEXCOORD4;
};

// 注意: マテリアル用cbuffer(b1)はGBufferPS.hlsl側で宣言する。
// VS側はb1をスキニング(CbSkeleton)が使っているため、ここに書くと衝突する。

#endif
