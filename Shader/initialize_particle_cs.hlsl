// パーティクルの初期位置、速度、寿命を生成するコンピュートシェーダー。
#include "particle.hlsli"

RWStructuredBuffer<particle> particles : register(u0);

[numthreads(PARTICLE_THREADS, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= PARTICLE_MAX_COUNT) return;
    particle p = (particle) 0;
    p.age  = 1.0f;
    p.life = 1.0f;
    particles[dtid.x] = p;
}
