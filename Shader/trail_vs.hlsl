// 軌跡頂点を画面座標へ変換して色とUVを渡す頂点シェーダー。
cbuffer SCENE_CONSTANT_BUFFER : register(b1)
{
    row_major float4x4 view_projection;
    float4 light_direction;
    float4 camera_position;
};

struct VS_IN
{
    float3 position : POSITION;
    float4 color    : COLOR;
    float2 texcoord : TEXCOORD;
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    float2 texcoord : TEXCOORD;
};

VS_OUT main(VS_IN vin)
{
    VS_OUT o;
    o.position = mul(float4(vin.position, 1.0f), view_projection);
    o.color    = vin.color;
    o.texcoord = vin.texcoord;
    return o;
}
