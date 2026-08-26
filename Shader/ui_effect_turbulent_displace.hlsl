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
float hash21(float2 p){p=frac(p*float2(123.34f,456.21f));p+=dot(p,p+45.32f);return frac(p.x*p.y);}
float noise(float2 p){float2 i=floor(p),f=frac(p);f=f*f*(3-2*f);return lerp(lerp(hash21(i),hash21(i+float2(1,0)),f.x),lerp(hash21(i+float2(0,1)),hash21(i+1),f.x),f.y);}
float fbm(float2 p){float v=0,a=.5f;[unroll]for(int k=0;k<5;k++){v+=a*noise(p);p=p*2.03f+17.17f;a*=.5f;}return v;}
float4 main(VSOutput i):SV_TARGET{float4 base=source_texture.Sample(source_sampler,i.uv);float scale=max(effect_params0.x,1.0f);float2 p=i.uv*target_size.xy/scale+effect_params2.z;float t=effect_params2.w*effect_params1.w;float2 n=float2(fbm(p+t),fbm(p+float2(37.2f,11.7f)-t));float2 duv=(n*2-1)*effect_params0.w*target_size.zw;float4 moved=source_texture.Sample(source_sampler,i.uv+duv);return lerp(base,moved,saturate(effect_params0.y));}
