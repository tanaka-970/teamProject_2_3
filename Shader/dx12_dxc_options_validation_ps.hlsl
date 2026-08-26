#include "dx12_dxc_validation_common.hlsli"
#ifndef PHASE2_DXC_OPTIONS
#error PHASE2_DXC_OPTIONS missing
#endif
float4 main() : SV_TARGET
{
    return DX12_VALIDATION_COLOR * PHASE2_DXC_OPTIONS;
}
