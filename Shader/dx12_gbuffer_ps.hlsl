cbuffer MaterialCB : register(b2)
{
    float4 baseColor;
    float4 emissiveStrength;
    float4 surfaceParams; // metallic、roughness、AO、alpha cutoff
    float4 renderParams;  // alpha mode、lighting model、receive shadow、texture semantic mask
};
Texture2D baseTexture : register(t0);
Texture2D normalTexture : register(t1);
Texture2D metallicTexture : register(t2);
Texture2D roughnessTexture : register(t3);
Texture2D emissiveTexture : register(t4);
Texture2D occlusionTexture : register(t5);
SamplerState materialSampler : register(s0);

static const uint MATERIAL_BASE_MAP = 1u << 0;
static const uint MATERIAL_NORMAL_MAP = 1u << 1;
static const uint MATERIAL_METALLIC_MAP = 1u << 2;
static const uint MATERIAL_ROUGHNESS_MAP = 1u << 3;
static const uint MATERIAL_EMISSIVE_MAP = 1u << 4;
static const uint MATERIAL_OCCLUSION_MAP = 1u << 5;
static const uint MATERIAL_PACKED_ORM_MAP = 1u << 6;

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
struct PSOut
{
    float4 base : SV_Target0;
    float4 emissive : SV_Target1;
    float4 normalDepth : SV_Target2;
    float4 material : SV_Target3;
    float2 velocity : SV_Target4;
};

float3 ResolveNormal(PSIn input, uint semanticMask)
{
    float3 N = normalize(input.normal);
    if ((semanticMask & MATERIAL_NORMAL_MAP) == 0u)
        return N;

    float3 T;
    float3 B;
    if (dot(input.tangent.xyz, input.tangent.xyz) > 1.0e-6f)
    {
        T = normalize(input.tangent.xyz - N * dot(N, input.tangent.xyz));
        B = normalize(cross(N, T) * (input.tangent.w < 0.0f ? -1.0f : 1.0f));
    }
    else
    {
        // static_mesh の既存32-byte vertex ABIには tangent が無い。
        // 旧 static PBR と同様に法線から安定した basis を作る。
        const float3 seed = abs(N.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
        T = normalize(cross(seed, N));
        B = normalize(cross(N, T));
    }
    const float3 mapNormal = normalTexture.Sample(materialSampler, input.uv).xyz * 2.0f - 1.0f;
    return normalize(mapNormal.x * T + mapNormal.y * B + mapNormal.z * N);
}

PSOut main(PSIn input)
{
    const uint semanticMask = (uint)round(max(renderParams.w, 0.0f));
    float4 albedo = baseTexture.Sample(materialSampler, input.uv) * baseColor;
    if (renderParams.x > 0.5f && renderParams.x < 1.5f)
        clip(albedo.a - surfaceParams.w);

    float metallic = saturate(surfaceParams.x);
    float roughness = clamp(surfaceParams.y, 0.045f, 1.0f);
    float ao = saturate(surfaceParams.z);
    if ((semanticMask & MATERIAL_PACKED_ORM_MAP) != 0u)
    {
        const float3 orm = metallicTexture.Sample(materialSampler, input.uv).rgb;
        ao = saturate(ao * orm.r);
        roughness = clamp(roughness * orm.g, 0.045f, 1.0f);
        metallic = saturate(metallic * orm.b);
    }
    else
    {
        if ((semanticMask & MATERIAL_METALLIC_MAP) != 0u)
            metallic = saturate(metallic * metallicTexture.Sample(materialSampler, input.uv).r);
        if ((semanticMask & MATERIAL_ROUGHNESS_MAP) != 0u)
            roughness = clamp(roughness * roughnessTexture.Sample(materialSampler, input.uv).r, 0.045f, 1.0f);
        if ((semanticMask & MATERIAL_OCCLUSION_MAP) != 0u)
            ao = saturate(ao * occlusionTexture.Sample(materialSampler, input.uv).r);
    }

    float3 emissive = emissiveStrength.rgb * emissiveStrength.a;
    if ((semanticMask & MATERIAL_EMISSIVE_MAP) != 0u)
        emissive = emissiveTexture.Sample(materialSampler, input.uv).rgb *
            emissiveStrength.rgb * emissiveStrength.a;

    PSOut output;
    // Shader/gbuffer_common.hlsliと一致させる。ModelはBase.a、ReceiveShadowはNormal.a、
    // MaterialはOcclusion/Roughness/Metalness/AO Strengthとして格納する。
    output.base = float4(albedo.rgb, saturate(renderParams.y / 255.0f));
    output.emissive = float4(emissive, 1.0f);
    output.normalDepth = float4(ResolveNormal(input, semanticMask) * 0.5f + 0.5f,
        renderParams.z >= 0.5f ? 1.0f : -1.0f);
    output.material = float4(ao, roughness, metallic, ao);

    const float2 currentNdc = input.currentClip.xy / max(abs(input.currentClip.w), 1.0e-5f);
    const float2 previousNdc = input.previousClip.xy / max(abs(input.previousClip.w), 1.0e-5f);
    output.velocity = (currentNdc - previousNdc) * float2(0.5f, -0.5f);
    return output;
}
