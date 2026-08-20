Texture2D source_texture : register(t0);
Texture2D aux_texture : register(t1);
SamplerState source_sampler : register(s0);
cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // radius, intensity, threshold, amount
    float4 effect_params1; // angle, progress, softness, speed
    float4 effect_params2; // direction.xy, seed, time
    float4 target_size;    // width,height,1/w,1/h
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
    float4 effect_params3;
    float4 brush_pattern_settings;
    float4 brush_pattern_weights[4];
};
struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
float4 main(VSOutput i):SV_TARGET
{
 float4 base=source_texture.Sample(source_sampler,i.uv);
 if(effect_params3.y<0.5f) return base;
 float2 d=aux_texture.Sample(source_sampler,i.uv).rg*2.0f-1.0f;
 d*=effect_params2.xy*effect_params0.w*target_size.zw;
 float4 moved=source_texture.Sample(source_sampler,i.uv+d);
 return lerp(base,moved,saturate(effect_params0.y));
}
