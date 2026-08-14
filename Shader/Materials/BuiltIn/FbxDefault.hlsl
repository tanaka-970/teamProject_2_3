// 組み込みシェーダ: 標準（FBX 既定）
//
// shading_model::fbx_default (= 0) をアセットとして置き直したもの。
// Lambert + テクスチャだけの、いちばん軽い描き方。
//
// 本体は既存ファイルをそのまま #include している。書き換えていない。

#pragma replay_guid     "00000000000000000000000000000001"
#pragma replay_name     "標準"
#pragma replay_category "BuiltIn"
#pragma replay_domain   surface
#pragma replay_lighting pbr

#pragma property color   BaseColor   "基本色"          = (1, 1, 1, 1) category "Surface"
#pragma property texture BaseMap     "基本色マップ"     default white category "Surface"
#pragma property range   AlphaCutoff "アルファ閾値" 0..1 = 0.5 category "Rendering"
#pragma property toggle  DoubleSided "両面を描く"       = false category "Rendering"

#define REPLAY_MATERIAL_PROPERTIES 1
#if REPLAY_SKINNED
#include "skinned_mesh_ps.hlsl"
#else
#include "static_mesh_ps.hlsl"
#endif
