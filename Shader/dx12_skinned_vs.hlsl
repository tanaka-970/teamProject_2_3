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
struct BoneMatrix { row_major float4x4 value; };
StructuredBuffer<BoneMatrix> currentBones : register(t8);
StructuredBuffer<BoneMatrix> previousBones : register(t9);
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
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 currentClip : TEXCOORD3;
    float4 previousClip : TEXCOORD4;
    float4 tangent : TEXCOORD5;
};
float4 SkinCurrentPosition(float3 p, float4 w, uint4 i)
{
    float4 v = float4(p, 1.0f);
    return mul(v, currentBones[i.x].value) * w.x + mul(v, currentBones[i.y].value) * w.y +
           mul(v, currentBones[i.z].value) * w.z + mul(v, currentBones[i.w].value) * w.w;
}
float4 SkinPreviousPosition(float3 p, float4 w, uint4 i)
{
    float4 v = float4(p, 1.0f);
    return mul(v, previousBones[i.x].value) * w.x + mul(v, previousBones[i.y].value) * w.y +
           mul(v, previousBones[i.z].value) * w.z + mul(v, previousBones[i.w].value) * w.w;
}
float3 SkinCurrentDirection(float3 p, float4 w, uint4 i)
{
    float4 v = float4(p, 0.0f);
    return (mul(v, currentBones[i.x].value) * w.x + mul(v, currentBones[i.y].value) * w.y +
            mul(v, currentBones[i.z].value) * w.z + mul(v, currentBones[i.w].value) * w.w).xyz;
}
VSOut main(VSIn input)
{
    VSOut o;
    float3 localPosition = input.position + input.morphPosition * morph.x;
    float3 localNormal = input.normal + input.morphNormal * morph.x;
    float4 skinned = SkinCurrentPosition(localPosition, input.weights, input.indices);
    float4 previousSkinned = SkinPreviousPosition(localPosition, input.weights, input.indices);
    float3 skinnedNormal = SkinCurrentDirection(localNormal, input.weights, input.indices);
    float3 skinnedTangent = SkinCurrentDirection(input.tangent.xyz, input.weights, input.indices);
    float4 wp = mul(skinned, world);
    float4 pwp = mul(previousSkinned, previousWorld);
    o.currentClip = mul(wp, viewProjection);
    o.previousClip = mul(pwp, previousViewProjection);
    o.position = o.currentClip;
    o.worldPosition = wp.xyz;
    o.normal = normalize(mul(float4(skinnedNormal, 0.0f), world).xyz);
    o.tangent = float4(normalize(mul(float4(skinnedTangent, 0.0f), world).xyz), input.tangent.w);
    o.uv = input.uv;
    return o;
}
