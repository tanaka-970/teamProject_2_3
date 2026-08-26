#include "dx12_lighting_common.hlsli"

Texture2D gBase : register(t0);
Texture2D gEmissive : register(t1);
Texture2D gNormalDepth : register(t2);
Texture2D gMaterial : register(t3);
Texture2D gVelocity : register(t4);
Texture2D gDepth : register(t5);
SamplerState pointSampler : register(s0);

float3 ReconstructWorld(float2 uv, float depth)
{
    const float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    const float4 world = mul(float4(ndc, depth, 1.0f), inverseViewProjection);
    const float safeW = max(abs(world.w), 1.0e-5f) * (world.w < 0.0f ? -1.0f : 1.0f);
    return world.xyz / safeW;
}

float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target0
{
    const float depth = gDepth.SampleLevel(pointSampler, uv, 0).r;
    clip(0.999999f - depth);
    const float4 base = gBase.SampleLevel(pointSampler, uv, 0);
    const float3 emissive = gEmissive.SampleLevel(pointSampler, uv, 0).rgb;
    const float4 normalValue = gNormalDepth.SampleLevel(pointSampler, uv, 0);
    const float3 normal = normalize(normalValue.xyz * 2.0f - 1.0f);
    const bool receiveShadow = normalValue.w >= 0.0f;
    const float4 material = gMaterial.SampleLevel(pointSampler, uv, 0);
    const float3 worldPosition = ReconstructWorld(uv, depth);
    const uint lightingModel = (uint)round(saturate(base.a) * 255.0f);

    const float ambientOcclusion = saturate(material.r);
    const float roughness = clamp(material.g, 0.045f, 1.0f);
    const float metallic = saturate(material.b);
    const float3 lit = Dx12EvaluateLighting(worldPosition, normal, base.rgb,
        metallic, roughness, ambientOcclusion, lightingModel, receiveShadow,
        position.xy);
    // Scene TargetはHDRリニア値を保持し、最終表示パスで一度だけ変換する。
    return float4(lit + emissive, 1.0f);
}
