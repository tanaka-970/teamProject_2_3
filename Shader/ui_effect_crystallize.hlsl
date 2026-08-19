Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // cell width, intensity, jitter, amount
    float4 effect_params1;
    float4 effect_params2; // direction.xy, seed, time
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
static const int neighbor_width = 3;

float2 Hash2(float2 value)
{
    return frac(sin(float2(dot(value, float2(127.1, 311.7)),
        dot(value, float2(269.5, 183.3)))) * 43758.5453);
}

float4 main(VSOutput input) : SV_TARGET
{
    const float cell_width = max(effect_params0.x, 2.0);
    const float2 grid_position = input.uv * target_size.xy / cell_width;
    const float2 base_cell = floor(grid_position);
    float nearest_distance = 1.0e20;
    float second_nearest_distance = 1.0e20;
    float2 nearest_center = base_cell + 0.5;
    [unroll]
    for (int y = 0; y < neighbor_width; ++y)
    {
        [unroll]
        for (int x = 0; x < neighbor_width; ++x)
        {
            const float2 cell = base_cell + float2(x - 1, y - 1);
            const float2 random_offset = (Hash2(cell + effect_params2.z) - 0.5) *
                saturate(effect_params0.z);
            const float2 center = cell + 0.5 + random_offset;
            const float distance_squared = dot(grid_position - center,
                grid_position - center);
            const bool closer = distance_squared < nearest_distance;
            const bool second_closer = !closer &&
                distance_squared < second_nearest_distance;
            second_nearest_distance = closer ? nearest_distance :
                (second_closer ? distance_squared : second_nearest_distance);
            nearest_distance = closer ? distance_squared : nearest_distance;
            nearest_center = closer ? center : nearest_center;
        }
    }
    const float2 sample_uv = nearest_center * cell_width * target_size.zw;
    const float4 crystal = source_texture.Sample(source_sampler, sample_uv);
    const float4 source = source_texture.Sample(source_sampler, input.uv);
    const float4 base_result = lerp(source, crystal, saturate(effect_params0.y));
    const float face_shading = saturate(effect_params1.y);
    const float edge_glow = saturate(effect_params1.z);
    float4 result = base_result;
    if (face_shading > 0.0 || edge_glow > 0.0)
    {
        const float light_angle = radians(effect_params1.x);
        const float2 light_direction = float2(cos(light_angle), sin(light_angle));
        const float2 from_center = grid_position - nearest_center;
        const float center_distance = max(length(from_center), 0.0001);
        const float face = 0.5 + 0.5 * dot(from_center / center_distance,
            light_direction);
        const float face_light = lerp(0.72, 1.28, face);
        float4 shaded_crystal = crystal;
        shaded_crystal.rgb *= lerp(1.0, face_light, face_shading);

        // 第 2 近傍との差はセル境界で 0 になる。セル分割は変えず、そこだけを
        // 縁として発光させる。
        const float boundary_distance = max(sqrt(second_nearest_distance) -
            sqrt(nearest_distance), 0.0);
        const float edge = 1.0 - smoothstep(0.0, 0.15, boundary_distance);
        shaded_crystal.rgb = lerp(shaded_crystal.rgb, effect_color.rgb,
            edge * edge_glow * saturate(effect_color.a));
        result = lerp(source, shaded_crystal, saturate(effect_params0.y));
    }
    return result;
}
