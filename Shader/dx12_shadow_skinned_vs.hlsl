cbuffer ShadowObjectCB : register(b0)
{
    row_major float4x4 world;
    float4 morph;
    float4 alpha;
};
cbuffer ShadowPassCB : register(b1)
{
    row_major float4x4 viewProjection;
};
struct BoneMatrix
{
    row_major float4x4 value;
};
StructuredBuffer<BoneMatrix> currentBones : register(t8);
struct VSIn
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv : TEXCOORD0;
    float4 weights : BLENDWEIGHT;
    uint4 indices : BLENDINDICES;
    float3 morphPosition : MORPHPOSITION;
    float3 morphNormal : MORPHNORMAL;
};
struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
};
float4 SkinPosition(float3 position, float4 weights, uint4 indices)
{
    const float4 value = float4(position, 1.0f);
    return mul(value, currentBones[indices.x].value) * weights.x +
           mul(value, currentBones[indices.y].value) * weights.y +
           mul(value, currentBones[indices.z].value) * weights.z +
           mul(value, currentBones[indices.w].value) * weights.w;
}
VSOut main(VSIn input)
{
    VSOut output;
    const float3 localPosition = input.position + input.morphPosition * morph.x;
    const float4 skinnedPosition = SkinPosition(localPosition, input.weights, input.indices);
    const float4 worldPosition = mul(skinnedPosition, world);
    output.position = mul(worldPosition, viewProjection);
    output.uv = input.uv;
    output.worldPosition = worldPosition.xyz;
    return output;
}
