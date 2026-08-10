// Bloom、ビネット、FXAAをまとめて最終画面へ合成するピクセルシェーダー。
#include "fullscreen_quad.hlsli"

cbuffer POST_CONSTANT_BUFFER : register(b0)
{
    float  exposure;          // 露出
    float  bloom_intensity;   // bloom強度
    float  vignette_strength; // 0=なし, 1=強
    float  fxaa_enable;       // 0/1
    float2 screen_size;       // px
    float2 padding_;
    float4 color_filter;      // PostProcessVolume の色フィルタ
};

SamplerState sampler_states[3] : register(s0);
Texture2D    texture_maps[4]   : register(t0);

#define POINT 0
#define LINEAR 1
#define ANISOTROPIC 2

float3 aces_tonemap(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    x *= 0.6f * exposure;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 sample_l(float2 uv) { return texture_maps[0].Sample(sampler_states[LINEAR], uv).rgb; }
float luma(float3 c)       { return dot(c, float3(0.299f, 0.587f, 0.114f)); }

float3 fxaa(float2 uv)
{
    float2 px = 1.0f / screen_size;
    float3 rgbNW = sample_l(uv + float2(-1, -1) * px);
    float3 rgbNE = sample_l(uv + float2( 1, -1) * px);
    float3 rgbSW = sample_l(uv + float2(-1,  1) * px);
    float3 rgbSE = sample_l(uv + float2( 1,  1) * px);
    float3 rgbM  = sample_l(uv);

    float lNW = luma(rgbNW), lNE = luma(rgbNE);
    float lSW = luma(rgbSW), lSE = luma(rgbSE);
    float lM  = luma(rgbM);

    float2 dir;
    dir.x = -((lNW + lNE) - (lSW + lSE));
    dir.y =  ((lNW + lSW) - (lNE + lSE));

    float reduce = max((lNW + lNE + lSW + lSE) * 0.03125f, 1.0f / 128.0f);
    float rcp_min = 1.0f / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcp_min, -8.0f, 8.0f) * px;

    float3 a = 0.5f * (sample_l(uv + dir * (1.0f / 3.0f - 0.5f)) +
                       sample_l(uv + dir * (2.0f / 3.0f - 0.5f)));
    float3 b = a * 0.5f + 0.25f * (sample_l(uv + dir * -0.5f) +
                                   sample_l(uv + dir *  0.5f));
    float lB = luma(b);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    return (lB < lMin || lB > lMax) ? a : b;
}

float4 main(VS_OUT pin) : SV_TARGET
{
    // 1) HDR + bloom
    float3 hdr   = texture_maps[0].Sample(sampler_states[LINEAR], pin.texcoord).rgb;
    float3 bloom = texture_maps[1].Sample(sampler_states[LINEAR], pin.texcoord).rgb;
    if (fxaa_enable > 0.5f) hdr = fxaa(pin.texcoord);
    float3 color = hdr + bloom * bloom_intensity;

    // 2) FXAA は LDR で行うのが定番だが、ここでは HDR をブレンド前にスムージング

    // 3) ACES
    color = aces_tonemap(color);

    // 4) Vignette (画面端を暗く)
    float2 uvc = pin.texcoord - 0.5f;
    float v = saturate(1.0f - dot(uvc, uvc) * (vignette_strength * 4.0f));
    color *= v;
    color *= color_filter.rgb;

    // 5) Gamma (linear -> sRGB)
    color = pow(max(color, 0.0f), 1.0f / 2.2f);

    return float4(color, 1.0f);
}
