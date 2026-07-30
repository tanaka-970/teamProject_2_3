// 静的メッシュをCSMの深度描画用座標へ変換する頂点シェーダー。
cbuffer OBJECT_CONSTANT_BUFFER : register(b0)
{
    row_major float4x4 world;
    float4 material_color;
};

struct VS_IN
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD;
};

struct GS_IN
{
    float4 world_position : POSITION;
};

GS_IN main(VS_IN vin)
{
    GS_IN o;
    o.world_position = mul(float4(vin.position, 1.0f), world);
    return o;
}
