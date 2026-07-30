#include "ImageAsset.h"

#include "../../DirectXTK-main/Inc/DDSTextureLoader.h"
#include "../../DirectXTK-main/Inc/WICTextureLoader.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>

namespace ReplayEngine::Assets
{
    bool ImageAsset::LoadFile(ID3D11Device* device, const std::wstring& path)
    {
        view_.Reset();
        description_ = {};
        if (!device || path.empty() || !std::filesystem::is_regular_file(path)) return false;

        std::wstring extension = std::filesystem::path(path).extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), std::towlower);
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;
        const HRESULT result = extension == L".dds"
            ? DirectX::CreateDDSTextureFromFile(device, path.c_str(), resource.GetAddressOf(), view_.GetAddressOf())
            : DirectX::CreateWICTextureFromFile(device, path.c_str(), resource.GetAddressOf(), view_.GetAddressOf());
        return SUCCEEDED(result) && CaptureDescription();
    }

    bool ImageAsset::LoadMemory(ID3D11Device* device, const void* bytes, size_t byte_count, bool dds)
    {
        view_.Reset();
        description_ = {};
        if (!device || !bytes || byte_count == 0) return false;
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;
        const auto* data = static_cast<const uint8_t*>(bytes);
        const HRESULT result = dds
            ? DirectX::CreateDDSTextureFromMemory(device, data, byte_count, resource.GetAddressOf(), view_.GetAddressOf())
            : DirectX::CreateWICTextureFromMemory(device, data, byte_count, resource.GetAddressOf(), view_.GetAddressOf());
        return SUCCEEDED(result) && CaptureDescription();
    }

    bool ImageAsset::CaptureDescription()
    {
        if (!view_) return false;
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        view_->GetResource(resource.GetAddressOf());
        if (!resource || FAILED(resource.As(&texture))) return false;
        texture->GetDesc(&description_);
        return true;
    }
}
