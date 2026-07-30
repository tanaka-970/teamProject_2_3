// モデル表面のUVを段階化し、ピクセレーション表現を追加合成する。
struct PS_IN
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
    float4 color : COLOR;
};

cbuffer PIXELATE_CONSTANTS : register(b10)
{
    float pixel_size;
    float pixelate_strength;
    float pixelate_opacity;
    float use_gbuffer_color;
};

Texture2D diffuse_map : register(t0);
Texture2D gbuffer_base_color : register(t12);
SamplerState sampler_linear : register(s1);

float4 main(PS_IN pin) : SV_TARGET
{
    float strength = saturate(pixelate_strength);
    float cell_size = max(pixel_size, 1.0f);
    float2 cell_center = (floor(pin.position.xy / cell_size) + 0.5f) * cell_size;
    float4 color = diffuse_map.Sample(sampler_linear, pin.texcoord) * pin.color;

    if (use_gbuffer_color > 0.5f)
    {
        uint gbuffer_width;
        uint gbuffer_height;
        gbuffer_base_color.GetDimensions(gbuffer_width, gbuffer_height);
        int2 gbuffer_position = clamp(int2(pin.position.xy), int2(0, 0),
            int2(gbuffer_width - 1, gbuffer_height - 1));
        color.rgb = gbuffer_base_color.Load(int3(gbuffer_position, 0)).rgb;
    }

    float2 local = (pin.position.xy - cell_center) / cell_size;
    float distance_from_center = length(local);
    float edge_softness = max(fwidth(distance_from_center), 0.015f);
    float dot_mask = 1.0f - smoothstep(0.42f - edge_softness,
        0.42f + edge_softness, distance_from_center);
    color.rgb *= lerp(1.0f, dot_mask, strength);
    color.a = saturate(pixelate_opacity);
    return color;
}
