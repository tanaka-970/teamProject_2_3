// 静的メッシュの材質情報をG-Bufferへ書き込むピクセルシェーダー。
#include "static_mesh.hlsli"
#include "gbuffer_common.hlsli"
#include "motion_vector_common.hlsli"

Texture2D base_color_map : register(t0);
Texture2D normal_map     : register(t1);
SamplerState sampler_lin : register(s1);

cbuffer MATERIAL_OVERRIDE : register(b9)
{
    float4 mat_params; // x=metallic y=roughness z=occlusion w=emissive
    uint   shading_model;
    float  texture_contrast;
    float  pixelate_size;
    float  pixelate_strength;
};

GBufferOut main(VS_OUT pin)
{
    float3 base = base_color_map.Sample(sampler_lin, pin.texcoord).rgb;
    base = saturate((base - 0.5f) * max(texture_contrast, 0.01f) + 0.5f);

    float3 N = normalize(pin.world_normal.xyz);
    float3 n = normal_map.Sample(sampler_lin, pin.texcoord).xyz * 2.0f - 1.0f;
    if (length(n) > 0.01f)
    {
        float3 seed = abs(N.y) < 0.999f ? float3(0, 1, 0) : float3(1, 0, 0);
        float3 T = normalize(cross(seed, N));
        float3 B = normalize(cross(N, T));
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
    d.velocity = compute_motion_vector(pin.current_clip, pin.previous_clip);

    GBufferOut output = EncodeGBuffer(d);
    if (shading_model == SHADING_MODEL_PIXELATE)
    {
        output.emissive.a = max(pixelate_size, 1.0f);
        output.normal.a = saturate(pixelate_strength);
    }
    return output;
}
