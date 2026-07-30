// 画像を縮小しながらガウシアンぼかしを行うピクセルシェーダー。
#include "fullscreen_quad.hlsli"

Texture2D    src_tex     : register(t0);
SamplerState sampler_lin : register(s1);

cbuffer BLUR_CONSTANTS : register(b0)
{
    float2 src_texel_size; // 1/srcWidth, 1/srcHeight
    float2 padding_;
};

float3 powerful_mean(float3 a, float3 b, float3 c, float3 d)
{
    // Karis 平均: 輝度の高すぎる火星を抑える
    float la = dot(a, float3(0.2126f, 0.7152f, 0.0722f));
    float lb = dot(b, float3(0.2126f, 0.7152f, 0.0722f));
    float lc = dot(c, float3(0.2126f, 0.7152f, 0.0722f));
    float ld = dot(d, float3(0.2126f, 0.7152f, 0.0722f));
    float wa = 1.0f / (1.0f + la);
    float wb = 1.0f / (1.0f + lb);
    float wc = 1.0f / (1.0f + lc);
    float wd = 1.0f / (1.0f + ld);
    return (a*wa + b*wb + c*wc + d*wd) / (wa+wb+wc+wd);
}

float4 main(VS_OUT pin) : SV_TARGET
{
    float2 uv = pin.texcoord;
    float2 t  = src_texel_size;

    // 13 tap (中心 + ring1 + ring2)
    float3 a = src_tex.Sample(sampler_lin, uv + float2(-2,-2) * t).rgb;
    float3 b = src_tex.Sample(sampler_lin, uv + float2( 0,-2) * t).rgb;
    float3 c = src_tex.Sample(sampler_lin, uv + float2( 2,-2) * t).rgb;
    float3 d = src_tex.Sample(sampler_lin, uv + float2(-2, 0) * t).rgb;
    float3 e = src_tex.Sample(sampler_lin, uv                ).rgb;
    float3 f = src_tex.Sample(sampler_lin, uv + float2( 2, 0) * t).rgb;
    float3 g = src_tex.Sample(sampler_lin, uv + float2(-2, 2) * t).rgb;
    float3 h = src_tex.Sample(sampler_lin, uv + float2( 0, 2) * t).rgb;
    float3 i = src_tex.Sample(sampler_lin, uv + float2( 2, 2) * t).rgb;
    float3 j = src_tex.Sample(sampler_lin, uv + float2(-1,-1) * t).rgb;
    float3 k = src_tex.Sample(sampler_lin, uv + float2( 1,-1) * t).rgb;
    float3 l = src_tex.Sample(sampler_lin, uv + float2(-1, 1) * t).rgb;
    float3 m = src_tex.Sample(sampler_lin, uv + float2( 1, 1) * t).rgb;

    float3 sum  = e * 0.125f;
    sum += (a + c + g + i) * 0.03125f;
    sum += (b + d + f + h) * 0.0625f;
    sum += powerful_mean(j, k, l, m) * 0.5f;
    return float4(sum, 1.0f);
}
