// 深度から距離フォグを計算してシーンへ合成するピクセルシェーダー。
#include "fullscreen_quad.hlsli"

Texture2D    scene_tex   : register(t0);
Texture2D    depth_tex   : register(t1);
SamplerState sampler_lin : register(s1);

cbuffer FOG_CONSTANTS : register(b0)
{
    float4 fog_color;            // rgb=色, a=最大濃度
    float4 fog_params;           // x=density, y=height_falloff, z=height_offset, w=enable(0/1)
    row_major float4x4 inv_view_projection;
    float4 camera_world;         // xyz = カメラのワールド座標
};

float linearize_depth(float z, float n, float f)
{
    // 透視投影: z(0..1) → ビュー空間 z
    return n * f / (f - z * (f - n));
}

float4 main(VS_OUT pin) : SV_TARGET
{
    float3 scene = scene_tex.Sample(sampler_lin, pin.texcoord).rgb;
    if (fog_params.w < 0.5f) return float4(scene, 1.0f);

    float z = depth_tex.Sample(sampler_lin, pin.texcoord).r;
    // NDC → world
    float4 ndc = float4(pin.texcoord.x * 2 - 1, (1 - pin.texcoord.y) * 2 - 1, z, 1);
    float4 wp4 = mul(ndc, inv_view_projection);
    float3 wp  = wp4.xyz / wp4.w;

    float dist = distance(camera_world.xyz, wp);

    // 指数フォグ
    float density = fog_params.x;
    float dist_fog = 1.0f - exp(-density * dist);

    // 高度フォグ
    float h_falloff = fog_params.y;
    float h_offset  = fog_params.z;
    float height_fog = saturate(exp(-(wp.y - h_offset) * h_falloff));

    float fog = saturate(max(dist_fog, height_fog) * fog_color.a);
    return float4(lerp(scene, fog_color.rgb, fog), 1.0f);
}
