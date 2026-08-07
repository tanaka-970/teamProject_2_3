// レイヤ用シェーダの第 1 号。
//
// 層の種類を C++ の enum に足さずに増やせることを示すための見本。
// フェーズ 10 でレイヤの実体がこちらへ移る。
//
// 今は Window → シェーダ一覧 に "Layer" として並ぶだけ。

#pragma replay_guid     "00000000000000000000000000000101"
#pragma replay_name     "Pixelate"
#pragma replay_category "Stylize"
#pragma replay_domain   layer

#pragma property range PixelSize "セル幅"   1..64 = 6
#pragma property range Strength  "強さ"     0..1  = 1
#pragma property range Opacity   "不透明度" 0..1  = 0.45

// cbuffer は書かない。上の #pragma property から自動生成される。
// PixelSize / Strength / Opacity はそのまま参照できる。

float4 main(float4 position : SV_POSITION) : SV_TARGET
{
    float2 cell = floor(position.xy / PixelSize) * PixelSize;
    float3 color = float3(cell * 0.001f, Strength);
    return float4(color, Opacity);
}
