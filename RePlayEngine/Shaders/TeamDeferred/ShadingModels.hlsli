// 標準、PBR、Toonなどの材質別照明計算を定義する。
#ifndef __SHADING_MODELS_HLSLI__
#define __SHADING_MODELS_HLSLI__

#include "GBuffer.hlsli"


// ライティングに使う表面情報のまとまり
struct SurfaceInfo
{
    float3 baseColor;
    float3 N;           // 面の向き(法線)
    float3 V;           // 表面→カメラ方向
    float  metallic;
    float  roughness;
    float  ambientOcclusion;
    float  exposure;
    float  toonSteps;
    int    shadingModel;
};

static const float PI = 3.14159265f;
// 低ラフネス・浅い角度でCook-Torrance鏡面項が発散しないようにする上限
// (水面やツルツルの金属で「キラキラの白飛び」が出るのを防ぐ)
// 8.0だとまだハイライトが尖りすぎて「白いつぶつぶ」に見えるため、
// 上限を下げてハイライトの粒状感を弱めている。
static const float kMaxSpecular = 3.0f;
// roughnessの下限。0.04(ほぼ鏡)だと法線マップの細かい凹凸ごとに
// ハイライトが分裂して粒々ノイズになりやすいので、少し広げてなめらかにする。
static const float kMinRoughness = 0.12f;

// フォン反射。プラスチックっぽい質感にする。
float3 ShadePhong(SurfaceInfo s, float3 L, float3 lightColor)
{
    float ndotl = saturate(dot(s.N, L));
    float3 H = normalize(L + s.V);
    float specPower = lerp(64.0f, 8.0f, saturate(s.roughness));
    float spec = pow(saturate(dot(s.N, H)), specPower) * 0.35f;
    return (s.baseColor * ndotl + spec) * lightColor;
}

// 物理ベース反射。金属感と粗さを出す。
float3 ShadePBR(SurfaceInfo s, float3 L, float3 lightColor)
{
    float3 N = s.N;
    float3 V = s.V;
    float3 H = normalize(L + V);
    float ndotl = saturate(dot(N, L));
    float ndotv = saturate(dot(N, V)) + 0.0001f;
    float ndoth = saturate(dot(N, H));
    float vdoth = saturate(dot(V, H));

    float roughness = max(s.roughness, kMinRoughness);
    float a = roughness * roughness;
    float a2 = a * a;

    // D: 表面の細かい凸凹の向きの分布 (GGX)
    float denom = ndoth * ndoth * (a2 - 1.0f) + 1.0f;
    float D = a2 / max(PI * denom * denom, 0.0001f);

    // G: 凸凹同士で光が遮られる割合 (Smith近似)
    float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
    float G = (ndotl / (ndotl * (1.0f - k) + k)) * (ndotv / (ndotv * (1.0f - k) + k));

    // F: 浅い角度ほど鏡みたいに反射する (Schlick近似)
    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), s.baseColor, s.metallic);
    float3 F = f0 + (1.0f - f0) * pow(1.0f - vdoth, 5.0f);

    float3 specular = (D * G * F) / max(4.0f * ndotl * ndotv, 0.0001f);
    specular = min(specular, kMaxSpecular); // ハイライトの発散(白飛び・つぶつぶ)を抑える
    // 金属は拡散反射しない
    float3 diffuse = s.baseColor * (1.0f - s.metallic) / PI;

    return (diffuse + specular) * lightColor * ndotl * PI;
}

// トゥーン反射。明るさを段階化する。
float3 ShadeToon(SurfaceInfo s, float3 L, float3 lightColor, float includeRim, int styleIndex)
{
	float4 styleToonParams = toonParams[styleIndex];
	float4 styleSpecularParams = toonSpecularParams[styleIndex];
	float4 styleShadowTint = toonShadowTint[styleIndex];
	float4 styleExtraParams = toonExtraParams[styleIndex];
    float ndotl = saturate(dot(s.N, L));
    float steps = max(s.toonSteps, 1.0f);
    // 0～1 を steps 段のカクカクした明るさに変換
    float banded = floor(ndotl * steps + 0.5f) / steps;
    banded = max(banded, 0.18f); // 影でも真っ黒にしない
    float3 H = normalize(L + s.V);
    float rimRaw = pow(1.0f - saturate(dot(s.N, s.V)), max(styleToonParams.x, 0.0001f));
    float rim = smoothstep(styleToonParams.y, styleToonParams.y + max(styleToonParams.w, 0.0001f), rimRaw) * styleToonParams.z;
    float specSource = pow(saturate(dot(s.N, H)), max(styleSpecularParams.x, 0.0001f));
    float specular = smoothstep(styleSpecularParams.y, styleSpecularParams.y + max(styleExtraParams.x, 0.0001f), specSource) * styleSpecularParams.z / 10.0f;

    float3 toonBase = lerp(
        s.baseColor,
        s.baseColor * styleShadowTint.rgb,
        (1.0f - banded) * saturate(styleShadowTint.a));
    float3 color = toonBase * banded * lightColor;
    color += toonRimColor[styleIndex].rgb * rim * toonRimColor[styleIndex].a * includeRim;
    color += toonSpecularColor[styleIndex].rgb * specular * toonSpecularColor[styleIndex].a * lightColor;
    return color;
}

