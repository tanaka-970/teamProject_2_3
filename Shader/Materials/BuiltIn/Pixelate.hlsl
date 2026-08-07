// 組み込みシェーダ: Pixelate（サーフェス）
//
// shading_model::pixelate (= 4) をアセットとして置き直したもの。
// モデルの色を画面上の四角いセル単位で低解像度化する。
//
// 【Shader/Layers/Pixelate.hlsl とは別物】
//   あちらは「他のシェーダの上に重ねる層」で domain が layer。
//   こちらは「そのメッシュ自体の描き方」で domain が surface。
//   GUID も別（層は ...0101、こちらは ...0005）。
//   同じ見た目でも、使われ方が違うので混ぜないこと。
//
// 【変種による違いが無い】
//   本体の object_pixelate_ps.hlsl は VS_OUT を使わず、
//   自前の PS_IN を宣言している。つまり静的でもスキンでも同じ。
//   それでも Static / Skinned の両方をコンパイルするのは、
//   surface ドメインの扱いを 1 つに揃えておくため。
//   ここだけ例外にすると、必ずどこかで場合分けが増える。

#pragma replay_guid     "00000000000000000000000000000005"
#pragma replay_name     "Pixelate"
#pragma replay_category "BuiltIn"
#pragma replay_domain   surface
#pragma replay_lighting pbr

#pragma property texture BaseMap           "基本色マップ"     default white category "Pixelate"
#pragma property range   PixelSize         "セル幅"     1..64 = 6.0 category "Pixelate"
#pragma property range   PixelateStrength  "強さ"       0..1  = 1.0 category "Pixelate"
#pragma property range   PixelateOpacity   "不透明度"   0..1  = 1.0 category "Pixelate"
#pragma property toggle  UseGBufferColor   "GBuffer の色を使う" = false category "Pixelate"

#define REPLAY_MATERIAL_PROPERTIES 1
#include "PostEffects/Pixelation/object_pixelate_ps.hlsl"
