// G-Bufferを読み、光源と材質から最終照明色を計算するピクセルシェーダー。
#include "fullscreen_quad.hlsli"
#include "gbuffer_common.hlsli"
#include "toon_common.hlsli"
#include "csm_common.hlsli"
#include "lights_common.hlsli"

cbuffer SCENE_CONSTANT_BUFFER : register(b1)
{
    row_major float4x4 view_projection;
    float4 light_direction;
    float4 camera_position;
};

cbuffer DEFERRED_CB : register(b8)
{
    row_major float4x4 inv_view_projection;
    float4             rt_size;
};

cbuffer TOON_MATERIAL_CONSTANTS : register(b6)
{
    float4 shadow_tint;
    float4 rim_color;
    float4 specular_tint;
    float4 toon_params;
    float4 specular_params;
};

Texture2D gb_base   : register(t0);
Texture2D gb_emi    : register(t1);
Texture2D gb_normal : register(t2);
Texture2D gb_param  : register(t3);
Texture2D gb_depth  : register(t6);

// PBR評価はシーン定数を参照するため、カメラと平行光源の宣言後に読み込む。
#include "pbr_brdf.hlsli"

float3 reconstruct_world(float2 uv, float z)
{
    float4 ndc = float4(uv.x * 2 - 1, (1 - uv.y) * 2 - 1, z, 1);
    float4 wp4 = mul(ndc, inv_view_projection);
    return wp4.xyz / wp4.w;
}

float4 main(VS_OUT pin) : SV_TARGET
{
    int2 render_size = max(int2(rt_size.xy), int2(1, 1));
    int2 pixel_position = clamp(int2(pin.position.xy), int2(0, 0), render_size - 1);
    float4 base = gb_base.Load(int3(pixel_position, 0));
    float4 emi  = gb_emi.Load(int3(pixel_position, 0));
    float4 nor  = gb_normal.Load(int3(pixel_position, 0));
    float4 par  = gb_param.Load(int3(pixel_position, 0));
    float  z    = gb_depth.Load(int3(pixel_position, 0)).r;

    uint original_shading_model = (uint) round(base.a * 255.0f);
    float2 sampled_uv = pin.texcoord;
    if (original_shading_model == SHADING_MODEL_PIXELATE)
    {
        float pixel_size = max(emi.a, 1.0f);
        float strength = saturate(nor.a);
        float2 cell_center = (floor(pin.position.xy / pixel_size) + 0.5f) * pixel_size;
        int2 sampled_position = clamp(int2(lerp(pin.position.xy, cell_center, strength)),
            int2(0, 0), render_size - 1);
        base = gb_base.Load(int3(sampled_position, 0));
        emi = gb_emi.Load(int3(sampled_position, 0));
        nor = gb_normal.Load(int3(sampled_position, 0));
        par = gb_param.Load(int3(sampled_position, 0));
        z = gb_depth.Load(int3(sampled_position, 0)).r;
        sampled_uv = (float2(sampled_position) + 0.5f) / rt_size.xy;
    }

    if (z >= 0.9999f) return float4(base.rgb, 1); // background

    GBufferData g = DecodeGBuffer(base, emi, nor, par);
    float3 wp = reconstruct_world(sampled_uv, z);

    int debug_mode = (int) round(rt_size.z);
    if (debug_mode == 1) return float4(g.base_color, 1);
    if (debug_mode == 2) return float4(nor.rgb, 1);
    if (debug_mode == 3) return float4(par.rgb, 1);
    if (debug_mode == 4)
    {
        float dz = saturate((1.0f - z) * 250.0f);
        return float4(dz, dz, dz, 1);
    }

    float3 N = g.world_normal;
    float3 L = normalize(-light_direction.xyz);
    float3 V = normalize(camera_position.xyz - wp);
    float3 H = normalize(L + V);
    float3 direct_light = directional_color.rgb * max(directional_color.a, 0.0f);
    float ambient_strength = 0.04f + ibl_params.x * 0.08f;

    float3 color = 0;
    if (g.shading_model == SHADING_MODEL_UNLIT)
    {
        color = g.base_color + g.emissive;
    }
    else if (g.shading_model == SHADING_MODEL_TOON)
    {
        float ndl = dot(N, L);
        float u = toon_ramp_band(ndl);
        float3 ramp = float3(u, u, u);
        float3 T = normalize(cross(abs(N.y) < 0.99f ? float3(0,1,0) : float3(1,0,0), N));
        color = toon_shade(g.base_color, ramp, N, L, V, H, T,
                           max(direct_light, float3(0.15f, 0.15f, 0.15f)),
                           shadow_tint,
                           rim_color,
                           specular_tint,
                           toon_params,
                           specular_params);
        color += g.base_color * ambient_strength * 0.5f;
        color += evaluate_point_lights(wp, N, V, g.base_color, g.roughness, g.metalness);
        color += evaluate_spot_lights(wp, N, V, g.base_color);
        color += g.emissive;
    }
    else // PBR (デフォルト)
    {
        // 簡易: Direct + Lambert+GGX (フル PBR は pbr_brdf.hlsli の evaluate_pbr を流用)
        color = evaluate_pbr(g.base_color, g.emissive,
            g.metalness, g.roughness, g.occlusion,
            N, V, wp);
    }
    return float4(color, 1.0f);
}
