cbuffer UIConstants : register(b0)
{
    float4 screen_size;
    float4 fill_color_2;
    float4 mode;
    float4 outline_color;
    float4 shadow_offset;
    float4 shadow_color;
    float4 atlas_size;
    float4 fill_parameters;
    float4 clip_parameters;
    float4 clip_bounds;
    float4 mask_parameters;
    float4 mask_uv;
};

struct VSInput
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float4 uv_bounds : TEXCOORD1;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float4 uv_bounds : TEXCOORD1;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float2 safe_size = max(screen_size.xy, float2(1.0, 1.0));
    float2 ndc = float2(input.position.x / safe_size.x * 2.0 - 1.0,
        1.0 - input.position.y / safe_size.y * 2.0);
    output.position = float4(ndc, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    output.uv_bounds = input.uv_bounds;
    return output;
}
