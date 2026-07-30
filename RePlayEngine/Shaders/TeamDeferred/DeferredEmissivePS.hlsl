// G-Bufferの自己発光成分を照明結果へ加えるピクセルシェーダー。
#include "DeferredLighting.hlsli"

// ネオン係: エミッシブ(自己発光)
float4 main(VS_OUT pin) : SV_TARGET
{
    GBufferData g = SampleGBuffer(pin.uv);
    if (IsEmptyPixel(g))
    {
        return float4(0, 0, 0, 0);
    }

    return float4(ToneMap(g.emissive * g.exposure * postParams.x), 1.0f);
}
