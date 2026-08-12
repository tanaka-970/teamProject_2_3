#include "gltf_modelInternal.h"

#include <algorithm>

namespace gltf_model_detail
{
    // ミップチェーンをCPU側で作ってからテクスチャを生成する。
    // ローダーはDeviceContextを持たないためGenerateMipsが使えないが、
    // 全レベルを初期データとして渡せば同じ結果になる。
    // 1K以上のテクスチャをミップ無しで使うと遠景が激しくちらつき、
    // TAAでも取り切れないのでミップは必須。
    bool CreateTextureWithMipChain(ID3D11Device* device,
        const std::vector<uint8_t>& top_level_rgba, UINT width, UINT height,
        ID3D11ShaderResourceView** out_view)
    {
        if (!device || !out_view || width == 0 || height == 0) return false;
        if (top_level_rgba.size() < static_cast<size_t>(width) * height * 4) return false;

        UINT mip_levels = 1;
        for (UINT size = (std::max)(width, height); size > 1; size >>= 1) ++mip_levels;

        // レベルごとの画素を保持する。initial dataがポインタを参照するため、
        // CreateTexture2Dが終わるまで生存させる必要がある。
        std::vector<std::vector<uint8_t>> levels;
        levels.reserve(mip_levels);
        levels.push_back(top_level_rgba);

        UINT level_width = width;
        UINT level_height = height;
        for (UINT level = 1; level < mip_levels; ++level)
        {
            const UINT next_width = (std::max)(1u, level_width >> 1);
            const UINT next_height = (std::max)(1u, level_height >> 1);
            const std::vector<uint8_t>& source = levels.back();
            std::vector<uint8_t> destination(static_cast<size_t>(next_width) * next_height * 4);

            // 2x2ボックスフィルタ。奇数サイズでも範囲外を読まないようクランプする。
            for (UINT y = 0; y < next_height; ++y)
            {
                const UINT y0 = (std::min)(y * 2, level_height - 1);
                const UINT y1 = (std::min)(y * 2 + 1, level_height - 1);
                for (UINT x = 0; x < next_width; ++x)
                {
                    const UINT x0 = (std::min)(x * 2, level_width - 1);
                    const UINT x1 = (std::min)(x * 2 + 1, level_width - 1);
                    const size_t taps[4]
                    {
                        (static_cast<size_t>(y0) * level_width + x0) * 4,
                        (static_cast<size_t>(y0) * level_width + x1) * 4,
                        (static_cast<size_t>(y1) * level_width + x0) * 4,
                        (static_cast<size_t>(y1) * level_width + x1) * 4,
                    };
                    const size_t out_index = (static_cast<size_t>(y) * next_width + x) * 4;
                    for (int channel = 0; channel < 4; ++channel)
                    {
                        const unsigned sum =
                            static_cast<unsigned>(source[taps[0] + channel]) +
                            static_cast<unsigned>(source[taps[1] + channel]) +
                            static_cast<unsigned>(source[taps[2] + channel]) +
                            static_cast<unsigned>(source[taps[3] + channel]);
                        destination[out_index + channel] = static_cast<uint8_t>((sum + 2) / 4);
                    }
                }
            }

            levels.push_back(std::move(destination));
            level_width = next_width;
            level_height = next_height;
        }

        std::vector<D3D11_SUBRESOURCE_DATA> initial(mip_levels);
        level_width = width;
        level_height = height;
        for (UINT level = 0; level < mip_levels; ++level)
        {
            initial[level].pSysMem = levels[level].data();
            initial[level].SysMemPitch = level_width * 4;
            initial[level].SysMemSlicePitch = 0;
            level_width = (std::max)(1u, level_width >> 1);
            level_height = (std::max)(1u, level_height >> 1);
        }

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = mip_levels;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if (FAILED(device->CreateTexture2D(&desc, initial.data(), texture.GetAddressOf())))
            return false;
        return SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, out_view));
    }
}
