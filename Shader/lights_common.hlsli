// 点光源とスポット光源の定数と照明計算を共通定義する。
#ifndef __LIGHTS_COMMON_HLSLI__
#define __LIGHTS_COMMON_HLSLI__

#include "local_shadow_common.hlsli"

#define POINT_LIGHT_MAX 8
#define SPOT_LIGHT_MAX  4

struct PointLight
{
    float4 position;   // xyz=位置 w=半径
    float4 color;      // rgb=色 a=強度
    // x=影マップの先頭スライス (負なら影なし) y=影の濃さ z/w=予約
    float4 shadow;
};
struct SpotLight
{
    float4 position;   // xyz=位置 w=半径
    float4 direction;  // xyz=方向 w=内コーン (cos)
    float4 color;      // rgb=色 a=外コーン (cos)
    // x=強度 y=影マップのスライス (負なら影なし) z=影の濃さ w=予約
    float4 params;
};

cbuffer LIGHTS_CONSTANT_BUFFER : register(b10)
{
    PointLight point_lights[POINT_LIGHT_MAX];
    SpotLight  spot_lights[SPOT_LIGHT_MAX];
    int4       light_counts; // x=point, y=spot, z/w=unused
};

// receive_shadow: 1=影を受ける / 0=受けない。0 なら影マップを読まない。
float3 evaluate_point_lights(float3 wp, float3 N, float3 V, float3 base_color,
                             float roughness, float metallic, float receive_shadow)
{
    float3 result = 0;
    [unroll] for (int i = 0; i < POINT_LIGHT_MAX; ++i)
    {
        if (i >= light_counts.x) break;
        PointLight pl = point_lights[i];
        float3 to_l = pl.position.xyz - wp;
        float d = length(to_l);
        if (d > pl.position.w) continue;
        float3 L = to_l / max(d, 0.0001f);
        float falloff = saturate(1.0f - d / pl.position.w);
        falloff *= falloff;
        float NoL = saturate(dot(N, L));
        if (NoL <= 0.0f) continue;
        // shadow.x が負のライトは影マップを持たないのでサンプルしない。
        float shadow = receive_shadow > 0.5f
            ? local_shadow_point((int) pl.shadow.x, pl.shadow.y,
                pl.position.xyz, wp, N, NoL)
            : 1.0f;
        result += pl.color.rgb * pl.color.a * NoL * falloff * base_color * shadow;
    }
    return result;
}

float3 evaluate_spot_lights(float3 wp, float3 N, float3 V, float3 base_color,
                            float receive_shadow)
{
    float3 result = 0;
    [unroll] for (int i = 0; i < SPOT_LIGHT_MAX; ++i)
    {
        if (i >= light_counts.y) break;
        SpotLight sl = spot_lights[i];
        float3 to_l = sl.position.xyz - wp;
        float d = length(to_l);
        if (d > sl.position.w) continue;
        float3 L = to_l / max(d, 0.0001f);
        float cos_angle = dot(-L, normalize(sl.direction.xyz));
        float inner = sl.direction.w;
        float outer = sl.color.a;
        if (cos_angle < outer) continue;
        float cone = smoothstep(outer, inner, cos_angle);
        float falloff = saturate(1.0f - d / sl.position.w);
        falloff *= falloff;
        float NoL = saturate(dot(N, L));
        if (NoL <= 0.0f) continue;
        float shadow = receive_shadow > 0.5f
            ? local_shadow_spot((int) sl.params.y, sl.params.z, wp, N, d, NoL)
            : 1.0f;
        result += sl.color.rgb * sl.params.x * NoL * falloff * cone *
            base_color * shadow;
    }
    return result;
}

#endif
