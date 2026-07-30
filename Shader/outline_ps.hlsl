// 膨張させたメッシュを単色で描いて輪郭を作るピクセルシェーダー。
struct VS_OUT
{
    float4 position       : SV_POSITION;
    float4 world_position : POSITION;
    float4 world_normal   : NORMAL;
    float4 world_tangent  : TANGENT;
    float2 texcoord       : TEXCOORD;
    float4 color          : COLOR;
};

float4 main(VS_OUT pin) : SV_TARGET
{
    return float4(pin.color.rgb, 1.0f);
}
