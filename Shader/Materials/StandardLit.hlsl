// 自作シェーダの見本。
//
// 組み込みの 5 種は BuiltIn/ にある。こちらは「自分で書くとどうなるか」を
// 示すための 1 枚で、組み込みと扱いは完全に同じ。
//   ・同じ .hlsl 形式
//   ・同じ走査経路
//   ・同じ Catalog
//   ・同じ Inspector
// 区別を作らないこと。区別すると必ず片方にしかない機能が生まれる。
//
// 【新しいシェーダの作り方】
//   1. このファイルをコピーする
//   2. **#pragma replay_guid の行を消す**（消さないと GUID が重複する）
//   3. 保存して「再走査」を押す。GUID は自動で振られてファイルへ書き戻る
//   4. #pragma property を書けば、そのまま Inspector の欄になる
//
// cbuffer は書かない。#pragma property から自動生成されて
// このファイルの先頭へ差し込まれる。だから BaseColor をそのまま参照できる。
//
// 【確かめ方】
//   下の BaseColor を BaseColorX に変えて保存する。
//   1 秒以内に「シェーダ一覧」が赤くなり、エラー行をクリックすると
//   この行が Visual Studio で開く。戻せば緑に戻る。

#pragma replay_guid     "00000000000000000000000000000201"
#pragma replay_name     "Standard Lit（見本）"
#pragma replay_category "Sample"
#pragma replay_domain   surface

#pragma property color   BaseColor     "基本色"          = (1, 1, 1, 1)
#pragma property texture BaseMap       "基本色マップ"     default white
#pragma property texture NormalMap     "法線マップ"       default bump
#pragma property range   Metallic      "金属度"    0..1  = 0.0
#pragma property range   Roughness     "粗さ"      0..1  = 0.55
#pragma property float3  Emissive      "発光色"          = (0, 0, 0)
#pragma property range   EmissivePower "発光の強さ" 0..16 = 0.0
#pragma property toggle  DoubleSided   "両面を描く"       = false

// まだ描画には使われない（接続はフェーズ 6）。
// 変種で分ける必要が無ければ REPLAY_SKINNED を見なくてよい。
float4 main(float4 position : SV_POSITION) : SV_TARGET
{
    float3 lit = BaseColor.rgb * (1.0f - Metallic * 0.5f);
    lit = lerp(lit, lit * Roughness, DoubleSided);
    return float4(lit + Emissive * EmissivePower, BaseColor.a);
}
