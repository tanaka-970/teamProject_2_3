// PBR、IBL、シャドウで共有する定数とテクスチャを定義する。
//  pbr_common.hlsli
//  PBR + IBL + Shadow の "追加" 定数バッファ / サンプラー / テクスチャ。
//  既存 skinned_mesh.hlsli / static_mesh.hlsli の cbuffer はそのまま流用するため
//  ここでは SCENE_CONSTANT_BUFFER(b1) / OBJECT_CONSTANT_BUFFER(b0) は定義しない。
//
//  使い方:
//    #include "skinned_mesh.hlsli"  または "static_mesh.hlsli"
//    #include "pbr_common.hlsli"
//
//  追加スロット:
//    t2  = metallic-roughness (B=metal, G=rough, R=AO) glTF ORM
//    t3  = emissive map
//    t4  = shadow map (R32_FLOAT depth)
//    t33 = diffuse IEM   (TextureCube)
//    t34 = specular PMREM(TextureCube)
//    t35 = lut_ggx       (Texture2D)
//    s4  = shadow comparison sampler
//    b2  = LIGHT_CONSTANT_BUFFER  (色 / IBL強度 / シャドウ)
//    b3  = SHADOW_CONSTANT_BUFFER (ライトVP)
#ifndef __PBR_COMMON_HLSLI__
#define __PBR_COMMON_HLSLI__

static const float PI = 3.14159265358979f;

SamplerState           pbr_sampler_point       : register(s0);
SamplerState           pbr_sampler_linear      : register(s1);
SamplerState           pbr_sampler_anisotropic : register(s2);
SamplerComparisonState pbr_sampler_shadow      : register(s4);

Texture2D pbr_base_color         : register(t0); // 既存 base color と重複OK (同スロット参照)
Texture2D pbr_normal_map         : register(t1); // 既存 normal と重複OK
Texture2D pbr_metallic_roughness : register(t2);
Texture2D pbr_emissive_map       : register(t3);

Texture2D pbr_shadow_map         : register(t4);

TextureCube pbr_diffuse_iem      : register(t33);
TextureCube pbr_specular_pmrem   : register(t34);
Texture2D   pbr_lut_ggx          : register(t35);

cbuffer LIGHT_CONSTANT_BUFFER : register(b2)
{
    float4 directional_color;   // rgb=色, a=直接光強度
    float4 ibl_params;          // x=diffuse強度, y=specular強度, z=AO強度, w=exposure
    float4 shadow_params;       // x=shadow強度, y=depth_bias, z=filter_radius, w=enable(0/1)
};

cbuffer SHADOW_CONSTANT_BUFFER : register(b3)
{
    row_major float4x4 light_view_projection;
};

float3 unpack_normal_map(float3 N, float3 T, float3 B, float2 uv)
{
    float3 n = pbr_normal_map.Sample(pbr_sampler_linear, uv).xyz;
    n = n * 2.0f - 1.0f;
    return normalize(n.x * T + n.y * B + n.z * N);
}

#endif // __PBR_COMMON_HLSLI__
