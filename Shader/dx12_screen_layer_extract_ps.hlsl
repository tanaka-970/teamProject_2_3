Texture2D sceneTexture : register(t0);
Texture2D layerTexture : register(t1);
SamplerState uiSampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float4 uvBounds : TEXCOORD1;
};

float4 main(PSInput input) : SV_TARGET
{
    const float4 scene = sceneTexture.Sample(uiSampler, input.uv);
    const float4 layer = layerTexture.Sample(uiSampler, input.uv);
    return float4(scene.rgb, saturate(layer.a));
}
