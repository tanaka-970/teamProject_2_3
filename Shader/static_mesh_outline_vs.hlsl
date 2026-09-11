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

struct VS_IN
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 vertexColor : COLOR0;
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
    const float3 worldNormal = normalize(mul(float4(vin.normal, 0.0f), world).xyz);
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
