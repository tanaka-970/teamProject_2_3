// デカール領域内の画素へ投影テクスチャを合成するピクセルシェーダー。
cbuffer DECAL_CONSTANT_BUFFER : register(b7)
{
    row_major float4x4 decal_world;
    row_major float4x4 decal_world_inverse;
    float4             decal_color;
    float4             decal_params;
};
cbuffer SCENE_CONSTANT_BUFFER : register(b1)
{
    row_major float4x4 view_projection;
    float4 light_direction;
    float4 camera_position;
};
cbuffer INV_VP_CB : register(b8)
{
    row_major float4x4 inv_view_projection;
    float4             rt_size; // x=width, y=height
};

Texture2D    decal_tex   : register(t0);
Texture2D    scene_depth : register(t6);
SamplerState sampler_lin : register(s1);

struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 worldpos : TEXCOORD0;
};

float4 main(VS_OUT pin) : SV_TARGET
{
    if (decal_params.x < 0.5f) discard;

    // 画面UV
    float2 uv = pin.position.xy * float2(1.0f / rt_size.x, 1.0f / rt_size.y);
    float z = scene_depth.Sample(sampler_lin, uv).r;

    // NDC → world
    float4 ndc = float4(uv.x * 2 - 1, (1 - uv.y) * 2 - 1, z, 1);
    float4 wp4 = mul(ndc, inv_view_projection);
    float3 wp = wp4.xyz / wp4.w;

    // デカールローカル空間
    float4 lp4 = mul(float4(wp, 1), decal_world_inverse);
    float3 lp = lp4.xyz;
    if (any(abs(lp) > 0.5f)) discard;

    float2 duv = lp.xz + 0.5f;
    float4 d = decal_tex.Sample(sampler_lin, duv);
    return float4(d.rgb * decal_color.rgb, d.a * decal_color.a);
}
