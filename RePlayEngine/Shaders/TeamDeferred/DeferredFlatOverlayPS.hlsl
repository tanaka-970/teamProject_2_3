// 選択カテゴリへ一定色のオーバーレイを重ねるピクセルシェーダー。
#include "DeferredLighting.hlsli"

float4 main(VS_OUT pin) : SV_TARGET
{
    GBufferData g = SampleGBuffer(pin.uv);
    if (IsEmptyPixel(g))
    {
        return float4(0, 0, 0, 0);
    }

    const int category = SaturationCategory(g.shadingModel);
    return flatOverlayColor[category];
}
