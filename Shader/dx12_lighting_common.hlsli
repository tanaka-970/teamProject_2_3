#ifndef __DX12_LIGHTING_COMMON_HLSLI__
#define __DX12_LIGHTING_COMMON_HLSLI__

#ifndef DX12_LIGHT_CB_REGISTER
#define DX12_LIGHT_CB_REGISTER b0
#endif

#define DX12_CSM_CASCADE_COUNT 4
#define DX12_CSM_POISSON_TAP_COUNT 16
#define DX12_CSM_BLOCKER_TAP_COUNT 8
#define DX12_LOCAL_SHADOW_SLICE_COUNT 16

struct Dx12PointLight
{
    float4 positionRange;
    float4 colorIntensity;
    float4 shadow; // x=開始Slice、y=強度、z=有効、w=予約
};

struct Dx12SpotLight
{
    float4 positionRange;
    float4 directionInner;
    float4 colorOuter;
    float4 params; // x=強度、y=Slice、z=Shadow強度、w=予約
};

struct Dx12LocalShadowSlice
{
    row_major float4x4 viewProjection;
    float4 params; // x=Near、y=Far、z=Depth Bias、w=予約
};

cbuffer Dx12LightCB : register(DX12_LIGHT_CB_REGISTER)
{
    row_major float4x4 inverseViewProjection;
    row_major float4x4 viewMatrix;
    float4 cameraPosition;
    float4 directionalDirectionIntensity;
    float4 directionalColorFlags;
    Dx12PointLight pointLights[8];
    Dx12SpotLight spotLights[4];
    uint4 lightCounts;
    row_major float4x4 csmViewProjection[DX12_CSM_CASCADE_COUNT];
    float4 csmSplitDistances;
    float4 csmParams;  // x=Depth Bias、y=Normal Bias、z=Filter半径、w=有効
    float4 csmParams2; // x=Mapサイズ、y=Cascade Blend、z=Light Size UV、w=PCSS有効
    float4 csmParams3; // x=Slope Bias倍率、y=最大Bias、z=強度、w=Tap倍率
    float4 csmTexelWorld;
    Dx12LocalShadowSlice localShadowSlices[DX12_LOCAL_SHADOW_SLICE_COUNT];
    uint4 shadowFlags; // x=CSM利用可能、y=Local Shadow利用可能
};

Texture2DArray<float> dx12CsmShadowArray : register(t6);
Texture2DArray<float> dx12LocalShadowArray : register(t7);
SamplerComparisonState dx12ShadowSampler : register(s1);
SamplerState dx12ShadowPointSampler : register(s2);

static const float2 DX12_CSM_POISSON_DISK[DX12_CSM_POISSON_TAP_COUNT] =
{
    float2(-0.94201624f, -0.39906216f), float2( 0.94558609f, -0.76890725f),
    float2(-0.09418410f, -0.92938870f), float2( 0.34495938f,  0.29387760f),
    float2(-0.91588581f,  0.45771432f), float2(-0.81544232f, -0.87912464f),
    float2(-0.38277543f,  0.27676845f), float2( 0.97484398f,  0.75648379f),
    float2( 0.44323325f, -0.97511554f), float2( 0.53742981f, -0.47373420f),
    float2(-0.26496911f, -0.41893023f), float2( 0.79197514f,  0.19090188f),
    float2(-0.24188840f,  0.99706507f), float2(-0.81409955f,  0.91437590f),
    float2( 0.19984126f,  0.78641367f), float2( 0.14383161f, -0.14100790f)
};

float Dx12InterleavedGradientNoise(float2 pixelPosition)
{
    const float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
    return frac(magic.z * frac(dot(pixelPosition, magic.xy)));
}

