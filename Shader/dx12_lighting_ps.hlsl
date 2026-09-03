#include "dx12_lighting_common.hlsli"
#include "gbuffer_common.hlsli"

Texture2D gBase : register(t0);
Texture2D gEmissive : register(t1);
Texture2D gNormalDepth : register(t2);
Texture2D gMaterial : register(t3);
Texture2D gVelocity : register(t4);
Texture2D gDepth : register(t5);
// 追加GBufferは影配列の t6/t7 を動かさないため t8 へ置く。
Texture2D gToon : register(t8);
SamplerState pointSampler : register(s0);

// GBuffer 側の Dx12PackColor565 と対になる復号。
float3 Dx12UnpackColor565(float packed)
{
    const uint code = (uint)(saturate(packed) * 65535.0f + 0.5f);
    return float3(((code >> 11) & 31u) / 31.0f,
        ((code >> 5) & 63u) / 63.0f, (code & 31u) / 31.0f);
}

// GBuffer 側の Dx12PackTwoBytes と対になる復号。
float2 Dx12UnpackTwoBytes(float packed)
{
    const uint code = (uint)(saturate(packed) * 65535.0f + 0.5f);
    return float2(((code >> 8) & 255u) / 255.0f, (code & 255u) / 255.0f);
}

float3 ReconstructWorld(float2 uv, float depth)
{
    const float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    const float4 world = mul(float4(ndc, depth, 1.0f), inverseViewProjection);
    const float safeW = max(abs(world.w), 1.0e-5f) * (world.w < 0.0f ? -1.0f : 1.0f);
    return world.xyz / safeW;
}

float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target0
{
    float depth = gDepth.SampleLevel(pointSampler, uv, 0).r;
    clip(0.999999f - depth);
    float4 base = gBase.SampleLevel(pointSampler, uv, 0);
    const float4 emissiveValue = gEmissive.SampleLevel(pointSampler, uv, 0);
    float3 emissive = emissiveValue.rgb;
    float4 normalValue = gNormalDepth.SampleLevel(pointSampler, uv, 0);
    float4 material = gMaterial.SampleLevel(pointSampler, uv, 0);
    float4 pixelateSettings = gToon.SampleLevel(pointSampler, uv, 0);
    if (HasPixelateSettings(emissiveValue))
    {
        uint gbufferWidth;
        uint gbufferHeight;
        gBase.GetDimensions(gbufferWidth, gbufferHeight);
        const int2 renderSize = max(int2(gbufferWidth, gbufferHeight), int2(1, 1));
        const float pixelSize = DecodePixelateSize(emissiveValue);
        const float strength = DecodePixelateStrength(normalValue);
        const float opacity = saturate(pixelateSettings.x);
        const float effectStrength = strength * opacity;
        const float2 cellCenter = (floor(position.xy / pixelSize) + 0.5f) * pixelSize;
        const int2 sampledPosition = clamp(int2(lerp(position.xy, cellCenter, effectStrength)),
            int2(0, 0), renderSize - 1);
        const float3 originalBase = base.rgb;
        base = gBase.Load(int3(sampledPosition, 0));
        emissive = gEmissive.Load(int3(sampledPosition, 0)).rgb;
        normalValue = gNormalDepth.Load(int3(sampledPosition, 0));
        material = gMaterial.Load(int3(sampledPosition, 0));
        depth = gDepth.Load(int3(sampledPosition, 0)).r;
        pixelateSettings = gToon.Load(int3(sampledPosition, 0));
        uv = (float2(sampledPosition) + 0.5f) / float2(renderSize);
        if (pixelateSettings.y < 0.5f)
            base.rgb = originalBase;
        else
            base.rgb = lerp(originalBase, base.rgb, opacity);
    }
    const float3 normal = normalize(normalValue.xyz * 2.0f - 1.0f);
    const bool receiveShadow = normalValue.w >= 0.0f;
    const float3 worldPosition = ReconstructWorld(uv, depth);
    if (debugFlags.x == 7u)
    {
        const float3 normal = normalize(normalValue.xyz * 2.0f - 1.0f);
        const float visibility = Dx12EvaluateShadowVisibility(worldPosition, normal,
            normalValue.w >= 0.0f, position.xy);
        return float4(visibility.xxx, 1.0f);
    }
    const uint encodedLightingModel = (uint)round(saturate(base.a) * 255.0f);
    const bool pixelatePayload = (encodedLightingModel & 128u) != 0u;
    const uint lightingModel = encodedLightingModel & 127u;

    const float ambientOcclusion = saturate(material.r);
    const float roughness = clamp(material.g, 0.045f, 1.0f);
    const float metallic = saturate(material.b);
    // Toon のときだけ material.a に階調数が入っている（GBuffer 側で符号化）。
    Dx12ToonSurface toon = Dx12DefaultToonSurface();
    if (lightingModel == 1u && !pixelatePayload)
    {
        const float4 packed = gToon.SampleLevel(pointSampler, uv, 0);
        const float2 powers = Dx12UnpackTwoBytes(packed.w);
        toon.shadowTint = Dx12UnpackColor565(packed.x);
        toon.rimColor = Dx12UnpackColor565(packed.y);
        toon.specularTint = Dx12UnpackColor565(packed.z);
        toon.rimPower = powers.x * 8.0f;
        toon.specularPower = 1.0f + powers.y * 127.0f;
        const uint toonCode = (uint)round(material.a * 255.0f);
        const bool normalized = toonCode >= 128u;
        toon.steps = (float)((toonCode - (normalized ? 128u : 0u)) / 4u);
        toon.normalizedRamp = normalized ? 1.0f : 0.0f;
    }
    const float3 lit = Dx12EvaluateLighting(worldPosition, normal, base.rgb,
        metallic, roughness, ambientOcclusion, lightingModel, receiveShadow,
        position.xy, toon);
    // Scene TargetはHDRリニア値を保持し、最終表示パスで一度だけ変換する。
    return float4(lit + emissive, 1.0f);
}
