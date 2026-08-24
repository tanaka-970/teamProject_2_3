#pragma once

#include "D3D12UploadContext.h"

#include <d3d12.h>
#include <wrl.h>

#include <cstdint>

namespace ReplayEngine::Rendering::DX12::D3D12ResourceFactory
{
    bool CreateBufferWithData(ID3D12Device* device, D3D12UploadContext& uploader,
        const void* data, std::uint64_t size, D3D12_RESOURCE_STATES final_state,
        Microsoft::WRL::ComPtr<ID3D12Resource>& resource) noexcept;

    bool CreateVertexBuffer(ID3D12Device* device, D3D12UploadContext& uploader,
        const void* data, std::uint32_t size, std::uint32_t stride,
        Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
        D3D12_VERTEX_BUFFER_VIEW& view) noexcept;

    bool CreateIndexBuffer(ID3D12Device* device, D3D12UploadContext& uploader,
        const void* data, std::uint32_t size, DXGI_FORMAT format,
        Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
        D3D12_INDEX_BUFFER_VIEW& view) noexcept;

    constexpr std::uint32_t AlignConstantBufferSize(std::uint32_t size) noexcept
    {
        return (size + 255u) & ~255u;
    }
}
