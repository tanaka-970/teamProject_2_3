// 静的メッシュを標準ライティングで描くピクセルシェーダー。
#include "skinned_mesh.hlsli"

#define POINT 0
#define LINEAR 1
#define ANISOTROPIC 2

SamplerState sampler_states[3] : register(s0);
#ifndef REPLAY_MATERIAL_PROPERTIES
Texture2D texture_maps[4] : register(t0);
#endif

float4 main(VS_OUT pin) : SV_TARGET
{
#ifdef REPLAY_MATERIAL_PROPERTIES
    float4 color = BaseMap.Sample(sampler_states[ANISOTROPIC], pin.texcoord) * BaseColor;
#else
    float4 color = texture_maps[0].Sample(sampler_states[ANISOTROPIC], pin.texcoord);
#endif

    float3 N = normalize(pin.world_normal.xyz);
    float3 L = normalize(-light_direction.xyz);
    float3 diffuse = color.rgb * max(0, dot(N, L));
    return float4(diffuse, color.a) * pin.color;
}
