// スポット光源の円錐範囲と減衰を計算するピクセルシェーダー。
#include "DeferredLighting.hlsli"

// 懐中電灯係: スポットライト
// 円錐の中だけ照らす。内側の角度では全力、外側の角度に向けてだんだん暗くなる。
float4 main(VS_OUT pin) : SV_TARGET
{
    GBufferData g = SampleGBuffer(pin.uv);
    if (IsEmptyPixel(g))
    {
        return float4(0, 0, 0, 0);
    }

    SurfaceInfo s = BuildSurface(g);

    float3 total = float3(0, 0, 0);
    int count = min((int) lightCounts.y, MAX_SPOT_LIGHTS);
    for (int i = 0; i < count; ++i)
    {
        float3 toLight = spotPositionRange[i].xyz - g.worldPosition;
        float dist = length(toLight);
        float range = max(spotPositionRange[i].w, 0.0001f);
        if (dist >= range) continue;

        float3 L = toLight / max(dist, 0.0001f);

        // 円錐判定: ライトの向きと「ライト→ピクセル」方向の角度差
        float cosAngle = dot(-L, normalize(spotDirectionInner[i].xyz));
        float cosOuter = spotColorOuter[i].w;  // 外側の角度(これより外は0)
        float cosInner = spotDirectionInner[i].w; // 内側の角度(これより内は全力)
        if (cosAngle <= cosOuter) continue;

        float cone = saturate((cosAngle - cosOuter) / max(cosInner - cosOuter, 0.0001f));
        cone = cone * cone * (3.0f - 2.0f * cone);
        float radial = saturate(1.0f - dist / range);
        float attenuation = radial * radial * (3.0f - 2.0f * radial);
        attenuation *= cone;

        float visibility = SampleLocalLightVisibility(g.worldPosition, spotPositionRange[i].xyz);
        total += ComputeDirectLighting(s, L, spotColorOuter[i].rgb, 0.0f) * attenuation * visibility;
    }

    total = ApplyCategorySaturation(total, g.shadingModel);
    return float4(ToneMap(total), 1.0f);
}
