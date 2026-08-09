#pragma replay_guid     "00000000000000000000000000000106"
#pragma replay_name     "Outline"
#pragma replay_category "BuiltIn"
#pragma replay_domain   layer
#pragma replay_lighting unlit
#pragma property color Color "Color" = (0, 0, 0, 1) category "Outline"
#pragma property range Width "Width" 0..0.2 = 0.02 category "Outline"
struct REPLAY_LAYER_INPUT { float4 position:SV_POSITION; float4 color:COLOR; float2 texcoord:TEXCOORD; };
float4 main(REPLAY_LAYER_INPUT pin):SV_TARGET { return Color; }
