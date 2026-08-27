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
    float4 debugOptions; // x=RenderOutput、y=DeferredDebugMode
    // SSR のレイマーチ用。GBuffer の法線と深度をビュー空間へ戻すのに使う。
    row_major float4x4 viewMatrix;
    row_major float4x4 projectionMatrix;
    row_major float4x4 inverseProjection;
    float4 cameraPlanes; // x=Near、y=Far
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

// 全画面で走るので step 数は固定上限にする。理由は実装報告書へ書いた。
static const int SSR_MAX_STEPS = 32;
static const int SSR_REFINE_STEPS = 4;
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

float3 screenSpaceReflection(float2 uv, float3 color)
{
    if (historyValid < 0.5f || featureFlags.z < 0.5f) return color;

    const float centerDepth = sceneDepth.SampleLevel(pointSampler, uv, 0).r;
    // 背景（最遠）は反射面ではない。ここを弾くのが今回の主目的。
    if (centerDepth >= 0.999999f) return color;

    const float roughness = saturate(sceneMaterial.SampleLevel(pointSampler, uv, 0).g);
    // 粗い面は反射がぼけて意味を持たないので早期に降りる。
    if (roughness > 0.6f) return color;

    const float4 normalSample = sceneNormal.SampleLevel(pointSampler, uv, 0);
    const float3 worldNormal = normalSample.xyz * 2.0f - 1.0f;
    if (dot(worldNormal, worldNormal) < 1.0e-4f) return color;

    const float3 N = normalize(mul(float4(normalize(worldNormal), 0.0f), viewMatrix).xyz);
    const float3 origin = viewPositionFromDepth(uv, centerDepth);
    const float3 V = normalize(origin);
    const float3 R = reflect(V, N);

    // レイ長はカメラからの距離に比例させ、Far で頭を打たせる。
    const float farPlane = max(cameraPlanes.y, cameraPlanes.x + 1.0e-3f);
    const float rayLength = min(max(length(origin) * 1.5f, 1.0f), farPlane * 0.5f);
    const float stepLength = rayLength / (float)SSR_MAX_STEPS;
    // 自分自身に当たらないよう法線方向へ 1 step ぶん逃がす。
    const float3 rayStart = origin + N * max(stepLength * 0.5f, 1.0e-3f);

    float2 hitUv = 0.0f;
    float hitWeight = 0.0f;
    float3 previousSample = rayStart;

    [loop] for (int marchIndex = 1; marchIndex <= SSR_MAX_STEPS; ++marchIndex)
    {
        const float3 rayPoint = rayStart + R * (stepLength * (float)marchIndex);
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
        if (abs(length(rayPoint) - length(scenePoint)) > SSR_THICKNESS)
        {
            previousSample = rayPoint;
            continue;
        }

        // 二分探索で交点を詰める。step 幅ぶんの縞を消すため。
        float3 low = previousSample;
        float3 high = rayPoint;
        [unroll] for (int refineIndex = 0; refineIndex < SSR_REFINE_STEPS; ++refineIndex)
        {
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
    const float2 edge = smoothstep(0.0f, 0.08f, hitUv) *
        smoothstep(0.0f, 0.08f, 1.0f - hitUv);
    float confidence = hitWeight * edge.x * edge.y;
    // 粗い面ほど反射を弱める。
    confidence *= saturate(1.0f - roughness / 0.6f);
    // 斜め入射ほど強く映る Fresnel 近似。
    confidence *= saturate(0.25f + 0.75f * pow(saturate(1.0f + dot(V, N)), 2.0f));
    confidence = saturate(confidence * saturate(ssrStrength));
    if (confidence <= 0.0f) return color;

    const float3 reflection = historyTexture.SampleLevel(sceneSampler, hitUv, 0).rgb;
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
    const uint output = (uint)(debugOptions.x + 0.5f);
    if (output != 0u)
    {
        float3 debugColor = 0.0f;
        if (output == 1u)
            debugColor = sceneTexture.SampleLevel(sceneSampler, saturate(input.uv), 0).rgb;
        else if (output == 2u)
        {
            const float2 debugPixel = 1.0f / max(screenSize, float2(1.0f, 1.0f));
            debugColor += max(sampleScene(input.uv + debugPixel * float2(-2.0f, 0.0f)) - 1.0f, 0.0f);
            debugColor += max(sampleScene(input.uv + debugPixel * float2(2.0f, 0.0f)) - 1.0f, 0.0f);
            debugColor += max(sampleScene(input.uv + debugPixel * float2(0.0f, -2.0f)) - 1.0f, 0.0f);
            debugColor += max(sampleScene(input.uv + debugPixel * float2(0.0f, 2.0f)) - 1.0f, 0.0f);
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
            debugColor = (1.0f - sceneDepth.SampleLevel(pointSampler, input.uv, 0).r).xxx;
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

        const bool hdrDebug = output == 1u || output == 2u || output == 3u || output == 9u;
        const float3 displayColor = hdrDebug
            ? acesToneMap(max(debugColor, 0.0f)) : saturate(max(debugColor, 0.0f));
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
