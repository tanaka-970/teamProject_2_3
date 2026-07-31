#pragma once

#include <d3d11.h>
#include <wrl.h>

#include <cstdint>

namespace ReplayEngine::Rendering
{
    // SSAO/SSR/TAA/タイルドDeferredが使う、書式を指定できる小さなレンダーテクスチャ。
    // 既存のframebufferはR8G8B8A8固定なので、HDRや1chの中間結果はこちらを使う。
    class RenderTexture final
    {
    public:
        bool Create(ID3D11Device* device, uint32_t width, uint32_t height,
            DXGI_FORMAT format, bool with_unordered_access = false)
        {
            Reset();
            if (!device || width == 0 || height == 0) return false;

            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = width;
            desc.Height = height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = format;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            if (with_unordered_access) desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
            if (FAILED(device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf())))
                return false;
            if (FAILED(device->CreateRenderTargetView(texture.Get(), nullptr,
                render_target_view.GetAddressOf()))) return false;
            if (FAILED(device->CreateShaderResourceView(texture.Get(), nullptr,
                shader_resource_view.GetAddressOf()))) return false;
            if (with_unordered_access &&
                FAILED(device->CreateUnorderedAccessView(texture.Get(), nullptr,
                    unordered_access_view.GetAddressOf()))) return false;

            this->width = width;
            this->height = height;
            this->format = format;
            viewport.TopLeftX = 0.0f;
            viewport.TopLeftY = 0.0f;
            viewport.Width = static_cast<float>(width);
            viewport.Height = static_cast<float>(height);
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            return true;
        }

        void Reset()
        {
            texture.Reset();
            render_target_view.Reset();
            shader_resource_view.Reset();
            unordered_access_view.Reset();
            width = 0;
            height = 0;
        }

        bool Valid() const noexcept { return render_target_view && shader_resource_view; }

        // 描画先として設定する。深度は使わないパス専用。
        void Activate(ID3D11DeviceContext* context) const
        {
            if (!context || !render_target_view) return;
            ID3D11RenderTargetView* views[1]{ render_target_view.Get() };
            context->OMSetRenderTargets(1, views, nullptr);
            context->RSSetViewports(1, &viewport);
        }

        void Clear(ID3D11DeviceContext* context,
            float r = 0.0f, float g = 0.0f, float b = 0.0f, float a = 0.0f) const
        {
            if (!context || !render_target_view) return;
            const FLOAT color[4]{ r, g, b, a };
            context->ClearRenderTargetView(render_target_view.Get(), color);
        }

        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> render_target_view;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shader_resource_view;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> unordered_access_view;
        D3D11_VIEWPORT viewport{};
        uint32_t width = 0;
        uint32_t height = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    };
}
