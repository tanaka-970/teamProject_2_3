// スキンメッシュを標準ライティングで描くピクセルシェーダー。
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
    float alpha = color.a;
    if (gltf_pbr.w > 0.5f && gltf_alpha.x > 0.5f)
        clip(alpha * pin.color.a - (gltf_alpha.x < 1.5f ? gltf_alpha.y : 0.01f));

    const float GAMMA = 2.2;
    color.rgb = pow(saturate(color.rgb), GAMMA);

    float3 N = normalize(pin.world_normal.xyz);
#ifndef REPLAY_MATERIAL_PROPERTIES
    float3 T = normalize(pin.world_tangent.xyz);
    float sigma = pin.world_tangent.w;
    T = normalize(T - N * dot(N, T));
    float3 B = normalize(cross(N, T) * sigma);
    float4 normal = texture_maps[1].Sample(sampler_states[LINEAR], pin.texcoord);
    normal = (normal * 2.0) - 1.0;
    N = normalize((normal.x * T) + (normal.y * B) + (normal.z * N));
#endif

    float3 L = normalize(-light_direction.xyz);
    float3 diffuse = color.rgb * max(0, dot(N, L));
    float3 V = normalize(camera_position.xyz - pin.world_position.xyz);
    float3 specular = pow(max(0, dot(N, normalize(V + L))), 128);
    return float4(diffuse + specular, alpha) * pin.color;
}
