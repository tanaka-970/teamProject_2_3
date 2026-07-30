// 静的メッシュへスタイライズドキャラクター表現を適用する。
#include "static_mesh.hlsli"
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
    float3 N = normalize(pin.world_normal.xyz);
    float3 seed = abs(N.y) < 0.999f ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 tangent = normalize(cross(seed, N));
    float3 color = stylized_character_shade(base.rgb * pin.color.rgb,
        N, tangent, pin.world_position.xyz);
    float alpha = base.a * pin.color.a * (1.0f - crystal_params.x * crystal_tint.a * 0.65f);
    return float4(color, alpha);
}
