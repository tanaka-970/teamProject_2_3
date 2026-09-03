// HDR Sceneを最終表示へ変換する共通パス。
// 現フレームGBuffer、TAA履歴、SSR用Scene履歴を別入力として扱う。

Texture2D sceneTexture : register(t0);
Texture2D historyTexture : register(t1);
Texture2D sceneDepth : register(t2);
Texture2D sceneVelocity : register(t3);
Texture2D sceneNormal : register(t4);
Texture2D sceneMaterial : register(t5);
Texture2D sceneBaseColor : register(t6);
Texture2D deferredLitTexture : register(t7);
Texture2D previousSceneDepth : register(t8);
Texture2D ssrHistoryTexture : register(t9);
// SSAO 専用パスの結果。半解像度で焼いてある。
Texture2D ssaoTexture : register(t10);
SamplerState sceneSampler : register(s0);
SamplerState pointSampler : register(s1);

cbuffer PostProcessConstants : register(b0)
{
    float exposure;
    float bloomIntensity;
    float bloomThreshold;
    float vignetteStrength;
    float fxaaEnabled;
    float taaBlend;
    float ssaoStrength;
    float ssrStrength;
    float historyValid;
    float3 alignmentPadding;
    float2 screenSize;
    float luminanceEnabled;
    float finalPassEnabled;
    float4 colorFilter;
    float4 featureFlags; // x=TAA、y=SSAO、z=SSR
    float4 debugOptions; // x=RenderOutput、y=DeferredDebugMode
    // SSR のレイマーチ用。GBuffer の法線と深度をビュー空間へ戻すのに使う。
    row_major float4x4 viewMatrix;
    row_major float4x4 projectionMatrix;
    row_major float4x4 inverseProjection;
    float4 cameraPlanes; // x=Near、y=Far
    float4 ssaoParams0; // x=半径、y=べき乗、z=薄物補正、w=法線バイアス
    float4 ssaoParams1; // x=スライス数、y=ステップ数、z=フェード開始、w=フェード終了
    float4 ssaoParams2; // x=ブラー有効、y=ブラー鮮明度
    float4 ssrParams0; // x=最大距離、y=厚み、z=ステップ幅、w=最大マーチ数
    float4 ssrParams1; // x=絞り込み回数、y=最大ラフネス、z=端フェード、w=レイバイアス
    float4 ssrParams2; // x=resolve半径、y=resolveタップ数
    float4 taaParams0; // x=分散幅、y=シャープ化、z=速度上限
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

// 全画面で走るので step 数は固定上限にする。理由は実装報告書へ書いた。
static const int SSR_MAX_STEPS = 64;
static const int SSR_REFINE_STEPS = 8;
// 交差とみなすカメラからの距離差。これを超える差は手前の別物として棄却する。

float3 viewPositionFromDepth(float2 uv, float depth)
{
    const float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    const float4 view = mul(float4(ndc, depth, 1.0f), inverseProjection);
    return view.xyz / (abs(view.w) < 1.0e-6f ? 1.0e-6f : view.w);
}

// view 空間の座標を uv と深度へ戻す。w <= 0 はカメラの後ろなので無効。
bool projectToScreen(float3 viewPosition, out float2 uv, out float depth)
{
    const float4 clip = mul(float4(viewPosition, 1.0f), projectionMatrix);
    uv = 0.0f;
    depth = 0.0f;
    if (clip.w <= 1.0e-6f) return false;
    const float3 ndc = clip.xyz / clip.w;
    uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
    depth = ndc.z;
    return true;
}

static const int SSAO_MAX_SLICES = 8;
static const int SSAO_MAX_STEPS = 12;
static const float SSAO_PI = 3.14159265f;

float linearViewDepth(float2 uv, float depth)
{
    const float nearPlane = max(cameraPlanes.x, 0.001f);
    const float farPlane = max(cameraPlanes.y, nearPlane + 0.001f);
    if (depth >= 0.999999f) return farPlane;
    const float denominator = max(farPlane - depth * (farPlane - nearPlane), 0.001f);
    return clamp((nearPlane * farPlane) / denominator, nearPlane, farPlane);
}

float ssaoRaw(float2 uv)
{
    const float hardwareDepth = sceneDepth.SampleLevel(pointSampler, uv, 0).r;
    if (hardwareDepth >= 0.999999f) return 1.0f;

    const float3 centerPosition = viewPositionFromDepth(uv, hardwareDepth);
    const float3 worldNormal = sceneNormal.SampleLevel(pointSampler, uv, 0).xyz * 2.0f - 1.0f;
    if (dot(worldNormal, worldNormal) < 1.0e-4f) return 1.0f;
    const float3 normal = normalize(mul(float4(normalize(worldNormal), 0.0f), viewMatrix).xyz);

    const float radius = max(ssaoParams0.x, 0.001f);
    const float power = max(ssaoParams0.y, 0.01f);
    const float thinOccluder = saturate(ssaoParams0.z);
    const float normalBias = max(ssaoParams0.w, 0.0f) * radius * 0.1f;
    const int sliceCount = clamp((int)round(ssaoParams1.x), 1, SSAO_MAX_SLICES);
    const int stepCount = clamp((int)round(ssaoParams1.y), 2, SSAO_MAX_STEPS);
    const float fadeStart = max(ssaoParams1.z, 0.0f);
    const float fadeEnd = max(ssaoParams1.w, fadeStart + 0.001f);
    const float centerDistance = length(centerPosition);
    const float distanceFade = 1.0f - saturate(
        (centerDistance - fadeStart) / (fadeEnd - fadeStart));
    const float occluderThickness = lerp(0.5f, 0.05f, thinOccluder);
    const float3 tangent = normalize(abs(normal.y) < 0.99f
        ? cross(normal, float3(0.0f, 1.0f, 0.0f))
        : cross(normal, float3(1.0f, 0.0f, 0.0f)));
    const float3 bitangent = normalize(cross(normal, tangent));

    float occlusion = 0.0f;
    float sampleCount = 0.0f;
    [loop] for (int slice = 0; slice < SSAO_MAX_SLICES; ++slice)
    {
        if (slice >= sliceCount) break;
        const float angle = (2.0f * SSAO_PI * ((float)slice + 0.5f)) /
            (float)sliceCount;
        [loop] for (int step = 1; step <= SSAO_MAX_STEPS; ++step)
        {
            if (step > stepCount) break;
            const float sampleScale = (float)step / (float)stepCount;
            const float sampleAngle = angle + SSAO_PI * sampleScale;
            const float3 sampleOffsetDirection = normalize(tangent * cos(sampleAngle) +
                bitangent * sin(sampleAngle));
            const float3 samplePosition = centerPosition +
                sampleOffsetDirection * radius * sampleScale;
            float2 sampleUv = 0.0f;
            float projectedDepth = 0.0f;
            if (!projectToScreen(samplePosition, sampleUv, projectedDepth) ||
                any(sampleUv < 0.0f) || any(sampleUv > 1.0f))
                continue;
            const float sampleHardwareDepth =
                sceneDepth.SampleLevel(pointSampler, sampleUv, 0).r;
            if (sampleHardwareDepth >= 0.999999f) continue;
            const float3 actualSamplePosition =
                viewPositionFromDepth(sampleUv, sampleHardwareDepth);
            const float sampleDepth = linearViewDepth(sampleUv, sampleHardwareDepth);
            const float expectedDepth = linearViewDepth(sampleUv, projectedDepth);
            const float depthDelta = expectedDepth - sampleDepth - normalBias;
            const float depthContribution = saturate(depthDelta / occluderThickness);
            const float distanceWeight = saturate(1.0f -
                distance(centerPosition, actualSamplePosition) / radius);
            const float3 sampleDirection = normalize(actualSamplePosition - centerPosition);
            const float normalWeight = saturate(0.35f + 0.65f *
                abs(dot(normal, sampleDirection)));
            occlusion += depthContribution * distanceWeight * normalWeight;
            sampleCount += 1.0f;
        }
    }

    const float averageOcclusion = occlusion / max(sampleCount, 1.0f);
    const float ambient = pow(saturate(1.0f - averageOcclusion *
        48.0f * saturate(ssaoStrength)), power);
    return lerp(1.0f, ambient, distanceFade);
}
