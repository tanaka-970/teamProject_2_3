// 深度に応じたフォグ色と濃度を出力するピクセルシェーダー。
#include "DeferredLighting.hlsli"

float4 main(VS_OUT pin) : SV_TARGET
{
    GBufferData g = SampleGBuffer(pin.uv);
    if (IsEmptyPixel(g))
    {
        return float4(0, 0, 0, 0);
    }

    const float fog = ComputeDistanceFogFactor(g.worldPosition);
    return float4(fogColor.rgb, fog);
}
