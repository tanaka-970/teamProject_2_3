// HDR Sceneを最終表示へ変換する共通パス。
// 現フレームのGBufferと前フレームHDR履歴をここで使い、TAA/SSAO/SSRを
// 照明ゲインで代用せず、実際の画面空間入力として合成する。

Texture2D sceneTexture : register(t0);
Texture2D historyTexture : register(t1);
Texture2D sceneDepth : register(t2);
Texture2D sceneVelocity : register(t3);
Texture2D sceneNormal : register(t4);
Texture2D sceneMaterial : register(t5);
Texture2D sceneBaseColor : register(t6);
Texture2D deferredLitTexture : register(t7);
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
    float2 padding;
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
static const float SSR_THICKNESS = 0.55f;

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

float ssao(float2 uv)
{
    const float center = ssaoRaw(uv);
    if (ssaoParams2.x < 0.5f) return center;
    const float sharpness = saturate(ssaoParams2.y);
    if (sharpness >= 0.9999f) return center;
    const float2 pixel = 1.0f / max(screenSize, float2(1.0f, 1.0f));
    const float blurred = (center + ssaoRaw(uv + float2(pixel.x, 0.0f)) +
        ssaoRaw(uv - float2(pixel.x, 0.0f)) + ssaoRaw(uv + float2(0.0f, pixel.y)) +
        ssaoRaw(uv - float2(0.0f, pixel.y))) * 0.2f;
    return lerp(blurred, center, sharpness);
}

