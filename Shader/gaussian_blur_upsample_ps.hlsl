// 縮小画像を拡大しながらぼかし結果を合成するピクセルシェーダー。
#include "fullscreen_quad.hlsli"

Texture2D    src_tex     : register(t0);
SamplerState sampler_lin : register(s1);

cbuffer BLUR_CONSTANTS : register(b0)
{
    float2 src_texel_size; // 1/srcWidth, 1/srcHeight (lower mip)
    float2 padding_;
};

float4 main(VS_OUT pin) : SV_TARGET
{
    float2 uv = pin.texcoord;
    float2 t  = src_texel_size;

    float3 sum = (float3)0;
    sum += src_tex.Sample(sampler_lin, uv + float2(-1, -1) * t).rgb;
    sum += src_tex.Sample(sampler_lin, uv + float2( 0, -1) * t).rgb * 2.0f;
    sum += src_tex.Sample(sampler_lin, uv + float2( 1, -1) * t).rgb;
    sum += src_tex.Sample(sampler_lin, uv + float2(-1,  0) * t).rgb * 2.0f;
    sum += src_tex.Sample(sampler_lin, uv                  ).rgb * 4.0f;
    sum += src_tex.Sample(sampler_lin, uv + float2( 1,  0) * t).rgb * 2.0f;
    sum += src_tex.Sample(sampler_lin, uv + float2(-1,  1) * t).rgb;
    sum += src_tex.Sample(sampler_lin, uv + float2( 0,  1) * t).rgb * 2.0f;
    sum += src_tex.Sample(sampler_lin, uv + float2( 1,  1) * t).rgb;
    sum *= (1.0f / 16.0f);
    return float4(sum, 1.0f);
}
