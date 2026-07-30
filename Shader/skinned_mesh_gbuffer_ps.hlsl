// スキンメッシュの材質情報をG-Bufferへ書き込むピクセルシェーダー。
#include "skinned_mesh.hlsli"
#include "gbuffer_common.hlsli"

Texture2D base_color_map : register(t0);
Texture2D normal_map     : register(t1);
SamplerState sampler_lin : register(s1);

cbuffer MATERIAL_OVERRIDE : register(b9)
{
    float4 mat_params; // x=metallic y=roughness z=occlusion w=emissive
    uint   shading_model;
    float  texture_contrast;
    uint2  padding_;
};

GBufferOut main(VS_OUT pin)
{
    float3 base = base_color_map.Sample(sampler_lin, pin.texcoord).rgb;
    base = saturate((base - 0.5f) * max(texture_contrast, 0.01f) + 0.5f);

    float3 N = normalize(pin.world_normal.xyz);
    float3 T = normalize(pin.world_tangent.xyz);
    T = normalize(T - N * dot(N, T));
    float3 B = normalize(cross(N, T) * pin.world_tangent.w);
    float3 n = normal_map.Sample(sampler_lin, pin.texcoord).xyz * 2.0f - 1.0f;
    if (length(n) > 0.01f)
    {
        N = normalize(n.x * T + n.y * B + n.z * N);
    }

    GBufferData d;
    d.base_color = base * pin.color.rgb;
    d.shading_model = shading_model;
    d.emissive = d.base_color * mat_params.w;
    d.world_normal = N;
    d.occlusion = max(mat_params.z, 0.001f);
    d.roughness = max(mat_params.y, 0.045f);
    d.metalness = mat_params.x;
    d.occlusion_strength = mat_params.z;

    return EncodeGBuffer(d);
}
