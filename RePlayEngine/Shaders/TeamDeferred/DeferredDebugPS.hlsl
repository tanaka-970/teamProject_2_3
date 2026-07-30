// G-Bufferの各情報を画面へ確認表示するピクセルシェーダー。
#include "DeferredLighting.hlsli"

// G-Bufferデバッグ表示係
// mode: 0=ベースカラー 1=法線 2=深度 3=エミッシブ 4=マテリアル 5=ワールド座標
cbuffer CbDebug : register(b2)
{
    float4 debugParams; // xが表示モード。
};

float4 main(VS_OUT pin) : SV_TARGET
{
    GBufferData g = SampleGBuffer(pin.uv);
    int mode = (int) debugParams.x;

    [branch]
    switch (mode)
    {
        case 1: return float4(g.worldNormal * 0.5f + 0.5f, 1.0f);       // 法線(色で向きを表示)
        case 2: { float d = saturate(g.ndcDepth); d = pow(d, 30.0f); return float4(d, d, d, 1.0f); } // 深度(白いほど奥)
        case 3: return float4(g.emissive, 1.0f);                         // エミッシブ
        case 4: return float4(g.metallic, g.roughness, g.ambientOcclusion, 1.0f); // マテリアル
        case 5: return float4(frac(g.worldPosition * 0.1f), 1.0f);       // ワールド座標(縞模様)
        default: return float4(g.baseColor, 1.0f);                       // ベースカラー
    }
}
