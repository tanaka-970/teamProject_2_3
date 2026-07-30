// モデル表面のUVを段階化し、ピクセレーション表現を追加合成する。
struct PS_IN
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
    float4 color : COLOR;
};

cbuffer PIXELATE_CONSTANTS : register(b10)
{
    float pixel_grid;
    float pixelate_strength;
    float2 padding_;
};

Texture2D diffuse_map : register(t0);
SamplerState sampler_linear : register(s1);

float4 main(PS_IN pin) : SV_TARGET
{
    float grid = max(pixel_grid, 1.0f);
    float2 pixelated_uv = (floor(pin.texcoord * grid) + 0.5f) / grid;
    float2 uv = lerp(pin.texcoord, pixelated_uv, saturate(pixelate_strength));
    return diffuse_map.Sample(sampler_linear, uv) * pin.color;
}
