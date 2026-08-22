// Model Effect Stack のうち面を消す Effect だけを、影パスでも同じ式で再現する。
#ifndef __SHADOW_COVERAGE_COMMON_HLSLI__
#define __SHADOW_COVERAGE_COMMON_HLSLI__

// s1 のサンプラーを共有する。影パスの Pixel Shader は必ず両方を include する。
#include "shadow_alpha_common.hlsli"

#define SHADOW_COVERAGE_MAX_EFFECTS 4
#define SHADOW_COVERAGE_MAX_REGIONS 4
#define shadow_coverage_sampler shadow_alpha_sampler

Texture2D shadow_coverage_mask_map : register(t46);

cbuffer SHADOW_COVERAGE_CONSTANTS : register(b8)
{
    // Effect を計算したカメラの view * projection。
    row_major float4x4 shadow_coverage_view_projection;
    // xy=カメラ viewport の左上、zw=その幅と高さ。
    float4 shadow_coverage_viewport;
    // xy=Effect の crop 矩形の左上、zw=その幅と高さ。これが Effect 側の target_size。
    float4 shadow_coverage_rect;
    // x=有効な Effect 数、y=マスク画像を貼った Effect の番号(-1で無し)、z=有効な範囲数。
    float4 shadow_coverage_control;
    // 各 Effect の UIEffectConstants と同じ 4 つのパラメータ。
    float4 shadow_coverage_params0[SHADOW_COVERAGE_MAX_EFFECTS];
    float4 shadow_coverage_params1[SHADOW_COVERAGE_MAX_EFFECTS];
    float4 shadow_coverage_params2[SHADOW_COVERAGE_MAX_EFFECTS];
    float4 shadow_coverage_params3[SHADOW_COVERAGE_MAX_EFFECTS];
    // x=UIEffectKind、y=この Effect へ範囲制限が掛かるか、zw=予約。
    float4 shadow_coverage_meta[SHADOW_COVERAGE_MAX_EFFECTS];
    // 範囲制限。region_blend と同じ center.xy / size.xy と rotation/feather/strength/shape。
    float4 shadow_coverage_region_params[SHADOW_COVERAGE_MAX_REGIONS];
    float4 shadow_coverage_region_settings[SHADOW_COVERAGE_MAX_REGIONS];
};

// ui_effect_dissolve.hlsl の Hash と同じ式。
float shadow_coverage_hash(float2 value)
{
    value = frac(value * float2(127.1f, 311.7f));
    value += dot(value, value + 19.19f);
    return frac(value.x * value.y);
}

// ui_effect_dynamic_common.hlsli の hash21 と同じ式。
float shadow_coverage_hash21(float2 p)
{
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

float shadow_coverage_noise21(float2 p)
{
    const float2 cell = floor(p);
    const float2 local = frac(p);
    const float2 smooth_local = local * local * (3.0f - 2.0f * local);
    const float a = shadow_coverage_hash21(cell);
    const float b = shadow_coverage_hash21(cell + float2(1.0f, 0.0f));
    const float c = shadow_coverage_hash21(cell + float2(0.0f, 1.0f));
    const float d = shadow_coverage_hash21(cell + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, smooth_local.x), lerp(c, d, smooth_local.x), smooth_local.y);
}

float shadow_coverage_fbm21(float2 p)
{
    float value = 0.0f;
    float amplitude = 0.5f;
    [unroll] for (int octave = 0; octave < 4; ++octave)
    {
        value += shadow_coverage_noise21(p) * amplitude;
        p = mul(float2x2(0.80f, -0.60f, 0.60f, 0.80f), p) * 2.03f + 13.17f;
        amplitude *= 0.5f;
    }
    return value / 0.9375f;
}

// ui_effect_dissolve.hlsl の PatternUV と同じ式。
float2 shadow_coverage_pattern_uv(float2 uv, float2 target, float radius, float angle)
{
    const float rotation_radians = radians(angle);
    float sine;
    float cosine;
    sincos(rotation_radians, sine, cosine);
    const float2 local = (uv - 0.5f) * target / max(radius, 1.0f);
    const float2 rotated = float2(
        local.x * cosine - local.y * sine,
        local.x * sine + local.y * cosine);
    return frac(rotated + 0.5f);
}

// ui_effect_region_blend.hlsl の single_region_weight のうち矩形と楕円だけ。
float shadow_coverage_single_region(float2 uv, float4 params, float4 settings)
{
    const float shape_flags = settings.w;
    float weight = 1.0f;
    if (shape_flags >= -0.5f)
    {
        const bool invert = shape_flags >= 4.0f;
        const int shape = (int) floor(shape_flags - (invert ? 4.0f : 0.0f) + 0.5f);
        const float2 center = params.xy;
        const float2 size = max(params.zw, float2(0.0001f, 0.0001f));
        const float angle = settings.x * 0.0174532925199433f;
        const float s = sin(angle);
        const float c = cos(angle);
        const float2 delta = uv - center;
        const float2 local = float2(delta.x * c + delta.y * s, -delta.x * s + delta.y * c);
        const float feather = saturate(settings.y);

        const float distance_to_edge = shape == 1
            ? length(local / size)
            : max(abs(local.x / size.x), abs(local.y / size.y));
        float mask = feather <= 0.00001f
            ? step(distance_to_edge, 1.0f)
            : 1.0f - smoothstep(1.0f - feather, 1.0f + feather, distance_to_edge);
        if (invert) mask = 1.0f - mask;
        weight = saturate(mask * settings.z);
    }
    return weight;
}

