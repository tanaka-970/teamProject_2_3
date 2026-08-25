cbuffer UIConstants : register(b0)
{
    float4 screen_size;
    float4 fill_color_2;
    float4 fill_color_3;
    float4 fill_color_4;
    float4 fill_stops;
    float4 stroke_color_2;
    float4 stroke_parameters;
    // xはShape種別、yはText/SDF、zはOutline幅、wは予約領域。
    float4 mode;
    float4 outline_color;
    float4 shadow_offset;
    float4 shadow_color;
    float4 atlas_size;
    float4 fill_parameters;
    float4 clip_parameters;
    float4 clip_bounds;
    float4 mask_parameters;
    float4 mask_uv;
    float4 mask_uvs[4];
    float4 mask_operations;
    float4 mask_luma;
    float4 clip_shape;
    float4 mask_origins[4];
    float4 mask_inverses[4];
    float4 mask_inverts;
    float4 mask_rotated;
    float4 clip_state;
    float4 clip_parameters_extra[3];
    float4 clip_bounds_extra[3];
    float4 clip_shapes_extra[3];
};

Texture2D ui_texture : register(t0);
Texture2D ui_mask_0 : register(t1);
Texture2D ui_mask_1 : register(t2);
Texture2D ui_mask_2 : register(t3);
Texture2D ui_mask_3 : register(t4);
SamplerState ui_sampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float4 uv_bounds : TEXCOORD1;
};

float4 SampleUiMask(int index, float2 uv)
{
    if (index == 0) return ui_mask_0.Sample(ui_sampler, uv);
    if (index == 1) return ui_mask_1.Sample(ui_sampler, uv);
    if (index == 2) return ui_mask_2.Sample(ui_sampler, uv);
    return ui_mask_3.Sample(ui_sampler, uv);
}

float GradientAmount(float2 uv)
{
    if (fill_parameters.w < 0.5) return 0.0;
    if (fill_parameters.w < 1.5)
    {
        const float2 direction = float2(cos(fill_parameters.x),
            sin(fill_parameters.x));
        return saturate(dot(uv - fill_parameters.yz, direction) + 0.5);
    }
    return saturate(length(uv - fill_parameters.yz) / 0.70710678);
}

float4 GradientColor(float4 first_color, float amount)
{
    const float stop2 = max(saturate(fill_stops.x), 0.0001);
    float4 color = lerp(first_color, fill_color_2, saturate(amount / stop2));
    if (fill_stops.y >= 0.0)
    {
        const float stop3 = max(saturate(fill_stops.y), stop2 + 0.0001);
        color = lerp(color, fill_color_3,
            saturate((amount - stop2) / (stop3 - stop2)));
    }
    if (fill_stops.z >= 0.0)
    {
        const float previous_stop = fill_stops.y >= 0.0
            ? max(saturate(fill_stops.y), stop2 + 0.0001) : stop2;
        const float stop4 = max(saturate(fill_stops.z), previous_stop + 0.0001);
        color = lerp(color, fill_color_4,
            saturate((amount - previous_stop) / (stop4 - previous_stop)));
    }
    return color;
}

float SdfSpread()
{
    return max(atlas_size.z, 0.0001);
}

bool InGlyphSdfRegion(float2 uv, float4 uv_bounds)
{
    const float2 texel = 1.0 / max(atlas_size.xy, float2(1.0, 1.0));
    const float2 padding = texel * SdfSpread();
    return uv.x >= uv_bounds.x - padding.x &&
        uv.y >= uv_bounds.y - padding.y &&
        uv.x <= uv_bounds.z + padding.x &&
        uv.y <= uv_bounds.w + padding.y;
}

float SampleSdfDistance(float2 uv, float4 uv_bounds)
{
    float distance_value = -SdfSpread();
    if (InGlyphSdfRegion(uv, uv_bounds))
    {
        const float encoded = ui_texture.Sample(ui_sampler, uv).a;
        distance_value = (encoded * 2.0 - 1.0) * SdfSpread();
    }
    return distance_value;
}

