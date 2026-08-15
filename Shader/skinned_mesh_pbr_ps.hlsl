// スキンメッシュをPBRとIBLで照明するピクセルシェーダー。
#include "skinned_mesh.hlsli"
#include "pbr_brdf.hlsli"

float4 main(VS_OUT pin) : SV_TARGET
{
#ifdef REPLAY_MATERIAL_PROPERTIES
    float4 base_sample = BaseMap.Sample(pbr_sampler_anisotropic, pin.texcoord) * BaseColor;
    float3 base = pow(max(base_sample.rgb, 0.0f), 2.2f);
    float alpha = base_sample.a;
    float metallic = saturate(MetallicMap.Sample(pbr_sampler_linear, pin.texcoord).r * Metallic);
    float roughness = max(RoughnessMap.Sample(pbr_sampler_linear, pin.texcoord).r * Roughness, 0.045f);
    float occlusion = saturate(OcclusionMap.Sample(pbr_sampler_linear, pin.texcoord).r * AmbientOcclusion);
    float3 emissive = EmissiveMap.Sample(pbr_sampler_linear, pin.texcoord).rgb * Emissive * EmissiveStrength;
#else
    float4 base_sample = pbr_base_color.Sample(pbr_sampler_anisotropic, pin.texcoord);
    float3 base = pow(max(base_sample.rgb, 0.0f), 2.2f);
    float alpha = base_sample.a;

    float3 mr = pbr_metallic_roughness.Sample(pbr_sampler_linear, pin.texcoord).rgb;
    float metallic = gltf_pbr.w > 0.5f ? mr.b * gltf_pbr.x : mr.b;
    float roughness = gltf_pbr.w > 0.5f
        ? max(mr.g * gltf_pbr.y, 0.045f) : max(mr.g, 0.55f);
    float occlusion = gltf_pbr.w > 0.5f
        ? saturate(mr.r * gltf_pbr.z) : ((mr.r > 0.0f) ? mr.r : 1.0f);

    float3 emissive = pbr_emissive_map.Sample(pbr_sampler_linear, pin.texcoord).rgb;
    if (gltf_pbr.w > 0.5f) emissive *= gltf_emissive.rgb * gltf_emissive.w;
#endif

    if (gltf_pbr.w > 0.5f && gltf_alpha.x > 0.5f)
        clip(alpha * pin.color.a - (gltf_alpha.x < 1.5f ? gltf_alpha.y : 0.01f));

    float3 N = normalize(pin.world_normal.xyz);
    float3 T = normalize(pin.world_tangent.xyz);
    float sigma = pin.world_tangent.w;
    T = normalize(T - N * dot(N, T));
    float3 B = normalize(cross(N, T) * sigma);
    N = unpack_normal_map(N, T, B, pin.texcoord);

    float3 V = normalize(camera_position.xyz - pin.world_position.xyz);

    float3 color = evaluate_pbr(base, emissive, metallic, roughness, occlusion,
                                N, V, pin.world_position.xyz);

    return float4(color * pin.color.rgb, alpha * pin.color.a);
}
