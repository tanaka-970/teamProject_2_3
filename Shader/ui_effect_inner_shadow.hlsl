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
float alpha_at(float2 uv){return source_texture.Sample(source_sampler,uv).a;}
float4 main(VSOutput i):SV_TARGET{float4 base=source_texture.Sample(source_sampler,i.uv);float a=radians(effect_params1.x);float2 off=float2(cos(a),sin(a))*effect_params0.w*target_size.zw;float2 px=target_size.zw*max(effect_params0.x,0);float outside=0;[unroll]for(int y=-1;y<=1;y++)[unroll]for(int x=-1;x<=1;x++) outside+=1-alpha_at(i.uv+off+float2(x,y)*px*.5f);outside/=9;float strength=outside*base.a*saturate(effect_params0.y);base.rgb=lerp(base.rgb,effect_color.rgb,strength*effect_color.a);return base;}
