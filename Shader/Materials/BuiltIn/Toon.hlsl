// 組み込みシェーダ: Toon
//
// shading_model::toon (= 2) をアセットとして置き直したもの。
// 3 階調ランプ + リム + 異方性ハイライト。
//
// 本体は既存ファイルをそのまま #include している。書き換えていない。
//
// 【Toon 固有の値について】
//   本体は TOON_MATERIAL_CONSTANTS (b6) から shadow_tint などを読んでいる。
//   今はまだそこへ繋がず、名前だけ宣言してある。
//   繋ぐのはフェーズ 6。今つなぐと絵が変わって移植の検証ができない。

#pragma replay_guid     "00000000000000000000000000000003"
#pragma replay_name     "Toon"
#pragma replay_category "BuiltIn"
#pragma replay_domain   surface

#pragma property color   BaseColor     "基本色"          = (1, 1, 1, 1)
#pragma property texture BaseMap       "基本色マップ"     default white
#pragma property texture RampMap       "ランプマップ"     default white
#pragma property color   ShadowTint    "影の色"          = (0.35, 0.38, 0.5, 1)
#pragma property color   RimColor      "リムの色"        = (1, 1, 1, 1)
#pragma property range   RimPower      "リムの強さ"  0..8 = 2.0
#pragma property color   SpecularTint  "ハイライトの色"   = (1, 1, 1, 1)
#pragma property range   SpecularPower "ハイライトの鋭さ" 1..128 = 32.0
#pragma property range   StepCount     "階調数"     1..8 = 3.0
#pragma property range   AlphaCutoff   "アルファ閾値" 0..1 = 0.5
#pragma property toggle  DoubleSided   "両面を描く"       = false

#if REPLAY_SKINNED
#include "skinned_mesh_toon_ps.hlsl"
#else
#include "static_mesh_toon_ps.hlsl"
#endif
