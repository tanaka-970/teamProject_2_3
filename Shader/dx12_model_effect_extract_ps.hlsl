Texture2D<float4> sceneColorTexture : register(t11);

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 currentClip : TEXCOORD3;
    float4 previousClip : TEXCOORD4;
    float4 tangent : TEXCOORD5;
};

float4 main(VSOutput input) : SV_TARGET
{
    return float4(sceneColorTexture.Load(int3((int2)input.position.xy, 0)).rgb, 1.0f);
}
