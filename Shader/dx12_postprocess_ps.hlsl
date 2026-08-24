Texture2D sceneTexture : register(t0);
SamplerState sceneSampler : register(s0);

cbuffer PostProcessConstants : register(b0)
{
    float exposure;
    float bloomIntensity;
    float vignetteStrength;
    float fxaaEnabled;
    float2 screenSize;
    float2 padding;
    float4 colorFilter;
};

struct PixelInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float3 sampleScene(float2 uv)
{
    return sceneTexture.SampleLevel(sceneSampler, saturate(uv), 0).rgb;
}

float luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 fxaa(float2 uv)
{
    const float2 pixel = 1.0f / max(screenSize, float2(1.0f, 1.0f));
    const float3 nw = sampleScene(uv + pixel * float2(-1.0f, -1.0f));
    const float3 ne = sampleScene(uv + pixel * float2(1.0f, -1.0f));
    const float3 sw = sampleScene(uv + pixel * float2(-1.0f, 1.0f));
    const float3 se = sampleScene(uv + pixel * float2(1.0f, 1.0f));
    const float3 center = sampleScene(uv);
    const float lumaMin = min(luminance(center), min(min(luminance(nw), luminance(ne)),
        min(luminance(sw), luminance(se))));
    const float lumaMax = max(luminance(center), max(max(luminance(nw), luminance(ne)),
        max(luminance(sw), luminance(se))));
    const float3 average = (nw + ne + sw + se + center * 2.0f) / 6.0f;
    const float averageLuma = luminance(average);
    return averageLuma < lumaMin || averageLuma > lumaMax ? center : average;
}

float3 acesToneMap(float3 color)
{
    color *= exp2(exposure);
    const float3 a = color * (color * 2.51f + 0.03f);
    const float3 b = color * (color * 2.43f + 0.59f) + 0.14f;
    return saturate(a / max(b, 0.0001f));
}

float4 main(PixelInput input) : SV_TARGET
{
    float3 color = fxaaEnabled > 0.5f ? fxaa(input.uv) : sampleScene(input.uv);

    // 単一パスの可変半径近似。別の明るさ固定値ではなく、実SceneのHDR値からBloomを作る。
    const float2 pixel = 1.0f / max(screenSize, float2(1.0f, 1.0f));
    float3 bloom = 0.0f;
    bloom += max(sampleScene(input.uv + pixel * float2(-2.0f, 0.0f)) - 1.0f, 0.0f);
    bloom += max(sampleScene(input.uv + pixel * float2(2.0f, 0.0f)) - 1.0f, 0.0f);
    bloom += max(sampleScene(input.uv + pixel * float2(0.0f, -2.0f)) - 1.0f, 0.0f);
    bloom += max(sampleScene(input.uv + pixel * float2(0.0f, 2.0f)) - 1.0f, 0.0f);
    bloom *= 0.25f * bloomIntensity;
    color += bloom;

    color = acesToneMap(max(color, 0.0f));
    if (vignetteStrength > 0.0f)
    {
        const float2 centered = input.uv - 0.5f;
        color *= saturate(1.0f - dot(centered, centered) * vignetteStrength * 4.0f);
    }
    color *= colorFilter.rgb;
    return float4(pow(max(color, 0.0f), 1.0f / 2.2f), 1.0f);
}
