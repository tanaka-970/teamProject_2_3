// GTAO (Ground Truth Ambient Occlusion) 相当の高品質SSAO。
//
// 半球サンプリング型のSSAOと違い、スライスごとに「地平線角」を探索して
// 可視性の積分を解析的に解くため、同じサンプル数でもバンディングと
// 平面上の偽オクルージョンが出にくい。
//   1. ピクセルのビュー空間位置Pと法線Nを深度とG-Bufferから復元する。
//   2. 画面上でスライス方向を回転させながら、左右の地平線角の最大値を探す。
//   3. Nをスライス平面へ射影し、GTAOの内側積分(コサイン重み)で可視性を得る。
// 出力: R=可視性(1=遮蔽なし), G=ビュー空間深度(バイラテラルブラー用)。

#include "../../fullscreen_quad.hlsli"
#include "ssao_common.hlsli"

Texture2D scene_depth   : register(t0);
Texture2D gbuffer_normal : register(t1);

float4 main(VS_OUT pin) : SV_TARGET
{
    const float2 uv = pin.texcoord;
    const float device_z = scene_depth.SampleLevel(ssao_sampler_point, uv, 0).r;

    // 遠クリップ面(背景)は遮蔽なしで抜ける。深度はブラー用に far を入れておく。
    if (device_z >= 0.999999f || ssao_params3.z < 0.5f)
        return float4(1.0f, frame_camera_planes.y, 0.0f, 0.0f);

    const float3 P = view_position_from_depth(uv, device_z);
    const float view_z = P.z;

    // 遠景はAOを薄くフェードして、精度不足のちらつきを避ける。
    const float fade_start = ssao_params2.x;
    const float fade_end   = max(ssao_params2.y, fade_start + 1.0e-3f);
    const float fade = 1.0f - saturate((view_z - fade_start) / (fade_end - fade_start));
    if (fade <= 0.0f) return float4(1.0f, view_z, 0.0f, 0.0f);

    // G-Bufferの法線はワールド空間なのでビュー空間へ移す。
    const float3 world_normal = normalize(
        gbuffer_normal.SampleLevel(ssao_sampler_point, uv, 0).xyz * 2.0f - 1.0f);
    const float3 N = world_to_view_direction(world_normal);
    const float3 V = normalize(-P);

    // ワールド半径をピクセル半径へ。projection._22 = 1/tan(fovY/2)。
    // 半解像度で走る場合はAOパス自身の高さを使う(そうしないと半径が倍になる)。
    const float projection_scale = 0.5f * ssao_target_size.y * frame_projection._22;
    const float world_radius = max(ssao_params0.x, 1.0e-3f);
    float pixel_radius = world_radius * projection_scale / max(view_z, 1.0e-4f);
    pixel_radius = clamp(pixel_radius, ssao_params1.w, ssao_params1.z);
    if (pixel_radius < 1.0f) return float4(1.0f, view_z, 0.0f, 0.0f);

    const int slice_count = (int) max(ssao_params1.x, 1.0f);
    const int step_count  = (int) max(ssao_params1.y, 1.0f);
    const float thin_compensation = saturate(ssao_params0.w);

    // 空間ノイズでスライスの回転とステップ位相をずらし、TAAで平均化されるようにする。
    const float noise = interleaved_gradient_noise(pin.position.xy, frame_params.x);
    const float slice_rotation_offset = noise * SSAO_PI;
    const float step_noise = frac(noise * 1.6180339887f);

    // 法線オフセットで自己遮蔽(アクネ)を抑える。
    const float3 origin = P + N * ssao_params3.y * max(view_z * 0.01f, 0.01f);

    float visibility_sum = 0.0f;

    for (int slice = 0; slice < slice_count; ++slice)
    {
        const float slice_angle =
            (float(slice) / float(slice_count)) * SSAO_PI + slice_rotation_offset;
        float sin_slice, cos_slice;
        sincos(slice_angle, sin_slice, cos_slice);
        const float2 slice_direction = float2(cos_slice, sin_slice);

        // スライス平面 (V と slice_direction が張る平面) の法線。
        const float3 slice_direction_view = float3(slice_direction, 0.0f);
        const float3 slice_plane_normal =
            normalize(cross(slice_direction_view, V));

        // Nをスライス平面へ射影する。
        const float3 projected_normal = N - slice_plane_normal * dot(N, slice_plane_normal);
        const float  projected_length = length(projected_normal);
        if (projected_length < 1.0e-4f) continue;
        const float3 projected_normal_unit = projected_normal / projected_length;

        // 射影法線の符号付き仰角 n。接線はスライス平面内でVに直交する向き。
        const float3 slice_tangent = normalize(cross(V, slice_plane_normal));
        const float  cos_n = clamp(dot(projected_normal_unit, V), -1.0f, 1.0f);
        const float  n = -sign(dot(projected_normal_unit, slice_tangent)) * acos(cos_n);

        // 左右それぞれの地平線コサインを探索する。初期値は視線方向の水平。
        float horizon_cos[2] = { -1.0f, -1.0f };

        [loop] for (int side = 0; side < 2; ++side)
        {
            const float2 direction = slice_direction * (side == 0 ? 1.0f : -1.0f);
            float attenuated_cos = -1.0f;

            [loop] for (int step = 0; step < step_count; ++step)
            {
                // 二次的に伸ばして近傍を細かく、遠方を粗く見る。
                const float t = (float(step) + step_noise + 0.5f) / float(step_count);
                const float sample_pixel_distance = max(t * t * pixel_radius, 1.0f);
                const float2 sample_uv =
                    uv + direction * sample_pixel_distance * ssao_target_size.zw;
                if (any(sample_uv < 0.0f) || any(sample_uv > 1.0f)) break;

                const float sample_device_z =
                    scene_depth.SampleLevel(ssao_sampler_point, sample_uv, 0).r;
                if (sample_device_z >= 0.999999f) continue;

                const float3 sample_position =
                    view_position_from_depth(sample_uv, sample_device_z);
                const float3 delta = sample_position - origin;
                const float  distance_squared = dot(delta, delta);
                const float  sample_distance = sqrt(max(distance_squared, 1.0e-8f));
                if (sample_distance > world_radius * 2.0f) continue;

                const float sample_cos = dot(delta / sample_distance, V);

                // 半径外へ向かって地平線を徐々に戻し、薄い物体が無限に
                // 遮蔽し続けるのを防ぐ(thin occluder compensation)。
                const float falloff =
                    saturate(1.0f - sample_distance / max(world_radius, 1.0e-4f));
                const float weighted_cos =
                    lerp(-1.0f, sample_cos, lerp(1.0f, falloff, thin_compensation));
                attenuated_cos = max(attenuated_cos, weighted_cos);
            }

            horizon_cos[side] = attenuated_cos;
        }

        // 地平線角を法線側の半球へクランプしてから解析積分する。
        float h1 =  acos(clamp(horizon_cos[0], -1.0f, 1.0f));
        float h2 = -acos(clamp(horizon_cos[1], -1.0f, 1.0f));
        h1 = n + min(h1 - n,  SSAO_HALF_PI);
        h2 = n + max(h2 - n, -SSAO_HALF_PI);

        const float sin_n = sin(n);
        const float cos_nn = cos(n);
        const float inner =
            0.25f * (-cos(2.0f * h1 - n) + cos_nn + 2.0f * h1 * sin_n) +
            0.25f * (-cos(2.0f * h2 - n) + cos_nn + 2.0f * h2 * sin_n);

        visibility_sum += projected_length * inner;
    }

    float visibility = saturate(visibility_sum / float(slice_count));
    visibility = pow(visibility, max(ssao_params0.z, 1.0e-3f));
    visibility = lerp(1.0f, visibility, saturate(ssao_params0.y) * fade);

    return float4(visibility, view_z, 0.0f, 0.0f);
}
