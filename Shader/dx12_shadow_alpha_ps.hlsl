cbuffer ShadowObjectCB : register(b0)
{
    row_major float4x4 world;
    float4 morph;
    float4 alpha;
};
Texture2D baseTexture : register(t0);
SamplerState materialSampler : register(s0);
#define SHADOW_COVERAGE_STANDALONE 1
// s0 は materialSampler が既に取っているので、新規宣言せず同じものを使う。
#define shadow_coverage_sampler materialSampler
#include "shadow_coverage_common.hlsli"
struct PSIn
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
};
float main(PSIn input) : SV_Depth
{
    if (alpha.x >= 0.5f)
    {
        const float sampledAlpha = baseTexture.Sample(materialSampler, input.uv).a * alpha.z;
        clip(sampledAlpha - alpha.y);
    }
    shadow_coverage_clip(input.worldPosition);
    return input.position.z;
}