float SdfCoverage(float distance_value)
{
    const float aa = max(fwidth(distance_value), 0.0001);
    return smoothstep(-aa, aa, distance_value);
}

float SdfOutlineCoverage(float distance_value, float width)
{
    const float aa = max(fwidth(distance_value), 0.0001);
    const float outer = smoothstep(-width - aa, -width + aa, distance_value);
    const float inner = smoothstep(-aa, aa, distance_value);
    return saturate(outer - inner);
}

float4 ClipParametersAt(int index)
{
    return index == 0 ? clip_parameters : clip_parameters_extra[index - 1];
}

float4 ClipBoundsAt(int index)
{
    return index == 0 ? clip_bounds : clip_bounds_extra[index - 1];
}

float4 ClipShapeAt(int index)
{
    return index == 0 ? clip_shape : clip_shapes_extra[index - 1];
}

float4 main(PSInput input) : SV_TARGET
{
    float clip_alpha = 1.0;
    const int clip_count = min((int)clip_state.x, 4);
    [unroll]
    for (int clip_index = 0; clip_index < 4; ++clip_index)
    {
        if (clip_index >= clip_count) continue;
        const float4 current_parameters = ClipParametersAt(clip_index);
        const float4 current_bounds = ClipBoundsAt(clip_index);
        const float4 current_shape = ClipShapeAt(clip_index);
        const int shape_kind = (int)round(current_parameters.x) - 1;
        const float2 clip_half_size = max(current_bounds.zw,
            float2(0.0001, 0.0001));
        const float angle = radians(current_shape.z);
        const float2 axis_x = float2(cos(angle), sin(angle));
        const float2 axis_y = float2(-axis_x.y, axis_x.x);
        const float2 centered = input.position.xy - current_bounds.xy;
        const float2 clip_local = float2(dot(centered, axis_x),
            dot(centered, axis_y)) / clip_half_size;
        const float radius = length(clip_local);
        float signed_distance = 1.0 - max(abs(clip_local.x), abs(clip_local.y));
        if (shape_kind == 1)
        {
            signed_distance = 1.0 - radius;
        }
        else if (shape_kind == 2)
        {
            const float sides = max(round(current_shape.x), 3.0);
            const float pi = 3.14159265359;
            const float sector = 2.0 * pi / sides;
            const float local_angle = fmod(abs(atan2(clip_local.y,
                clip_local.x)) + pi / sides, sector) - pi / sides;
            const float boundary = cos(pi / sides) /
                max(cos(local_angle), 0.0001);
            signed_distance = boundary - radius;
        }
        else if (shape_kind == 3)
        {
            const float sides = max(round(current_shape.x), 3.0);
            const float pi = 3.14159265359;
            const float lobe_sector = pi / sides;
            const float local_angle = fmod(abs(atan2(clip_local.y,
                clip_local.x)), 2.0 * lobe_sector);
            const float lobe = 1.0 -
                abs(local_angle - lobe_sector) / lobe_sector;
            const float boundary = lerp(saturate(current_shape.y), 1.0, lobe);
            signed_distance = boundary - radius;
        }
        else if (shape_kind == 4)
        {
            const float corner = saturate(current_parameters.w);
            float2 q = abs(clip_local) - (1.0 - corner);
            signed_distance = corner - length(max(q, 0.0)) -
                min(max(q.x, q.y), 0.0);
        }
        const float feather = max(current_parameters.z,
            max(fwidth(signed_distance), 0.0001));
        float current_alpha = smoothstep(-feather, feather, signed_distance);
        if (current_parameters.y > 0.5) current_alpha = 1.0 - current_alpha;
        clip_alpha *= current_alpha;
        if (clip_alpha <= 0.0001) discard;
    }

    float2 sample_uv = input.uv;
    float4 sampled = ui_texture.Sample(ui_sampler, sample_uv);
    // 描画対象自身のローカルUV。マスクごとのUVとは分離して扱う。
    const float2 visual_local_uv = saturate(
        (input.uv - input.uv_bounds.xy) /
        max(input.uv_bounds.zw - input.uv_bounds.xy, float2(0.0001, 0.0001)));

    // ShapeもImageと同じCommand経路を使う。Circle以外の輪郭はCPU側で分割済み。
    if (mode.x == 1.0)
    {
        float2 centered = input.uv * 2.0 - 1.0;
        float distance_to_edge = 1.0 - length(centered);
        float aa = max(fwidth(distance_to_edge), 0.0001);
        sampled.a *= smoothstep(0.0, aa, distance_to_edge);
    }

    float mask_alpha = 1.0;
    const int mask_count = min((int)mask_parameters.x, 4);
    [unroll]
    for (int mask_index = 0; mask_index < 4; ++mask_index)
    {
        if (mask_index >= mask_count) continue;
        const float2 delta = input.position.xy - mask_origins[mask_index].xy;
        float2 mask_local_uv = float2(
            dot(delta, mask_inverses[mask_index].xy),
            dot(delta, mask_inverses[mask_index].zw));
        const bool inside_matte = all(mask_local_uv >= 0.0) &&
            all(mask_local_uv <= 1.0);
        mask_local_uv = saturate(mask_local_uv);
        float2 matte_local = mask_local_uv;
        if (mask_rotated[mask_index] > 0.5)
            matte_local = float2(1.0 - mask_local_uv.y, mask_local_uv.x);
        const float2 matte_uv = mask_uvs[mask_index].xy + matte_local *
            mask_uvs[mask_index].zw;
        const float4 mask_sample = SampleUiMask(mask_index, matte_uv);
        float matte_value = mask_luma[mask_index] > 0.5
            ? dot(mask_sample.rgb, float3(0.2126, 0.7152, 0.0722)) : mask_sample.a;
        if (!inside_matte) matte_value = 0.0;
        if (mask_inverts[mask_index] > 0.5) matte_value = 1.0 - matte_value;
        if (mask_index == 0)
        {
            mask_alpha = matte_value;
        }
        else if (mask_operations[mask_index] < 0.5)
        {
            mask_alpha = saturate(mask_alpha + matte_value);
        }
        else if (mask_operations[mask_index] < 1.5)
        {
            mask_alpha = saturate(mask_alpha - matte_value);
        }
        else
        {
            mask_alpha = min(mask_alpha, matte_value);
        }
    }
    if (mode.y > 0.5)
    {
        const float distance_value = SampleSdfDistance(input.uv, input.uv_bounds);
        const float outline_width = max(mode.z, 0.0);
        const float distance_per_pixel = max(fwidth(distance_value), 0.0001);
        const float sdf_outline_width = min(
            outline_width * distance_per_pixel, SdfSpread());
        const float fill = SdfCoverage(distance_value);
        const float outline = outline_width > 0.0
            ? SdfOutlineCoverage(distance_value, sdf_outline_width) : 0.0;

        float shadow = 0.0;
        if (shadow_color.a > 0.0)
        {
            const float2 shadow_uv_offset = ddx(input.uv) * shadow_offset.x +
                ddy(input.uv) * shadow_offset.y;
            const float shadow_distance = SampleSdfDistance(
                input.uv - shadow_uv_offset, input.uv_bounds);
            shadow = SdfCoverage(shadow_distance);
            if (outline_width > 0.0)
            {
                const float shadow_derivative = max(
                    fwidth(shadow_distance), 0.0001);
                const float shadow_outline_width = min(
                    outline_width * shadow_derivative, SdfSpread());
                shadow = max(shadow, SdfOutlineCoverage(
                    shadow_distance, shadow_outline_width));
            }
        }

        float4 text_result = shadow_color * shadow;
        text_result = lerp(text_result, outline_color, outline);
        text_result = lerp(text_result, input.color, fill);
        text_result.a = max(text_result.a, input.color.a * fill);
        text_result.a *= mask_alpha * clip_alpha;
        return text_result;
    }

    float4 visual_color = GradientColor(input.color,
        GradientAmount(visual_local_uv));
    if (stroke_parameters.x > 0.5)
    {
        visual_color = lerp(visual_color, stroke_color_2, visual_local_uv.x);
    }
    float4 shaded = sampled * visual_color;
    shaded.a *= mask_alpha * clip_alpha;
    return shaded;
}
