#ifndef REPLAY_MATERIAL_SCHEMA_INJECTED
cbuffer REPLAY_MATERIAL_CB : register(b9) { float4 PhaseColor; };
Texture2D PhaseMap : register(t40);
#endif
#include "static_mesh.hlsli"
#include "frame_common.hlsli"
SamplerState PhaseSampler : register(s1);
float4 main(VS_OUT pin) : SV_TARGET
{
    return PhaseMap.Sample(PhaseSampler, pin.texcoord) * PhaseColor * pin.color;
}
