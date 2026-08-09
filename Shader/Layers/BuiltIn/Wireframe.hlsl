#pragma replay_guid     "00000000000000000000000000000105"
#pragma replay_name     "Wireframe"
#pragma replay_category "BuiltIn"
#pragma replay_domain   layer
#pragma replay_lighting unlit
#pragma property color Tint "Color" = (0.2, 0.9, 1, 1) category "Wireframe"
#pragma property range Opacity "Opacity" 0..1 = 0.7 category "Wireframe"
struct REPLAY_LAYER_INPUT { float4 position:SV_POSITION; float4 color:COLOR; float2 texcoord:TEXCOORD; };
float4 main(REPLAY_LAYER_INPUT pin):SV_TARGET { float4 c=Tint*pin.color; c.a*=Opacity; return c; }
