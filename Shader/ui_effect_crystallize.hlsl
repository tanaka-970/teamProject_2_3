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
            nearest_distance = closer ? distance_squared : nearest_distance;
            nearest_center = closer ? center : nearest_center;
        }
    }
    const float2 sample_uv = nearest_center * cell_width * target_size.zw;
    const float4 crystal = source_texture.Sample(source_sampler, sample_uv);
    const float4 source = source_texture.Sample(source_sampler, input.uv);
    return lerp(source, crystal, saturate(effect_params0.y));
}
