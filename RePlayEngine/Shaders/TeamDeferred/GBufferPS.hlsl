// モデルの材質情報をG-Bufferへ書き込むピクセルシェーダー。
#include "GBufferModel.hlsli"
#include "TerrainCarve.hlsli"

// ジオメトリパス ピクセルシェーダー (メモを取る係)
// 色を画面に塗る代わりに、ライティングに必要な情報をメモ帳4枚へ書き込む。

// このモデルの「材質メモ」: C++側(GBufferShader)から渡される
cbuffer CbGBufferMaterial : register(b1)
{
    float4 materialColor; // テクスチャに乗算する色
    float4 emissiveColor; // rgb=自己発光の色 a=強さ
    float4 surfaceParams; // x=metallic y=roughness z=トゥーン段数 w=シェーディングモデルID
    float4 options;       // x=テクスチャ無視(Flat用) yzw=予備
    float4 textureFlags;
    float4 textureChannels;
    float4 uvParams;
    float4 materialEmissive; // xyzが発光色、wが強さ。
    float4 materialExtras;   // x=height strength, y=displacement midpoint, z=リム発光(0/1)
};

Texture2D DiffuseMap : register(t0);
Texture2D NormalMap : register(t1);
Texture2D RoughnessMap : register(t2);
Texture2D MetallicMap : register(t3);
Texture2D EmissiveMap : register(t4);
Texture2D HeightMap : register(t5);
SamplerState LinearSampler : register(s0);

float SelectChannel(float4 value, float channelValue)
{
    int channel = clamp((int) (channelValue + 0.5f), 0, 3);
    return value[channel];
}

float2 ApplyParallax(float2 uv, VS_OUT pin)
{
    if (textureChannels.w < 0.5f || options.x > 0.5f ||
        dot(pin.tangent, pin.tangent) <= 0.000001f ||
        abs(materialExtras.x) <= 0.000001f)
    {
        return uv;
    }

    float3 n = normalize(pin.normal);
    float3 t = normalize(pin.tangent - n * dot(n, pin.tangent));
    float3 b = normalize(cross(n, t));
    float3 viewWS = normalize(cameraPosition.xyz - pin.positionWS);
    float3 viewTS = float3(dot(viewWS, t), dot(viewWS, b), dot(viewWS, n));
    float height = SelectChannel(HeightMap.Sample(LinearSampler, uv), textureChannels.z)
        - materialExtras.y;
    return uv - (viewTS.xy / max(abs(viewTS.z), 0.2f)) * height * materialExtras.x;
}

PSGBufferOut main(VS_OUT pin)
{
    // スカルプト地形チャンク内の平面(y≈0)は地形メッシュへ置き換えるため描かない
    if (ShouldCarveTerrain(pin.positionWS))
    {
        discard;
    }

    // Flat(単色)のときはテクスチャを掛けない
    float2 tiledUV = ApplyParallax(pin.texcoord * uvParams.xy, pin);
    float4 color = materialColor;
    if (options.x < 0.5f)
    {
        color *= DiffuseMap.Sample(LinearSampler, tiledUV);
    }

    float metallic = saturate(surfaceParams.x);
    float roughness = clamp(surfaceParams.y, 0.04f, 1.0f);
    if (textureFlags.y > 0.5f)
    {
        roughness *= SelectChannel(RoughnessMap.Sample(LinearSampler, tiledUV), textureChannels.x);
        roughness = clamp(roughness, 0.04f, 1.0f);
    }
    if (textureFlags.z > 0.5f)
    {
        metallic *= SelectChannel(MetallicMap.Sample(LinearSampler, tiledUV), textureChannels.y);
        metallic = saturate(metallic);
    }

    float3 normal = normalize(pin.normal);
    if (textureFlags.x > 0.5f && dot(pin.tangent, pin.tangent) > 0.000001f)
    {
        float3 tangent = normalize(pin.tangent - normal * dot(normal, pin.tangent));
        float3 bitangent = normalize(cross(normal, tangent));
        float3 normalTS = NormalMap.Sample(LinearSampler, tiledUV).xyz * 2.0f - 1.0f;
        normalTS.xy *= max(options.z, 0.0f);
        normal = normalize(normalTS.x * tangent + normalTS.y * bitangent + normalTS.z * normal);
    }

    GBufferData data = (GBufferData) 0;
    data.baseColor    = color.rgb;
    // 自己発光。options.w > 0 の間は「輪郭だけ光らせる」リム発光になる。
    // 攻撃予兆で機体全体を単色で塗り潰すと、形が消えてバグのように見えるため、
    // 縁が強く光り面は素の色が残る出し方にして、シルエットで危険を伝える。
    {
        float emissiveShape = 1.0f;
        if (materialExtras.z > 0.0f)
        {
            const float3 viewDirection =
                normalize(cameraPosition.xyz - pin.positionWS);
            const float facing = saturate(dot(normal, viewDirection));
            // 縁(視線と面が直交)ほど1へ。pow で縁だけに絞る。
            float rim = pow(1.0f - facing, 2.4f);
            // 面側にも薄く残して、暗所で機体を見失わないようにする。
            emissiveShape = saturate(rim * 1.35f + 0.12f);
        }
        data.emissive = emissiveColor.rgb * emissiveColor.a * emissiveShape;
    }
    if (textureFlags.w > 0.5f && options.x < 0.5f)
    {
        data.emissive += EmissiveMap.Sample(LinearSampler, tiledUV).rgb
            * materialEmissive.rgb * max(materialEmissive.a, 0.0f);
    }
    data.worldNormal  = normal;
    data.ndcDepth     = pin.ndcPosition.z / pin.ndcPosition.w;
    data.metallic     = metallic;
    data.roughness    = roughness;
    data.ambientOcclusion = saturate(options.y);
    data.exposure     = max(options.w, 0.01f);
    data.toonSteps    = surfaceParams.z;
    data.shadingModel = (int) surfaceParams.w;

    return EncodeGBuffer(data);
}
