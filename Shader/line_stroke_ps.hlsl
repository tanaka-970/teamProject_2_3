Texture2D line_texture : register(t0);
SamplerState line_sampler : register(s0);

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
};

float4 main(VSOutput input) : SV_TARGET
{
    return line_texture.Sample(line_sampler, input.uv) * input.color;
}
