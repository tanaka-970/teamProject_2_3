// テクスチャを参照せず Material の基本色だけで平面的に塗る。

#pragma replay_guid     "00000000000000000000000000000006"
#pragma replay_name     "Flat Fill"
#pragma replay_category "BuiltIn"
#pragma replay_domain   surface
#pragma replay_lighting unlit

#pragma property color  BaseColor   "基本色"        = (1, 1, 1, 1) category "Surface"
#pragma property range  AlphaCutoff "アルファ閾値" 0..1 = 0.5 category "Rendering"
#pragma property toggle DoubleSided "両面を描く"     = false category "Rendering"

#define REPLAY_MATERIAL_PROPERTIES 1
#if REPLAY_SKINNED
#include "skinned_mesh.hlsli"
#else
#include "static_mesh.hlsli"
#endif

float4 main(VS_OUT pin) : SV_TARGET
{
    const float4 color = BaseColor * pin.color;
    clip(color.a - AlphaCutoff);
    return color;
}
