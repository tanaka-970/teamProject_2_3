cbuffer REPLAY_MATERIAL_CB : register(b9)
{
    float4 base_color;
    float4 emissive_strength;
    float4 surface_params;
    float4 render_params;
};
Texture2D BaseMap : register(t0);
SamplerState BaseSampler : register(s1);
struct PixelInput
{
    float4 position : SV_POSITION;
    float4 world_position : POSITION;
    float4 world_normal : NORMAL;
    float4 color : COLOR;
    float2 texcoord : TEXCOORD;
    float4 current_clip : TEXCOORD1;
    float4 previous_clip : TEXCOORD2;
};
float4 main(PixelInput input) : SV_TARGET
{
    float4 texel = BaseMap.Sample(BaseSampler, input.texcoord) * base_color * input.color;
    const uint alpha_mode = (uint)(render_params.x + 0.5f);
    if (alpha_mode == 1u && texel.a < surface_params.w) discard;
    float3 color = texel.rgb + emissive_strength.rgb * emissive_strength.w;
    return float4(color, texel.a);
}
