cbuffer LayerCB : register(b7)
{
    float4 layerColor;
    float4 layerParams;
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

float4 main(VS_OUT pin) : SV_TARGET
{
    const float opacity = saturate(pin.color.a * layerParams.y);
    if (layerParams.z > 1.5f)
        return float4(lerp(float3(1.0f, 1.0f, 1.0f), pin.color.rgb, opacity), 1.0f);
    if (layerParams.z > 0.5f)
        return float4(pin.color.rgb * opacity, 1.0f);
    return float4(pin.color.rgb, opacity);
}
