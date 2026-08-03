// スクリーンスペース反射のレイマーチ。
//
// 設計:
//   - GGXの重要度サンプリングで1本のレイを飛ばす(確率的SSR)。ラフネスに応じて
//     散らばるので、鏡面から粗い金属まで同じコードで扱える。分散はresolveで潰す。
//   - 画面空間の探索は uv と 1/z を線形補間する。1/z はスクリーン空間で線形なので、
//     ビュー空間で等間隔に進めるより正確で、遠方でのすり抜けが起きない。
//   - 交差したら二分探索で境界を詰め、厚み(thickness)判定で誤ヒットを捨てる。
//   - 色は「前フレームのライティング結果」から取る。ヒット点のワールド座標を
//     前フレームのビュー射影で投影するため、カメラが動いてもズレない。
//
// 出力: rgb=反射色, a=信頼度(0で反射なし)

#include "../../fullscreen_quad.hlsli"
#include "ssr_common.hlsli"

Texture2D scene_depth    : register(t0);
Texture2D gbuffer_normal : register(t1);
Texture2D gbuffer_param  : register(t2);
Texture2D color_history  : register(t3);

// uv の位置のビュー空間zを返す。背景は far。
float sample_view_depth(float2 uv)
{
    float device_z = scene_depth.SampleLevel(ssr_sampler_point, uv, 0).r;
    float result = frame_camera_planes.y * 4.0f;
    if (device_z < 0.999999f) result = view_position_from_depth(uv, device_z).z;
    return result;
}

