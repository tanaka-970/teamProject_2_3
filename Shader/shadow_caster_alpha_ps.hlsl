// 影パスのアルファ抜き。静的Mesh / Primitive / 静的glTF / Landscape が使う。
#include "shadow_alpha_common.hlsli"

struct PS_IN
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

void main(PS_IN pin)
{
    // 内蔵材質は持たないので、抜き方は影用定数 (b7) だけで決まる。
    shadow_alpha_clip(pin.texcoord, float2(0.0f, 0.0f));
}
