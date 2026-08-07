// 照明を使わず静的メッシュの色と画像を出力するピクセルシェーダー。
#include "static_mesh.hlsli"

#ifndef REPLAY_MATERIAL_PROPERTIES
Texture2D diffuse_map : register(t0);
#endif
SamplerState sampler_lin : register(s1);

float4 main(VS_OUT pin) : SV_TARGET
{
#ifdef REPLAY_MATERIAL_PROPERTIES
    float4 base = BaseMap.Sample(sampler_lin, pin.texcoord) * BaseColor;
#else
    float4 base = diffuse_map.Sample(sampler_lin, pin.texcoord);
#endif
    return base * pin.color;
}
