Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // cell width, intensity, threshold, amount
    float4 effect_params1;
    float4 effect_params2;
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
static const int sample_count = 16;

float4 main(VSOutput input) : SV_TARGET
{
    const float cell_width = max(effect_params0.x, 1.0);
    const float2 pixel = input.uv * target_size.xy;
    const float2 cell_origin = floor(pixel / cell_width) * cell_width;
    float4 cell_average = 0.0;
    // 正方セル自体は保ちつつ、軸整列した少数格子を避けるため
    // 無理数ステップの低食い違い列でセル全体をサンプルする。
    [unroll]
    for (int sample_index = 0; sample_index < sample_count; ++sample_index)
    {
        const float2 fraction = frac(float2(0.5, 0.5) + sample_index *
            float2(0.754877666, 0.569840296));
        const float2 sample_uv = (cell_origin + fraction * cell_width) * target_size.zw;
        cell_average += source_texture.Sample(source_sampler, sample_uv);
    }
    cell_average /= sample_count;
    const float4 source = source_texture.Sample(source_sampler, input.uv);
    return lerp(source, cell_average, saturate(effect_params0.y));
}
