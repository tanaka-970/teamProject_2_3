cbuffer MaterialCB : register(b2)
{
    float4 baseColor;
    float4 emissiveStrength;
    float4 surfaceParams; // metallic、roughness、AO、alpha cutoff
    float4 renderParams;  // alpha mode、lighting model、receive shadow、texture semantic mask
};

#define DX12_LIGHT_CB_REGISTER b3
#include "dx12_lighting_common.hlsli"

Texture2D baseTexture : register(t0);
Texture2D normalTexture : register(t1);
Texture2D metallicTexture : register(t2);
Texture2D roughnessTexture : register(t3);
Texture2D emissiveTexture : register(t4);
Texture2D occlusionTexture : register(t5);
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
    const float4 albedo = baseTexture.Sample(materialSampler, input.uv) * baseColor;
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
    const float3 lit = Dx12EvaluateLighting(input.worldPosition, worldNormal, albedo.rgb,
        metallic, roughness, ambientOcclusion, lightingModel, receiveShadow, input.position.xy);

    float3 emissive = emissiveStrength.rgb * emissiveStrength.a;
    if ((semanticMask & MATERIAL_EMISSIVE_MAP) != 0u)
        emissive = emissiveTexture.Sample(materialSampler, input.uv).rgb *
            emissiveStrength.rgb * emissiveStrength.a;
    // Scene TargetはHDRリニア値を保持し、最終表示パスで一度だけ変換する。
    return float4(lit + emissive, albedo.a);
}
