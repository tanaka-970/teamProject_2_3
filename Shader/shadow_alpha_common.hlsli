// 影パスでアルファ抜き材質を扱うための共通処理。
//
// 影パスは通常 Pixel Shader を貼らないが、アルファで形を抜く材質（葉・髪・
// フェンスなど）は、抜きを無視すると板のままの影になる。抜きを宣言している
// 材質のときだけこの Pixel Shader を貼り、それ以外は従来どおり PS なしで走る。
#ifndef __SHADOW_ALPHA_COMMON_HLSLI__
#define __SHADOW_ALPHA_COMMON_HLSLI__

// 通常描画と同じスロットからベースカラーを読む。
// t0  … static_mesh / skinned_mesh / gltf_model が貼る材質テクスチャ
// t40 … RePlay Material Asset の BaseMap (MaterialGpuBinder が貼る)
Texture2D shadow_legacy_base_map : register(t0);
Texture2D shadow_replay_base_map : register(t40);
SamplerState shadow_alpha_sampler : register(s1);

cbuffer SHADOW_ALPHA_CONSTANTS : register(b7)
{
    // x=抜き方 (0:抜かない / 1:この定数のcutoff / 2:内蔵材質のalpha_modeを見る)
    // y=cutoff, z=BaseMapの出所 (0:t0 / 1:t40), w=予約
    float4 shadow_alpha_params;
};

// embedded_alpha は内蔵材質の (mode, cutoff)。使わない経路では 0 を渡す。
void shadow_alpha_clip(float2 texcoord, float2 embedded_alpha)
{
    float mode = shadow_alpha_params.x;
    float cutoff = shadow_alpha_params.y;

    if (mode > 1.5f)
    {
        // GLB 内蔵材質。1=MASK は宣言された cutoff、2=BLEND は最小限だけ抜く。
        if (embedded_alpha.x < 0.5f) return;
        cutoff = embedded_alpha.x < 1.5f ? embedded_alpha.y : 0.01f;
    }
    else if (mode < 0.5f)
    {
        return;
    }

    float alpha = shadow_alpha_params.z > 0.5f
        ? shadow_replay_base_map.Sample(shadow_alpha_sampler, texcoord).a
        : shadow_legacy_base_map.Sample(shadow_alpha_sampler, texcoord).a;
    clip(alpha - cutoff);
}

#endif