float shadow_coverage_region_weight(float2 uv)
{
    const int count = (int) floor(shadow_coverage_control.z + 0.5f);
    float mask = 1.0f;
    if (count > 0)
    {
        mask = shadow_coverage_single_region(uv,
            shadow_coverage_region_params[0], shadow_coverage_region_settings[0]);
        [unroll] for (int index = 1; index < SHADOW_COVERAGE_MAX_REGIONS; ++index)
        {
            if (index >= count) break;
            mask = max(mask, shadow_coverage_single_region(uv,
                shadow_coverage_region_params[index],
                shadow_coverage_region_settings[index]));
        }
    }
    return saturate(mask);
}

// ui_effect_wipe.hlsl の alpha 減衰と同じ式。
float shadow_coverage_wipe(float2 uv, int index)
{
    const float angle = radians(shadow_coverage_params1[index].x);
    const float2 direction = normalize(float2(cos(angle), sin(angle)));
    const float projected = dot(uv - 0.5f, direction) + 0.5f;
    const float progress = saturate(shadow_coverage_params1[index].y);
    const float softness = max(shadow_coverage_params1[index].z, 0.0001f);
    return 1.0f - smoothstep(progress, progress + softness, projected);
}

// ui_effect_dissolve.hlsl の keep と同じ式。
float shadow_coverage_dissolve(float2 uv, float2 target, int index, bool mask_bound)
{
    const float progress = saturate(shadow_coverage_params1[index].y);
    const float edge = max(shadow_coverage_params0[index].z, 0.0001f);
    float keep;
    if (shadow_coverage_params3[index].y > 0.5f && mask_bound)
    {
        const float2 pattern_uv = shadow_coverage_pattern_uv(uv, target,
            shadow_coverage_params0[index].x, shadow_coverage_params1[index].x);
        const float pattern = dot(shadow_coverage_mask_map.Sample(
            shadow_coverage_sampler, pattern_uv).rgb, float3(0.2126f, 0.7152f, 0.0722f));
        keep = 1.0f - smoothstep(1.0f - progress - edge, 1.0f - progress + edge, pattern);
    }
    else
    {
        const float noise = shadow_coverage_hash(
            floor(uv * target * 0.25f) + shadow_coverage_params2[index].z);
        keep = smoothstep(progress - edge, progress + edge, noise);
    }
    return keep;
}

// ui_effect_burn_reveal.hlsl の alpha 減衰と同じ式。
float shadow_coverage_burn(float2 uv, float2 target, int index)
{
    const float scale = max(shadow_coverage_params0[index].x, 2.0f);
    const float time = shadow_coverage_params2[index].w * shadow_coverage_params1[index].w;
    const float n = shadow_coverage_fbm21(uv * target / scale +
        shadow_coverage_params2[index].z * 17.0f + float2(time * 0.17f, -time * 0.11f));
    const float width = max(shadow_coverage_params1[index].z, 0.002f) *
        lerp(0.7f, 1.8f, saturate(shadow_coverage_params0[index].w * 0.5f));
    const float reveal = smoothstep(n - width, n + width, shadow_coverage_params1[index].y);
    return lerp(1.0f, reveal, saturate(shadow_coverage_params0[index].y));
}

