// Toon陰影の段階化、リムライト、輪郭設定を共通定義する。
#ifndef __TOON_COMMON_HLSLI__
#define __TOON_COMMON_HLSLI__

// 3段階のセルランプ
float toon_ramp_band(float ndotl)
{
    float lit = saturate(ndotl * 0.5f + 0.5f);
    float band = lit < 0.42f ? 0.0f : (lit < 0.76f ? 1.0f : 2.0f);
    return (band + 0.5f) / 3.0f;
}

// Toon シェーディングのコア計算
//   base_color  : 拡散テクスチャ x マテリアルカラー
//   ramp_sample : ランプテクスチャを toon_ramp_band の U で1Dサンプルした色
//   N, L, V, H  : 法線/ライト/視線/ハーフ (すべて正規化済み)
//   T           : タンジェント (異方性スペキュラ用、無ければ float3(1,0,0))
//   light_color : シーン光源色
//   shadow_tint : シャドウ部のティント (rgb=色, a=強度)
//   rim_color   : リムライト色 (rgb=色, a=強度)
//   spec_color  : ハイライト色 (rgb=色, a=強度)
// toon_params：x=リム指数、y=リム閾値、z=リム強度
// spec_params：x=鏡面指数、y=閾値、z=強度、w=異方性
float3 toon_shade(float3 base_color,
                  float3 ramp_sample,
                  float3 N, float3 L, float3 V, float3 H, float3 T,
                  float3 light_color,
                  float4 shadow_tint,
                  float4 rim_color,
                  float4 spec_color,
                  float4 toon_params,
                  float4 spec_params)
{
    float ndotl = dot(N, L);

    float3 lit = base_color * ramp_sample * light_color;

    // 影部にティント
    float shadow_mask = 1.0f - saturate(ndotl * 0.5f + 0.5f);
    lit = lerp(lit, lit * shadow_tint.rgb, shadow_mask * shadow_tint.a);

    // リムライト
    float rim_raw = pow(1.0f - saturate(dot(N, V)), toon_params.x);
    float rim = smoothstep(toon_params.y, toon_params.y + 0.035f, rim_raw) * toon_params.z;

    // 異方性 or Blinn-Phong ブレンド
    float tangent_half = dot(T, H);
    float aniso = pow(sqrt(saturate(1.0f - tangent_half * tangent_half)), spec_params.x);
    float blinn = pow(saturate(dot(N, H)), spec_params.x);
    float spec_src = lerp(blinn, aniso, saturate(spec_params.w));
    float spec = smoothstep(spec_params.y, spec_params.y + 0.025f, spec_src) * spec_params.z;

    float3 color = lit;
    color += rim_color.rgb * rim * rim_color.a;
    color += spec_color.rgb * spec * spec_color.a;
    return color;
}

#endif // __TOON_COMMON_HLSLI__
