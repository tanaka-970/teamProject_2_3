#pragma once

#include <d3d11.h>
#include <wrl.h>

#include <cstddef>
#include <string>

namespace ReplayEngine::Assets
{
// エンジン向け画像資産。DDSは専用経路で扱い、PNG／JPEG／BMP／TIFFなどはWICで読む。
    class ImageAsset final
    {
    public:
        bool LoadFile(ID3D11Device* device, const std::wstring& path);
        bool LoadMemory(ID3D11Device* device, const void* bytes, size_t byte_count, bool dds);

        ID3D11ShaderResourceView* View() const noexcept { return view_.Get(); }
        const D3D11_TEXTURE2D_DESC& Description() const noexcept { return description_; }
        bool IsLoaded() const noexcept { return view_ != nullptr; }

    private:
        bool CaptureDescription();

        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view_;
        D3D11_TEXTURE2D_DESC description_{};
    };
}