float3 screenSpaceReflection(float2 uv, float3 color)
{
    if (historyValid < 0.5f || featureFlags.z < 0.5f) return color;

    const float centerDepth = sceneDepth.SampleLevel(pointSampler, uv, 0).r;
    // 背景（最遠）は反射面ではない。ここを弾くのが今回の主目的。
    if (centerDepth >= 0.999999f) return color;

    const float roughness = saturate(sceneMaterial.SampleLevel(pointSampler, uv, 0).g);
    const bool legacyDefaults = abs(ssrParams0.x - 40.0f) < 0.0001f &&
        abs(ssrParams0.y - 0.4f) < 0.0001f && abs(ssrParams0.z - 3.0f) < 0.0001f &&
        abs(ssrParams0.w - 48.0f) < 0.0001f && abs(ssrParams1.x - 5.0f) < 0.0001f &&
        abs(ssrParams1.y - 0.65f) < 0.0001f && abs(ssrParams1.z - 0.12f) < 0.0001f &&
        abs(ssrParams1.w - 1.0f) < 0.0001f && abs(ssrParams2.x - 12.0f) < 0.0001f &&
        abs(ssrParams2.y - 8.0f) < 0.0001f;
    // 粗い面は反射がぼけて意味を持たないので早期に降りる。
    if (roughness > (legacyDefaults ? 0.6f : saturate(ssrParams1.y))) return color;

    const float4 normalSample = sceneNormal.SampleLevel(pointSampler, uv, 0);
    const float3 worldNormal = normalSample.xyz * 2.0f - 1.0f;
    if (dot(worldNormal, worldNormal) < 1.0e-4f) return color;

    const float3 N = normalize(mul(float4(normalize(worldNormal), 0.0f), viewMatrix).xyz);
    const float3 origin = viewPositionFromDepth(uv, centerDepth);
    const float3 V = normalize(origin);
    const float3 R = reflect(V, N);

    const int maxSteps = legacyDefaults ? 32 :
        clamp((int)round(ssrParams0.w), 4, SSR_MAX_STEPS);
    const int refineSteps = legacyDefaults ? 4 :
        clamp((int)round(ssrParams1.x), 0, SSR_REFINE_STEPS);
    const float farPlane = max(cameraPlanes.y, cameraPlanes.x + 1.0e-3f);
    const float rayLength = legacyDefaults
        ? min(max(length(origin) * 1.5f, 1.0f), farPlane * 0.5f)
        : min(max(ssrParams0.x, 1.0f), farPlane * 0.5f);
    const float baseStepLength = rayLength / (float)maxSteps;
    const float strideScale = legacyDefaults ? 1.0f : max(ssrParams0.z / 3.0f, 0.25f);
    const float stepLength = baseStepLength * strideScale;
    const float3 rayStart = legacyDefaults
        ? origin + N * max(baseStepLength * 0.5f, 1.0e-3f)
        : origin + N * max(ssrParams1.w, 1.0e-3f);

    float2 hitUv = 0.0f;
    float hitWeight = 0.0f;
    float3 previousSample = rayStart;

    [loop] for (int marchIndex = 1; marchIndex <= SSR_MAX_STEPS; ++marchIndex)
    {
        if (marchIndex > maxSteps) break;
        const float3 rayPoint = rayStart + R * min(rayLength,
            stepLength * (float)marchIndex);
        float2 sampleUv = 0.0f;
        float rayDepth = 0.0f;
        // カメラ後方へ回ったレイは打ち切る。
        if (!projectToScreen(rayPoint, sampleUv, rayDepth)) break;
        // 画面外へ出たレイは打ち切る。混ぜないので端で色が飛ばない。
        if (any(sampleUv < 0.0f) || any(sampleUv > 1.0f)) break;

        const float sceneZ = sceneDepth.SampleLevel(pointSampler, sampleUv, 0).r;
        // 背景の上は交差とみなさない。
        if (sceneZ >= 0.999999f) { previousSample = rayPoint; continue; }
        // 深度バッファ値で比較する。まだ面より手前なら交差していない。
        if (rayDepth <= sceneZ) { previousSample = rayPoint; continue; }
        // 面の裏へ深く回り込んだ場合は別の物体なので棄却する。
        const float3 scenePoint = viewPositionFromDepth(sampleUv, sceneZ);
        const float thickness = legacyDefaults ? SSR_THICKNESS : max(ssrParams0.y, 0.001f);
        if (abs(length(rayPoint) - length(scenePoint)) > thickness)
        {
            previousSample = rayPoint;
            continue;
        }

        // 二分探索で交点を詰める。step 幅ぶんの縞を消すため。
        float3 low = previousSample;
        float3 high = rayPoint;
        [loop] for (int refineIndex = 0; refineIndex < SSR_REFINE_STEPS; ++refineIndex)
        {
            if (refineIndex >= refineSteps) break;
            const float3 middle = (low + high) * 0.5f;
            float2 middleUv = 0.0f;
            float middleDepth = 0.0f;
            if (!projectToScreen(middle, middleUv, middleDepth)) break;
            const float middleSceneZ = sceneDepth.SampleLevel(pointSampler, middleUv, 0).r;
            if (middleDepth > middleSceneZ) high = middle;
            else low = middle;
        }
        float refinedDepth = 0.0f;
        if (!projectToScreen(high, hitUv, refinedDepth)) break;
        if (any(hitUv < 0.0f) || any(hitUv > 1.0f)) break;
        hitWeight = 1.0f;
        break;
    }

    // 当たらなかったレイは反射を出さない。
    if (hitWeight <= 0.0f) return color;

    // 画面端は情報が無いのでフェードさせ、不連続にしない。
    const float edgeFade = legacyDefaults ? 0.08f : max(ssrParams1.z, 0.001f);
    const float2 edge = smoothstep(0.0f, edgeFade, hitUv) *
        smoothstep(0.0f, edgeFade, 1.0f - hitUv);
    float confidence = hitWeight * edge.x * edge.y;
    // 粗い面ほど反射を弱める。
    const float maxRoughness = legacyDefaults ? 0.6f : max(ssrParams1.y, 0.001f);
    confidence *= saturate(1.0f - roughness / maxRoughness);
    // 斜め入射ほど強く映る Fresnel 近似。
    confidence *= saturate(0.25f + 0.75f * pow(saturate(1.0f + dot(V, N)), 2.0f));
    confidence = saturate(confidence * saturate(ssrStrength));
    if (confidence <= 0.0f) return color;

    float3 reflection = historyTexture.SampleLevel(sceneSampler, hitUv, 0).rgb;
    if (!legacyDefaults)
    {
        const int tapCount = clamp((int)round(ssrParams2.y), 1, 16);
        const float2 resolvePixel = ssrParams2.x / max(screenSize, float2(1.0f, 1.0f));
        float3 resolveSum = reflection;
        float resolveSamples = 1.0f;
        [loop] for (int tap = 1; tap <= 16; ++tap)
        {
            if (tap >= tapCount) break;
            const float angle = 2.39996323f * (float)tap;
            const float2 offset = float2(cos(angle), sin(angle)) * resolvePixel;
            resolveSum += historyTexture.SampleLevel(sceneSampler, hitUv + offset, 0).rgb;
            resolveSamples += 1.0f;
        }
        reflection = resolveSum / resolveSamples;
    }
    return lerp(color, reflection, confidence);
}

