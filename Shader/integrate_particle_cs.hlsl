// 速度と経過時間からパーティクル状態を更新するコンピュートシェーダー。
#include "particle.hlsli"

RWStructuredBuffer<particle> particles : register(u0);

uint hash_u(uint x)
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
float rand_f(uint x)
{
    return (hash_u(x) & 0x00FFFFFFu) / 16777216.0f;
}

[numthreads(PARTICLE_THREADS, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= PARTICLE_MAX_COUNT) return;

    particle p = particles[dtid.x];
    p.age += simulation_time.x;

    // 死亡したら再スポーン (spawn_count > 0 の場合のみ)
    if (p.age >= p.life)
    {
        if ((float) dtid.x < simulation_time.w)
        {
            uint seed = dtid.x * 2654435761u + asuint(simulation_time.z);
            float r0 = rand_f(seed + 0);
            float r1 = rand_f(seed + 1);
            float r2 = rand_f(seed + 2);
            float r3 = rand_f(seed + 3);

            float speed = lerp(spawn_params.x, spawn_params.y, r0);
            float3 dir  = normalize(spawn_direction.xyz);
            // コーン状に揺らす
            float cone = spawn_direction.w;
            float3 ortho = abs(dir.y) < 0.99f ? float3(0,1,0) : float3(1,0,0);
            float3 t1 = normalize(cross(dir, ortho));
            float3 t2 = cross(dir, t1);
            float a  = r1 * 6.2831853f;
            float rad = r2 * cone;
            float3 jitter = (cos(a) * t1 + sin(a) * t2) * sin(rad);
            float3 v = normalize(dir * cos(rad) + jitter) * speed;

            p.position = spawn_origin.xyz;
            p.velocity = v;
            p.age      = 0.0f;
            p.life     = lerp(spawn_params.z, spawn_params.w, r3);
            p.color    = spawn_color;
            p.size     = spawn_scalar.x;
            p.rotation = 0.0f;
        }
    }
    else
    {
        // 物理
        p.velocity.y -= spawn_scalar.z * simulation_time.x;
        p.velocity   *= max(0.0f, 1.0f - spawn_scalar.w * simulation_time.x);
        p.position   += p.velocity * simulation_time.x;
        p.rotation   += spawn_scalar.y * simulation_time.x;
        // 寿命に応じてアルファをフェード
        float t = saturate(p.age / max(p.life, 0.0001f));
        p.color.a = spawn_color.a * (1.0f - t);
    }

    particles[dtid.x] = p;
}
