cbuffer MaterialCB : register(b2)
{
    float4 baseColor;
    float4 emissiveStrength;
    float4 surfaceParams; // metallic、roughness、AO、alpha cutoff
    float4 renderParams;  // alpha mode、lighting model、receive shadow、texture semantic mask
    float4 builtinParams;
    float4 builtinParams1;
    float4 builtinParams2;
    float4 builtinParams3;
};

#define DX12_LIGHT_CB_REGISTER b3
#include "dx12_lighting_common.hlsli"

Texture2D baseTexture : register(t0);
Texture2D normalTexture : register(t1);
Texture2D metallicTexture : register(t2);
Texture2D roughnessTexture : register(t3);
Texture2D emissiveTexture : register(t4);
Texture2D occlusionTexture : register(t5);
Texture2D rampTexture : register(t10);
SamplerState materialSampler : register(s0);

struct PSIn
{
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 currentClip : TEXCOORD3;
    float4 previousClip : TEXCOORD4;
    float4 tangent : TEXCOORD5;
};

static const uint MATERIAL_NORMAL_MAP = 1u << 1;
static const uint MATERIAL_METALLIC_MAP = 1u << 2;
static const uint MATERIAL_ROUGHNESS_MAP = 1u << 3;
static const uint MATERIAL_EMISSIVE_MAP = 1u << 4;
static const uint MATERIAL_OCCLUSION_MAP = 1u << 5;
static const uint MATERIAL_PACKED_ORM_MAP = 1u << 6;
static const uint MATERIAL_RAMP_MAP = 1u << 7;
static const uint BUILTIN_EFFECT_TOON = 2u;

float3 QuantizeColor565(float3 color)
{
    const float3 levels = float3(31.0f, 63.0f, 31.0f);
    return floor(saturate(color) * levels + 0.5f) / levels;
}

float QuantizeUnorm8(float value)
{
    return floor(saturate(value) * 255.0f + 0.5f) / 255.0f;
}

Dx12ToonSurface ResolveDeferredToonSurface()
{
    Dx12ToonSurface toon = Dx12DefaultToonSurface();
    const bool hasToonParams = (uint)(builtinParams.x + 0.5f) == BUILTIN_EFFECT_TOON;
    const float stepCount = hasToonParams ? builtinParams.y : 3.0f;
    toon.steps = QuantizeUnorm8(stepCount / 16.0f) * 16.0f;
    if (!hasToonParams) return toon;

    toon.shadowTint = QuantizeColor565(builtinParams1.rgb);
    toon.rimColor = QuantizeColor565(builtinParams2.rgb);
    toon.specularTint = QuantizeColor565(builtinParams3.rgb);
    toon.rimPower = QuantizeUnorm8(builtinParams1.w / 8.0f) * 8.0f;
    toon.specularPower = 1.0f + QuantizeUnorm8(
        (builtinParams2.w - 1.0f) / 127.0f) * 127.0f;
    return toon;
}

float3 ResolveNormal(PSIn input, uint semanticMask)
{
    float3 normal = normalize(input.normal);
    if ((semanticMask & MATERIAL_NORMAL_MAP) == 0u) return normal;

    float3 tangent;
    float3 bitangent;
    if (dot(input.tangent.xyz, input.tangent.xyz) > 1.0e-6f)
    {
        tangent = normalize(input.tangent.xyz - normal * dot(normal, input.tangent.xyz));
        bitangent = normalize(cross(normal, tangent) *
            (input.tangent.w < 0.0f ? -1.0f : 1.0f));
    }
    else
    {
        const float3 seed = abs(normal.y) < 0.999f ?
            float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
        tangent = normalize(cross(seed, normal));
        bitangent = normalize(cross(normal, tangent));
    }

    const float3 mapNormal = normalTexture.Sample(materialSampler, input.uv).xyz * 2.0f - 1.0f;
    return normalize(mapNormal.x * tangent + mapNormal.y * bitangent + mapNormal.z * normal);
}

float4 main(PSIn input) : SV_Target0
{
    const uint semanticMask = (uint)round(max(renderParams.w, 0.0f));
    float4 albedo = baseTexture.Sample(materialSampler, input.uv) * baseColor;
    if (renderParams.x > 0.5f && renderParams.x < 1.5f)
        clip(albedo.a - surfaceParams.w);

    float metallic = saturate(surfaceParams.x);
    float roughness = clamp(surfaceParams.y, 0.045f, 1.0f);
    float ambientOcclusion = saturate(surfaceParams.z);
    if ((semanticMask & MATERIAL_PACKED_ORM_MAP) != 0u)
    {
        const float3 orm = metallicTexture.Sample(materialSampler, input.uv).rgb;
        ambientOcclusion = saturate(ambientOcclusion * orm.r);
        roughness = clamp(roughness * orm.g, 0.045f, 1.0f);
        metallic = saturate(metallic * orm.b);
    }
    else
    {
        if ((semanticMask & MATERIAL_METALLIC_MAP) != 0u)
            metallic = saturate(metallic * metallicTexture.Sample(materialSampler, input.uv).r);
        if ((semanticMask & MATERIAL_ROUGHNESS_MAP) != 0u)
            roughness = clamp(roughness * roughnessTexture.Sample(materialSampler, input.uv).r,
                0.045f, 1.0f);
        if ((semanticMask & MATERIAL_OCCLUSION_MAP) != 0u)
            ambientOcclusion = saturate(ambientOcclusion *
                occlusionTexture.Sample(materialSampler, input.uv).r);
    }

    const float3 worldNormal = ResolveNormal(input, semanticMask);
    const uint lightingModel = (uint)round(max(renderParams.y, 0.0f));
    const bool receiveShadow = renderParams.z >= 0.5f;
    const bool matchDeferredToon = builtinParams3.w >= 0.5f && lightingModel == 1u;
    if (matchDeferredToon && (semanticMask & MATERIAL_RAMP_MAP) != 0u)
    {
        const bool hasToonParams = (uint)(builtinParams.x + 0.5f) == BUILTIN_EFFECT_TOON;
        const float stepCount = hasToonParams ? builtinParams.y : 3.0f;
        const float3 lightDirection = normalize(-directionalDirectionIntensity.xyz);
        const float noL = directionalColorFlags.w > 0.5f ?
            saturate(dot(worldNormal, lightDirection)) : 1.0f;
        const float band = Dx12ToonNoL(noL, stepCount);
        albedo.rgb *= rampTexture.Sample(materialSampler, float2(band, 0.5f)).rgb;
    }
    Dx12ToonSurface toon = Dx12DefaultToonSurface();
    if (matchDeferredToon) toon = ResolveDeferredToonSurface();
    const float3 lit = Dx12EvaluateLighting(input.worldPosition, worldNormal, albedo.rgb,
        metallic, roughness, ambientOcclusion, lightingModel, receiveShadow,
        input.position.xy, toon);

    float3 emissive = emissiveStrength.rgb * emissiveStrength.a;
    if ((semanticMask & MATERIAL_EMISSIVE_MAP) != 0u)
        emissive = emissiveTexture.Sample(materialSampler, input.uv).rgb *
            emissiveStrength.rgb * emissiveStrength.a;
    // Scene TargetはHDRリニア値を保持し、最終表示パスで一度だけ変換する。
    const float outputAlpha = builtinParams3.w >= 0.5f && renderParams.x < 1.5f ?
        1.0f : albedo.a;
    return float4(lit + emissive, outputAlpha);
}
