#include "dx12_postprocess_common.hlsli"


// SSAO は半解像度で焼いてある。線形補間だけで引き伸ばすと、深度の段差をまたいで
// 混ざり、輪郭の周りに滲みが出る。深度が近い画素だけを重みづけして拾う。
float ssao(float2 uv)
{
    const float centerDepth = linearViewDepth(uv,
        sceneDepth.SampleLevel(pointSampler, uv, 0).r);
    const float2 halfPixel = 2.0f / max(screenSize, float2(1.0f, 1.0f));
    // 深度の許容幅。近景は厳しく、遠景は緩く。段差だけを弾きたい。
    const float depthTolerance = max(0.02f, centerDepth * 0.02f);

    float total = 0.0f;
    float weightSum = 0.0f;
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            const float2 tapUv = uv + float2(x, y) * halfPixel;
            const float tapDepth = linearViewDepth(tapUv,
                sceneDepth.SampleLevel(pointSampler, tapUv, 0).r);
            const float depthWeight =
                saturate(1.0f - abs(tapDepth - centerDepth) / depthTolerance);
            if (depthWeight <= 0.0f) continue;
            total += ssaoTexture.SampleLevel(sceneSampler, tapUv, 0).r * depthWeight;
            weightSum += depthWeight;
        }
    }
    if (weightSum <= 0.0f)
        return ssaoTexture.SampleLevel(pointSampler, uv, 0).r;
    return total / weightSum;
}

float3 screenSpaceReflection(float2 uv, float3 color)
{
    if (historyValid < 0.5f || featureFlags.z < 0.5f) return color;

    const float centerDepth = sceneDepth.SampleLevel(pointSampler, uv, 0).r;
    // 背景（最遠）は反射面ではない。ここを弾くのが今回の主目的。
    if (centerDepth >= 0.999999f) return color;

    const float roughness = saturate(sceneMaterial.SampleLevel(pointSampler, uv, 0).g);
    // 粗い面は反射がぼけて意味を持たないので早期に降りる。
    if (roughness > saturate(ssrParams1.y)) return color;

    const float4 normalSample = sceneNormal.SampleLevel(pointSampler, uv, 0);
    const float3 worldNormal = normalSample.xyz * 2.0f - 1.0f;
    if (dot(worldNormal, worldNormal) < 1.0e-4f) return color;

    const float3 N = normalize(mul(float4(normalize(worldNormal), 0.0f), viewMatrix).xyz);
    const float3 origin = viewPositionFromDepth(uv, centerDepth);
    const float3 V = normalize(origin);
    const float3 R = reflect(V, N);

    const int maxSteps = clamp((int)round(ssrParams0.w), 4, SSR_MAX_STEPS);
    const int refineSteps = clamp((int)round(ssrParams1.x), 0, SSR_REFINE_STEPS);
    const float farPlane = max(cameraPlanes.y, cameraPlanes.x + 1.0e-3f);
    const float rayLength = min(min(max(length(origin) * 1.5f, 1.0f),
        max(ssrParams0.x, 1.0f)), farPlane * 0.5f);
    const float baseStepLength = rayLength / (float)maxSteps;
    const float strideScale = max(ssrParams0.z / 3.0f, 0.25f);
    const float stepLength = baseStepLength * strideScale;
    const float3 rayStart = origin + N * max(baseStepLength * 0.5f,
        ssrParams1.w);

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
        const float thickness = max(ssrParams0.y, 0.001f);
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
    const float edgeFade = max(ssrParams1.z, 0.001f);
    const float2 edge = smoothstep(0.0f, edgeFade, hitUv) *
        smoothstep(0.0f, edgeFade, 1.0f - hitUv);
    float confidence = hitWeight * edge.x * edge.y;
    // 粗い面ほど反射を弱める。
    const float maxRoughness = max(ssrParams1.y, 0.001f);
    confidence *= saturate(1.0f - roughness / maxRoughness);
    // 金属でない面は正面でおよそ 4% しか映らない。ここを見ずに 25% 下駄を履かせると、
    // 肌や布に周りの暗い部分が映り込んで、硬い縁のゴミになる。
    const float metallic = saturate(sceneMaterial.SampleLevel(pointSampler, uv, 0).b);
    const float f0 = lerp(0.04f, 1.0f, metallic);
    const float grazing = saturate(1.0f + dot(V, N));
    confidence *= saturate(f0 + (1.0f - f0) * pow(grazing, 5.0f));
    confidence = saturate(confidence * saturate(ssrStrength));
    if (confidence <= 0.0f) return color;

    float3 reflection = ssrHistoryTexture.SampleLevel(sceneSampler, hitUv, 0).rgb;
    const int requestedTapCount = clamp((int)round(ssrParams2.y), 1, 16);
    const int tapCount = min(16, max(requestedTapCount,
        ssrParams2.x > 0.0f ? 2 : 1));
    const float resolveRadius = max(ssrParams2.x,
        requestedTapCount > 1 ? 1.0f : 0.0f);
    const float2 resolvePixel = resolveRadius / max(screenSize, float2(1.0f, 1.0f));
    float3 resolveSum = reflection;
    float resolveSamples = 1.0f;
    [loop] for (int tap = 1; tap <= 16; ++tap)
    {
        if (tap >= tapCount) break;
        const float angle = 2.39996323f * (float)tap;
        const float2 offset = float2(cos(angle), sin(angle)) * resolvePixel;
        resolveSum += ssrHistoryTexture.SampleLevel(sceneSampler, hitUv + offset, 0).rgb;
        resolveSamples += 1.0f;
    }
    reflection = resolveSum / resolveSamples;
    return lerp(color, reflection, confidence);
}