// 材質番号から陰影の計算を選ぶ。
float3 ShadePBRToon(SurfaceInfo s, float3 L, float3 lightColor, float includeRim, int styleIndex)
{
	float4 hybridShadowColor = pbrToonShadowColor[styleIndex];
	float4 hybridRimColor = pbrToonRimColor[styleIndex];
	float4 hybridParams0 = pbrToonParams0[styleIndex];
	float4 hybridParams1 = pbrToonParams1[styleIndex];
	float4 hybridParams2 = pbrToonParams2[styleIndex];
    float3 N = s.N;
    float3 V = s.V;
    float3 H = normalize(L + V);
    float ndotl = saturate(dot(N, L));
    float ndotv = saturate(dot(N, V)) + 0.0001f;
    float ndoth = saturate(dot(N, H));
    float vdoth = saturate(dot(V, H));

    float steps = max(hybridParams0.x, 1.0f);
    float softness = saturate(hybridParams0.y);
    float scaled = ndotl * steps;
    float bandBase = floor(scaled);
    float bandEdge = smoothstep(0.5f - softness, 0.5f + softness, frac(scaled));
    float banded = saturate((bandBase + bandEdge) / steps);
    banded = lerp(banded, ndotl, saturate(hybridParams0.z));
    float stylizedLight = lerp(saturate(hybridParams2.y), 1.0f, banded);

    float shadowAmount = 1.0f - banded;
    float3 tintedBase = lerp(
        s.baseColor,
        s.baseColor * hybridShadowColor.rgb,
        shadowAmount * saturate(hybridShadowColor.a));

    float roughness = max(s.roughness, kMinRoughness);
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = ndoth * ndoth * (a2 - 1.0f) + 1.0f;
    float D = a2 / max(PI * denom * denom, 0.0001f);
    float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
    float G = (ndotl / (ndotl * (1.0f - k) + k)) *
        (ndotv / (ndotv * (1.0f - k) + k));
    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), s.baseColor, s.metallic);
    float3 F = f0 + (1.0f - f0) * pow(1.0f - vdoth, 5.0f);
    float3 specular = (D * G * F) / max(4.0f * ndotl * ndotv, 0.0001f);
    specular = min(specular, kMaxSpecular); // ハイライトの発散(白飛び)を抑える

    float specSoftness = max(hybridParams1.y, 0.0001f);
    float specMask = smoothstep(
        hybridParams1.x - specSoftness,
        hybridParams1.x + specSoftness,
        ndoth);
    specular *= specMask * max(hybridParams0.w, 0.0f);

    float3 diffuse = tintedBase * (1.0f - s.metallic) * stylizedLight;
    float3 color = (diffuse + specular * ndotl * PI) * lightColor;

    float rimRaw = pow(1.0f - saturate(dot(N, V)), max(hybridParams1.z, 0.0001f));
    float rimSoftness = max(hybridParams2.x, 0.0001f);
    float rim = smoothstep(
        hybridParams1.w - rimSoftness,
        hybridParams1.w + rimSoftness,
        rimRaw);
    color += hybridRimColor.rgb * hybridRimColor.a * rim * includeRim;
    return color;
}

float3 ComputeDirectLighting(SurfaceInfo s, float3 L, float3 lightColor, float includeRim)
{
    [branch]
    switch (s.shadingModel)
    {
        case SHADING_MODEL_PHONG: return ShadePhong(s, L, lightColor) * s.exposure * postParams.x;
        case SHADING_MODEL_PHONG_PLAYER: return ShadePhong(s, L, lightColor) * s.exposure * postParams.x;
        case SHADING_MODEL_PHONG_ENEMY: return ShadePhong(s, L, lightColor) * s.exposure * postParams.x;
        case SHADING_MODEL_PBR:   return ShadePBR(s, L, lightColor) * s.exposure * postParams.x;
        case SHADING_MODEL_PBR_PLAYER:   return ShadePBR(s, L, lightColor) * s.exposure * postParams.x;
        case SHADING_MODEL_PBR_ENEMY:   return ShadePBR(s, L, lightColor) * s.exposure * postParams.x;
        case SHADING_MODEL_TOON_STAGE: return ShadeToon(s, L, lightColor, includeRim, 0) * s.exposure * postParams.x;
        case SHADING_MODEL_TOON_PLAYER: return ShadeToon(s, L, lightColor, includeRim, 1) * s.exposure * postParams.x;
        case SHADING_MODEL_TOON_ENEMY: return ShadeToon(s, L, lightColor, includeRim, 2) * s.exposure * postParams.x;
        case SHADING_MODEL_PBR_TOON_STAGE: return ShadePBRToon(s, L, lightColor, includeRim, 0) * s.exposure * postParams.x;
        case SHADING_MODEL_PBR_TOON_PLAYER: return ShadePBRToon(s, L, lightColor, includeRim, 1) * s.exposure * postParams.x;
        case SHADING_MODEL_PBR_TOON_ENEMY: return ShadePBRToon(s, L, lightColor, includeRim, 2) * s.exposure * postParams.x;
        default:                  return float3(0.0f, 0.0f, 0.0f); // Unlitは直接光に反応しない
    }
}

#endif
