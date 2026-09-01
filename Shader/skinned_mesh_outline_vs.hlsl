cbuffer ObjectCB : register(b0)
{
    row_major float4x4 world;
    row_major float4x4 previousWorld;
    float4 morph;
};

cbuffer SceneCB : register(b1)
{
    row_major float4x4 viewProjection;
    row_major float4x4 previousViewProjection;
};

cbuffer LayerCB : register(b7)
{
    float4 layerColor;
    float4 layerParams;
};

struct BoneMatrix
{
    row_major float4x4 value;
};

StructuredBuffer<BoneMatrix> currentBones : register(t8);

struct VS_IN
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texcoord : TEXCOORD0;
    float4 weights : BLENDWEIGHT;
    uint4 indices : BLENDINDICES;
    float3 morphPosition : MORPHPOSITION;
    float3 morphNormal : MORPHNORMAL;
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

float4 SkinPosition(float3 position, float4 weights, uint4 indices)
{
    const float4 local = float4(position, 1.0f);
    return mul(local, currentBones[indices.x].value) * weights.x +
        mul(local, currentBones[indices.y].value) * weights.y +
        mul(local, currentBones[indices.z].value) * weights.z +
        mul(local, currentBones[indices.w].value) * weights.w;
}

float3 SkinDirection(float3 direction, float4 weights, uint4 indices)
{
    const float4 local = float4(direction, 0.0f);
    return (mul(local, currentBones[indices.x].value) * weights.x +
        mul(local, currentBones[indices.y].value) * weights.y +
        mul(local, currentBones[indices.z].value) * weights.z +
        mul(local, currentBones[indices.w].value) * weights.w).xyz;
}

VS_OUT main(VS_IN vin)
{
    VS_OUT vout;
    const float3 localPosition = vin.position + vin.morphPosition * morph.x;
    const float3 localNormal = vin.normal + vin.morphNormal * morph.x;
    const float4 skinnedPosition = SkinPosition(localPosition, vin.weights, vin.indices);
    const float3 skinnedNormal = SkinDirection(localNormal, vin.weights, vin.indices);
    float4 worldPosition = mul(skinnedPosition, world);
    const float3 worldNormal = normalize(mul(float4(skinnedNormal, 0.0f), world).xyz);
    worldPosition.xyz += worldNormal * max(layerParams.x, 0.0f);
    vout.position = mul(worldPosition, viewProjection);
    vout.color = layerColor;
    return vout;
}
