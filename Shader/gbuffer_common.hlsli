// G-Bufferの格納形式と復元処理を共通定義する。
#ifndef __GBUFFER_COMMON_HLSLI__
#define __GBUFFER_COMMON_HLSLI__

#include "lighting_model_hlsl.hlsli"

struct GBufferOut
{
    float4 base_color : SV_TARGET0;
    float4 emissive   : SV_TARGET1;
    float4 normal     : SV_TARGET2;
    float4 parameter  : SV_TARGET3;
    // TAA用のスクリーン空間移動量(UV単位)。「前フレームUV = 現UV - velocity」。
    float2 velocity   : SV_TARGET4;
};

struct GBufferData
{
    float3 base_color;
    uint   lighting_model;
    float3 emissive;
    float3 world_normal;
    float  occlusion;
    float  roughness;
    float  metalness;
    float  occlusion_strength;
    float2 velocity;
};

GBufferOut EncodeGBuffer(GBufferData d)
{
    GBufferOut o = (GBufferOut) 0;
    // alpha には「シェーダの種類」ではなく照明式だけを 8bit で保存する。
    o.base_color = float4(d.base_color, (float) d.lighting_model / 255.0f);
    o.emissive   = float4(d.emissive, 1.0f);
    o.normal     = float4(d.world_normal * 0.5f + 0.5f, 1.0f);
    o.parameter  = float4(d.occlusion, d.roughness, d.metalness, d.occlusion_strength);
    o.velocity   = d.velocity;
    return o;
}

GBufferData DecodeGBuffer(float4 base, float4 emi, float4 nor, float4 par)
{
    GBufferData d;
    d.base_color     = base.rgb;
    d.lighting_model = (uint) round(base.a * 255.0f);
    d.emissive       = emi.rgb;
    d.world_normal   = normalize(nor.rgb * 2.0f - 1.0f);
    d.occlusion      = par.r;
    d.roughness      = par.g;
    d.metalness      = par.b;
    d.occlusion_strength = par.a;
    return d;
}

// Pixelate は照明式ではないため、base_color.a の lighting_model へ混ぜない。
// emissive.a を負数にして「Pixelate設定あり」を表し、絶対値にセル幅を保存する。
// GBuffer は R16G16B16A16_FLOAT なので負数を保持でき、RGBの見た目は変わらない。
void EncodePixelateSettings(inout GBufferOut output,
    float pixel_size, float pixel_strength)
{
    output.emissive.a = -max(pixel_size, 1.0f);
    output.normal.a = saturate(pixel_strength);
}

bool HasPixelateSettings(float4 emissive_value)
{
    return emissive_value.a < 0.0f;
}

float DecodePixelateSize(float4 emissive_value)
{
    return max(-emissive_value.a, 1.0f);
}

float DecodePixelateStrength(float4 normal_value)
{
    return saturate(normal_value.a);
}

#endif
