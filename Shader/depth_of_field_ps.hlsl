// 深度に応じて鮮明画像とぼかし画像を合成するピクセルシェーダー。
#include "fullscreen_quad.hlsli"

Texture2D    scene_tex   : register(t0);
Texture2D    blur_tex    : register(t1);
Texture2D    depth_tex   : register(t2);
SamplerState sampler_lin : register(s1);

cbuffer DOF_CONSTANTS : register(b0)
{
    float4 dof_params;     // x=focus_depth(0..1), y=focus_range, z=blur_strength, w=enable
    float4 dof_extra;      // x=near_blend, y=far_blend, z/w=unused
};

float circle_of_confusion(float depth)
{
    float focus = dof_params.x;
    float range = max(dof_params.y, 0.0001f);
    float coc = saturate(abs(depth - focus) / range);
    coc = pow(coc, 1.5f);
    return coc;
}

float4 main(VS_OUT pin) : SV_TARGET
{
    float3 sharp = scene_tex.Sample(sampler_lin, pin.texcoord).rgb;
    if (dof_params.w < 0.5f) return float4(sharp, 1.0f);

    float3 blur  = blur_tex.Sample(sampler_lin, pin.texcoord).rgb;
    float  z     = depth_tex.Sample(sampler_lin, pin.texcoord).r;

    float coc = circle_of_confusion(z) * dof_params.z;
    return float4(lerp(sharp, blur, saturate(coc)), 1.0f);
}
