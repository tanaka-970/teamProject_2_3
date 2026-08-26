cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0;
    float4 effect_params1;
    float4 effect_params2;
    float4 target_size;
};

struct VSInput
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    // エフェクト定数の並びを移行前と変えず、対象寸法から直接NDCへ変換する。
    const float2 size = max(target_size.xy, float2(1.0f, 1.0f));
    const float2 normalized = input.position / size;
    output.position = float4(normalized.x * 2.0f - 1.0f,
        1.0f - normalized.y * 2.0f, 0.0f, 1.0f);
    output.uv = input.uv;
    return output;
}