float4 main(VS_OUT pin) : SV_TARGET
{
    const float2 uv = pin.texcoord;

    if (ssr_params2.x < 0.5f || ssr_params2.w < 0.5f) return float4(0, 0, 0, 0);

    const float device_z = scene_depth.SampleLevel(ssr_sampler_point, uv, 0).r;
    if (device_z >= 0.999999f) return float4(0, 0, 0, 0);

    // ラフネスが高い面はSSRの寄与が小さく、コストだけ増えるので早期に打ち切る。
    const float4 material = gbuffer_param.SampleLevel(ssr_sampler_point, uv, 0);
    const float roughness = saturate(material.g);
    const float max_roughness = max(ssr_params1.x, 1.0e-3f);
    if (roughness > max_roughness) return float4(0, 0, 0, 0);

    const float3 P = view_position_from_depth(uv, device_z);
    const float3 world_normal = normalize(
        gbuffer_normal.SampleLevel(ssr_sampler_point, uv, 0).xyz * 2.0f - 1.0f);
    const float3 N = world_to_view_direction(world_normal);
    const float3 V = normalize(-P);
    if (dot(N, V) <= 0.0f) return float4(0, 0, 0, 0);

    // 確率的に半ベクトルを選ぶ。フレーム番号を混ぜてTAA/resolveで平均化させる。
    const float noise = interleaved_gradient_noise(pin.position.xy, frame_params.x);
    const uint sample_index = (uint) (frame_params.x) & 15u;
    float2 random = ssr_hammersley(sample_index, 16u);
    random = frac(random + noise);

    const float3 H = ssr_importance_sample_ggx(random, roughness, N);
    float3 R = reflect(-V, H);
    if (dot(R, N) <= 0.0f) R = reflect(-V, N); // 裏を向いたら鏡面反射へ戻す

    // レイの始点は自己交差を避けて法線方向へ少し押し出す。
    const float depth_scale = max(P.z, 1.0f);
    const float3 ray_origin = P + N * ssr_params2.z * depth_scale * 0.01f;

    // 終点はニアクリップより手前に来ないよう長さを詰める。
    float ray_length = max(ssr_params0.x, 0.1f);
    const float near_plane = max(frame_camera_planes.x, 1.0e-3f);
    if (ray_origin.z + R.z * ray_length < near_plane)
        ray_length = (near_plane - ray_origin.z) / R.z;
    if (ray_length <= 0.0f) return float4(0, 0, 0, 0);
    const float3 ray_end = ray_origin + R * ray_length;

    // 始点/終点を画面へ落とし、uv と 1/z を線形補間する準備をする。
    const float3 start_projection = view_position_to_uv_depth(ray_origin);
    const float3 end_projection = view_position_to_uv_depth(ray_end);
    const float2 start_uv = start_projection.xy;
    const float2 end_uv = end_projection.xy;
    const float inv_z_start = 1.0f / max(ray_origin.z, 1.0e-4f);
    const float inv_z_end = 1.0f / max(ray_end.z, 1.0e-4f);

    // 画面上の移動量からステップ数を決める。stride はピクセル単位。
    // ステップ数はSSRパスの解像度で数える(半解像度なら半分のステップで足りる)。
    const float2 pixel_delta = (end_uv - start_uv) * ssr_target_size.xy;
    const float pixel_distance = length(pixel_delta);
    if (pixel_distance < 1.0f) return float4(0, 0, 0, 0);

    const float stride = max(ssr_params0.z, 1.0f);
    const int max_step = (int) clamp(ssr_params0.w, 4.0f, 128.0f);
    const int step_count = (int) clamp(pixel_distance / stride, 4.0f, (float) max_step);
    const float step_size = 1.0f / (float) step_count;

    // ステップ位相をずらしてバンディングを散らす。
    const float jitter = frac(noise + 0.5f);
    const float thickness = max(ssr_params0.y, 1.0e-3f);

    float previous_t = 0.0f;
    float previous_ray_z = ray_origin.z;
    bool hit = false;
    float hit_t = 0.0f;

    [loop] for (int step = 1; step <= step_count; ++step)
    {
        const float t = saturate(((float) step - 1.0f + jitter) * step_size);
        const float2 sample_uv = lerp(start_uv, end_uv, t);
        if (any(sample_uv < 0.0f) || any(sample_uv > 1.0f)) break;

        // 1/z の線形補間 = スクリーン空間で正しい奥行き。
        const float ray_z = 1.0f / max(lerp(inv_z_start, inv_z_end, t), 1.0e-6f);
        const float scene_z = sample_view_depth(sample_uv);

        // レイがシーン表面より奥へ潜り、かつ厚みの範囲内なら交差。
        const float difference = ray_z - scene_z;
        if (difference > 0.0f && difference < thickness * max(scene_z, 1.0f) * 0.05f + thickness)
        {
            hit = true;
            hit_t = t;
            break;
        }

        previous_t = t;
        previous_ray_z = ray_z;
    }

    if (!hit) return float4(0, 0, 0, 0);

    // 二分探索で交差点を詰める。粗いstrideでも輪郭が階段状にならない。
    const int refine_step = (int) clamp(ssr_params1.w, 0.0f, 8.0f);
    float low = previous_t;
    float high = hit_t;
    [loop] for (int refine = 0; refine < refine_step; ++refine)
    {
        const float mid = (low + high) * 0.5f;
        const float2 mid_uv = lerp(start_uv, end_uv, mid);
        const float mid_ray_z = 1.0f / max(lerp(inv_z_start, inv_z_end, mid), 1.0e-6f);
        const float mid_scene_z = sample_view_depth(mid_uv);
        if (mid_ray_z - mid_scene_z > 0.0f) high = mid;
        else low = mid;
    }

    const float2 hit_uv = lerp(start_uv, end_uv, high);
    const float hit_device_z = scene_depth.SampleLevel(ssr_sampler_point, hit_uv, 0).r;
    if (hit_device_z >= 0.999999f) return float4(0, 0, 0, 0);

    // ヒット点をワールドへ戻し、前フレームのビュー射影で履歴を引く。
    const float3 hit_view_position = view_position_from_depth(hit_uv, hit_device_z);
    const float4 hit_world = mul(float4(hit_view_position, 1.0f), frame_inv_view);
    float4 previous_clip = mul(float4(hit_world.xyz, 1.0f), frame_prev_view_projection);
    previous_clip.xyz /= max(previous_clip.w, 1.0e-6f);
    const float2 history_uv = ndc_to_uv(previous_clip.xy);
    if (any(history_uv < 0.0f) || any(history_uv > 1.0f)) return float4(0, 0, 0, 0);

    // ラフネスに応じてミップを上げ、粗い面の反射をあらかじめぼかす。
    const float lod = roughness * max(ssr_params3.y, 0.0f);
    float3 reflection = color_history.SampleLevel(ssr_sampler_linear, history_uv, lod).rgb;

    // 信頼度: 画面端 / ラフネス / カメラへ向かうレイ で落とす。
    float confidence = ssr_screen_edge_fade(hit_uv, max(ssr_params1.z, 1.0e-4f));
    confidence *= saturate(1.0f - roughness / max_roughness);
    // 視線と逆向き(カメラへ戻る方向)のレイは情報が画面に無いので弱める。
    confidence *= saturate(1.0f - dot(R, V) * 1.2f);
    confidence *= saturate(ssr_params1.y);

    if (confidence <= 0.0f) return float4(0, 0, 0, 0);
    return float4(reflection, confidence);
}
