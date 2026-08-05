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
// 【property はまだ描画に効かない】
//   宣言だけしてある。実際に本体へ渡すのはフェーズ 6。
//   ここで宣言しておく理由は、フェーズ 5 で MaterialAsset の
//   固定フィールドをこの名前へ詰め替えるため。
//   名前が食い違うと移行で値が落ちる。

#pragma replay_guid     "00000000000000000000000000000002"
#pragma replay_name     "PBR"
#pragma replay_category "BuiltIn"
#pragma replay_domain   surface

#pragma property color   BaseColor        "基本色"          = (1, 1, 1, 1)
#pragma property texture BaseMap          "基本色マップ"     default white
#pragma property texture NormalMap        "法線マップ"       default bump
#pragma property range   Metallic         "金属度"    0..1  = 0.0
#pragma property texture MetallicMap      "金属度マップ"     default white
#pragma property range   Roughness        "粗さ"      0..1  = 0.55
#pragma property texture RoughnessMap     "粗さマップ"       default white
#pragma property float3  Emissive         "発光色"          = (0, 0, 0)
#pragma property range   EmissiveStrength "発光の強さ" 0..16 = 0.0
#pragma property texture EmissiveMap      "発光マップ"       default black
#pragma property range   AmbientOcclusion "遮蔽"      0..1  = 1.0
#pragma property texture OcclusionMap     "遮蔽マップ"       default white
#pragma property range   AlphaCutoff      "アルファ閾値" 0..1 = 0.5
#pragma property toggle  DoubleSided      "両面を描く"       = false

// 静的メッシュとスキンメッシュで接線の作り方が違う。
// REPLAY_SKINNED は ShaderLibrary が変種ごとに define する。
#if REPLAY_SKINNED
#include "skinned_mesh_pbr_ps.hlsl"
#else
#include "static_mesh_pbr_ps.hlsl"
#endif
