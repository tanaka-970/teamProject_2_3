// 現行DX12 Scene3D経路では未使用。TAA本体はShader/dx12_postprocess_ps.hlslへ統合している。
// テンポラルアンチエイリアシング(TAA)の解決パス。
//
// やっていること:
//   1. モーションベクターを近傍で「拡張(dilate)」する。最も手前のピクセルの
//      動きを採用することで、動く物体の輪郭がにじまない。
//   2. 履歴はCatmull-Rom(9タップ)で再サンプリングする。バイリニアだと
//      毎フレーム再フィルタされて画面全体がボケていく。
//   3. 近傍3x3の色範囲をYCoCg空間の分散から求め、履歴をその楕円へクリップする
//      (variance clipping)。ゴーストと残像の主因を潰す。
//   4. 履歴の信頼度に応じてブレンド率を変える。画面外・遮蔽解除・動きが速い
//      ところは現フレーム寄りにして、残像を出さない。

#include "../../fullscreen_quad.hlsli"
#include "../../frame_common.hlsli"

// b12はSSAOと共有する。どちらもフルスクリーンパスの直前に自前でバインドし直すため、
// 同一フレーム内で取り合いにならない(D3D11の定数バッファはb0..b13しか無い)。
cbuffer TAA_CONSTANT_BUFFER : register(b12)
{
    float4 taa_params0; // x=blend(履歴の比率), y=variance_gamma, z=sharpness, w=enable
    float4 taa_params1; // x=history_valid, y=motion_scale, z=max_velocity_reject, w=予約
};

Texture2D scene_color   : register(t0);
Texture2D history_color : register(t1);
Texture2D scene_depth   : register(t2);
Texture2D scene_velocity : register(t3);

SamplerState taa_sampler_point  : register(s0);
SamplerState taa_sampler_linear : register(s1);

float3 rgb_to_ycocg(float3 color)
{
    return float3(
         0.25f * color.r + 0.5f * color.g + 0.25f * color.b,
         0.5f  * color.r                  - 0.5f  * color.b,
        -0.25f * color.r + 0.5f * color.g - 0.25f * color.b);
}

float3 ycocg_to_rgb(float3 color)
{
    const float y = color.x;
    const float co = color.y;
    const float cg = color.z;
    return float3(y + co - cg, y + cg, y - co - cg);
}

// Catmull-Rom による履歴の再サンプリング。バイリニア9タップで近似する
// 定番の実装で、テクスチャフェッチを5回に削れるがここは素直に9タップにする。
float3 sample_history_catmull_rom(float2 uv)
{
    const float2 texture_size = frame_screen_size.xy;
    const float2 sample_position = uv * texture_size;
    const float2 texel_center = floor(sample_position - 0.5f) + 0.5f;
    const float2 f = sample_position - texel_center;

    // Catmull-Rom の重み。
    const float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    const float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    const float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    const float2 w3 = f * f * (-0.5f + 0.5f * f);

    const float2 weights[4] = { w0, w1, w2, w3 };
    const float offsets[4] = { -1.0f, 0.0f, 1.0f, 2.0f };

    float3 accumulated = 0.0f;
    float weight_sum = 0.0f;

    [unroll] for (int y = 0; y < 4; ++y)
    {
        [unroll] for (int x = 0; x < 4; ++x)
        {
            const float weight = weights[x].x * weights[y].y;
            if (weight == 0.0f) continue;
            const float2 tap_uv =
                (texel_center + float2(offsets[x], offsets[y])) / texture_size;
            accumulated += history_color.SampleLevel(
                taa_sampler_linear, saturate(tap_uv), 0).rgb * weight;
            weight_sum += weight;
        }
    }

    return weight_sum > 0.0f ? accumulated / weight_sum : 0.0f;
}

