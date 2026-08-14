#pragma replay_guid     "00000000000000000000000000000101"
#pragma replay_name     "Pixelate"
#pragma replay_category "BuiltIn"
#pragma replay_domain   layer
#pragma replay_lighting unlit
#pragma property texture BaseMap   "Base Map" default white category "Pixelate"
#pragma property range PixelSize   "Pixel Size" 1..64 = 6 category "Pixelate"
#pragma property range Strength    "Strength" 0..1 = 1 category "Pixelate"
#pragma property range Opacity     "Opacity" 0..1 = 0.45 category "Pixelate"
struct REPLAY_LAYER_INPUT { float4 position:SV_POSITION; float4 color:COLOR; float2 texcoord:TEXCOORD; };
SamplerState replay_layer_sampler : register(s1);
float4 main(REPLAY_LAYER_INPUT pin):SV_TARGET
{
    float size=max(PixelSize,1.0); float2 q=(floor(pin.position.xy/size)+0.5)*size;
    float2 d=(pin.position.xy-q)/size; float mask=1.0-smoothstep(0.35,0.48,length(d));
    float4 c=BaseMap.Sample(replay_layer_sampler,pin.texcoord)*pin.color;
    c.rgb*=lerp(1.0,mask,saturate(Strength)); c.a*=Opacity; return c;
}
