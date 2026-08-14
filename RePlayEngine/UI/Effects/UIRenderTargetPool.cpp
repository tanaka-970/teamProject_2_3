#include "UIRenderTargetPool.h"

#include <algorithm>

namespace ReplayEngine::UI
{
    bool UIRenderTarget::Resize(ID3D11Device* device, std::uint32_t next_width,
        std::uint32_t next_height)
    {
        if (device == nullptr || next_width == 0 || next_height == 0) return false;
        if (width == next_width && height == next_height && texture && rtv && srv)
        {
            return true;
        }

        Release();

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = next_width;
        desc.Height = next_height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        texture.Reset();
        rtv.Reset();
        srv.Reset();
        if (FAILED(device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf())))
        {
            return false;
        }
        if (FAILED(device->CreateRenderTargetView(texture.Get(), nullptr,
            rtv.GetAddressOf())))
        {
            Release();
            return false;
        }
        if (FAILED(device->CreateShaderResourceView(texture.Get(), nullptr,
            srv.GetAddressOf())))
        {
            Release();
            return false;
        }

        width = next_width;
        height = next_height;
        return true;
    }

    void UIRenderTarget::Release() noexcept
    {
        srv.Reset();
        rtv.Reset();
        texture.Reset();
        width = 0;
        height = 0;
    }

    void UIRenderTargetPool::Initialize(ID3D11Device* device) noexcept
    {
        Release();
        device_ = device;
    }

    void UIRenderTargetPool::BeginFrame() noexcept
    {
        cursor_ = 0;
    }

    UIRenderTarget* UIRenderTargetPool::Acquire(std::uint32_t width,
        std::uint32_t height)
    {
        if (device_ == nullptr || width == 0 || height == 0) return nullptr;
        if (cursor_ >= targets_.size())
        {
            targets_.push_back(std::make_unique<UIRenderTarget>());
        }

        UIRenderTarget* target = targets_[cursor_++].get();
        const std::uint32_t safe_width = (std::max)(std::uint32_t{ 1 }, width);
        const std::uint32_t safe_height = (std::max)(std::uint32_t{ 1 }, height);
        return target->Resize(device_.Get(), safe_width, safe_height) ? target : nullptr;
    }

    void UIRenderTargetPool::Release() noexcept
    {
        for (std::unique_ptr<UIRenderTarget>& target : targets_)
        {
            if (target) target->Release();
        }
        targets_.clear();
        cursor_ = 0;
        device_.Reset();
    }
}
