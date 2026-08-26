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
struct VSIn
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};
struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
};
VSOut main(VSIn input)
{
    VSOut output;
    const float4 worldPosition = mul(float4(input.position, 1.0f), world);
    output.position = mul(worldPosition, viewProjection);
    output.uv = input.uv;
    output.worldPosition = worldPosition.xyz;
    return output;
}
