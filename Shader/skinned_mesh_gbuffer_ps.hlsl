// スキンメッシュの材質情報をG-Bufferへ書き込むピクセルシェーダー。
#include "skinned_mesh.hlsli"
#include "gbuffer_common.hlsli"
#include "motion_vector_common.hlsli"

Texture2D legacy_base_color_map : register(t0);
Texture2D legacy_normal_map     : register(t1);
Texture2D replay_base_map       : register(t40);
Texture2D replay_normal_map     : register(t41);
Texture2D replay_metallic_map   : register(t42);
Texture2D replay_roughness_map  : register(t43);
Texture2D replay_emissive_map   : register(t44);
Texture2D replay_occlusion_map  : register(t45);
SamplerState sampler_lin : register(s1);

cbuffer GBUFFER_MATERIAL_CONSTANTS : register(b9)
{
    float4 base_color_factor;
    float4 emissive_factor;
    float4 mat_params;
    uint   lighting_model;
    float  texture_contrast;
    float  pixelate_size;
    float  pixelate_strength;
    uint   texture_mask;
    float3 _replay_gbuffer_padding;
};

static const uint REPLAY_TEX_BASE      = 1u << 0;
static const uint REPLAY_TEX_NORMAL    = 1u << 1;
static const uint REPLAY_TEX_METALLIC  = 1u << 2;
static const uint REPLAY_TEX_ROUGHNESS = 1u << 3;
static const uint REPLAY_TEX_EMISSIVE  = 1u << 4;
static const uint REPLAY_TEX_OCCLUSION = 1u << 5;

GBufferOut main(VS_OUT pin)
{
    float4 base_sample = (texture_mask & REPLAY_TEX_BASE) != 0
        ? replay_base_map.Sample(sampler_lin, pin.texcoord)
        : legacy_base_color_map.Sample(sampler_lin, pin.texcoord);
    float3 base = base_sample.rgb * base_color_factor.rgb;
    base = saturate((base - 0.5f) * max(texture_contrast, 0.01f) + 0.5f);

    float3 N = normalize(pin.world_normal.xyz);
    float3 T = normalize(pin.world_tangent.xyz);
    T = normalize(T - N * dot(N, T));
    float3 B = normalize(cross(N, T) * pin.world_tangent.w);
    float3 n = ((texture_mask & REPLAY_TEX_NORMAL) != 0
        ? replay_normal_map.Sample(sampler_lin, pin.texcoord)
        : legacy_normal_map.Sample(sampler_lin, pin.texcoord)).xyz * 2.0f - 1.0f;
    if (length(n) > 0.01f)
        N = normalize(n.x * T + n.y * B + n.z * N);

    float metallic = mat_params.x;
    float roughness = mat_params.y;
    float occlusion = mat_params.z;
    if ((texture_mask & REPLAY_TEX_METALLIC) != 0)
        metallic *= replay_metallic_map.Sample(sampler_lin, pin.texcoord).r;
    if ((texture_mask & REPLAY_TEX_ROUGHNESS) != 0)
        roughness *= replay_roughness_map.Sample(sampler_lin, pin.texcoord).r;
    if ((texture_mask & REPLAY_TEX_OCCLUSION) != 0)
        occlusion *= replay_occlusion_map.Sample(sampler_lin, pin.texcoord).r;

    float3 emissive = emissive_factor.rgb * emissive_factor.w;
    if ((texture_mask & REPLAY_TEX_EMISSIVE) != 0)
        emissive = replay_emissive_map.Sample(sampler_lin, pin.texcoord).rgb *
            emissive_factor.rgb * emissive_factor.w;

    GBufferData d;
    d.base_color = base * pin.color.rgb;
    d.lighting_model = lighting_model;
    d.emissive = emissive;
    d.world_normal = N;
    d.occlusion = max(occlusion, 0.001f);
    d.roughness = max(roughness, 0.045f);
    d.metalness = saturate(metallic);
    d.occlusion_strength = saturate(occlusion);
    d.velocity = compute_motion_vector(pin.current_clip, pin.previous_clip);

    GBufferOut output = EncodeGBuffer(d);
    if (pixelate_size > 0.0f && pixelate_strength > 0.0f)
        EncodePixelateSettings(output, pixelate_size, pixelate_strength);
    return output;
}