float3 Dx12FresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (1.0f - f0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

float Dx12DistributionGgx(float3 normal, float3 halfVector, float roughness)
{
    const float alpha = max(roughness * roughness, 0.0025f);
    const float alpha2 = alpha * alpha;
    const float noH = saturate(dot(normal, halfVector));
    const float denominator = noH * noH * (alpha2 - 1.0f) + 1.0f;
    return alpha2 / max(3.14159265f * denominator * denominator, 1.0e-5f);
}

float Dx12GeometrySchlickGgx(float noV, float roughness)
{
    const float r = roughness + 1.0f;
    const float k = (r * r) * 0.125f;
    return noV / max(noV * (1.0f - k) + k, 1.0e-5f);
}

float3 Dx12EvaluatePbr(float3 albedo, float metallic, float roughness,
    float3 normal, float3 viewDirection, float3 lightDirection,
    float3 radiance, float noL)
{
    const float3 halfVector = normalize(viewDirection + lightDirection);
    const float noV = max(saturate(dot(normal, viewDirection)), 1.0e-4f);
    const float3 f0 = lerp(0.04f.xxx, albedo, saturate(metallic));
    const float3 fresnel = Dx12FresnelSchlick(saturate(dot(halfVector, viewDirection)), f0);
    const float distribution = Dx12DistributionGgx(normal, halfVector, roughness);
    const float geometry = Dx12GeometrySchlickGgx(noV, roughness) *
        Dx12GeometrySchlickGgx(max(noL, 1.0e-4f), roughness);
    const float3 specular = (distribution * geometry * fresnel) /
        max(4.0f * noV * max(noL, 1.0e-4f), 1.0e-4f);
    const float3 diffuseWeight = (1.0f - fresnel) * (1.0f - saturate(metallic));
    return (diffuseWeight * albedo / 3.14159265f + specular) * radiance * noL;
}

int Dx12PickCascade(float viewZ)
{
    int index = 0;
    [unroll] for (int i = 0; i < DX12_CSM_CASCADE_COUNT - 1; ++i)
    {
        if (viewZ > csmSplitDistances[i]) index = i + 1;
    }
    return index;
}

float3 Dx12CsmLightForward(int cascade)
{
    return normalize(float3(csmViewProjection[cascade][0][2],
        csmViewProjection[cascade][1][2], csmViewProjection[cascade][2][2]));
}

float4 Dx12ProjectCsm(float3 worldPosition, float3 worldNormal,
    int cascade, float noL)
{
    const float texelWorld = max(csmTexelWorld[cascade], 1.0e-4f);
    const float slope = saturate(1.0f - noL);
    const float normalOffset = csmParams.y * texelWorld * (1.0f + slope * 2.0f);
    float depthBiasWorld = csmParams.x * (1.0f + slope * csmParams3.x);
    depthBiasWorld = min(depthBiasWorld, max(csmParams3.y, csmParams.x));

    const float3 biasedPosition = worldPosition + worldNormal * normalOffset -
        Dx12CsmLightForward(cascade) * depthBiasWorld;
    float4 lightPosition = mul(float4(biasedPosition, 1.0f), csmViewProjection[cascade]);
    const float safeW = max(abs(lightPosition.w), 1.0e-6f) *
        (lightPosition.w < 0.0f ? -1.0f : 1.0f);
    lightPosition.xyz /= safeW;
    const float2 uv = float2(lightPosition.x * 0.5f + 0.5f,
        -lightPosition.y * 0.5f + 0.5f);
    const bool inside = !any(uv < 0.0f) && !any(uv > 1.0f) &&
        lightPosition.z > 0.0f && lightPosition.z < 1.0f;
    return float4(uv, lightPosition.z, inside ? 1.0f : 0.0f);
}

float Dx12EstimatePenumbra(float2 uv, float referenceDepth, int cascade, float rotation)
{
    const float searchRadius = max(csmParams2.z, 1.0e-4f);
    float sinRotation;
    float cosRotation;
    sincos(rotation, sinRotation, cosRotation);
    float blockerSum = 0.0f;
    float blockerCount = 0.0f;

    [unroll] for (int i = 0; i < DX12_CSM_BLOCKER_TAP_COUNT; ++i)
    {
        const float2 tap = DX12_CSM_POISSON_DISK[i];
        const float2 rotated = float2(tap.x * cosRotation - tap.y * sinRotation,
            tap.x * sinRotation + tap.y * cosRotation);
        const float2 sampleUv = uv + rotated * searchRadius;
        if (any(sampleUv < 0.0f) || any(sampleUv > 1.0f)) continue;
        const float depth = dx12CsmShadowArray.SampleLevel(dx12ShadowPointSampler,
            float3(sampleUv, cascade), 0);
        if (depth < referenceDepth)
        {
            blockerSum += depth;
            blockerCount += 1.0f;
        }
    }

    if (blockerCount < 0.5f) return 0.0f;
    const float averageBlockerDepth = blockerSum / blockerCount;
    const float penumbra = (referenceDepth - averageBlockerDepth) /
        max(averageBlockerDepth, 1.0e-5f);
    return clamp(penumbra * csmParams2.z * csmParams2.x, 0.5f, 24.0f);
}

float Dx12FilterCsm(float2 uv, float referenceDepth, int cascade,
    float filterRadiusTexels, float rotation)
{
    const float texel = 1.0f / max(csmParams2.x, 1.0f);
    const float2 radius = filterRadiusTexels * texel;
    float sinRotation;
    float cosRotation;
    sincos(rotation, sinRotation, cosRotation);
    float visibility = 0.0f;

    [unroll] for (int i = 0; i < DX12_CSM_POISSON_TAP_COUNT; ++i)
    {
        const float2 tap = DX12_CSM_POISSON_DISK[i];
        const float2 rotated = float2(tap.x * cosRotation - tap.y * sinRotation,
            tap.x * sinRotation + tap.y * cosRotation);
        visibility += dx12CsmShadowArray.SampleCmpLevelZero(dx12ShadowSampler,
            float3(uv + rotated * radius, cascade), referenceDepth);
    }
    return visibility / (float)DX12_CSM_POISSON_TAP_COUNT;
}

float Dx12SampleCsmCascade(float3 worldPosition, float3 worldNormal,
    int cascade, float noL, float rotation)
{
    const float4 projected = Dx12ProjectCsm(worldPosition, worldNormal, cascade, noL);
    if (projected.w < 0.5f) return 1.0f;

    float filterRadius = max(csmParams.z, 0.5f) * max(csmParams3.w, 0.1f);
    if (csmParams2.w >= 0.5f)
    {
        const float penumbra = Dx12EstimatePenumbra(projected.xy, projected.z,
            cascade, rotation);
        if (penumbra <= 0.0f) return 1.0f;
        filterRadius = clamp(penumbra, max(filterRadius, 1.5f), 24.0f);
    }
    return Dx12FilterCsm(projected.xy, projected.z, cascade, filterRadius, rotation);
}

float Dx12SampleCsm(float3 worldPosition, float3 worldNormal, float noL, float2 noiseCoord)
{
    if (shadowFlags.x == 0u || csmParams.w < 0.5f) return 1.0f;
    const float viewZ = mul(float4(worldPosition, 1.0f), viewMatrix).z;
    const int cascade = Dx12PickCascade(viewZ);
    const float rotation = Dx12InterleavedGradientNoise(noiseCoord) * 6.28318530718f;
    float visibility = Dx12SampleCsmCascade(worldPosition, worldNormal,
        cascade, noL, rotation);

    const float blendWidth = max(csmParams2.y, 0.0f);
    if (blendWidth > 0.0f && cascade < DX12_CSM_CASCADE_COUNT - 1)
    {
        const float split = csmSplitDistances[cascade];
        const float blendStart = split - blendWidth;
        if (viewZ > blendStart)
        {
            const float blend = saturate((viewZ - blendStart) / max(blendWidth, 1.0e-4f));
            const float nextVisibility = Dx12SampleCsmCascade(worldPosition, worldNormal,
                cascade + 1, noL, rotation);
            visibility = lerp(visibility, nextVisibility, blend);
        }
    }
    return lerp(1.0f, visibility, saturate(csmParams3.z));
}

int Dx12SelectPointFace(float3 lightToSurface)
{
    const float3 magnitude = abs(lightToSurface);
    if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z)
        return lightToSurface.x > 0.0f ? 0 : 1;
    if (magnitude.y >= magnitude.z)
        return lightToSurface.y > 0.0f ? 2 : 3;
    return lightToSurface.z > 0.0f ? 4 : 5;
}

