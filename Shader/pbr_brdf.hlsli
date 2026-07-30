// PBRの直接光、IBL、影を組み合わせるBRDF計算を定義する。
#ifndef __PBR_BRDF_HLSLI__
#define __PBR_BRDF_HLSLI__

#include "pbr_common.hlsli"
#include "lights_common.hlsli"

// Schlick フレネル近似
float3 f_schlick(float3 f0, float3 f90, float VoH)
{
    return f0 + (f90 - f0) * pow(clamp(1.0f - VoH, 0.0f, 1.0f), 5.0f);
}

// Smith 可視性関数 (GGX correlated)
float v_ggx(float NoL, float NoV, float alpha_roughness)
{
    float a2 = alpha_roughness * alpha_roughness;
    float ggxv = NoL * sqrt(NoV * NoV * (1.0f - a2) + a2);
    float ggxl = NoV * sqrt(NoL * NoL * (1.0f - a2) + a2);
    float ggx  = ggxv + ggxl;
    return (ggx > 0.0f) ? 0.5f / ggx : 0.0f;
}

// GGX 法線分布関数
float d_ggx(float NoH, float alpha_roughness)
{
    float a2 = alpha_roughness * alpha_roughness;
    float f  = (NoH * NoH) * (a2 - 1.0f) + 1.0f;
    return a2 / (PI * f * f);
}

// 拡散項 (Lambert + Schlick エネルギー保存)
float3 brdf_lambertian(float3 f0, float3 f90, float3 diffuse_color, float VoH)
{
    return (1.0f - f_schlick(f0, f90, VoH)) * (diffuse_color / PI);
}

// 鏡面項 (Cook-Torrance GGX)
float3 brdf_specular_ggx(float3 f0, float3 f90, float alpha_roughness,
                         float VoH, float NoL, float NoV, float NoH)
{
    float3 F   = f_schlick(f0, f90, VoH);
    float  Vis = v_ggx(NoL, NoV, alpha_roughness);
    float  D   = d_ggx(NoH, alpha_roughness);
    return F * Vis * D;
}

#ifndef PBR_BRDF_CORE_ONLY

float3 ibl_radiance_lambertian(float3 N, float3 V, float roughness,
                               float3 diffuse_color, float3 f0)
{
    float NoV = clamp(dot(N, V), 0.0f, 1.0f);

    float2 brdf_sample_point = clamp(float2(NoV, roughness), 0.0f, 1.0f);
    float2 f_ab = pbr_lut_ggx.Sample(pbr_sampler_linear, brdf_sample_point).rg;

    float3 irradiance = pbr_diffuse_iem.Sample(pbr_sampler_linear, N).rgb;

    float3 Fr     = max(1.0f - roughness, f0) - f0;
    float3 k_S    = f0 + Fr * pow(1.0f - NoV, 5.0f);
    float3 FssEss = k_S * f_ab.x + f_ab.y;

    return diffuse_color * (1.0f - FssEss) * irradiance / PI;
}

float3 ibl_radiance_ggx(float3 N, float3 V, float roughness, float3 f0)
{
    float NoV = clamp(dot(N, V), 0.0f, 1.0f);
    float3 R  = normalize(reflect(-V, N));

    uint  w = 1, h = 1, mips = 1;
    pbr_specular_pmrem.GetDimensions(0, w, h, mips);
    float lod = roughness * float(max(mips - 1, 1));

    float3 specular_light = pbr_specular_pmrem.SampleLevel(pbr_sampler_linear, R, lod).rgb;

    float2 brdf_sample_point = clamp(float2(NoV, roughness), 0.0f, 1.0f);
    float2 f_ab = pbr_lut_ggx.Sample(pbr_sampler_linear, brdf_sample_point).rg;

    float3 Fr     = max(1.0f - roughness, f0) - f0;
    float3 k_S    = f0 + Fr * pow(1.0f - NoV, 5.0f);
    float3 FssEss = k_S * f_ab.x + f_ab.y;

    return specular_light * FssEss;
}