float3 rgbToYCoCg(float3 color)
{
    return float3(
        0.25f * color.r + 0.5f * color.g + 0.25f * color.b,
        0.5f * color.r - 0.5f * color.b,
        -0.25f * color.r + 0.5f * color.g - 0.25f * color.b);
}

float3 yCoCgToRgb(float3 color)
{
    const float y = color.x;
    const float co = color.y;
    const float cg = color.z;
    return float3(y + co - cg, y + cg, y - co - cg);
}

float3 temporalResolve(float2 uv, float3 color)
{
    if (historyValid < 0.5f || featureFlags.x < 0.5f) return color;
    const int2 maxPixel = max(int2(screenSize) - 1, int2(0, 0));
    const int2 pixel = clamp(int2(uv * screenSize), int2(0, 0), maxPixel);
    float closestDepth = sceneDepth.Load(int3(pixel, 0)).r;
    int2 velocityPixel = pixel;
    [unroll] for (int oy = -1; oy <= 1; ++oy)
    {
        [unroll] for (int ox = -1; ox <= 1; ++ox)
        {
            const int2 tapPixel = clamp(pixel + int2(ox, oy), int2(0, 0), maxPixel);
            const float tapDepth = sceneDepth.Load(int3(tapPixel, 0)).r;
            if (tapDepth < closestDepth)
            {
                closestDepth = tapDepth;
                velocityPixel = tapPixel;
            }
        }
    }

    const float2 velocity = sceneVelocity.Load(int3(velocityPixel, 0)).rg;
    const float2 historyUv = uv - velocity;
    if (any(historyUv < 0.0f) || any(historyUv > 1.0f)) return color;

    const float currentDepth = sceneDepth.Load(int3(pixel, 0)).r;
    const float previousDepth = previousSceneDepth.SampleLevel(pointSampler, historyUv, 0).r;
    const bool currentBackground = currentDepth >= 0.999999f;
    const bool previousBackground = previousDepth >= 0.999999f;
    if (currentBackground != previousBackground) return color;
    if (!currentBackground)
    {
        const float currentLinearDepth = linearViewDepth(uv, currentDepth);
        const float previousLinearDepth = linearViewDepth(historyUv, previousDepth);
        const float disocclusionThreshold = max(0.05f, currentLinearDepth * 0.05f);
        if (abs(previousLinearDepth - currentLinearDepth) > disocclusionThreshold) return color;
    }

    const float motionPixels = length(velocity * screenSize);
    const float motionWeight = saturate(1.0f - motionPixels / max(taaParams0.z, 0.001f));
    const float weight = saturate(taaBlend) * motionWeight;
    float3 moment1 = 0.0f;
    float3 moment2 = 0.0f;
    float3 neighborhoodMin = 1.0e30f;
    float3 neighborhoodMax = -1.0e30f;
    [unroll] for (int ny = -1; ny <= 1; ++ny)
    {
        [unroll] for (int nx = -1; nx <= 1; ++nx)
        {
            const int2 tapPixel = clamp(pixel + int2(nx, ny), int2(0, 0), maxPixel);
            const float3 tapColor = rgbToYCoCg(sceneTexture.Load(int3(tapPixel, 0)).rgb);
            moment1 += tapColor;
            moment2 += tapColor * tapColor;
            neighborhoodMin = min(neighborhoodMin, tapColor);
            neighborhoodMax = max(neighborhoodMax, tapColor);
        }
    }
    const float3 mean = moment1 / 9.0f;
    const float3 variance = max(moment2 / 9.0f - mean * mean, 0.0f);
    const float3 deviation = sqrt(variance) * max(taaParams0.x, 0.001f);
    const float3 clipMin = max(mean - deviation, neighborhoodMin);
    const float3 clipMax = min(mean + deviation, neighborhoodMax);
    float3 history = rgbToYCoCg(historyTexture.SampleLevel(sceneSampler, historyUv, 0).rgb);
    const float3 clipCenter = 0.5f * (clipMax + clipMin);
    const float3 clipExtent = 0.5f * (clipMax - clipMin) + 1.0e-6f;
    const float3 clipOffset = history - clipCenter;
    const float3 clipUnit = clipOffset / clipExtent;
    const float maximumUnit = max(abs(clipUnit.x), max(abs(clipUnit.y), abs(clipUnit.z)));
    if (maximumUnit > 1.0f) history = clipCenter + clipOffset / maximumUnit;

    float3 resolved = yCoCgToRgb(lerp(rgbToYCoCg(color), history, weight));
    if (taaParams0.y > 0.0f)
    {
        const float3 neighborhood = yCoCgToRgb(mean);
        resolved = max(resolved + (resolved - neighborhood) * taaParams0.y * 2.0f, 0.0f);
    }
    return max(resolved, 0.0f);
}

