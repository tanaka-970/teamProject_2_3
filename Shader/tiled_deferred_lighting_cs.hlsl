// タイルドDeferredライティング。
//
// 1ディスパッチで「深度範囲の算出 → ライトのカリング → シェーディング」を行う。
//   1. 16x16の各スレッドが自分のピクセル深度を読み、タイルの最小/最大深度を求める。
//      InterlockedMin/Max はuintしか扱えないので、深度をビットパターンとして比較する
//      (非負のfloatはIEEE754のビット列がそのまま単調増加になる性質を使う)。
//   2. タイルのスクリーン範囲から錐台6面をビュー空間で作る。近/遠面は1で求めた
//      実際の深度範囲を使うため、タイル内に何も無ければライトはほぼ全て落ちる。
//   3. 各スレッドがライトを分担して球と錐台の交差判定を行い、通ったものだけを
//      groupsharedのリストへ詰める。ここが「タイルごとにライトを絞る」本体。
//   4. リストのライトだけでPBRを評価し、UAVへ書き込む。
//
// フルスクリーンPS版(deferred_lighting_ps.hlsl)との違いは、点光源/スポットの
// 評価回数がタイル単位で絞られること。ライト数を増やしたときの伸びが緩くなる。

// Sample()を含むPS専用関数を除外する。CSでは勾配が取れないため使えない。
#define PBR_COMPUTE_SHADER

#include "frame_common.hlsli"
#include "gbuffer_common.hlsli"
#include "csm_common.hlsli"
#include "tiled_light_common.hlsli"

cbuffer SCENE_CONSTANT_BUFFER : register(b1)
{
    row_major float4x4 view_projection;
    float4 light_direction;
    float4 camera_position;
};

Texture2D gb_base   : register(t0);
Texture2D gb_emi    : register(t1);
Texture2D gb_normal : register(t2);
Texture2D gb_param  : register(t3);
Texture2D gb_depth  : register(t6);
Texture2D ss_ambient_occlusion : register(t7);
Texture2D ss_reflection        : register(t8);

RWTexture2D<float4> output_color : register(u0);

// PBR評価はシーン定数(camera_position/light_direction)を参照するため後で読む。
#include "pbr_brdf.hlsli"

// --- タイル共有メモリ -------------------------------------------------------
groupshared uint tile_min_depth_bits;
groupshared uint tile_max_depth_bits;
groupshared uint tile_light_count;
groupshared uint tile_light_indices[TILE_MAX_LIGHT];