float Dx12SampleLocalSlice(int slice, float3 worldPosition, float3 worldNormal,
    float lightDistance, float noL)
{
    if (shadowFlags.y == 0u || slice < 0 || slice >= DX12_LOCAL_SHADOW_SLICE_COUNT)
        return 1.0f;

    const Dx12LocalShadowSlice source = localShadowSlices[slice];
    uint width;
    uint height;
    uint elementCount;
    uint mipCount;
    dx12LocalShadowArray.GetDimensions(0, width, height, elementCount, mipCount);
    const float mapSize = max((float)width, 1.0f);
    const float texelWorld = lightDistance * 2.0f / mapSize;
    const float slope = saturate(1.0f - noL);
    const float3 lightForward = normalize(float3(source.viewProjection[0][2],
        source.viewProjection[1][2], source.viewProjection[2][2]));
    const float3 biasedPosition = worldPosition + worldNormal * texelWorld *
        (1.5f + slope * 3.0f) - lightForward * source.params.z * (1.0f + slope * 2.0f);

    const float4 lightPosition = mul(float4(biasedPosition, 1.0f), source.viewProjection);
    if (lightPosition.w <= 0.0f) return 1.0f;
    const float3 ndc = lightPosition.xyz / lightPosition.w;
    const float2 uv = float2(ndc.x * 0.5f + 0.5f, -ndc.y * 0.5f + 0.5f);
    if (any(uv < 0.0f) || any(uv > 1.0f) || ndc.z <= 0.0f || ndc.z >= 1.0f)
        return 1.0f;

    const float texel = 1.0f / mapSize;
    float visibility = 0.0f;
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            visibility += dx12LocalShadowArray.SampleCmpLevelZero(dx12ShadowSampler,
                float3(uv + float2(x, y) * texel, slice), ndc.z);
        }
    }
    return visibility / 9.0f;
}

