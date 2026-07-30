// 同一モデルをまとめてG-Bufferへ描く頂点シェーダー。
#include "GBufferModel.hlsli"

// G-Bufferインスタンス描画用の頂点シェーダー。
// 同じモデルの配置オブジェクトを1回のDrawIndexedInstancedでまとめて描くため、
// ワールド行列をCbSkeletonではなく頂点ストリーム1(インスタンス毎データ)から
// 受け取る。剛体(スキニング無し)メッシュ専用。スキン付きモデルは従来の
// GBufferVSを使うこと。
// 行列はC++側のXMFLOAT4X4(行優先)をそのまま4つのfloat4行として渡し、
// float4x4(row0..row3)で再構成する。mul(v, M)の行ベクトル規約は
// Scene.hlsliのrow_major CBと同じ。

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

VS_OUT main(
    float4 position     : POSITION,
    float2 texcoord     : TEXCOORD,
    float3 normal       : NORMAL,
    float3 tangent      : TANGENT,
    float4 instanceRow0 : INSTANCE_TRANSFORM0,
    float4 instanceRow1 : INSTANCE_TRANSFORM1,
    float4 instanceRow2 : INSTANCE_TRANSFORM2,
    float4 instanceRow3 : INSTANCE_TRANSFORM3)
{
    VS_OUT vout = (VS_OUT) 0;

    float4x4 world = float4x4(
        instanceRow0, instanceRow1, instanceRow2, instanceRow3);

    position = mul(position, world);
    float3 normalWS = normalize(mul(float4(normal, 0), world).xyz);
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
    vout.tangent = mul(float4(tangent, 0), world).xyz;

    return vout;
}