float4 temporal_input_main(PixelInput input) : SV_TARGET
{
    float3 color = sampleScene(input.uv);
    if (finalPassEnabled > 0.5f)
    {
        color = fxaaEnabled > 0.5f ? fxaa(input.uv) : color;
        if (featureFlags.y > 0.5f) color *= ssao(input.uv);
        color = screenSpaceReflection(input.uv, color);
    }
    return float4(max(color, 0.0f), 1.0f);
}

float4 taa_resolve_main(PixelInput input) : SV_TARGET
{
    float3 color = sampleScene(input.uv);
    if (finalPassEnabled > 0.5f) color = temporalResolve(input.uv, color);
    return float4(max(color, 0.0f), 1.0f);
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
            if (luminanceEnabled > 0.5f)
            {
                debugColor += max(sampleScene(input.uv + debugPixel * float2(-2.0f, 0.0f)) - bloomThreshold, 0.0f);
                debugColor += max(sampleScene(input.uv + debugPixel * float2(2.0f, 0.0f)) - bloomThreshold, 0.0f);
                debugColor += max(sampleScene(input.uv + debugPixel * float2(0.0f, -2.0f)) - bloomThreshold, 0.0f);
                debugColor += max(sampleScene(input.uv + debugPixel * float2(0.0f, 2.0f)) - bloomThreshold, 0.0f);
            }
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

    float3 color = sampleScene(input.uv);
    if (finalPassEnabled > 0.5f)
    {
        const float2 pixel = 1.0f / max(screenSize, float2(1.0f, 1.0f));
        float3 bloom = 0.0f;
        if (luminanceEnabled > 0.5f)
        {
            bloom += max(sampleScene(input.uv + pixel * float2(-2.0f, 0.0f)) - bloomThreshold, 0.0f);
            bloom += max(sampleScene(input.uv + pixel * float2(2.0f, 0.0f)) - bloomThreshold, 0.0f);
            bloom += max(sampleScene(input.uv + pixel * float2(0.0f, -2.0f)) - bloomThreshold, 0.0f);
            bloom += max(sampleScene(input.uv + pixel * float2(0.0f, 2.0f)) - bloomThreshold, 0.0f);
        }
        color += bloom * (0.25f * bloomIntensity);

        color = acesToneMap(max(color, 0.0f));
        if (vignetteStrength > 0.0f)
        {
            const float2 centered = input.uv - 0.5f;
            color *= saturate(1.0f - dot(centered, centered) * vignetteStrength * 4.0f);
        }
        color *= colorFilter.rgb;
    }
    else
    {
        color = acesToneMap(max(color, 0.0f));
    }
    return float4(pow(max(color, 0.0f), 1.0f / 2.2f), 1.0f);
}
