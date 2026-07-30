// 画像を使わず頂点色だけでスプライトを描くピクセルシェーダー。
#include "sprite.hlsli"

float4 main(VS_OUT input) : SV_TARGET
{
    // 単色UIは1x1の仮テクスチャをサンプリングしない。
    // 共有BORDERサンプラー経由で黒が混ざり、起動映像にあった十字状のむらが出るためである。
    return input.color;
}
