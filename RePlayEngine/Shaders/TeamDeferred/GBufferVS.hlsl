// スキニングと変位を適用してG-Buffer用頂点を生成するシェーダー。
#include "GBufferModel.hlsli"
#include "Skinning.hlsli"

cbuffer CbDisplacement : register(b2)
{
    float4 displacementParams;   // 有効、強さ、中心値、画像の成分。
    float4 displacementUvParams; // xyが画像の繰り返し数。
};

Texture2D DisplacementMap : register(t0);
SamplerState DisplacementSampler : register(s0);

float SelectDisplacementChannel(float4 value, float channelValue)
{
    int channel = clamp((int)(channelValue + 0.5f), 0, 3);
    return value[channel];
}

// ジオメトリパス頂点シェーダー
// 普通の描画と同じくスキニングして画面に配置するだけ。
// ライティング計算は一切しない(それは後のライティングパスの仕事)。
VS_OUT main(
    float4 position    : POSITION,
    float4 boneWeights : BONE_WEIGHTS,
    uint4  boneIndices : BONE_INDICES,
    float2 texcoord    : TEXCOORD,
    float3 normal      : NORMAL,
    float3 tangent     : TANGENT)
{
    VS_OUT vout = (VS_OUT) 0;

    position = SkinningPosition(position, boneWeights, boneIndices);
    float3 normalWS = normalize(SkinningVector(normal, boneWeights, boneIndices));
    if (displacementParams.x > 0.5f && abs(displacementParams.y) > 0.000001f)
    {
        float height = SelectDisplacementChannel(
            DisplacementMap.SampleLevel(
                DisplacementSampler,
                texcoord * displacementUvParams.xy,
                0.0f),
            displacementParams.w);
        position.xyz += normalWS * ((height - displacementParams.z) * displacementParams.y);
    }
    vout.positionWS = position.xyz;
    vout.vertex = mul(position, viewProjection);
    vout.ndcPosition = vout.vertex; // PSで z/w して深度メモにする
    vout.texcoord = texcoord;
    vout.normal = normalWS;
    vout.tangent = SkinningVector(tangent, boneWeights, boneIndices);

    return vout;
}