// スクリーン上のタイル境界からビュー空間の錐台側面を作る。
// 平面は float4(法線xyz, 距離w) で、内側が正になる向きに揃える。
float4 make_plane_from_origin(float3 point_a, float3 point_b)
{
    // 原点(カメラ)を通る平面なのでwは0。
    return float4(normalize(cross(point_a, point_b)), 0.0f);
}

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(uint3 group_id : SV_GroupID,
          uint3 dispatch_id : SV_DispatchThreadID,
          uint3 thread_id : SV_GroupThreadID,
          uint group_index : SV_GroupIndex)
{
    const int2 screen_size = int2(frame_screen_size.xy);
    const int2 pixel = int2(dispatch_id.xy);

    // --- 1. タイルの深度範囲 ------------------------------------------------
    if (group_index == 0)
    {
        tile_min_depth_bits = 0x7F7FFFFFu; // ほぼFLT_MAX
        tile_max_depth_bits = 0u;
        tile_light_count = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    float device_z = 1.0f;
    bool inside_screen = all(pixel < screen_size);
    if (inside_screen) device_z = gb_depth.Load(int3(pixel, 0)).r;

    // 背景は深度範囲に含めない。含めると錐台が遠方まで伸びて絞り込みが効かない。
    const bool is_background = device_z >= 0.999999f;
    float view_z = 0.0f;
    if (inside_screen && !is_background)
    {
        view_z = view_position_from_depth(
            (float2(pixel) + 0.5f) * frame_screen_size.zw, device_z).z;
        const uint depth_bits = asuint(max(view_z, 0.0f));
        InterlockedMin(tile_min_depth_bits, depth_bits);
        InterlockedMax(tile_max_depth_bits, depth_bits);
    }
    GroupMemoryBarrierWithGroupSync();

    const float tile_min_z = asfloat(tile_min_depth_bits);
    const float tile_max_z = asfloat(tile_max_depth_bits);
    // タイル全体が背景なら min > max のまま。ライト評価は不要。
    const bool tile_has_geometry = tile_max_z >= tile_min_z;

    // --- 2. タイル錐台の構築 ------------------------------------------------
    // タイルの四隅をNDCへ変換し、逆射影でビュー空間の方向ベクトルを得る。
    float4 tile_planes[4];
    {
        const float2 tile_pixel_min = float2(group_id.xy) * float(TILE_SIZE);
        const float2 tile_pixel_max = tile_pixel_min + float(TILE_SIZE);
        const float2 uv_min = tile_pixel_min * frame_screen_size.zw;
        const float2 uv_max = tile_pixel_max * frame_screen_size.zw;

        // 近クリップ面上の4隅(ビュー空間)。原点からの方向として使う。
        const float3 corner_00 = view_position_from_depth(float2(uv_min.x, uv_min.y), 0.0f);
        const float3 corner_10 = view_position_from_depth(float2(uv_max.x, uv_min.y), 0.0f);
        const float3 corner_11 = view_position_from_depth(float2(uv_max.x, uv_max.y), 0.0f);
        const float3 corner_01 = view_position_from_depth(float2(uv_min.x, uv_max.y), 0.0f);

        // 隣り合う2隅と原点で1つの側面になる。外積の向きを揃えて内側を正にする。
        tile_planes[0] = make_plane_from_origin(corner_00, corner_01); // 左
        tile_planes[1] = make_plane_from_origin(corner_11, corner_10); // 右
        tile_planes[2] = make_plane_from_origin(corner_10, corner_00); // 上
        tile_planes[3] = make_plane_from_origin(corner_01, corner_11); // 下
    }

    // --- 3. ライトのカリング ------------------------------------------------
    const uint light_count = (uint) max(tiled_counts.x, 0);
    if (tile_has_geometry)
    {
        // 256スレッドでライトを分担する。
        for (uint light_index = group_index; light_index < light_count;
             light_index += TILE_SIZE * TILE_SIZE)
        {
            const TiledLight light = tiled_lights[light_index];
            const float radius = light.position_radius.w;
            if (radius <= 0.0f) continue;

            // ライト中心をビュー空間へ。
            const float3 light_view = mul(
                float4(light.position_radius.xyz, 1.0f), frame_view).xyz;

            // 近/遠面: タイル内の実際の深度範囲で判定する。
            if (light_view.z + radius < tile_min_z) continue;
            if (light_view.z - radius > tile_max_z) continue;

            // 側面: 球の中心と平面の符号付き距離が -radius 未満なら外側。
            bool visible = true;
            [unroll] for (int plane = 0; plane < 4; ++plane)
            {
                if (dot(tile_planes[plane].xyz, light_view) - radius > 0.0f)
                {
                    visible = false;
                    break;
                }
            }
            if (!visible) continue;

            uint slot = 0;
            InterlockedAdd(tile_light_count, 1u, slot);
            if (slot < TILE_MAX_LIGHT) tile_light_indices[slot] = light_index;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (!inside_screen) return;

    const uint visible_light_count = min(tile_light_count, (uint) TILE_MAX_LIGHT);

    // タイルあたりのライト数をヒートマップで可視化する(デバッグ表示)。
    if (tiled_counts.w == 1)
    {
        const float load = saturate(float(visible_light_count) /
            max(tiled_params.x, 1.0f));
        // 青→緑→赤。負荷の高いタイルが赤くなる。
        const float3 heat = load < 0.5f
            ? lerp(float3(0, 0, 1), float3(0, 1, 0), load * 2.0f)
            : lerp(float3(0, 1, 0), float3(1, 0, 0), (load - 0.5f) * 2.0f);
        output_color[pixel] = float4(heat, 1.0f);
        return;
    }

    // --- 4. シェーディング --------------------------------------------------
    const float4 base = gb_base.Load(int3(pixel, 0));
    const float4 emissive_sample = gb_emi.Load(int3(pixel, 0));
    const float4 normal_sample = gb_normal.Load(int3(pixel, 0));
    const float4 param_sample = gb_param.Load(int3(pixel, 0));

    if (is_background)
    {
        output_color[pixel] = float4(base.rgb, 1.0f);
        return;
    }

    const GBufferData g = DecodeGBuffer(base, emissive_sample, normal_sample, param_sample);
    const float2 uv = (float2(pixel) + 0.5f) * frame_screen_size.zw;
    const float3 world_position = world_position_from_depth(uv, device_z);

    const float3 N = g.world_normal;
    const float3 V = normalize(camera_position.xyz - world_position);
    const float3 L = normalize(-light_direction.xyz);

    // スクリーン空間入力。未バインドなら「効果なし」へ読み替える。
    PbrScreenSpaceInputs screen = pbr_default_screen_inputs();
    {
        // SSAOは半解像度で走ることがあるため、ピクセル座標ではなくUVで読む。
        const float4 occlusion_sample =
            ss_ambient_occlusion.SampleLevel(pbr_sampler_linear, uv, 0);
        screen.ambient_occlusion = occlusion_sample.g > 0.0f
            ? saturate(occlusion_sample.r) : 1.0f;
        // SSRも半解像度で走ることがあるためUVで読む。
        const float4 reflection_sample =
            ss_reflection.SampleLevel(pbr_sampler_linear, uv, 0);
        screen.reflection_color = reflection_sample.rgb;
        screen.reflection_weight = saturate(reflection_sample.a);
    }

    // 影はCSMを優先し、無効時のみ従来の単一シャドウマップへ落とす。
    const float shadow_rotation_seed =
        interleaved_gradient_noise(float2(pixel), frame_params.x);
    // PS 版と同じ判定。片方だけ見るとタイルド ON/OFF で影が変わる。
    float shadow_visibility = 1.0f;
    if (g.receive_shadow)
    {
        shadow_visibility = csm_params.w >= 0.5f
            ? csm_sample_shadow_hq(world_position, N, view_z,
                saturate(dot(N, L)), shadow_rotation_seed)
            : sample_shadow(world_position);
    }

    // 平行光源 + IBL + SSAO/SSR は共通のPBR評価を使う。
    // (evaluate_pbr_ex内の点光源/スポットはCB配列版なので、ここでは使わない)
    // ここでの点光源/スポットは 0 灯。タイル内のライトは下のループで評価する。
    float3 color = evaluate_pbr_ex(g.base_color, g.emissive,
        g.metalness, g.roughness, g.occlusion,
        N, V, world_position, shadow_visibility, screen,
        g.receive_shadow ? 1.0f : 0.0f);

    // タイル内のライトだけを評価する。
    const float alpha_roughness = max(g.roughness * g.roughness, 0.0025f);
    const float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), g.base_color, g.metalness);
    const float3 f90 = float3(1.0f, 1.0f, 1.0f);
    const float3 diffuse_color = g.base_color * (1.0f - 0.04f) * (1.0f - g.metalness);
    const float NoV = clamp(dot(N, V), 0.0f, 1.0f);
    const float2 f_ab = env_dfg(NoV, g.roughness);
    const float3 energy_compensation = specular_energy_compensation(f0, f_ab);

    float3 local_lighting = float3(0.0f, 0.0f, 0.0f);
    for (uint slot = 0; slot < visible_light_count; ++slot)
    {
        const TiledLight light = tiled_lights[tile_light_indices[slot]];
        const float3 to_light = light.position_radius.xyz - world_position;
        const float distance_squared = dot(to_light, to_light);
        const float radius = light.position_radius.w;
        if (distance_squared > radius * radius) continue;

        const float3 light_dir = to_light * rsqrt(max(distance_squared, 1.0e-8f));
        const float NoL = saturate(dot(N, light_dir));
        if (NoL <= 0.0f) continue;

        float attenuation = tiled_distance_attenuation(distance_squared, radius);
        if ((int) light.params.y == TILED_LIGHT_TYPE_SPOT)
        {
            attenuation *= tiled_spot_attenuation(-light_dir,
                normalize(light.direction_cone.xyz),
                light.direction_cone.w, light.params.x);
        }
        if (attenuation <= 0.0f) continue;

        // 影は PS 版 (lights_common.hlsli) と同じ関数を使う。
        const int shadow_slice = (int) light.params.z;
        float local_shadow = 1.0f;
        if (shadow_slice >= 0 && g.receive_shadow)
        {
            local_shadow = ((int) light.params.y == TILED_LIGHT_TYPE_SPOT)
                ? local_shadow_spot(shadow_slice, light.params.w,
                    world_position, N, sqrt(max(distance_squared, 1.0e-8f)), NoL)
                : local_shadow_point(shadow_slice, light.params.w,
                    light.position_radius.xyz, world_position, N, NoL);
            if (local_shadow <= 0.0f) continue;
        }

        // 平行光源と同じBRDFを使い、点光源だけ簡易式になる不整合を避ける。
        const float3 H = normalize(light_dir + V);
        const float NoH = saturate(dot(N, H));
        const float VoH = saturate(dot(V, H));
        const float3 diffuse = brdf_lambertian(f0, f90, diffuse_color, VoH);
        const float3 specular = brdf_specular_ggx(f0, f90, alpha_roughness,
            VoH, NoL, NoV, NoH) * energy_compensation;

        local_lighting += (diffuse + specular) * NoL * attenuation *
            light.color_intensity.rgb * light.color_intensity.a * local_shadow;
    }

    color += local_lighting * max(ibl_params.w, 0.0f);
    output_color[pixel] = float4(color, 1.0f);
}
