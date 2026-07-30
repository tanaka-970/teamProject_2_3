// 指定範囲の露出を局所的に下げるピクセルシェーダー。
#include "DeferredLighting.hlsli"

// 範囲内の最終色へ黒を通常合成し、選択カテゴリだけ局所的に露出を下げる。
float4 main(VS_OUT pin) : SV_TARGET
{
    GBufferData g = SampleGBuffer(pin.uv);
    if (IsEmptyPixel(g))
    {
        return float4(0, 0, 0, 0);
    }

    const int categoryBit = 1 << SaturationCategory(g.shadingModel);
    float transmission = 1.0f;
    const int count = min((int)lightCounts.z, MAX_LOCAL_EXPOSURE_LIGHTS);
    for (int i = 0; i < count; ++i)
    {
        const int targetMask = (int)round(localExposureParams[i].y);
        if ((targetMask & categoryBit) == 0) continue;

        float distanceToCenter = distance(
            g.worldPosition, localExposurePositionRadius[i].xyz);
        float radius = max(localExposurePositionRadius[i].w, 0.0001f);
        if (distanceToCenter >= radius) continue;

        float attenuation = saturate(1.0f - distanceToCenter / radius);
        attenuation = attenuation * attenuation * (3.0f - 2.0f * attenuation);
        // 以前はスクリーンスペースの遮蔽判定(SampleLocalLightVisibility)を掛けていたが、
        // 01の二値判定がカメラ依存でまだらに切り替わりすぎて、露出ライトの範囲内に
        // ブロック状の白い模様(減光の抜け)が出ていたため撤去。ゆるせ。コードが汚いから直すぜ。
        // 距離減衰のみの滑らかな減光に戻す(壁越しの多少の漏れより模様ゼロを優先しようね。見てないかもだけど）。
        float strength = saturate(localExposureParams[i].x) * attenuation;
        transmission *= 1.0f - strength;
    }

    return float4(0.0f, 0.0f, 0.0f, saturate(1.0f - transmission));
}
