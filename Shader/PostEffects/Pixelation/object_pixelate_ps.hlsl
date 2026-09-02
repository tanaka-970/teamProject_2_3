// モデル表面のUVを段階化し、ピクセレーション表現を追加合成する。
struct PS_IN
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
    float4 color : COLOR;
};

#ifndef REPLAY_MATERIAL_PROPERTIES
cbuffer PIXELATE_CONSTANTS : register(b10)
{
    float pixel_size;
    float pixelate_strength;
    float pixelate_opacity;
    float use_gbuffer_color;
};
Texture2D diffuse_map : register(t0);
#endif
Texture2D gbuffer_base_color : register(t12);
SamplerState sampler_linear : register(s1);

float4 main(PS_IN pin) : SV_TARGET
{
#ifdef REPLAY_MATERIAL_PROPERTIES
    float replay_pixel_size = PixelSize;
    float replay_strength = PixelateStrength;
    float replay_opacity = PixelateOpacity;
    float replay_use_gbuffer = UseGBufferColor;
#else
    float replay_pixel_size = pixel_size;
    float replay_strength = pixelate_strength;
    float replay_opacity = pixelate_opacity;
    float replay_use_gbuffer = use_gbuffer_color;
#endif
    float strength = saturate(replay_strength);
    float cell_size = max(replay_pixel_size, 1.0f);
    uint texture_width;
    uint texture_height;
#ifdef REPLAY_MATERIAL_PROPERTIES
    BaseMap.GetDimensions(texture_width, texture_height);
#else
    diffuse_map.GetDimensions(texture_width, texture_height);
#endif
    float2 uv_cell_size = cell_size / max(float2(texture_width, texture_height), 1.0f);
    float2 pixelated_uv = (floor(pin.texcoord / uv_cell_size) + 0.5f) * uv_cell_size;
    pixelated_uv = saturate(pixelated_uv);
#ifdef REPLAY_MATERIAL_PROPERTIES
    float4 color = BaseMap.Sample(sampler_linear,
        lerp(pin.texcoord, pixelated_uv, strength)) * pin.color;
#else
    float4 color = diffuse_map.Sample(sampler_linear,
        lerp(pin.texcoord, pixelated_uv, strength)) * pin.color;
#endif

    if (replay_use_gbuffer > 0.5f)
    {
        uint gbuffer_width;
        uint gbuffer_height;
        gbuffer_base_color.GetDimensions(gbuffer_width, gbuffer_height);
        float2 screen_cell_center = (floor(pin.position.xy / cell_size) + 0.5f) * cell_size;
        int2 gbuffer_position = clamp(int2(lerp(pin.position.xy, screen_cell_center, strength)),
            int2(0, 0), int2(gbuffer_width - 1, gbuffer_height - 1));
        color.rgb = gbuffer_base_color.Load(int3(gbuffer_position, 0)).rgb;
    }

    color.a = saturate(replay_opacity);
    return color;
}
