#include "D3D12ResourceFactory.h"

#include <cstring>
#include <vector>

namespace ReplayEngine::Rendering::DX12::D3D12ResourceFactory
{
    bool CreateBufferWithData(ID3D12Device* device, D3D12UploadContext& uploader,
        const void* data, std::uint64_t size, D3D12_RESOURCE_STATES final_state,
        Microsoft::WRL::ComPtr<ID3D12Resource>& resource) noexcept
    {
        resource.Reset();
        if (device == nullptr || !uploader.IsInitialized() || data == nullptr || size == 0)
            return false;

        D3D12_HEAP_PROPERTIES heap_properties{};
        heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        description.Width = size;
        description.Height = 1;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.SampleDesc.Count = 1;
        description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(&heap_properties,
            D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&resource))))
            return false;
        if (!uploader.UploadBuffer(resource.Get(), data, size, final_state))
        {
            resource.Reset();
            return false;
        }
        return true;
    }

    bool CreateConstantBuffer(ID3D12Device* device, D3D12UploadContext& uploader,
        const void* data, std::uint32_t size,
        Microsoft::WRL::ComPtr<ID3D12Resource>& resource) noexcept
    {
        resource.Reset();
        if (device == nullptr || !uploader.IsInitialized() || data == nullptr || size == 0)
            return false;
        const std::uint32_t aligned_size = AlignConstantBufferSize(size);
        try
        {
            std::vector<std::uint8_t> padded_data(aligned_size, 0);
            std::memcpy(padded_data.data(), data, size);
            return CreateBufferWithData(device, uploader, padded_data.data(), aligned_size,
                D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, resource);
        }
        catch (...)
        {
            return false;
        }
    }

    bool CreateVertexBuffer(ID3D12Device* device, D3D12UploadContext& uploader,
        const void* data, std::uint32_t size, std::uint32_t stride,
        Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
        D3D12_VERTEX_BUFFER_VIEW& view) noexcept
    {
        view = {};
        if (stride == 0 || size == 0 || size % stride != 0 ||
            !CreateBufferWithData(device, uploader, data, size,
                D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, resource))
            return false;
        view.BufferLocation = resource->GetGPUVirtualAddress();
        view.SizeInBytes = size;
        view.StrideInBytes = stride;
        return true;
    }

    bool CreateIndexBuffer(ID3D12Device* device, D3D12UploadContext& uploader,
        const void* data, std::uint32_t size, DXGI_FORMAT format,
        Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
        D3D12_INDEX_BUFFER_VIEW& view) noexcept
    {
        view = {};
        const std::uint32_t element_size = format == DXGI_FORMAT_R16_UINT ? 2u :
            format == DXGI_FORMAT_R32_UINT ? 4u : 0u;
        if (element_size == 0 || size == 0 || size % element_size != 0 ||
            !CreateBufferWithData(device, uploader, data, size,
                D3D12_RESOURCE_STATE_INDEX_BUFFER, resource))
            return false;
        view.BufferLocation = resource->GetGPUVirtualAddress();
        view.SizeInBytes = size;
        view.Format = format;
        return true;
    }
}