float Dx12PointShadow(Dx12PointLight light, float3 worldPosition,
    float3 worldNormal, float noL)
{
    const int baseSlice = (int)round(light.shadow.x);
    if (light.shadow.z < 0.5f || baseSlice < 0) return 1.0f;
    const float3 lightToSurface = worldPosition - light.positionRange.xyz;
    const int slice = baseSlice + Dx12SelectPointFace(lightToSurface);
    const float visibility = Dx12SampleLocalSlice(slice, worldPosition, worldNormal,
        length(lightToSurface), noL);
    return lerp(1.0f, visibility, saturate(light.shadow.y));
}

float Dx12SpotShadow(Dx12SpotLight light, float3 worldPosition,
    float3 worldNormal, float lightDistance, float noL)
{
    const int slice = (int)round(light.params.y);
    if (slice < 0) return 1.0f;
    const float visibility = Dx12SampleLocalSlice(slice, worldPosition, worldNormal,
        lightDistance, noL);
    return lerp(1.0f, visibility, saturate(light.params.z));
}

// Toon 固有値の受け渡し。GBuffer の追加 RT から復号した値がそのまま入る。
struct Dx12ToonSurface
{
    float3 shadowTint;
    float3 rimColor;
    float rimPower;
    float3 specularTint;
    float specularPower;
    float steps;
};

// 既定は全項目オフ。Toon 以外と Forward はこれを渡すので絵が変わらない。
Dx12ToonSurface Dx12DefaultToonSurface()
{
    Dx12ToonSurface toon;
    toon.shadowTint = float3(0.0f, 0.0f, 0.0f);
    toon.rimColor = float3(0.0f, 0.0f, 0.0f);
    toon.rimPower = 1.0f;
    toon.specularTint = float3(0.0f, 0.0f, 0.0f);
    toon.specularPower = 32.0f;
    toon.steps = 0.0f;
    return toon;
}

// トゥーンのハイライトは階調と同じく硬い境界にする。tint が黒なら消える。
float3 Dx12ToonSpecular(Dx12ToonSurface toon, float3 N, float3 V, float3 L)
{
    const float3 H = normalize(L + V);
    const float raw = pow(saturate(dot(N, H)), max(toon.specularPower, 1.0f));
    return toon.specularTint * smoothstep(0.45f, 0.55f, raw);
}

// リムは視線と法線の角度だけで決まるので光源ループの外で 1 回だけ足す。
float3 Dx12ToonRim(Dx12ToonSurface toon, float3 N, float3 V)
{
    return toon.rimColor * pow(saturate(1.0f - saturate(dot(N, V))),
        max(toon.rimPower, 0.01f));
}

// steps <= 0 なら従来の 3 段固定。1 以上なら Material の階調数で量子化する。
float Dx12ToonNoL(float noL, float steps)
{
    if (steps < 1.0f)
        return noL > 0.5f ? 1.0f : (noL > 0.15f ? 0.45f : 0.08f);
    const float levels = max(floor(steps + 0.5f), 1.0f);
    if (levels < 2.0f) return noL > 0.5f ? 1.0f : 0.08f;
    return saturate(floor(saturate(noL) * levels) / (levels - 1.0f));
}

