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
struct VSIn { float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
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
VSOut main(VSIn input)
{
    VSOut o;
    float4 local = float4(input.position, 1.0f);
    float4 wp = mul(local, world);
    float4 pwp = mul(local, previousWorld);
    o.currentClip = mul(wp, viewProjection);
    o.previousClip = mul(pwp, previousViewProjection);
    o.position = o.currentClip;
    o.worldPosition = wp.xyz;
    o.normal = normalize(mul(float4(input.normal, 0.0f), world).xyz);
    // static_mesh の既存 ABI は tangent を持たない。PS 側で安定した basis を構築する。
    o.tangent = 0.0f.xxxx;
    o.uv = input.uv;
    return o;
}
