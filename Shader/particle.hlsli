// GPUパーティクルの構造体と最大数を共通定義する。
#ifndef __PARTICLE_HLSLI__
#define __PARTICLE_HLSLI__

#define PARTICLE_MAX_COUNT 4096
#define PARTICLE_THREADS   64

struct particle
{
    float3 position;
    float  age;
    float3 velocity;
    float  life;        // 全寿命 (s)
    float4 color;
    float  size;
    float  rotation;
    float2 padding_;
};

cbuffer PARTICLE_CONSTANTS : register(b6)
{
    float4 spawn_origin;       // xyz=位置, w=spawn_rate(/sec)
    float4 spawn_direction;    // xyz=平均方向, w=cone角(rad)
    float4 spawn_params;       // x=min_speed, y=max_speed, z=min_life, w=max_life
    float4 spawn_color;        // 初期色
    float4 spawn_scalar;       // x=size, y=rotation_speed, z=gravity, w=damping
    float4 simulation_time;    // x=delta, y=total, z=rand_seed, w=spawn_count
};

#endif
