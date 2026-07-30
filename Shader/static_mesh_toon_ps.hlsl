// 静的メッシュを段階陰影のToon方式で描くピクセルシェーダー。
#include "static_mesh.hlsli"
#include "toon_common.hlsli"
#include "lights_common.hlsli"
#include "csm_common.hlsli"

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
SamplerState sampler_lin   : register(s1);

float4 main(VS_OUT pin) : SV_TARGET
{
    float4 base = diffuse_map.Sample(sampler_lin, pin.texcoord) * pin.color;

    float3 N = normalize(pin.world_normal.xyz);
    float3 L = normalize(-light_direction.xyz);
    float3 V = normalize(camera_position.xyz - pin.world_position.xyz);
    float3 H = normalize(L + V);

    // タンジェントが無いので適当な接平面ベクトルを作る
    float3 seed = abs(N.y) < 0.999f ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 T = normalize(cross(seed, N));

    float ndotl = dot(N, L);
    float u = toon_ramp_band(ndotl);
    float3 ramp = float3(u, u, u);

    float3 color = toon_shade(base.rgb, ramp,
                              N, L, V, H, T,
                              float3(1.0f, 1.0f, 1.0f),
                              shadow_tint, rim_color, specular_tint,
                              toon_params, specular_params);
    float view_z = mul(float4(pin.world_position.xyz, 1.0f), view_projection).z;
    float shadow = csm_sample_shadow(pin.world_position.xyz, N, view_z);
    color *= shadow * 0.5f + 0.5f;
    color += evaluate_point_lights(pin.world_position.xyz, N, V, base.rgb, 0.55f, 0.0f);
    color += evaluate_spot_lights(pin.world_position.xyz, N, V, base.rgb);
    return float4(saturate(color), base.a);
}
