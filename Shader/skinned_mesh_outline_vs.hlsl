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
    float4 cameraPosition;
    float4 outlineParams;
};

cbuffer LayerCB : register(b7)
{
    float4 layerColor;
    float4 layerParams;
    float4 layerParams2;
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
    float4 vertexColor : COLOR0;
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
    if (layerParams2.x < 0.5f)
    {
        float cameraToVertex = distance(worldPosition.xyz, cameraPosition.xyz);
        cameraToVertex *= max(outlineParams.x, 1.0e-4f);
        cameraToVertex *= 1.0f + vin.vertexColor.g;
        const float baseThickness = max(layerParams.x, 0.0f) * cameraToVertex;
        const float thickness = baseThickness * (vin.vertexColor.a * 2.0f);
        worldPosition.xyz += worldNormal * thickness;
        const float depthOffset = max(layerParams.w, 0.0f) * vin.vertexColor.b;
        worldPosition.xyz += normalize(worldPosition.xyz - cameraPosition.xyz) * depthOffset;
    }
    else
    {
        worldPosition.xyz += worldNormal * max(layerParams.x, 0.0f);
    }
    vout.position = mul(worldPosition, viewProjection);
    vout.color = layerColor;
    return vout;
}
