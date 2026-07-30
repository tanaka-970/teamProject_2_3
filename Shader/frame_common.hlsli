// SSAO/SSR/TAAが共有するカメラ定数と、深度からの座標復元ヘルパー。
// RePlayEngine\Rendering\FrameConstants.h と並びを一致させること。
#ifndef __FRAME_COMMON_HLSLI__
#define __FRAME_COMMON_HLSLI__

// b9はG-BufferのMATERIAL_OVERRIDE、b7はデカール/アウトラインが使っているため、
// 競合しないb4を使う(D3D11の定数バッファはb0..b13までしか無い)。
cbuffer FRAME_CONSTANT_BUFFER : register(b4)
{
    row_major float4x4 frame_view;
    row_major float4x4 frame_projection;
    row_major float4x4 frame_view_projection;
    row_major float4x4 frame_inv_view;
    row_major float4x4 frame_inv_projection;
    row_major float4x4 frame_inv_view_projection;
    row_major float4x4 frame_prev_view_projection;
    float4 frame_camera_position;
    float4 frame_screen_size;   // x=w, y=h, z=1/w, w=1/h
    float4 frame_camera_planes; // x=near, y=far, z=tan(fovY/2), w=aspect
    float4 frame_jitter;        // xy=今フレーム, zw=前フレーム (NDC)
    float4 frame_params;        // x=frame_index, y=elapsed_time
};

// UVは左上原点。NDCへ変換するときにYを反転する。
float2 uv_to_ndc(float2 uv)
{
    return float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
}

float2 ndc_to_uv(float2 ndc)
{
    return float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
}

// 逆射影で復元するので、どんな射影行列(ゲーム側カメラ含む)でも一致する。
float3 view_position_from_depth(float2 uv, float device_z)
{
    float4 ndc = float4(uv_to_ndc(uv), device_z, 1.0f);
    float4 view_position = mul(ndc, frame_inv_projection);
    return view_position.xyz / max(view_position.w, 1.0e-8f);
}

float3 world_position_from_depth(float2 uv, float device_z)
{
    float4 ndc = float4(uv_to_ndc(uv), device_z, 1.0f);
    float4 world_position = mul(ndc, frame_inv_view_projection);
    return world_position.xyz / max(world_position.w, 1.0e-8f);
}

// 逆射影を使ってビュー空間zを求める。遠クリップ面上(z=1)は far を返す。
float linear_view_depth(float2 uv, float device_z)
{
    return view_position_from_depth(uv, device_z).z;
}

float3 world_to_view_direction(float3 world_direction)
{
    return normalize(mul(world_direction, (float3x3) frame_view));
}

float3 view_to_world_direction(float3 view_direction)
{
    return normalize(mul(view_direction, (float3x3) frame_inv_view));
}

// ビュー空間座標を今フレームのスクリーンUVへ投影する。
float3 view_position_to_uv_depth(float3 view_position)
{
    float4 clip = mul(float4(view_position, 1.0f), frame_projection);
    clip.xyz /= max(clip.w, 1.0e-8f);
    return float3(ndc_to_uv(clip.xy), clip.z);
}

// 空間的にも時間的にもばらける低コストなノイズ (Jimenez の interleaved gradient noise)。
float interleaved_gradient_noise(float2 pixel_position, float frame_index)
{
    pixel_position += frame_index * 5.588238f;
    return frac(52.9829189f * frac(dot(pixel_position, float2(0.06711056f, 0.00583715f))));
}

#endif
