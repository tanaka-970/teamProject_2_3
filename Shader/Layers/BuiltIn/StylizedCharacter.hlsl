#pragma replay_guid     "00000000000000000000000000000107"
#pragma replay_name     "Stylized Character"
#pragma replay_category "BuiltIn"
#pragma replay_domain   layer
#pragma replay_lighting toon
#pragma property color Tint "Tint" = (1, 1, 1, 1) category "Character"
#pragma property range Opacity "Opacity" 0..1 = 0.45 category "Character"
struct REPLAY_LAYER_INPUT { float4 position:SV_POSITION; float4 color:COLOR; float2 texcoord:TEXCOORD; };
float4 main(REPLAY_LAYER_INPUT pin):SV_TARGET { float4 c=Tint*pin.color; c.a*=Opacity; return c; }
