// 組み込みシェーダ: PBR
//
// これは shading_model::pbr (= 1) をアセットとして置き直したもの。
//
// 【中身を書き直していない】
//   本体は既存の static_mesh_pbr_ps.hlsl / skinned_mesh_pbr_ps.hlsl を
//   そのまま #include している。1 行も書き換えていない。
//   書き換えると必ず絵が変わり、移植が正しいか判断できなくなる。
//   このフェーズの完了条件は「見た目が 1 ピクセルも変わらないこと」。
//
// 【GUID は決め打ち】
//   自動採番だと環境ごとに違う値になり、
//   既存マテリアルの移行先（shading_model 1 → このシェーダ）が定まらない。
//   一度決めたら絶対に変えないこと。変えると全マテリアルの参照が切れる。
//
// 【property は MaterialAsset v3 の正本】
//   Phase 6/12 で b9 / t40+ の実描画へ接続済み。
//   Phase 7 ではこの宣言から Material Inspector も自動生成する。
//   名前を変えると保存済み Material の値対応が変わるため慎重に扱う。

#pragma replay_guid     "00000000000000000000000000000002"
#pragma replay_name     "PBR"
#pragma replay_category "BuiltIn"
#pragma replay_domain   surface
#pragma replay_lighting pbr

#pragma property color   BaseColor        "基本色"          = (1, 1, 1, 1) category "Surface"
#pragma property texture BaseMap          "基本色マップ"     default white category "Surface"
#pragma property texture NormalMap        "法線マップ"       default bump category "Surface"
#pragma property range   NormalStrength   "法線の強さ" 0..2  = 1.0 category "Surface"
#pragma property range   Metallic         "金属度"    0..1  = 0.0 category "Surface"
#pragma property texture MetallicMap      "金属度マップ"     default white category "Surface"
#pragma property range   Roughness        "粗さ"      0..1  = 0.55 category "Surface"
#pragma property texture RoughnessMap     "粗さマップ"       default white category "Surface"
#pragma property float3  Emissive         "発光色"          = (0, 0, 0) category "Emission"
#pragma property range   EmissiveStrength "発光の強さ" 0..16 = 0.0 category "Emission"
#pragma property texture EmissiveMap      "発光マップ"       default black category "Emission"
#pragma property range   AmbientOcclusion "遮蔽"      0..1  = 1.0 category "Surface"
#pragma property texture OcclusionMap     "遮蔽マップ"       default white category "Surface"
#pragma property range   AlphaCutoff      "アルファ閾値" 0..1 = 0.5 category "Rendering"
#pragma property toggle  DoubleSided      "両面を描く"       = false category "Rendering"

// 静的メッシュとスキンメッシュで接線の作り方が違う。
// REPLAY_SKINNED は ShaderLibrary が変種ごとに define する。
#define REPLAY_MATERIAL_PROPERTIES 1
#if REPLAY_SKINNED
#include "skinned_mesh_pbr_ps.hlsl"
#else
#include "static_mesh_pbr_ps.hlsl"
#endif
