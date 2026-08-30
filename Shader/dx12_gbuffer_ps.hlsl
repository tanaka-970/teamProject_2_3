cbuffer MaterialCB : register(b2)
{
    float4 baseColor;
    float4 emissiveStrength;
    float4 surfaceParams; // metallic、roughness、AO、alpha cutoff
    float4 renderParams;  // alpha mode、lighting model、receive shadow、texture semantic mask
    float4 builtinParams;  // BuiltIn固有表現。x=効果ID、y/z/w=引数
    float4 builtinParams1; // Toon。rgb=ShadowTint、w=RimPower
    float4 builtinParams2; // Toon。rgb=RimColor、w=SpecularPower
    float4 builtinParams3; // Toon。rgb=SpecularTint、w=予約
};

// Ramp を階調で引くために Directional Light の向きが要る。
#define DX12_LIGHT_CB_REGISTER b3
#include "dx12_lighting_common.hlsli"
#include "gbuffer_common.hlsli"

// BuiltInは自前PSOを持てないため、固有表現をここで分岐して適用する。
// 効果を足すときはIDを増やし、この関数へ分岐を1つ加える。
static const uint BUILTIN_EFFECT_NONE = 0u;
static const uint BUILTIN_EFFECT_PIXELATE = 1u;
static const uint BUILTIN_EFFECT_TOON = 2u;

Texture2D baseTexture : register(t0);
Texture2D normalTexture : register(t1);
Texture2D metallicTexture : register(t2);
Texture2D roughnessTexture : register(t3);
Texture2D emissiveTexture : register(t4);
Texture2D occlusionTexture : register(t5);
// t8/t9 は Bone Palette の root SRV が使うので RampMap は t10 に置く。
Texture2D rampTexture : register(t10);
SamplerState materialSampler : register(s0);

// 色を RGB565 へ詰めて UNORM16 の 1ch に載せる。復号は lighting pass 側。
float Dx12PackColor565(float3 color)
{
    const float3 c = saturate(color);
    const float code = floor(c.r * 31.0f + 0.5f) * 2048.0f +
        floor(c.g * 63.0f + 0.5f) * 32.0f + floor(c.b * 31.0f + 0.5f);
    return code / 65535.0f;
}

// 0..1 の値 2 つを 8bit ずつ UNORM16 の 1ch へ詰める。
float Dx12PackTwoBytes(float high, float low)
{
    return (floor(saturate(high) * 255.0f + 0.5f) * 256.0f +
        floor(saturate(low) * 255.0f + 0.5f)) / 65535.0f;
}

static const uint MATERIAL_BASE_MAP = 1u << 0;
static const uint MATERIAL_NORMAL_MAP = 1u << 1;
static const uint MATERIAL_METALLIC_MAP = 1u << 2;
static const uint MATERIAL_ROUGHNESS_MAP = 1u << 3;
static const uint MATERIAL_EMISSIVE_MAP = 1u << 4;
static const uint MATERIAL_OCCLUSION_MAP = 1u << 5;
static const uint MATERIAL_PACKED_ORM_MAP = 1u << 6;
static const uint MATERIAL_RAMP_MAP = 1u << 7;

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
    // Toon の色3つと指数2つを bit pack して運ぶ。Toon 以外は 0。
    float4 toon : SV_Target5;
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
    // 判定は lighting model 基準。Material Asset 無しの「描画方式=トゥーン」も拾うため。
    const bool isToon = (uint)round(renderParams.y) == 1u;
    const bool hasToonParams = (uint)(builtinParams.x + 0.5f) == BUILTIN_EFFECT_TOON;
    const float toonStepCount = hasToonParams ? builtinParams.y : 3.0f;
    const bool normalizedRamp = hasToonParams && builtinParams.w >= 0.5f;
    const float3 N = ResolveNormal(input, semanticMask);

    // ランプは遅延ライティングでは1枚に絞れないのでGBufferで階調を引いて乗算する。
    if (isToon && (semanticMask & MATERIAL_RAMP_MAP) != 0u)
    {
        const float3 L = normalize(-directionalDirectionIntensity.xyz);
        const float noL = directionalColorFlags.w > 0.5f ? saturate(dot(N, L)) : 1.0f;
        const float levels = max(floor(toonStepCount + 0.5f), 1.0f);
        const float aa = fwidth(saturate(noL) * levels);
        const float band = normalizedRamp ? Dx12ToonBand(noL, toonStepCount, aa) :
            Dx12ToonNoL(noL, toonStepCount);
        albedo.rgb *= rampTexture.Sample(materialSampler, float2(band, 0.5f)).rgb;
    }

    const bool isPixelate = (uint)(builtinParams.x + 0.5f) == BUILTIN_EFFECT_PIXELATE &&
        builtinParams.z > 0.0f;
    output.base = float4(albedo.rgb, saturate(renderParams.y / 255.0f));
    output.emissive = float4(emissive, 1.0f);
    output.normalDepth = float4(N * 0.5f + 0.5f,
        renderParams.z >= 0.5f ? 1.0f : -1.0f);
    // material.a は従来 ao の重複で誰も読んでいない。Toon のときだけ階調数を運ぶ。
    // 他のシェーダでは今までどおり ao を入れるので既存の見た目は変わらない。
    const float toonSteps = isToon ? saturate(toonStepCount / 16.0f +
        (normalizedRamp ? 0.5f : 0.0f)) : ao;
    output.material = float4(ao, roughness, metallic, toonSteps);

    // 追加RTはToonのときだけ埋める。他は0で、lighting側もmodelで門を閉じている。
    output.toon = 0.0f;
    if (isPixelate)
    {
        GBufferOut pixelate_output = (GBufferOut)0;
        pixelate_output.emissive = output.emissive;
        pixelate_output.normal = output.normalDepth;
        EncodePixelateSettings(pixelate_output, builtinParams.y, builtinParams.z);
        output.emissive.a = pixelate_output.emissive.a;
        output.normalDepth.a = pixelate_output.normal.a;
        output.toon = float4(saturate(builtinParams.w),
            builtinParams1.x >= 0.5f ? 1.0f : 0.0f, 0.0f, 0.0f);
    }
    if (isToon && hasToonParams)
    {
        output.toon.x = Dx12PackColor565(builtinParams1.rgb);
        output.toon.y = Dx12PackColor565(builtinParams2.rgb);
        output.toon.z = Dx12PackColor565(builtinParams3.rgb);
        output.toon.w = Dx12PackTwoBytes(builtinParams1.w / 8.0f,
            (builtinParams2.w - 1.0f) / 127.0f);
    }

    const float2 currentNdc = input.currentClip.xy / max(abs(input.currentClip.w), 1.0e-5f);
    const float2 previousNdc = input.previousClip.xy / max(abs(input.previousClip.w), 1.0e-5f);
    output.velocity = (currentNdc - previousNdc) * float2(0.5f, -0.5f);
    return output;
}
