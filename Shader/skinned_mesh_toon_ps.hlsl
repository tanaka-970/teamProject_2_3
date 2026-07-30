// スキンメッシュを段階陰影と輪郭向けのToon方式で描くピクセルシェーダー。
#include "skinned_mesh.hlsli"
#include "toon_common.hlsli"
#include "lights_common.hlsli"
#include "csm_common.hlsli"

cbuffer TOON_MATERIAL_CONSTANTS : register(b6)
{
    float4 shadow_tint;        // rgb=影色 a=強度
    float4 rim_color;          // rgb=リム色 a=強度
    float4 specular_tint;      // rgb=ハイライト a=強度
    float4 toon_params;        // x=rimPow y=rimThresh z=rimInt w=未使用
    float4 specular_params;    // x=specPow y=threshold z=intensity w=anisotropy
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
    float3 T = normalize(pin.world_tangent.xyz);
    T = normalize(T - N * dot(N, T));

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
