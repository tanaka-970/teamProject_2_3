// 点光源の距離減衰を計算して加算するピクセルシェーダー。
#include "DeferredLighting.hlsli"

// 裸電球係: ポイントライト
// 電球から近いほど明るく、届く距離(radius)の外は真っ暗。複数まとめて足し算。
float4 main(VS_OUT pin) : SV_TARGET
{
    GBufferData g = SampleGBuffer(pin.uv);
    if (IsEmptyPixel(g))
    {
        return float4(0, 0, 0, 0);
    }

    SurfaceInfo s = BuildSurface(g);

    float3 total = float3(0, 0, 0);
    int count = min((int) lightCounts.x, MAX_POINT_LIGHTS);
    for (int i = 0; i < count; ++i)
    {
        float3 toLight = pointPositionRadius[i].xyz - g.worldPosition;
        float dist = length(toLight);
        float radius = max(pointPositionRadius[i].w, 0.0001f);
        if (dist >= radius) continue; // 届く距離の外

        float3 L = toLight / max(dist, 0.0001f);
        // 距離減衰: 近いほど1、端で0 (2乗で自然な落ち方に)
        float attenuation = saturate(1.0f - dist / radius);
        attenuation = attenuation * attenuation * (3.0f - 2.0f * attenuation);

        float visibility = SampleLocalLightVisibility(g.worldPosition, pointPositionRadius[i].xyz);
        total += ComputeDirectLighting(s, L, pointColor[i].rgb, 0.0f) * attenuation * visibility;
    }

    total = ApplyCategorySaturation(total, g.shadingModel);
    return float4(ToneMap(total), 1.0f);
}
