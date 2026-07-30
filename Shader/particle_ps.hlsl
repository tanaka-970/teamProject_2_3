// パーティクル画像と頂点色を合成するピクセルシェーダー。
Texture2D    particle_tex : register(t0);
SamplerState sampler_lin  : register(s1);

struct GS_OUT
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    float2 texcoord : TEXCOORD0;
};

float4 main(GS_OUT pin) : SV_TARGET
{
    float4 t = particle_tex.Sample(sampler_lin, pin.texcoord);
    // テクスチャ未バインドでも使えるように、デフォルトで丸いブロブ
    if (t.a <= 0.001f)
    {
        float r = length(pin.texcoord - 0.5f) * 2.0f;
        t.a = saturate(1.0f - r);
        t.rgb = 1.0f;
    }
    return pin.color * t;
}