float3 temporalResolve(float2 uv, float3 color)
{
    if (historyValid < 0.5f || featureFlags.x < 0.5f) return color;
    const float2 velocity = sceneVelocity.SampleLevel(pointSampler, uv, 0).rg;
    const float2 historyUv = uv - velocity;
    if (any(historyUv < 0.0f) || any(historyUv > 1.0f)) return color;
    const float motionPixels = length(velocity * screenSize);
    const float motionWeight = saturate(1.0f - motionPixels / max(taaParams0.z, 0.001f));
    const float weight = saturate(taaBlend) * motionWeight;
    const float3 history = historyTexture.SampleLevel(sceneSampler, historyUv, 0).rgb;
    const float2 pixel = 1.0f / max(screenSize, float2(1.0f, 1.0f));
    const float3 neighborA = sampleScene(uv + float2(pixel.x, 0.0f));
    const float3 neighborB = sampleScene(uv - float2(pixel.x, 0.0f));
    const float3 neighborhoodMin = min(color, min(neighborA, neighborB));
    const float3 neighborhoodMax = max(color, max(neighborA, neighborB));
    const float gamma = max(taaParams0.x, 0.001f);
    const bool legacyClamp = abs(gamma - 1.0f) < 0.0001f;
    const float3 expandedMin = legacyClamp ? neighborhoodMin : color -
        (color - neighborhoodMin) * gamma;
    const float3 expandedMax = legacyClamp ? neighborhoodMax : color +
        (neighborhoodMax - color) * gamma;
    float3 resolved = lerp(color, clamp(history, expandedMin, expandedMax), weight);
    if (abs(taaParams0.y - 0.35f) > 0.0001f && taaParams0.y > 0.0f)
    {
        const float3 neighborhood = (neighborA + neighborB) * 0.5f;
        resolved = max(resolved + (resolved - neighborhood) * taaParams0.y * 3.0f, 0.0f);
    }
    return resolved;
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
    const uint output = (uint)(debugOptions.x + 0.5f);
    if (output != 0u)
    {
        float3 debugColor = 0.0f;
        if (output == 1u)
            debugColor = sceneTexture.SampleLevel(sceneSampler, saturate(input.uv), 0).rgb;
        else if (output == 2u)
        {
            const float2 debugPixel = 1.0f / max(screenSize, float2(1.0f, 1.0f));
            debugColor += max(sampleScene(input.uv + debugPixel * float2(-2.0f, 0.0f)) - bloomThreshold, 0.0f);
            debugColor += max(sampleScene(input.uv + debugPixel * float2(2.0f, 0.0f)) - bloomThreshold, 0.0f);
            debugColor += max(sampleScene(input.uv + debugPixel * float2(0.0f, -2.0f)) - bloomThreshold, 0.0f);
            debugColor += max(sampleScene(input.uv + debugPixel * float2(0.0f, 2.0f)) - bloomThreshold, 0.0f);
        }
        else if (output == 3u)
            debugColor = deferredLitTexture.SampleLevel(pointSampler, input.uv, 0).rgb;
        else if (output == 4u)
            debugColor = sceneBaseColor.SampleLevel(pointSampler, input.uv, 0).rgb;
        else if (output == 5u)
            debugColor = sceneNormal.SampleLevel(pointSampler, input.uv, 0).rgb;
        else if (output == 6u)
            debugColor = sceneMaterial.SampleLevel(pointSampler, input.uv, 0).rgb;
        else if (output == 7u)
        {
            const float hardwareDepth = sceneDepth.SampleLevel(sceneSampler, input.uv, 0).r;
            const float nearPlane = max(cameraPlanes.x, 0.001f);
            const float farPlane = max(cameraPlanes.y, nearPlane + 0.001f);
            const float linearDepth = linearViewDepth(input.uv, hardwareDepth);
            const float normalizedDepth = saturate(log2(max(linearDepth, nearPlane) /
                nearPlane) / max(log2(farPlane / nearPlane), 0.001f));
            const float displayDepth = saturate((1.0f - normalizedDepth - 0.3f) * 2.5f);
            debugColor = (hardwareDepth >= 0.999999f ? 0.0f : displayDepth).xxx;
        }
        else if (output == 8u)
            debugColor = featureFlags.y < 0.5f ? 0.5f.xxx : ssao(input.uv).xxx;
        else if (output == 9u)
            debugColor = historyValid < 0.5f || featureFlags.z < 0.5f
                ? sampleScene(input.uv) * 0.25f
                : screenSpaceReflection(input.uv, 0.0f.xxx);
        else if (output == 10u)
        {
            debugColor = deferredLitTexture.SampleLevel(pointSampler, input.uv, 0).rgb;
            if (sceneDepth.SampleLevel(pointSampler, input.uv, 0).r >= 0.999999f)
                debugColor = 1.0f.xxx;
        }

        const float debugDisplayScale = output == 3u ? 0.5f : 1.0f;
        const bool hdrDebug = output == 1u || output == 2u || output == 3u || output == 9u;
        const float3 displayColor = hdrDebug
            ? acesToneMap(max(debugColor * debugDisplayScale, 0.0f))
            : saturate(max(debugColor * debugDisplayScale, 0.0f));
        return float4(pow(displayColor, 1.0f / 2.2f), 1.0f);
    }

    float3 color = fxaaEnabled > 0.5f ? fxaa(input.uv) : sampleScene(input.uv);
    if (featureFlags.y > 0.5f)
        color *= ssao(input.uv);
    color = screenSpaceReflection(input.uv, color);
    color = temporalResolve(input.uv, color);

    // 実SceneのHDR値からBloomを作る。固定の明るさを加算しない。
    const float2 pixel = 1.0f / max(screenSize, float2(1.0f, 1.0f));
    float3 bloom = 0.0f;
    bloom += max(sampleScene(input.uv + pixel * float2(-2.0f, 0.0f)) - bloomThreshold, 0.0f);
    bloom += max(sampleScene(input.uv + pixel * float2(2.0f, 0.0f)) - bloomThreshold, 0.0f);
    bloom += max(sampleScene(input.uv + pixel * float2(0.0f, -2.0f)) - bloomThreshold, 0.0f);
    bloom += max(sampleScene(input.uv + pixel * float2(0.0f, 2.0f)) - bloomThreshold, 0.0f);
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
