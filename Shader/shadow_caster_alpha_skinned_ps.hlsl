// 影パスのアルファ抜き（Skinned Mesh 用）。
// GLB 内蔵材質の alpha_mode は subset ごとに b0 へ載るので、そちらも見る。
#include "skinned_mesh.hlsli"
#include "shadow_alpha_common.hlsli"
#include "shadow_coverage_common.hlsli"

struct PS_IN
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
    float3 world_position : TEXCOORD1;
};

void main(PS_IN pin)
{
    shadow_alpha_clip(pin.texcoord, gltf_alpha.xy);
    shadow_coverage_clip(pin.world_position);
}