float4 main(VS_OUT pin) : SV_TARGET
{
    const float2 uv = pin.texcoord;
    const int2 pixel = int2(pin.position.xy);
    const float3 current = scene_color.Load(int3(pixel, 0)).rgb;

    if (taa_params0.w < 0.5f || taa_params1.x < 0.5f)
        return float4(current, 1.0f);

    // --- 1. モーションベクターの拡張 -------------------------------------
    // 3x3のうち最も手前(深度が小さい)のピクセルの動きを採用する。
    float closest_depth = 1.0f;
    int2 closest_offset = int2(0, 0);
    [unroll] for (int oy = -1; oy <= 1; ++oy)
    {
        [unroll] for (int ox = -1; ox <= 1; ++ox)
        {
            const int2 tap = clamp(pixel + int2(ox, oy), int2(0, 0),
                int2(frame_screen_size.xy) - 1);
            const float tap_depth = scene_depth.Load(int3(tap, 0)).r;
            if (tap_depth < closest_depth)
            {
                closest_depth = tap_depth;
                closest_offset = int2(ox, oy);
            }
        }
    }
    const int2 velocity_pixel = clamp(pixel + closest_offset, int2(0, 0),
        int2(frame_screen_size.xy) - 1);
    float2 velocity = scene_velocity.Load(int3(velocity_pixel, 0)).xy;

    // 背景(遠クリップ)はG-Bufferに速度が書かれないため、深度からカメラ再投影で補う。
    if (closest_depth >= 0.999999f)
    {
        const float3 world_position = world_position_from_depth(uv, closest_depth);
        float4 previous_clip = mul(float4(world_position, 1.0f), frame_prev_view_projection);
        previous_clip.xyz /= max(abs(previous_clip.w), 1.0e-6f);
        float2 current_ndc = uv_to_ndc(uv) - frame_jitter.xy;
        float2 previous_ndc = previous_clip.xy - frame_jitter.zw;
        velocity = (current_ndc - previous_ndc) * float2(0.5f, -0.5f);
    }
    velocity *= taa_params1.y;

    const float2 history_uv = uv - velocity;

    // 画面外へ出た履歴は使えない。
    if (any(history_uv < 0.0f) || any(history_uv > 1.0f))
        return float4(current, 1.0f);

    // --- 2. 近傍の色範囲を求める (YCoCgの平均と分散) ---------------------
    float3 moment1 = 0.0f;
    float3 moment2 = 0.0f;
    float3 neighbor_min = 1.0e30f;
    float3 neighbor_max = -1.0e30f;

    [unroll] for (int ny = -1; ny <= 1; ++ny)
    {
        [unroll] for (int nx = -1; nx <= 1; ++nx)
        {
            const int2 tap = clamp(pixel + int2(nx, ny), int2(0, 0),
                int2(frame_screen_size.xy) - 1);
            const float3 tap_color = rgb_to_ycocg(scene_color.Load(int3(tap, 0)).rgb);
            moment1 += tap_color;
            moment2 += tap_color * tap_color;
            neighbor_min = min(neighbor_min, tap_color);
            neighbor_max = max(neighbor_max, tap_color);
        }
    }

    const float inverse_count = 1.0f / 9.0f;
    const float3 mean = moment1 * inverse_count;
    const float3 variance = max(moment2 * inverse_count - mean * mean, 0.0f);
    const float3 deviation = sqrt(variance) * max(taa_params0.y, 0.0f);

    // 分散で作った範囲と実際の最小/最大の狭い方を採用する。
    const float3 clip_min = max(mean - deviation, neighbor_min);
    const float3 clip_max = min(mean + deviation, neighbor_max);

    // --- 3. 履歴の取得とクリッピング -------------------------------------
    float3 history = rgb_to_ycocg(sample_history_catmull_rom(history_uv));

    // 中心へ向けてレイを詰める形でクリップする(単純なclampより色ズレが小さい)。
    const float3 center = 0.5f * (clip_max + clip_min);
    const float3 extent = 0.5f * (clip_max - clip_min) + 1.0e-6f;
    const float3 offset_from_center = history - center;
    const float3 unit = offset_from_center / extent;
    const float maximum_unit = max(abs(unit.x), max(abs(unit.y), abs(unit.z)));
    if (maximum_unit > 1.0f) history = center + offset_from_center / maximum_unit;

    // --- 4. ブレンド ------------------------------------------------------
    // 動きが速いほど履歴を弱める。速度はピクセル単位に直して評価する。
    const float velocity_pixels = length(velocity * frame_screen_size.xy);
    const float motion_reject =
        saturate(velocity_pixels / max(taa_params1.z, 1.0e-3f));
    float blend = saturate(taa_params0.x) * (1.0f - motion_reject * 0.5f);

    const float3 current_ycocg = rgb_to_ycocg(current);
    float3 resolved = ycocg_to_rgb(lerp(current_ycocg, history, blend));

    // TAAで落ちた分の解像感を軽く戻す(アンシャープマスク)。
    const float sharpness = max(taa_params0.z, 0.0f);
    if (sharpness > 0.0f)
    {
        const float3 blurred = ycocg_to_rgb(mean);
        resolved += (resolved - blurred) * sharpness;
    }

    return float4(max(resolved, 0.0f), 1.0f);
}
