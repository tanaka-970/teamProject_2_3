Texture2D source_texture : register(t0);
Texture2D mask_texture : register(t1);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // brush length, intensity, jitter, brush width
    float4 effect_params1; // angle, progress = stamp size, softness = variation, speed
    float4 effect_params2; // direction.xy, seed, time
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
    float4 effect_params3; // x = waveform, y = optional texture is bound
    float4 brush_pattern_settings; // x = fallback pattern, y = 0 single / 1 weighted random
    float4 brush_pattern_weights[4];
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
static const int tensor_neighborhood_width = 3;
static const int brush_sample_count = 24;
static const float golden_angle = 2.39996323;

float Luminance(float2 uv)
{
    return dot(source_texture.Sample(source_sampler, uv).rgb,
        float3(0.2126, 0.7152, 0.0722));
}

float Hash(float2 value)
{
    return frac(sin(dot(value, float2(127.1, 311.7))) * 43758.5453);
}

float BrushPatternWeight(int index)
{
    const int group = clamp(index / 4, 0, 3);
    const int lane = clamp(index % 4, 0, 3);
    return max(0.0, brush_pattern_weights[group][lane]);
}

int SelectBrushPattern(float2 stamp_cell)
{
    const int fallback = clamp((int)floor(brush_pattern_settings.x + 0.5), 0, 15);
    int selected = fallback;
    if (brush_pattern_settings.y >= 0.5)
    {
        float total_weight = 0.0;
        [unroll]
        for (int weight_index = 0; weight_index < 16; ++weight_index)
        {
            total_weight += BrushPatternWeight(weight_index);
        }
        if (total_weight > 0.0)
        {
            const float target = Hash(stamp_cell + effect_params2.z + 53.0) * total_weight;
            float accumulated = 0.0;
            bool selected_once = false;
            [unroll]
            for (int pick_index = 0; pick_index < 16; ++pick_index)
            {
                accumulated += BrushPatternWeight(pick_index);
                if (!selected_once && target <= accumulated)
                {
                    selected = pick_index;
                    selected_once = true;
                }
            }
        }
    }
    return selected;
}

float4 BrushPatternProfile(int index)
{
    // x = スタンプ間隔、y = 大きさ、z = 向きの揺れ、w = 濃淡の強さ。
    // 同じマスクをただ切り替えるだけでは筆種の差が薄いため、元の筆致に合わせた
    // 形状プロファイルも選択する。未指定の通常マスクはこの関数を通らない。
    float4 profile = float4(1.25, 1.20, 0.35, 1.00);
    if (index == 0) profile = float4(1.10, 1.08, 0.55, 0.92);
    else if (index == 1) profile = float4(0.68, 0.88, 0.20, 0.78);
    else if (index == 2) profile = float4(1.05, 1.10, 0.45, 0.90);
    else if (index == 3) profile = float4(0.95, 1.05, 0.35, 0.95);
    else if (index == 4) profile = float4(1.15, 1.10, 0.85, 0.90);
    else if (index == 5) profile = float4(1.00, 1.12, 0.35, 1.00);
    else if (index == 6) profile = float4(0.90, 0.98, 0.70, 0.78);
    else if (index == 7) profile = float4(1.12, 1.15, 0.35, 1.00);
    else if (index == 8) profile = float4(1.00, 1.08, 0.55, 0.82);
    else if (index == 9) profile = float4(0.75, 0.90, 0.22, 0.75);
    else if (index == 10) profile = float4(1.05, 1.15, 0.40, 1.00);
    else if (index == 11) profile = float4(0.82, 0.92, 0.80, 0.78);
    else if (index == 12) profile = float4(0.95, 0.98, 0.70, 0.75);
    else if (index == 13) profile = float4(1.00, 1.08, 0.25, 0.88);
    else if (index == 14) profile = float4(1.08, 1.16, 0.60, 1.00);
    return profile;
}

