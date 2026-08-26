// HDR Sceneを最終表示へ変換する共通パス。
// 現フレームのGBufferと前フレームHDR履歴をここで使い、TAA/SSAO/SSRを
// 照明ゲインで代用せず、実際の画面空間入力として合成する。

Texture2D sceneTexture : register(t0);
Texture2D historyTexture : register(t1);
Texture2D sceneDepth : register(t2);
Texture2D sceneVelocity : register(t3);
Texture2D sceneNormal : register(t4);
Texture2D sceneMaterial : register(t5);
SamplerState sceneSampler : register(s0);
SamplerState pointSampler : register(s1);

cbuffer PostProcessConstants : register(b0)
{
    float exposure;
    float bloomIntensity;
    float vignetteStrength;
    float fxaaEnabled;
    float taaBlend;
    float ssaoStrength;
    float ssrStrength;
    float historyValid;
    float2 screenSize;
    float2 padding;
    float4 colorFilter;
    float4 featureFlags; // x=TAA、y=SSAO、z=SSR
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

float ssao(float2 uv)
{
    const float centerDepth = sceneDepth.SampleLevel(pointSampler, uv, 0).r;
    if (centerDepth >= 0.999999f) return 1.0f;
    const float2 pixel = 1.0f / max(screenSize, float2(1.0f, 1.0f));
    const float neighborDepth[4] = {
        sceneDepth.SampleLevel(pointSampler, uv + float2(pixel.x, 0.0f), 0).r,
        sceneDepth.SampleLevel(pointSampler, uv - float2(pixel.x, 0.0f), 0).r,
        sceneDepth.SampleLevel(pointSampler, uv + float2(0.0f, pixel.y), 0).r,
        sceneDepth.SampleLevel(pointSampler, uv - float2(0.0f, pixel.y), 0).r };
    float occlusion = 0.0f;
    [unroll] for (int index = 0; index < 4; ++index)
    {
        const float difference = centerDepth - neighborDepth[index];
        occlusion += saturate(difference * 32.0f) *
            saturate(1.0f - abs(difference) * 48.0f);
    }
    return saturate(1.0f - occlusion * 0.2f * ssaoStrength);
}

float3 screenSpaceReflection(float2 uv, float3 color)
{
    if (historyValid < 0.5f || featureFlags.z < 0.5f) return color;
    const float4 material = sceneMaterial.SampleLevel(pointSampler, uv, 0);
    const float roughness = saturate(material.g);
    const float3 normal = normalize(sceneNormal.SampleLevel(pointSampler, uv, 0).xyz * 2.0f - 1.0f);
    const float2 reflection_offset = normal.xy * (0.018f + 0.032f * (1.0f - roughness)) *
        ssrStrength;
    const float2 reflection_uv = saturate(uv + reflection_offset);
    const float3 reflection = historyTexture.SampleLevel(sceneSampler, reflection_uv, 0).rgb;
    const float confidence = saturate((1.0f - roughness) * 0.35f * ssrStrength);
    return lerp(color, reflection, confidence);
}

float3 temporalResolve(float2 uv, float3 color)
{
    if (historyValid < 0.5f || featureFlags.x < 0.5f) return color;
    const float2 velocity = sceneVelocity.SampleLevel(pointSampler, uv, 0).rg;
    const float2 historyUv = uv - velocity;
    if (any(historyUv < 0.0f) || any(historyUv > 1.0f)) return color;
    const float motionPixels = length(velocity * screenSize);
    const float motionWeight = saturate(1.0f - motionPixels / 48.0f);
    const float weight = saturate(taaBlend) * motionWeight;
    const float3 history = historyTexture.SampleLevel(sceneSampler, historyUv, 0).rgb;
    const float2 pixel = 1.0f / max(screenSize, float2(1.0f, 1.0f));
    const float3 neighborhoodMin = min(color,
        min(sampleScene(uv + float2(pixel.x, 0.0f)),
            sampleScene(uv - float2(pixel.x, 0.0f))));
    const float3 neighborhoodMax = max(color,
        max(sampleScene(uv + float2(pixel.x, 0.0f)),
            sampleScene(uv - float2(pixel.x, 0.0f))));
    return lerp(color, clamp(history, neighborhoodMin, neighborhoodMax), weight);
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
    if (featureFlags.y > 0.5f)
        color *= ssao(input.uv);
    color = screenSpaceReflection(input.uv, color);
    color = temporalResolve(input.uv, color);

    // 実SceneのHDR値からBloomを作る。固定の明るさを加算しない。
    const float2 pixel = 1.0f / max(screenSize, float2(1.0f, 1.0f));
    float3 bloom = 0.0f;
    bloom += max(sampleScene(input.uv + pixel * float2(-2.0f, 0.0f)) - 1.0f, 0.0f);
    bloom += max(sampleScene(input.uv + pixel * float2(2.0f, 0.0f)) - 1.0f, 0.0f);
    bloom += max(sampleScene(input.uv + pixel * float2(0.0f, -2.0f)) - 1.0f, 0.0f);
    bloom += max(sampleScene(input.uv + pixel * float2(0.0f, 2.0f)) - 1.0f, 0.0f);
    color += bloom * (0.25f * bloomIntensity);

    color = acesToneMap(max(color, 0.0f));
    if (vignetteStrength > 0.0f)
    {
        const float2 centered = input.uv - 0.5f;
        color *= saturate(1.0f - dot(centered, centered) * vignetteStrength * 4.0f);
    }
    color *= colorFilter.rgb;
    return float4(pow(max(color, 0.0f), 1.0f / 2.2f), 1.0f);
}
