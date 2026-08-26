Texture2D ImGuiTexture : register(t0);
SamplerState ImGuiSampler : register(s0);

struct PixelInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

float4 main(PixelInput input) : SV_TARGET
{
    return ImGuiTexture.Sample(ImGuiSampler, input.texcoord) * input.color;
}
