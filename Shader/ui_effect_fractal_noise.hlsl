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
float hash21(float2 p){p=frac(p*float2(127.1f,311.7f));p+=dot(p,p+19.19f);return frac(p.x*p.y);}
float n2(float2 p){float2 q=floor(p),f=frac(p);f=f*f*(3-2*f);return lerp(lerp(hash21(q),hash21(q+float2(1,0)),f.x),lerp(hash21(q+float2(0,1)),hash21(q+1),f.x),f.y);}
float4 main(VSOutput i):SV_TARGET{float4 base=source_texture.Sample(source_sampler,i.uv);float2 p=i.uv*max(effect_params0.x,.01f)+effect_params2.z+effect_params2.w*effect_params1.w;float v=0,a=.5f,norm=0;int oct=(int)clamp(round(effect_params0.w),1,8);[loop]for(int k=0;k<8;k++){if(k>=oct)break;v+=n2(p)*a;norm+=a;p=p*2.03f+13.7f;a*=.5f;}v/=max(norm,.001f);float4 nc=lerp(effect_color,effect_color_2,v);nc.a=base.a;return lerp(base,nc,saturate(effect_params0.y));}
