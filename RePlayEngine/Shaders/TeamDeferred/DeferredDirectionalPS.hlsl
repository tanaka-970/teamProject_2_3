// 平行光源とシャドウでDeferred照明を計算するピクセルシェーダー。
#include "DeferredLighting.hlsli"

// 太陽係: 平行光源 + シャドウマップ
float4 main(VS_OUT pin) : SV_TARGET
{
    GBufferData g = SampleGBuffer(pin.uv);
    if (IsEmptyPixel(g))
    {
        return float4(0, 0, 0, 0); // 何も描かれていない場所(空)はスキップ
    }

    SurfaceInfo s = BuildSurface(g);
    float3 L = normalize(-directionalDirection.xyz); // 表面→光源の方向

    float3 lit = ComputeDirectLighting(s, L, directionalColor.rgb, 1.0f);
    float shadow = SampleShadowVisibility(g.worldPosition);
    lit = ApplyCategorySaturation(lit, g.shadingModel);

    return float4(ToneMap(lit * shadow), 1.0f);
}
