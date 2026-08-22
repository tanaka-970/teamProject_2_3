// 静的メッシュを段階陰影のToon方式で描くピクセルシェーダー。
#include "static_mesh.hlsli"
#include "toon_common.hlsli"
#include "lights_common.hlsli"
#include "csm_common.hlsli"

#ifndef REPLAY_MATERIAL_PROPERTIES
cbuffer TOON_MATERIAL_CONSTANTS : register(b6)
{
    float4 shadow_tint;
    float4 rim_color;
    float4 specular_tint;
    float4 toon_params;
    float4 specular_params;
};

Texture2D    diffuse_map   : register(t0);
Texture2D    ramp_map      : register(t1);
#endif
SamplerState sampler_lin   : register(s1);

float4 main(VS_OUT pin) : SV_TARGET
{
#ifdef REPLAY_MATERIAL_PROPERTIES
    float4 base = BaseMap.Sample(sampler_lin, pin.texcoord) * BaseColor * pin.color;
    float4 replay_shadow_tint = ShadowTint;
    float4 replay_rim_color = RimColor;
    float4 replay_specular_tint = SpecularTint;
    float4 replay_toon_params = float4(RimPower, 0.35f, 1.0f, 0.0f);
    float4 replay_specular_params = float4(SpecularPower, 0.55f, 1.0f, 0.0f);
#else
    float4 base = diffuse_map.Sample(sampler_lin, pin.texcoord) * pin.color;
    float4 replay_shadow_tint = shadow_tint;
    float4 replay_rim_color = rim_color;
    float4 replay_specular_tint = specular_tint;
    float4 replay_toon_params = toon_params;
    float4 replay_specular_params = specular_params;
#endif

    float3 N = normalize(pin.world_normal.xyz);
    float3 L = normalize(-light_direction.xyz);
    float3 V = normalize(camera_position.xyz - pin.world_position.xyz);
    float3 H = normalize(L + V);

    // タンジェントが無いので適当な接平面ベクトルを作る
    float3 seed = abs(N.y) < 0.999f ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 T = normalize(cross(seed, N));

    float ndotl = dot(N, L);
#ifdef REPLAY_MATERIAL_PROPERTIES
    float steps = max(StepCount, 1.0f);
    float u = floor(saturate(ndotl * 0.5f + 0.5f) * steps) / max(steps - 1.0f, 1.0f);
    float3 ramp = float3(u, u, u) * RampMap.Sample(sampler_lin, float2(u, 0.5f)).rgb;
#else
    float u = toon_ramp_band(ndotl);
    float3 ramp = float3(u, u, u);
#endif

    float3 color = toon_shade(base.rgb, ramp,
                              N, L, V, H, T,
                              float3(1.0f, 1.0f, 1.0f),
                              replay_shadow_tint, replay_rim_color, replay_specular_tint,
                              replay_toon_params, replay_specular_params);
    float view_z = mul(float4(pin.world_position.xyz, 1.0f), view_projection).z;
    float shadow = csm_sample_shadow(pin.world_position.xyz, N, view_z);
    color *= shadow * 0.5f + 0.5f;
    // 前方描画には GBuffer が無く Receive Shadow を運べないため、
    // 従来どおり常に影を受ける扱いにする。Receive Shadow を尊重するのは
    // Deferred 経路 (deferred_lighting_ps / tiled_deferred_lighting_cs)。
    color += evaluate_point_lights(pin.world_position.xyz, N, V, base.rgb,
        0.55f, 0.0f, 1.0f);
    color += evaluate_spot_lights(pin.world_position.xyz, N, V, base.rgb, 1.0f);
    return float4(saturate(color), base.a);
}
