// SSR(スクリーンスペース反射)パスが共有する定数とサンプリング補助。
#ifndef __SSR_COMMON_HLSLI__
#define __SSR_COMMON_HLSLI__

#include "../../frame_common.hlsli"

cbuffer SSR_CONSTANT_BUFFER : register(b13)
{
    float4 ssr_params0; // x=max_distance(view), y=thickness, z=stride(pixel), w=max_step
    float4 ssr_params1; // x=max_roughness, y=intensity, z=edge_fade, w=refine_step
    float4 ssr_params2; // x=enable, y=resolve_radius(pixel), z=ray_bias, w=history_valid
    float4 ssr_params3; // x=resolve_tap_count, y=roughness_lod_scale, z/w=予約
    // SSRパス自体の解像度。frame_screen_sizeはフル解像度なので、
    // 半解像度で走るときはこちらでステップ幅やタップ間隔を決める。
    float4 ssr_target_size; // x=w, y=h, z=1/w, w=1/h
};

SamplerState ssr_sampler_point  : register(s0);
SamplerState ssr_sampler_linear : register(s1);

static const float SSR_PI = 3.14159265358979f;

// Hammersley列。GGXの重要度サンプリングに使う低食い違い列。
float2 ssr_hammersley(uint index, uint count)
{
    uint bits = index;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float2(float(index) / float(max(count, 1u)), float(bits) * 2.3283064365386963e-10f);
}

// GGXのNDFに沿って半ベクトルをサンプリングする(可視法線分布ではなくNDF版)。
float3 ssr_importance_sample_ggx(float2 random, float roughness, float3 normal)
{
    float alpha = max(roughness * roughness, 1.0e-3f);

    float phi = 2.0f * SSR_PI * random.x;
    float cos_theta = sqrt((1.0f - random.y) / (1.0f + (alpha * alpha - 1.0f) * random.y));
    float sin_theta = sqrt(saturate(1.0f - cos_theta * cos_theta));

    float3 tangent_space_half = float3(
        sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);

    // 法線を軸にした正規直交基底を作る (Duff らの分岐なし版)。
    float sign_z = normal.z >= 0.0f ? 1.0f : -1.0f;
    float a = -1.0f / (sign_z + normal.z);
    float b = normal.x * normal.y * a;
    float3 tangent = float3(1.0f + sign_z * normal.x * normal.x * a, sign_z * b, -sign_z * normal.x);
    float3 bitangent = float3(b, sign_z + normal.y * normal.y * a, -normal.y);

    return normalize(tangent * tangent_space_half.x +
                     bitangent * tangent_space_half.y +
                     normal * tangent_space_half.z);
}

// 画面端で反射が切れるのを目立たせないためのフェード。
float ssr_screen_edge_fade(float2 uv, float fade_width)
{
    float2 distance_to_edge = min(uv, 1.0f - uv);
    float2 fade = saturate(distance_to_edge / max(fade_width, 1.0e-4f));
    return fade.x * fade.y;
}

#endif
