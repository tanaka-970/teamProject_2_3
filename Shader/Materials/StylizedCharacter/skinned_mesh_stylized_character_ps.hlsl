#pragma replay_guid "5a8d351a5d3408c03ec343c561dbe3fa"
// スキンメッシュへスタイライズドキャラクター表現を適用する。
#include "skinned_mesh.hlsli"
#include "stylized_character_common.hlsli"

Texture2D diffuse_map : register(t0);
SamplerState sampler_linear : register(s1);

float4 main(VS_OUT pin) : SV_TARGET
{
    float dispersion = crystal_params.z * crystal_tint.a * 0.004f;
    float4 base = diffuse_map.Sample(sampler_linear, pin.texcoord);
    float red = diffuse_map.Sample(sampler_linear, pin.texcoord + float2(dispersion, 0.0f)).r;
    float blue = diffuse_map.Sample(sampler_linear, pin.texcoord - float2(dispersion, 0.0f)).b;
    base.rgb = lerp(base.rgb, float3(red, base.g, blue), crystal_tint.a);
    float3 color = stylized_character_shade(base.rgb * pin.color.rgb,
        pin.world_normal.xyz, pin.world_tangent.xyz, pin.world_position.xyz);
    float alpha = base.a * pin.color.a * (1.0f - crystal_params.x * crystal_tint.a * 0.65f);
    return float4(color, alpha);
}
