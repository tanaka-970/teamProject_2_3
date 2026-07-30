// 軌跡画像と頂点色を合成して半透明描画するピクセルシェーダー。
Texture2D    trail_tex   : register(t0);
SamplerState sampler_lin : register(s1);

struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    float2 texcoord : TEXCOORD;
};

float4 main(VS_OUT pin) : SV_TARGET
{
    float4 t = trail_tex.Sample(sampler_lin, pin.texcoord);
    if (t.a <= 0.001f)
    {
        // テクスチャ未指定: V方向 (=トレイル進行) でフェード
        t = float4(1, 1, 1, 1.0f - pin.texcoord.y);
    }
    return pin.color * t;
}
