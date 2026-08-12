Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // matrix size, intensity, threshold, levels
    float4 effect_params1;
    float4 effect_params2;
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

float BayerThreshold(int2 pixel, int matrix_size)
{
    const int mask = matrix_size - 1;
    const int x = pixel.x & mask;
    const int y = pixel.y & mask;
    int value = 0;
    int scale = matrix_size * matrix_size / 2;
    [unroll]
    for (int bit = 0; bit < 3; ++bit)
    {
        const int bit_enabled = (1 << bit) < matrix_size ? 1 : 0;
        const int xb = (x >> bit) & 1;
        const int yb = (y >> bit) & 1;
        value += bit_enabled * (((xb ^ yb) * scale) + yb * (scale / 2));
        scale = max(scale / 4, 1);
    }
    return (value + 0.5) / (matrix_size * matrix_size);
}

float4 main(VSOutput input) : SV_TARGET
{
    int matrix_size = 8;
    if (effect_params0.x < 3.0) matrix_size = 2;
    else if (effect_params0.x < 6.0) matrix_size = 4;
    const float threshold = BayerThreshold(int2(floor(input.uv * target_size.xy)),
        matrix_size) - 0.5;
    const float levels = max(floor(effect_params0.w + 0.5), 2.0);
    const float4 source = source_texture.Sample(source_sampler, input.uv);
    float4 dithered = source;
    dithered.rgb = floor(saturate(source.rgb) * (levels - 1.0) +
        threshold + 0.5) / (levels - 1.0);
    return lerp(source, dithered, saturate(effect_params0.y));
}