#if 0
float sample_shadow(float3 world_position)
{
    if (shadow_params.w < 0.5f) return 1.0f; // 無効

    float4 sp = mul(float4(world_position, 1.0f), light_view_projection);
    sp.xyz /= sp.w;

    float2 uv = sp.xy * float2(0.5f, -0.5f) + 0.5f;
    if (any(uv < 0.0f) || any(uv > 1.0f)) return 1.0f;

    float depth = sp.z - shadow_params.y;

    uint w = 1, h = 1, mips = 1;
    pbr_shadow_map.GetDimensions(0, w, h, mips);
    float2 texel = shadow_params.z / float2(max(w,1), max(h,1));

    float accum = 0.0f;
    [unroll]
    for (int y = -2; y <= 2; ++y)
    {
        [unroll]
        for (int x = -2; x <= 2; ++x)
        {
            float2 offset = float2(x, y) * texel;
            accum += pbr_shadow_map.SampleCmpLevelZero(pbr_sampler_shadow, uv + offset, depth);
        }
    }
    float visibility = accum / 25.0f;
    return lerp(1.0f, visibility, shadow_params.x);
}
#endif

float sample_shadow(float3 world_position)
{
    float visibility = 1.0f;
    if (shadow_params.w >= 0.5f)
    {
        float4 sp = mul(float4(world_position, 1.0f), light_view_projection);
        float safe_w = max(abs(sp.w), 1.0e-6f) * (sp.w < 0.0f ? -1.0f : 1.0f);
        sp.xyz /= safe_w;
        float2 uv = sp.xy * float2(0.5f, -0.5f) + 0.5f;
        if (!any(uv < 0.0f) && !any(uv > 1.0f))
        {
            float depth = sp.z - shadow_params.y;
            uint w = 1, h = 1, mips = 1;
            pbr_shadow_map.GetDimensions(0, w, h, mips);
            float2 texel = shadow_params.z / float2(max(w, 1), max(h, 1));
            float accum = 0.0f;
            [unroll]
            for (int y = -2; y <= 2; ++y)
            {
                [unroll]
                for (int x = -2; x <= 2; ++x)
                {
                    float2 offset = float2(x, y) * texel;
                    accum += pbr_shadow_map.SampleCmpLevelZero(pbr_sampler_shadow, uv + offset, depth);
                }
            }
            visibility = lerp(1.0f, accum / 25.0f, shadow_params.x);
        }
    }
    return visibility;
}

float3 evaluate_pbr(float3 base_color, float3 emissive,
                    float metallic, float roughness, float occlusion,
                    float3 N, float3 V, float3 world_position)
{
    // perceptual -> alpha
    float alpha_roughness = max(roughness * roughness, 0.0025f);

    // F0 計算 (誘電体は 4%、金属は base color)
    float3 f0  = lerp(float3(0.04f, 0.04f, 0.04f), base_color, metallic);
    float3 f90 = float3(1.0f, 1.0f, 1.0f);

    float3 diffuse_color = base_color * (1.0f - 0.04f) * (1.0f - metallic);

    float3 L   = normalize(-light_direction.xyz);
    float3 H   = normalize(L + V);
    float  NoL = clamp(dot(N, L), 0.0f, 1.0f);
    float  NoV = clamp(dot(N, V), 0.0f, 1.0f);
    float  NoH = clamp(dot(N, H), 0.0f, 1.0f);
    float  VoH = clamp(dot(V, H), 0.0f, 1.0f);

    float3 direct = float3(0.0f, 0.0f, 0.0f);
    if (NoL > 0.0f && NoV > 0.0f)
    {
        float3 diff = brdf_lambertian(f0, f90, diffuse_color, VoH);
        float3 spec = brdf_specular_ggx(f0, f90, alpha_roughness, VoH, NoL, NoV, NoH);
        direct = (diff + spec) * NoL * directional_color.rgb * directional_color.a;
    }

    float shadow = sample_shadow(world_position);
    direct *= shadow;
    direct += evaluate_point_lights(world_position, N, V, base_color, roughness, metallic);
    direct += evaluate_spot_lights(world_position, N, V, base_color);

    float3 ibl_diff = ibl_radiance_lambertian(N, V, roughness, diffuse_color, f0) * ibl_params.x;
    float3 ibl_spec = ibl_radiance_ggx(N, V, roughness, f0)                       * ibl_params.y;
    float3 indirect = ibl_diff + ibl_spec;

    // AO
    indirect *= lerp(1.0f, occlusion, ibl_params.z);

    return (direct + indirect + emissive) * max(ibl_params.w, 0.0f);
}

#endif // PBR_BRDF_CORE_ONLY
#endif // __PBR_BRDF_HLSLI__