float3 Dx12EvaluateLighting(float3 worldPosition, float3 normal, float3 albedo,
    float metallic, float roughness, float ambientOcclusion, uint lightingModel,
    bool receiveShadow, float2 noiseCoord, Dx12ToonSurface toon)
{
    if (lightingModel >= 2u) return albedo;

    const bool isToon = lightingModel == 1u;
    const float toonSteps = toon.steps;
    const float3 N = normalize(normal);
    const float3 V = normalize(cameraPosition.xyz - worldPosition);
    float3 result = albedo * 0.035f * saturate(ambientOcclusion);
    // Toon の影側の色付けに使う。光が届いた度合いの最大値。
    float toonLit = 0.0f;

    if (directionalColorFlags.w > 0.5f)
    {
        const float3 L = normalize(-directionalDirectionIntensity.xyz);
        const float physicalNoL = saturate(dot(N, L));
        const float shadedNoL = lightingModel == 1u ? Dx12ToonNoL(physicalNoL, toonSteps) : physicalNoL;
        const float visibility = receiveShadow ?
            Dx12SampleCsm(worldPosition, N, physicalNoL, noiseCoord) : 1.0f;
        const float3 lightColor =
            directionalColorFlags.rgb * directionalDirectionIntensity.w;
        result += Dx12EvaluatePbr(albedo, metallic, roughness, N, V, L,
            lightColor, shadedNoL) * visibility;
        if (isToon)
        {
            result += Dx12ToonSpecular(toon, N, V, L) * lightColor * visibility;
            toonLit = max(toonLit, shadedNoL * visibility);
        }
    }

    [loop] for (uint i = 0; i < min(lightCounts.x, 8u); ++i)
    {
        const float3 delta = pointLights[i].positionRange.xyz - worldPosition;
        const float distanceToLight = length(delta);
        const float range = max(pointLights[i].positionRange.w, 1.0e-3f);
        const float3 L = delta / max(distanceToLight, 1.0e-5f);
        float attenuation = saturate(1.0f - distanceToLight / range);
        attenuation *= attenuation;
        const float physicalNoL = saturate(dot(N, L));
        const float shadedNoL = lightingModel == 1u ? Dx12ToonNoL(physicalNoL, toonSteps) : physicalNoL;
        const float visibility = receiveShadow ?
            Dx12PointShadow(pointLights[i], worldPosition, N, physicalNoL) : 1.0f;
        const float3 lightColor =
            pointLights[i].colorIntensity.rgb * pointLights[i].colorIntensity.w * attenuation;
        result += Dx12EvaluatePbr(albedo, metallic, roughness, N, V, L,
            lightColor, shadedNoL) * visibility;
        if (isToon)
        {
            result += Dx12ToonSpecular(toon, N, V, L) * lightColor * visibility;
            toonLit = max(toonLit, shadedNoL * visibility * attenuation);
        }
    }

    [loop] for (uint i = 0; i < min(lightCounts.y, 4u); ++i)
    {
        const float3 delta = spotLights[i].positionRange.xyz - worldPosition;
        const float distanceToLight = length(delta);
        const float range = max(spotLights[i].positionRange.w, 1.0e-3f);
        const float3 L = delta / max(distanceToLight, 1.0e-5f);
        const float cone = dot(normalize(-spotLights[i].directionInner.xyz), L);
        const float coneAttenuation = saturate((cone - spotLights[i].colorOuter.w) /
            max(spotLights[i].directionInner.w - spotLights[i].colorOuter.w, 1.0e-4f));
        float attenuation = saturate(1.0f - distanceToLight / range);
        attenuation *= attenuation * coneAttenuation;
        const float physicalNoL = saturate(dot(N, L));
        const float shadedNoL = lightingModel == 1u ? Dx12ToonNoL(physicalNoL, toonSteps) : physicalNoL;
        const float visibility = receiveShadow ?
            Dx12SpotShadow(spotLights[i], worldPosition, N, distanceToLight, physicalNoL) : 1.0f;
        const float3 lightColor =
            spotLights[i].colorOuter.rgb * spotLights[i].params.x * attenuation;
        result += Dx12EvaluatePbr(albedo, metallic, roughness, N, V, L,
            lightColor, shadedNoL) * visibility;
        if (isToon)
        {
            result += Dx12ToonSpecular(toon, N, V, L) * lightColor * visibility;
            toonLit = max(toonLit, shadedNoL * visibility * attenuation);
        }
    }

    if (isToon)
    {
        // 影側の色。tint が黒なら 1 項も足されず従来の絵と一致する。
        result += albedo * toon.shadowTint * saturate(1.0f - toonLit) *
            saturate(ambientOcclusion);
        result += Dx12ToonRim(toon, N, V);
    }
    return result;
}

float3 Dx12EncodeDisplayColor(float3 linearColor)
{
    // 線形照明をLDR swap chainへ書く前に表示エンコードする。
    // 露出・ACES・BloomはPostProcess移行時に独立した最終パスへ接続する。
    return pow(saturate(max(linearColor, 0.0f)), 1.0f / 2.2f);
}

#endif