float4 main(VSOutput input) : SV_TARGET
{
    float jxx = 0.0;
    float jxy = 0.0;
    float jyy = 0.0;
    [unroll]
    for (int y = 0; y < tensor_neighborhood_width; ++y)
    {
        [unroll]
        for (int x = 0; x < tensor_neighborhood_width; ++x)
        {
            const float2 neighborhood = float2(x - 1, y - 1) * target_size.zw;
            const float gx = Luminance(input.uv + neighborhood + float2(target_size.z, 0.0)) -
                Luminance(input.uv + neighborhood - float2(target_size.z, 0.0));
            const float gy = Luminance(input.uv + neighborhood + float2(0.0, target_size.w)) -
                Luminance(input.uv + neighborhood - float2(0.0, target_size.w));
            jxx += gx * gx;
            jxy += gx * gy;
            jyy += gy * gy;
        }
    }

    // The dominant eigenvector is the strongest luminance change. Brush marks
    // use its perpendicular, so they follow structure instead of crossing it.
    const float gradient_angle = 0.5 * atan2(2.0 * jxy, jxx - jyy);
    const float2 jitter_cell = floor(input.uv * target_size.xy /
        max(effect_params0.x, 1.0));
    const float jitter = (Hash(jitter_cell + effect_params2.z) - 0.5) *
        effect_params0.z;
    const float tangent_angle = gradient_angle + 1.57079632679 + jitter;
    const float2 tangent = float2(cos(tangent_angle), sin(tangent_angle));
    const float2 normal = float2(-tangent.y, tangent.x);
    const float4 source = source_texture.Sample(source_sampler, input.uv);
    float4 brushed = source;
    // アトラス経路は下で独立ストロークの顔料色へ必ず置き換える。旧方式の
    // 24タップ楕円平均は出力に一切使われないため、通常マスク時だけ実行する。
    if (effect_params3.z < 0.5)
    {
        brushed = 0.0;
        float weight_sum = 0.0;
        [unroll]
        for (int sample_index = 0; sample_index < brush_sample_count; ++sample_index)
        {
            const float radius_fraction = sqrt((sample_index + 0.5) / brush_sample_count);
            const float angle = sample_index * golden_angle;
            const float2 ellipse_offset = tangent * (cos(angle) * radius_fraction * effect_params0.x) +
                normal * (sin(angle) * radius_fraction * max(effect_params0.w, 0.5));
            const float weight = exp(-radius_fraction * radius_fraction * 2.0);
            brushed += source_texture.Sample(source_sampler,
                input.uv + ellipse_offset * target_size.zw) * weight;
            weight_sum += weight;
        }
        brushed /= max(weight_sum, 0.0001);
    }
    // 通常マスクは従来の平均色を使う。アトラスだけは下でスタンプ中心色へ
    // 差し替えるため、色が面全体へにじむ従来経路を残さない。
    float4 painted = brushed;
    float brush_amount = saturate(effect_params0.y);
    if (effect_params3.z > 0.5)
    {
        // 近傍セルから独立した筆を重ねる。1セルを全面に繰り返すと、筆跡ではなく
        // モザイクになるため、アンカーを揺らし、黒地の領域は一切描かない。
        const float2 pixel = input.uv * target_size.xy;
        const float2 grid_step = float2(
            max(effect_params1.y * 0.78, 8.0),
            max(effect_params0.w * 1.35, 6.0));
        const float2 base_cell = floor(pixel / grid_step);
        const float variation = saturate(effect_params1.z);
        float best_coverage = 0.0;
        float4 best_pigment = source;
        [unroll]
        for (int cell_y = -1; cell_y <= 1; ++cell_y)
        {
            [unroll]
            for (int cell_x = -1; cell_x <= 1; ++cell_x)
            {
                const float2 cell = base_cell + float2(cell_x, cell_y);
                const int pattern_index = SelectBrushPattern(cell);
                const float4 profile = BrushPatternProfile(pattern_index);
                const float2 random_offset = float2(
                    Hash(cell + effect_params2.z + 11.0),
                    Hash(cell.yx + effect_params2.z + 23.0)) - 0.5;
                const float2 anchor = (cell + 0.5 + random_offset *
                    lerp(0.12, 0.62, variation)) * grid_step;
                const float rotation = tangent_angle + (Hash(cell + effect_params2.z + 37.0) -
                    0.5) * 1.57079632679 * variation * profile.z;
                float sine;
                float cosine;
                sincos(rotation, sine, cosine);
                const float2 delta = pixel - anchor;
                const float2 stroke_size = float2(
                    max(effect_params1.y * profile.x, 8.0),
                    max(effect_params0.w * 2.2 * profile.y, 5.0));
                const float2 local = float2(
                    delta.x * cosine + delta.y * sine,
                    -delta.x * sine + delta.y * cosine) / stroke_size + 0.5;
                if (all(local >= 0.0) && all(local <= 1.0))
                {
                    const float pattern = (float)pattern_index;
                    const float atlas_row = floor(pattern * 0.25);
                    const float2 atlas_cell = float2(pattern - atlas_row * 4.0, atlas_row);
                    const float3 mask_rgb = mask_texture.Sample(source_sampler,
                        (atlas_cell + local) * 0.25).rgb;
                    const float coverage = smoothstep(0.16, 0.70,
                        dot(mask_rgb, float3(0.2126, 0.7152, 0.0722))) * profile.w;
                    if (coverage > best_coverage)
                    {
                        best_coverage = coverage;
                        float4 pigment = 0.0;
                        [unroll]
                        for (int pigment_sample = 0; pigment_sample < 3; ++pigment_sample)
                        {
                            const float along = (pigment_sample - 1.0) *
                                stroke_size.x * 0.16;
                            pigment += source_texture.Sample(source_sampler,
                                saturate((anchor + float2(cosine, sine) * along) *
                                    target_size.zw));
                        }
                        pigment *= 1.0 / 3.0;
                        best_pigment = lerp(source, pigment, 0.72);
                    }
                }
            }
        }
        const float color_steps = lerp(15.0, 8.0, saturate(effect_params1.x));
        best_pigment.rgb = floor(saturate(best_pigment.rgb) * color_steps + 0.5) /
            color_steps;
        painted = best_pigment;
        brush_amount *= saturate(best_coverage);
    }
    else if (effect_params3.y > 0.5)
    {
        // アトラスを使わない既存の単一マスク経路は、従来どおり維持する。
        const float stamp_size = max(effect_params1.y, 1.0);
        const float2 stamp_grid = input.uv * target_size.xy / stamp_size;
        const float2 stamp_cell = floor(stamp_grid);
        const float rotation = tangent_angle + (Hash(stamp_cell + effect_params2.z) - 0.5) *
            6.28318530718 * saturate(effect_params1.z);
        float sine;
        float cosine;
        sincos(rotation, sine, cosine);
        const float2 local = frac(stamp_grid) - 0.5;
        const float2 stamp_uv = frac(float2(local.x * cosine - local.y * sine,
            local.x * sine + local.y * cosine) + 0.5);
        brush_amount *= mask_texture.Sample(source_sampler, stamp_uv).a;
    }
    return lerp(source, painted, brush_amount);
}
