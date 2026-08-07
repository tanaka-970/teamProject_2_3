// 組み込みシェーダ: Unlit
//
// shading_model::unlit (= 3) をアセットとして置き直したもの。
// ライティングを一切しない。基本色とテクスチャだけ。
//
// 本体は既存ファイルをそのまま #include している。書き換えていない。

#pragma replay_guid     "00000000000000000000000000000004"
#pragma replay_name     "Unlit"
#pragma replay_category "BuiltIn"
#pragma replay_domain   surface
#pragma replay_lighting unlit

#pragma property color   BaseColor   "基本色"          = (1, 1, 1, 1) category "Surface"
#pragma property texture BaseMap     "基本色マップ"     default white category "Surface"
#pragma property range   AlphaCutoff "アルファ閾値" 0..1 = 0.5 category "Rendering"
#pragma property toggle  DoubleSided "両面を描く"       = false category "Rendering"

#define REPLAY_MATERIAL_PROPERTIES 1
#if REPLAY_SKINNED
#include "skinned_mesh_unlit_ps.hlsl"
#else
#include "static_mesh_unlit_ps.hlsl"
#endif
