// 組み込みシェーダ: GGST風トゥーン

#pragma replay_guid     "00000000000000000000000000000007"
#pragma replay_name     "GGST Toon"
#pragma replay_category "BuiltIn"
#pragma replay_domain   surface
#pragma replay_lighting toon

#pragma property color   BaseColor        "基本色"             = (1, 1, 1, 1) category "Surface" tooltip "ベースとなる色。基本色マップと乗算します。"
#pragma property texture BaseMap          "基本色マップ"        default white category "Surface" tooltip "キャラクターの基本色テクスチャ。"
#pragma property color   ShadeColor       "影色"               = (0.12, 0.18, 0.24, 1) category "GGST / 陰影" tooltip "2値陰影の影側に使う単色。"
#pragma property range   ShadingTerminator "明暗の分かれ目" 0..1 = 0.5 category "GGST / 陰影" tooltip "法線と光源の内積で明暗境界を直接指定します。0.5は光源から60度です。"
#pragma property range   ShadingThreshold "しきい値微調整" 0..1 = 0.5 category "GGST / 陰影" tooltip "0.5を中心に明暗の分かれ目を微調整します。頂点カラーRによる補正は維持されます。"
#pragma property range   ShadingOffset    "陰影オフセット" -1..1 = 0.0 category "GGST / 陰影" tooltip "ライト判定を明部側または影側へずらします。"
#pragma property range   SpecularSize     "ハイライトサイズ" 0..1 = 0.2 category "GGST / ハイライト" tooltip "2値ハイライトが出る範囲を調整します。"
#pragma property range   SpecularIntensity "ハイライト強度" 0..20 = 10.0 category "GGST / ハイライト" tooltip "ハイライトの明るさを調整します。"
#pragma property texture IlmMap           "ILM Map"            default white category "GGST / ILM" tooltip "R=ハイライト強度、G=陰影調整、B=ハイライトの出やすさ。未設定時は標準値を使います。"
#pragma property texture SssMap           "SSS Map"            default white category "GGST / 陰影" tooltip "影色へ乗算するテクスチャ。未設定時は影色をそのまま使います。"
#pragma property toggle  FaceLighting     "顔専用ライティング" = false category "GGST / 顔" tooltip "顔ボーンのRight/Front平面へライト方向を射影します。"
#pragma property range   FaceBoneIndex    "顔ボーンID" 0..1023 = 0.0 category "GGST / 顔" tooltip "顔ボーン名が空、または一致しない場合に使うSkeleton上のボーン番号。Bone 0固定ではありません。"
#pragma property toggle  FlipFaceRight    "顔Rightを反転"      = false category "GGST / 顔" tooltip "リグのX軸が逆向きの場合に有効にします。"
#pragma property toggle  FlipFaceFront    "顔Frontを反転"      = false category "GGST / 顔" tooltip "リグのZ軸が逆向きの場合に有効にします。"
#pragma property range   AlphaCutoff      "アルファ閾値" 0..1  = 0.5 category "Rendering"
#pragma property toggle  DoubleSided      "両面を描く"          = false category "Rendering"

#define REPLAY_MATERIAL_PROPERTIES 1
#if REPLAY_SKINNED
#include "skinned_mesh_unlit_ps.hlsl"
#else
#include "static_mesh_unlit_ps.hlsl"
#endif
