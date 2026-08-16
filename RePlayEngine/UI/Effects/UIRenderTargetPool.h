#pragma once

#include <d3d11.h>
#include <wrl.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace ReplayEngine::UI
{
    struct UIRenderTarget final
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;

        bool Resize(ID3D11Device* device, std::uint32_t next_width,
            std::uint32_t next_height,
            DXGI_FORMAT next_format = DXGI_FORMAT_R8G8B8A8_UNORM);
        void Release() noexcept;
    };

    class UIRenderTargetPool final
    {
    public:
        void Initialize(ID3D11Device* device) noexcept;
        void BeginFrame() noexcept;
        UIRenderTarget* Acquire(std::uint32_t width, std::uint32_t height,
            DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM);
        void Release() noexcept;

    private:
        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        std::vector<std::unique_ptr<UIRenderTarget>> targets_;
        std::size_t cursor_ = 0;
    };
}
