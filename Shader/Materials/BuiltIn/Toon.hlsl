// 組み込みシェーダ: Toon
//
// shading_model::toon (= 2) をアセットとして置き直したもの。
// 3 階調ランプ + リム + 異方性ハイライト。
//
// 本体は既存ファイルをそのまま #include している。書き換えていない。
//
// 【Toon 固有の値について】
//   Phase 6/12 で REPLAY_MATERIAL_PROPERTIES 経路へ接続済み。
//   下の宣言は b9 / t40+ と Phase 7 Inspector の両方の正本になる。

#pragma replay_guid     "00000000000000000000000000000003"
#pragma replay_name     "Toon"
#pragma replay_category "BuiltIn"
#pragma replay_domain   surface
#pragma replay_lighting toon

#pragma property color   BaseColor     "基本色"          = (1, 1, 1, 1) category "Surface"
#pragma property texture BaseMap       "基本色マップ"     default white category "Surface"
#pragma property texture RampMap       "ランプマップ"     default white category "Toon"
#pragma property color   ShadowTint    "影の色"          = (0.35, 0.38, 0.5, 1) category "Toon"
#pragma property color   RimColor      "リムの色"        = (1, 1, 1, 1) category "Rim"
#pragma property range   RimPower      "リムの強さ"  0..8 = 2.0 category "Rim"
#pragma property color   SpecularTint  "ハイライトの色"   = (1, 1, 1, 1) category "Highlight"
#pragma property range   SpecularPower "ハイライトの鋭さ" 1..128 = 32.0 category "Highlight"
#pragma property range   StepCount     "階調数"     1..8 = 3.0 category "Toon"
#pragma property range   AlphaCutoff   "アルファ閾値" 0..1 = 0.5 category "Rendering"
#pragma property toggle  DoubleSided   "両面を描く"       = false category "Rendering"

#define REPLAY_MATERIAL_PROPERTIES 1
#if REPLAY_SKINNED
#include "skinned_mesh_toon_ps.hlsl"
#else
#include "static_mesh_toon_ps.hlsl"
#endif
