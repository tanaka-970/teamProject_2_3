Texture2D ui_texture : register(t0);
SamplerState ui_sampler : register(s0);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

float4 main(PS_INPUT input) : SV_TARGET
{
    return ui_texture.Sample(ui_sampler, input.uv) * input.color;
}
