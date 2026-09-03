// SSAO を半解像度で 1 回だけ焼くパス。ポスト処理からはこの結果を読むだけにする。
#include "dx12_postprocess_common.hlsli"

float main(PixelInput input) : SV_TARGET
{
    return ssaoRaw(input.uv);
}
