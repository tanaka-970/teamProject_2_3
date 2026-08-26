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
float3 sampleLUT(float3 c){float size=max(effect_params0.x,2);float blue=c.b*(size-1);float b0=floor(blue),b1=min(b0+1,size-1);float width=size*size;float2 uv0=float2((b0*size+c.r*(size-1)+.5f)/width,(c.g*(size-1)+.5f)/size);float2 uv1=float2((b1*size+c.r*(size-1)+.5f)/width,(c.g*(size-1)+.5f)/size);return lerp(aux_texture.Sample(source_sampler,uv0).rgb,aux_texture.Sample(source_sampler,uv1).rgb,frac(blue));}
float4 main(VSOutput i):SV_TARGET{float4 base=source_texture.Sample(source_sampler,i.uv);if(effect_params3.y<.5f)return base;float3 graded=sampleLUT(saturate(base.rgb));return float4(lerp(base.rgb,graded,saturate(effect_params0.y)),base.a);}