// ui_effect_mask.hlsl の edge と同じ式。マスク画像経路もそのまま合わせる。
float shadow_coverage_mask(float2 uv, int index, bool mask_bound)
{
    const int shape_kind = (int) round(shadow_coverage_params3[index].x) - 1;
    float edge = 1.0f;
    if (shape_kind >= 0)
    {
        const float2 centered = uv - shadow_coverage_params2[index].xy;
        const float angle = radians(shadow_coverage_params1[index].x);
        const float2 axis_x = float2(cos(angle), sin(angle));
        const float2 axis_y = float2(-axis_x.y, axis_x.x);
        const float2 local = float2(dot(centered, axis_x), dot(centered, axis_y)) /
            max(shadow_coverage_params2[index].zw, float2(0.0001f, 0.0001f));
        const float radius = length(local);
        const float sides = max(round(shadow_coverage_params0[index].z), 3.0f);
        const float pi = 3.14159265359f;
        float signed_distance = 0.0f;

        if (shape_kind == 0)
        {
            signed_distance = 1.0f - max(abs(local.x), abs(local.y));
        }
        else if (shape_kind == 1)
        {
            signed_distance = 1.0f - radius;
        }
        else if (shape_kind == 2)
        {
            const float sector = 2.0f * pi / sides;
            const float local_angle =
                fmod(abs(atan2(local.y, local.x)) + pi / sides, sector) - pi / sides;
            const float boundary = cos(pi / sides) / max(cos(local_angle), 0.0001f);
            signed_distance = boundary - radius;
        }
        else if (shape_kind == 3)
        {
            const float lobe_sector = pi / sides;
            const float local_angle = fmod(abs(atan2(local.y, local.x)), 2.0f * lobe_sector);
            const float lobe = 1.0f - abs(local_angle - lobe_sector) / lobe_sector;
            const float inner_radius = saturate(shadow_coverage_params0[index].x);
            const float boundary = lerp(inner_radius, 1.0f, lobe);
            signed_distance = boundary - radius;
        }
        else
        {
            const float corner = saturate(shadow_coverage_params0[index].y);
            const float2 q = abs(local) - float2(1.0f - corner, 1.0f - corner);
            signed_distance = corner - length(max(q, 0.0f)) - min(max(q.x, q.y), 0.0f);
        }

        const float softness = max(shadow_coverage_params1[index].z, 0.0001f);
        edge = smoothstep(-softness, softness, signed_distance);
    }
    else if (shape_kind == -1)
    {
        const float2 centered = uv - shadow_coverage_params2[index].xy;
        const float angle = radians(shadow_coverage_params1[index].x);
        const float2 axis_x = float2(cos(angle), sin(angle));
        const float2 axis_y = float2(-axis_x.y, axis_x.x);
        const float2 local = float2(dot(centered, axis_x), dot(centered, axis_y)) /
            max(shadow_coverage_params2[index].zw, float2(0.0001f, 0.0001f));
        const float rectangle = max(abs(local.x), abs(local.y));
        const float circle = length(local);
        const float shape = lerp(rectangle, circle, saturate(shadow_coverage_params0[index].w));
        edge = saturate((1.0f - shape) / max(shadow_coverage_params1[index].z, 0.0001f));
    }

    if (shadow_coverage_params1[index].w > 0.5f && mask_bound)
    {
        const float4 mask_sample = shadow_coverage_mask_map.Sample(shadow_coverage_sampler, uv);
        float mask_value = mask_sample.a;
        if (shadow_coverage_params3[index].z > 0.5f)
            mask_value = dot(mask_sample.rgb, float3(0.2126f, 0.7152f, 0.0722f));
        if (shadow_coverage_params3[index].w > 0.5f)
            mask_value = 1.0f - mask_value;
        edge *= smoothstep(0.0f, max(shadow_coverage_params1[index].z, 0.0001f), mask_value);
    }
    return edge;
}

// ワールド座標を Effect の crop 内 UV へ落とす。z=0 ならカメラの後ろで評価できない。
float3 shadow_coverage_uv(float3 world_position)
{
    const float4 clip = mul(float4(world_position, 1.0f), shadow_coverage_view_projection);
    float3 result = float3(0.0f, 0.0f, 0.0f);
    if (clip.w > 1.0e-5f)
    {
        const float2 ndc = clip.xy / clip.w;
        const float2 screen = shadow_coverage_viewport.xy +
            float2(ndc.x * 0.5f + 0.5f, -ndc.y * 0.5f + 0.5f) * shadow_coverage_viewport.zw;
        result.xy = (screen - shadow_coverage_rect.xy) /
            max(shadow_coverage_rect.zw, float2(1.0f, 1.0f));
        result.z = 1.0f;
    }
    return result;
}

// Effect Stack が面を消しているぶんだけ影も消す。
void shadow_coverage_clip(float3 world_position)
{
    const int count = (int) floor(shadow_coverage_control.x + 0.5f);
    if (count <= 0) return;

    const float3 projected = shadow_coverage_uv(world_position);
    if (projected.z < 0.5f) return;
    const float2 uv = projected.xy;

    const int mask_index = (int) floor(shadow_coverage_control.y + 0.5f);
    const float2 target = max(shadow_coverage_rect.zw, float2(1.0f, 1.0f));
    float keep = 1.0f;

    [unroll] for (int index = 0; index < SHADOW_COVERAGE_MAX_EFFECTS; ++index)
    {
        if (index >= count) break;
        const int kind = (int) floor(shadow_coverage_meta[index].x + 0.5f);
        const bool mask_bound = index == mask_index;
        float effect_keep = 1.0f;
        if (kind == 6) effect_keep = shadow_coverage_wipe(uv, index);
        else if (kind == 7) effect_keep = shadow_coverage_dissolve(uv, target, index, mask_bound);
        else if (kind == 5) effect_keep = shadow_coverage_mask(uv, index, mask_bound);
        else if (kind == 71) effect_keep = shadow_coverage_burn(uv, target, index);

        // 範囲制限が掛かる Effect は範囲の外では元の面をそのまま残す。
        if (shadow_coverage_meta[index].y > 0.5f)
            effect_keep = lerp(1.0f, effect_keep, shadow_coverage_region_weight(uv));
        keep *= saturate(effect_keep);
    }
    clip(keep - 0.5f);
}

#endif
