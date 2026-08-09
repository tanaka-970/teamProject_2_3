cbuffer ui_constants : register(b0)
{
    float4 screen_size;
};

struct VS_INPUT
{
    float2 position : POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

struct VS_OUTPUT
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    const float2 inv_screen = float2(1.0, 1.0) /
        max(screen_size.xy, float2(1.0, 1.0));
    output.position = float4(
        input.position.x * inv_screen.x * 2.0 - 1.0,
        1.0 - input.position.y * inv_screen.y * 2.0,
        0.0,
        1.0);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}
