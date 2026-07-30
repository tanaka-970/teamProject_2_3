// スキンメッシュをPBRとIBLで照明するピクセルシェーダー。
#include "skinned_mesh.hlsli"
#include "pbr_brdf.hlsli"

float4 main(VS_OUT pin) : SV_TARGET
{
    float4 base_sample = pbr_base_color.Sample(pbr_sampler_anisotropic, pin.texcoord);
    float3 base = pow(max(base_sample.rgb, 0.0f), 2.2f);
    float alpha = base_sample.a;

    float3 mr = pbr_metallic_roughness.Sample(pbr_sampler_linear, pin.texcoord).rgb;
    float metallic = mr.b;
    float roughness = max(mr.g, 0.55f);
    float occlusion = (mr.r > 0.0f) ? mr.r : 1.0f;

    float3 emissive = pbr_emissive_map.Sample(pbr_sampler_linear, pin.texcoord).rgb;

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
