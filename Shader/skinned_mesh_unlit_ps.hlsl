// 照明を使わずスキンメッシュの色と画像を出力するピクセルシェーダー。
#include "skinned_mesh.hlsli"

#ifndef REPLAY_MATERIAL_PROPERTIES
Texture2D diffuse_map : register(t0);
#endif
SamplerState sampler_lin : register(s1);

float4 main(VS_OUT pin) : SV_TARGET
{
#ifdef REPLAY_MATERIAL_PROPERTIES
    float4 base = BaseMap.Sample(sampler_lin, pin.texcoord) * BaseColor;
    return base * pin.color;
#else
    float4 base = diffuse_map.Sample(sampler_lin, pin.texcoord);
    if (gltf_pbr.w > 0.5f && gltf_alpha.x > 0.5f)
        clip(base.a * pin.color.a - (gltf_alpha.x < 1.5f ? gltf_alpha.y : 0.01f));
    base.rgb = max(base.rgb, float3(0.18f, 0.18f, 0.18f));
    float3 tint = max(pin.color.rgb, float3(0.75f, 0.75f, 0.75f));
    return float4(base.rgb * tint, base.a * pin.color.a);
#endif
}
