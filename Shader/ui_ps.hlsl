Texture2D ui_texture : register(t0);
SamplerState ui_sampler : register(s0);

cbuffer ui_visual_constants : register(b0)
{
    float4 fill_color_2;
    float4 fill_params;
    float4 stroke_color_2;
    float4 stroke_params;
    float4 outline_color;
    float4 shadow_offset;
    float4 shadow_color;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float2 gradient_uv : TEXCOORD1;
    float4 color    : COLOR0;
};

// 単一の戻り値へまとめている。分岐ごとに return を書くと fxc が
// X4000（未初期化の可能性）を出すため、初期化した局所変数で受ける。
float GradientAmount(float2 uv)
{
    float amount = 0.0f;
    if (fill_params.x >= 0.5f)
    {
        if (fill_params.x < 1.5f)
        {
            float2 direction = float2(cos(fill_params.y), sin(fill_params.y));
            amount = saturate(dot(uv - fill_params.zw, direction) + 0.5f);
        }
        else
        {
            amount = saturate(length(uv - fill_params.zw) / 0.70710678f);
        }
    }
    return amount;
}

float GlyphAlpha(float2 uv)
{
    return ui_texture.Sample(ui_sampler, uv).a;
}

float4 main(PS_INPUT input) : SV_TARGET
{
    if (stroke_params.z > 0.5f)
    {
        float4 glyph_sample = ui_texture.Sample(ui_sampler, input.uv);
        // 縁取りも影も無い既存 Text は、従来の単純なサンプル経路をそのまま使う。
        if (stroke_params.y <= 0.0f && shadow_color.a <= 0.0f)
            return glyph_sample * input.color;

        float alpha = glyph_sample.a;
        float2 texel = max(fwidth(input.uv),
            float2(1.0f / 2048.0f, 1.0f / 2048.0f));
        float2 outline_step = texel * max(stroke_params.y, 0.0f);
        float outline = 0.0f;
        outline = max(outline, GlyphAlpha(input.uv + float2(outline_step.x, 0.0f)));
        outline = max(outline, GlyphAlpha(input.uv - float2(outline_step.x, 0.0f)));
        outline = max(outline, GlyphAlpha(input.uv + float2(0.0f, outline_step.y)));
        outline = max(outline, GlyphAlpha(input.uv - float2(0.0f, outline_step.y)));

        float shadow = GlyphAlpha(input.uv - shadow_offset.xy * texel);
        float4 result = shadow_color * shadow;
        result = lerp(result, outline_color, outline);
        result = lerp(result, input.color, alpha);
        result.a = max(result.a, alpha * input.color.a);
        return result;
    }

    float4 color = input.color;
    float amount = GradientAmount(input.gradient_uv);
    color = lerp(color, fill_color_2, amount);
    if (stroke_params.x > 0.5f)
    {
        color = lerp(color, stroke_color_2, saturate(input.gradient_uv.x));
    }
    return ui_texture.Sample(ui_sampler, input.uv) * color;
}
