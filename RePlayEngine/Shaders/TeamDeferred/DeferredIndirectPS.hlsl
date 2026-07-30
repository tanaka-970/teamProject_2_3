// 直接光が届かない場所へ環境光を加えるピクセルシェーダー。
#include "DeferredLighting.hlsli"

// 部屋のぼんやり係: 間接光(環境光)
// 直接光が当たらない場所でも真っ黒にならないよう、全体に薄く明るさを敷く。
// Unlit(ライティング無し)の物はここで素の色をそのまま出す。//二の丸先輩は神
float4 main(VS_OUT pin) : SV_TARGET
{
    GBufferData g = SampleGBuffer(pin.uv);
    if (IsEmptyPixel(g))
    {
        return float4(0, 0, 0, 0);
    }

    if (g.shadingModel == SHADING_MODEL_UNLIT ||
        g.shadingModel == SHADING_MODEL_UNLIT_PLAYER ||
        g.shadingModel == SHADING_MODEL_UNLIT_ENEMY)
    {
        // Unlitは素の色100%だが、露出で1.0を超えたぶんは白飛びさせずなだらかに丸める
        return float4(ToneMap(g.baseColor * g.exposure * postParams.x), 1.0f);
    }

    // シャドウマップ由来の「空の見え具合」(0=屋根の下 1=空が見える)。
    // IBLの屋内漏れ防止と、太陽光が無いステージでも影を出すために共有する。ここうまくいってないので確認後で
    const float skyVisibility = SampleSkyVisibility(g.worldPosition);
    // 環境光への影: postParams.y の強さぶんだけ環境光を影で落とす。
    // 太陽光(Directional)が置かれていないステージでも
    // キャラや建物の影が全シーンで見えるようになる。
    const float ambientShadow = lerp(1.0f, skyVisibility, saturate(postParams.y));
    // IBLへの遮蔽: postParams.z の強さで屋根の下のIBLを遮る(屋内への空の漏れ防止)
    const float iblOcclusion = lerp(1.0f, skyVisibility, saturate(postParams.z));

    // 上を向いた面ほど空の明かりを受ける、簡易の半球ライティング
    float skyFactor = g.worldNormal.y * 0.25f + 0.75f;
    float3 ambient = g.baseColor * ambientColor.rgb * ambientColor.a * skyFactor
        * g.ambientOcclusion * ambientShadow * g.exposure * postParams.x;

    // 空画像から環境光を足す。
    // 有効時は、環境マップの色で拡散環境光を染め、なめらか/金属面には映り込みを足す。
    // 拡散はぼかした環境色(高mip)を法線方向で、反射は粗さに応じたmipを反射方向で拾う。
    if (iblParams.z > 0.5f)
    {
        // 拡散: 大きくぼかした環境色を法線方向でサンプル
        float3 envDiffuse = SampleEnvironment(g.worldNormal, 6.0f);
        // 金属は自前の拡散反射を持たないので、金属ほど拡散環境光を弱める
        float3 iblDiffuse = envDiffuse * g.baseColor * (1.0f - g.metallic)
			* iblParams.x * g.ambientOcclusion * iblOcclusion;
        ambient += iblDiffuse * g.exposure * postParams.x;

        // 反射(映り込み): 視線を法線で反射した方向で、粗さに応じたmipをサンプル
        float3 viewDir = normalize(cameraPosition.xyz - g.worldPosition);
        float3 reflectDir = reflect(-viewDir, g.worldNormal);
        float specMip = saturate(g.roughness) * 7.0f;
        float3 envSpecular = SampleEnvironment(reflectDir, specMip);
        // フレネル(端ほど強く映る)。金属は基本色で、非金属は白系で薄く。
        float nDotV = saturate(dot(g.worldNormal, viewDir));
        float fresnel = pow(1.0f - nDotV, 5.0f);
        float3 specColor = lerp(float3(0.04f, 0.04f, 0.04f), g.baseColor, g.metallic);
        specColor = lerp(specColor, float3(1.0f, 1.0f, 1.0f), fresnel);
        // ザラザラ面ほど映り込みは弱まる
        float specStrength = iblParams.y * (1.0f - saturate(g.roughness) * 0.7f);
		ambient += envSpecular * specColor * specStrength * g.ambientOcclusion
			* iblOcclusion
            * g.exposure * postParams.x;
    }
    // 環境光の色をシェーディングモデルに応じて彩度調整する
    //こころがうんち
    ambient = ApplyCategorySaturation(ambient, g.shadingModel);
    return float4(ToneMap(ambient), 1.0f);
}
