// SSAO(GTAO)パスが共有する定数とサンプラー。
#ifndef __SSAO_COMMON_HLSLI__
#define __SSAO_COMMON_HLSLI__

#include "../../frame_common.hlsli"

cbuffer SSAO_CONSTANT_BUFFER : register(b12)
{
    float4 ssao_params0; // x=radius(world), y=intensity, z=power, w=thin_occluder_compensation
    float4 ssao_params1; // x=slice_count, y=step_count, z=max_pixel_radius, w=min_pixel_radius
    float4 ssao_params2; // x=fade_start(view z), y=fade_end, z=blur_dir_x, w=blur_dir_y
    float4 ssao_params3; // x=blur_sharpness, y=normal_bias, z=enable, w=予約
    // AOパス自体の解像度。frame_screen_sizeはフル解像度なので、
    // 半解像度で走るときはこちらを使ってピクセル半径やタップ間隔を決める。
    float4 ssao_target_size; // x=w, y=h, z=1/w, w=1/h
};

SamplerState ssao_sampler_point  : register(s0);
SamplerState ssao_sampler_linear : register(s1);

static const float SSAO_PI      = 3.14159265358979f;
static const float SSAO_HALF_PI = 1.57079632679490f;

#endif
