// 新しいシェーダ資産の第 1 号。
//
// このファイルはまだ描画に使われない（フェーズ 4 以降で接続する）。
// 今の役目は「#pragma を書けば Inspector の項目になる」ことを
// 目で確かめられるようにすること。
//
// Window → シェーダ一覧 を開くと、ここに書いた宣言がそのまま出る。
// property を 1 行足して保存し、「再走査」を押せば欄が増える。
// C++ は 1 行も書かなくてよい。

#pragma replay_guid     "00000000000000000000000000000002"
#pragma replay_name     "Standard Lit"
#pragma replay_category "Lit"
#pragma replay_domain   surface

#pragma property color   BaseColor   "基本色"          = (1, 1, 1, 1)
#pragma property texture BaseMap     "基本色マップ"     default white
#pragma property texture NormalMap   "法線マップ"       default bump
#pragma property range   Metallic    "金属度"    0..1  = 0.0
#pragma property range   Roughness   "粗さ"      0..1  = 0.55
#pragma property float3  Emissive    "発光色"          = (0, 0, 0)
#pragma property range   EmissivePower "発光の強さ" 0..16 = 0.0
#pragma property toggle  DoubleSided "両面を描く"       = false

// 【ここから下はフェーズ 4 で本体を移植する】
//
// 移植のときは既存の static_mesh_pbr_ps.hlsl を #include するだけにして、
// 中身を書き直さないこと。書き直すと必ず絵が変わる。
// このフェーズの完了条件は「見た目が 1 ピクセルも変わらないこと」。

float4 main(float4 position : SV_POSITION) : SV_TARGET
{
    return float4(1.0f, 1.0f, 1.0f, 1.0f);
}
