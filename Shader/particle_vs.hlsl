// GPUバッファから生存中のパーティクルを読み出す頂点シェーダー。
#include "particle.hlsli"

StructuredBuffer<particle> particles : register(t0);

struct VS_OUT
{
    float4 position : SV_POSITION; // ここは点のワールド座標を入れる (GSで投影)
    float4 color    : COLOR;
    float2 size_rot : TEXCOORD0;   // x=size y=rotation
};

VS_OUT main(uint vid : SV_VertexID)
{
    particle p = particles[vid];
    VS_OUT o;
    o.position = float4(p.position, p.age < p.life ? 1.0f : 0.0f);
    o.color    = p.color;
    o.size_rot = float2(p.size, p.rotation);
    return o;
}
