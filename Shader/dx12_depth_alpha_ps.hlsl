cbuffer MaterialCB : register(b2)
{
    float4 baseColor;
    float4 emissiveStrength;
    float4 surfaceParams; // metallic、roughness、AO、alpha cutoff
    float4 renderParams;  // alpha mode、lighting model、receive shadow、予約
};
Texture2D baseTexture : register(t0);
SamplerState materialSampler : register(s0);
struct PSIn
{
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 currentClip : TEXCOORD3;
    float4 previousClip : TEXCOORD4;
    float4 tangent : TEXCOORD5;
};
float main(PSIn input) : SV_Depth
{
    const float alpha = baseTexture.Sample(materialSampler, input.uv).a * baseColor.a;
    clip(alpha - surfaceParams.w);
    return input.position.z;
}
