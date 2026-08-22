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

// ---------------------------------------------------------------------------
// エネルギー保存とオクルージョンの補正項
// ---------------------------------------------------------------------------

// 分割和近似のDFG項。LUTが無い環境でも破綻しないよう解析近似を用意しておく。
float2 env_dfg_analytic(float NoV, float roughness)
{
    // Karis の環境BRDF近似 (Mobile向け, "Physically Based Shading on Mobile")
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28f * NoV)) * r.x + r.y;
    return float2(-1.04f, 1.04f) * a004 + r.zw;
}

float2 env_dfg(float NoV, float roughness)
{
    float2 uv = clamp(float2(NoV, roughness), 0.0f, 1.0f);
    float2 f_ab = pbr_lut_ggx.SampleLevel(pbr_sampler_linear, uv, 0).rg;
    // LUT未バインド時は0が返るため、そのときだけ解析近似へ落とす。
    return (f_ab.x + f_ab.y) <= 1.0e-5f ? env_dfg_analytic(NoV, roughness) : f_ab;
}

// 直接光の鏡面反射に対するマルチスキャッタ補正。
// 単散乱GGXはラフな金属で目に見えて暗くなるため、失われたエネルギーを戻す。
float3 specular_energy_compensation(float3 f0, float2 f_ab)
{
    return 1.0f + f0 * (1.0f / max(f_ab.x, 1.0e-4f) - 1.0f);
}

// Lagarde の specular occlusion。AOをそのまま鏡面へ掛けると
// ラフネスが低い面で不自然に暗くなるので、NoVとラフネスで効き方を変える。
float specular_occlusion(float NoV, float ao, float roughness)
{
    return saturate(pow(abs(NoV) + ao, exp2(-16.0f * roughness - 1.0f)) - 1.0f + ao);
}

// 反射ベクトルが面の裏側を向いたときのリークを潰す(horizon occlusion)。
float horizon_occlusion(float3 R, float3 N, float fade)
{
    float horizon = saturate(1.0f + fade * dot(R, N));
    return horizon * horizon;
}

float3 ibl_radiance_lambertian(float3 N, float3 V, float roughness,
                               float3 diffuse_color, float3 f0)
{
    float NoV = clamp(dot(N, V), 0.0f, 1.0f);

    float2 f_ab = env_dfg(NoV, roughness);
    // SampleLevel(0)で読む。IEMは畳み込み済みなのでミップは不要で、
    // コンピュートシェーダーからも同じ関数が使える。
    float3 irradiance = pbr_diffuse_iem.SampleLevel(pbr_sampler_linear, N, 0).rgb;

    float3 Fr     = max(1.0f - roughness, f0) - f0;
    float3 k_S    = f0 + Fr * pow(1.0f - NoV, 5.0f);
    float3 FssEss = k_S * f_ab.x + f_ab.y;

    return diffuse_color * (1.0f - FssEss) * irradiance / PI;
}

