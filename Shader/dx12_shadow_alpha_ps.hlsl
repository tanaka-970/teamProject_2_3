cbuffer ShadowObjectCB : register(b0)
{
    row_major float4x4 world;
    float4 morph;
    float4 alpha;
};
Texture2D baseTexture : register(t0);
SamplerState materialSampler : register(s0);
struct PSIn
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};
float main(PSIn input) : SV_Depth
{
    if (alpha.x < 0.5f)
        return input.position.z;
    const float sampledAlpha = baseTexture.Sample(materialSampler, input.uv).a * alpha.z;
    clip(sampledAlpha - alpha.y);
    return input.position.z;
}
