#include "dx12_lighting_common.hlsli"

struct SkyOutput
{
    float4 color : SV_Target0;
    float2 velocity : SV_Target1;
};

float2 SkyUvToNdc(float2 uv)
{
    return float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
}

float3 SkyDirection(float2 uv)
{
    const float4 farWorld = mul(float4(SkyUvToNdc(uv), 1.0f, 1.0f),
        inverseViewProjection);
    const float safeW = max(abs(farWorld.w), 1.0e-5f) *
        (farWorld.w < 0.0f ? -1.0f : 1.0f);
    return normalize(farWorld.xyz / safeW - cameraPosition.xyz);
}

float2 SkyVelocity(float2 uv, float3 direction)
{
    const float4 previousClip = mul(float4(direction, 0.0f), previousViewProjection);
    const float safeW = max(abs(previousClip.w), 1.0e-5f) *
        (previousClip.w < 0.0f ? -1.0f : 1.0f);
    float2 currentNdc = SkyUvToNdc(uv);
    const float2 previousNdc = previousClip.xy / safeW;
    currentNdc -= skyJitter.xy;
    const float2 correctedPreviousNdc = previousNdc - skyJitter.zw;
    return (currentNdc - correctedPreviousNdc) * float2(0.5f, -0.5f);
}

SkyOutput main(float4 position : SV_POSITION, float2 uv : TEXCOORD0)
{
    SkyOutput output;
    const float3 direction = SkyDirection(uv);
    const float3 lookupDirection = normalize(mul(direction, (float3x3)skyRotation));
    output.color = float4(dx12SkySource.SampleLevel(dx12IblSampler,
        lookupDirection, 0).rgb * max(iblParams.w, 0.0f), 1.0f);
    output.velocity = SkyVelocity(uv, direction);
    return output;
}