// Fdez-Agüera のマルチスキャッタIBL。拡散と鏡面を同時に解き、
// 単散乱で失われるエネルギーを多重散乱項として拡散側へ戻す。
// 金属のラフ面が黒ずむ / 誘電体が暗くなる問題が消える。
void ibl_multiscatter(float3 N, float3 V, float roughness,
                      float3 diffuse_color, float3 f0,
                      out float3 out_diffuse, out float3 out_specular)
{
    float  NoV = clamp(dot(N, V), 0.0f, 1.0f);
    float3 R   = reflect(-V, N);

    float2 f_ab = env_dfg(NoV, roughness);

    uint width = 1, height = 1, mip_count = 1;
    pbr_specular_pmrem.GetDimensions(0, width, height, mip_count);
    float lod = roughness * float(max(mip_count - 1, 1));

    // SampleLevel(0)で読む。IEMは畳み込み済みなのでミップは不要で、
    // コンピュートシェーダーからも同じ関数が使える。
    float3 irradiance = pbr_diffuse_iem.SampleLevel(pbr_sampler_linear, N, 0).rgb;
    float3 radiance   = pbr_specular_pmrem.SampleLevel(pbr_sampler_linear, R, lod).rgb;

    float3 Fr     = max(1.0f - roughness, f0) - f0;
    float3 k_S    = f0 + Fr * pow(1.0f - NoV, 5.0f);
    float3 FssEss = k_S * f_ab.x + f_ab.y;

    // 単散乱で取りこぼした割合 Ems と、平均フレネル Favg から多重散乱を求める。
    float  Ems  = 1.0f - (f_ab.x + f_ab.y);
    float3 Favg = f0 + (1.0f - f0) / 21.0f;
    float3 Fms  = FssEss * Favg / (1.0f - Ems * Favg);
    float3 k_D  = diffuse_color * (1.0f - FssEss - Fms * Ems);

    out_diffuse  = (Fms * Ems + k_D) * irradiance;
    out_specular = FssEss * radiance;
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
    float2 f_ab = pbr_lut_ggx.SampleLevel(pbr_sampler_linear, brdf_sample_point, 0).rg;

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

// スクリーン空間の追加入力。使わない側は既定値のまま渡せばよい。
struct PbrScreenSpaceInputs
{
    float  ambient_occlusion;   // 1=遮蔽なし
    float3 reflection_color;    // SSRの色
    float  reflection_weight;   // SSRの信頼度 0..1
};

PbrScreenSpaceInputs pbr_default_screen_inputs()
{
    PbrScreenSpaceInputs inputs;
    inputs.ambient_occlusion = 1.0f;
    inputs.reflection_color  = float3(0.0f, 0.0f, 0.0f);
    inputs.reflection_weight = 0.0f;
    return inputs;
}

float3 evaluate_pbr_ex(float3 base_color, float3 emissive,
                       float metallic, float roughness, float occlusion,
                       float3 N, float3 V, float3 world_position,
                       float shadow, PbrScreenSpaceInputs screen,
                       // 1=影を受ける / 0=受けない。Point / Spot の影にも効く。
                       float receive_shadow)
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

    // マルチスキャッタ補正は直接光と鏡面IBLで共通に使う。
    float2 f_ab = env_dfg(NoV, roughness);
    float3 energy_compensation = specular_energy_compensation(f0, f_ab);

    float3 direct = float3(0.0f, 0.0f, 0.0f);
    if (NoL > 0.0f && NoV > 0.0f)
    {
        float3 diff = brdf_lambertian(f0, f90, diffuse_color, VoH);
        float3 spec = brdf_specular_ggx(f0, f90, alpha_roughness, VoH, NoL, NoV, NoH)
                    * energy_compensation;
        direct = (diff + spec) * NoL * directional_color.rgb * directional_color.a;
    }

    direct *= shadow;
    direct += evaluate_point_lights(world_position, N, V, base_color,
        roughness, metallic, receive_shadow);
    direct += evaluate_spot_lights(world_position, N, V, base_color, receive_shadow);

    // 間接光はマルチスキャッタIBLで拡散/鏡面を同時に解く。
    float3 ibl_diffuse, ibl_specular;
    ibl_multiscatter(N, V, roughness, diffuse_color, f0, ibl_diffuse, ibl_specular);
    ibl_diffuse  *= ibl_params.x;
    ibl_specular *= ibl_params.y;

    // AOはマップとSSAOの両方を掛け合わせ、拡散と鏡面で効き方を分ける。
    float ao = saturate(occlusion * screen.ambient_occlusion);
    float ao_strength = saturate(ibl_params.z);
    float diffuse_ao = lerp(1.0f, ao, ao_strength);
    float spec_ao = lerp(1.0f, specular_occlusion(NoV, ao, roughness), ao_strength);

    float3 R = reflect(-V, N);
    float horizon = horizon_occlusion(R, N, 1.0f);

    ibl_diffuse  *= diffuse_ao;
    ibl_specular *= spec_ao * horizon;

    // SSRはIBLの鏡面項を置き換える形で合成する(二重計上を避ける)。
    float reflection_weight = saturate(screen.reflection_weight);
    if (reflection_weight > 0.0f)
    {
        float2 reflect_f_ab = f_ab;
        float3 FssEss = f0 * reflect_f_ab.x + reflect_f_ab.y;
        float3 screen_specular = screen.reflection_color * FssEss
                               * energy_compensation * spec_ao * horizon * ibl_params.y;
        ibl_specular = lerp(ibl_specular, screen_specular, reflection_weight);
    }

    float3 indirect = ibl_diffuse + ibl_specular;

    return (direct + indirect + emissive) * max(ibl_params.w, 0.0f);
}

float3 evaluate_pbr(float3 base_color, float3 emissive,
                    float metallic, float roughness, float occlusion,
                    float3 N, float3 V, float3 world_position)
{
    // 前方描画は Receive Shadow を運ぶ経路が無いので常に受ける扱い。
    return evaluate_pbr_ex(base_color, emissive, metallic, roughness, occlusion,
        N, V, world_position, sample_shadow(world_position),
        pbr_default_screen_inputs(), 1.0f);
}

#endif // PBR_BRDF_CORE_ONLY
#endif // __PBR_BRDF_HLSLI__
