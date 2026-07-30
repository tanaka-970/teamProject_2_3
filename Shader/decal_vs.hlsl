// デカール投影用ボックスを画面へ変換する頂点シェーダー。
cbuffer DECAL_CONSTANT_BUFFER : register(b7)
{
    row_major float4x4 decal_world;             // 直方体のローカル→ワールド
    row_major float4x4 decal_world_inverse;     // PS でワールド→ローカルに使う
    float4             decal_color;             // rgb=tint, a=intensity
    float4             decal_params;            // x=enable, y/z/w=unused
};
cbuffer SCENE_CONSTANT_BUFFER : register(b1)
{
    row_major float4x4 view_projection;
    float4 light_direction;
    float4 camera_position;
};

struct VS_IN  { float3 position : POSITION; };
struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 worldpos : TEXCOORD0;
};

VS_OUT main(VS_IN vin)
{
    VS_OUT o;
    float4 wp = mul(float4(vin.position, 1.0f), decal_world);
    o.position = mul(wp, view_projection);
    o.worldpos = wp;
    return o;
}
