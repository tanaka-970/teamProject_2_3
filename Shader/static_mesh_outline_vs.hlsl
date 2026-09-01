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

struct VS_IN
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

VS_OUT main(VS_IN vin)
{
    VS_OUT vout;
    float4 worldPosition = mul(float4(vin.position, 1.0f), world);
    float3 worldNormal = normalize(mul(float4(vin.normal, 0.0f), world).xyz);
    worldPosition.xyz += worldNormal * max(layerParams.x, 0.0f);
    vout.position = mul(worldPosition, viewProjection);
    vout.color = layerColor;
    return vout;
}
