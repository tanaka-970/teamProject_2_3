#include "UIRenderTargetPool.h"
#include "../../Rendering/RenderStats.h"

#include <algorithm>

namespace ReplayEngine::UI
{
    bool UIRenderTarget::Resize(ID3D11Device* device, std::uint32_t next_width,
        std::uint32_t next_height, DXGI_FORMAT next_format)
    {
        if (device == nullptr || next_width == 0 || next_height == 0) return false;
        if (next_format == DXGI_FORMAT_UNKNOWN) return false;
        if (width == next_width && height == next_height && format == next_format &&
            texture && rtv && srv)
        {
            return true;
        }

        Release();

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = next_width;
        desc.Height = next_height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = next_format;
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
        format = next_format;
        return true;
    }

    void UIRenderTarget::Release() noexcept
    {
        srv.Reset();
        rtv.Reset();
        texture.Reset();
        width = 0;
        height = 0;
        format = DXGI_FORMAT_R8G8B8A8_UNORM;
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
        std::uint32_t height, DXGI_FORMAT format)
    {
        if (device_ == nullptr || width == 0 || height == 0) return nullptr;
        if (cursor_ >= targets_.size())
        {
            targets_.push_back(std::make_unique<UIRenderTarget>());
        }

        UIRenderTarget* target = targets_[cursor_++].get();
        const std::uint32_t safe_width = (std::max)(std::uint32_t{ 1 }, width);
        const std::uint32_t safe_height = (std::max)(std::uint32_t{ 1 }, height);
        const bool exact_reuse = target->width == safe_width &&
            target->height == safe_height && target->format == format &&
            target->texture && target->rtv && target->srv;
        if (!target->Resize(device_.Get(), safe_width, safe_height, format)) return nullptr;
        Rendering::Stats().CountRenderTargetAcquire(exact_reuse);
        return target;
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
