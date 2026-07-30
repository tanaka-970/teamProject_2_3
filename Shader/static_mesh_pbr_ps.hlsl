// 静的メッシュをPBRとIBLで照明するピクセルシェーダー。
#include "static_mesh.hlsli"
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

    // static_meshは接線を持たないため、ここで簡易接線座標系を構築する。
    float3 N = normalize(pin.world_normal.xyz);
    float3 tangent_seed = abs(N.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 T = normalize(cross(tangent_seed, N));
    float3 B = normalize(cross(N, T));
    N = unpack_normal_map(N, T, B, pin.texcoord);

    float3 V = normalize(camera_position.xyz - pin.world_position.xyz);

    float3 color = evaluate_pbr(base, emissive, metallic, roughness, occlusion,
                                N, V, pin.world_position.xyz);

    return float4(color * pin.color.rgb, alpha * pin.color.a);
}
