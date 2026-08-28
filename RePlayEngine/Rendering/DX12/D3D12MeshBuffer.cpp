#include "D3D12MeshBuffer.h"
#include "D3D12ObjectName.h"

namespace ReplayEngine::Rendering::DX12
{
    bool D3D12MeshBuffer::Upload(ID3D12Device* device,
        D3D12UploadContext& uploader, const void* vertices,
        std::uint32_t vertex_size, std::uint32_t vertex_stride,
        const void* indices, std::uint32_t index_size,
        DXGI_FORMAT index_format) noexcept
    {
        Reset();
        if (vertices == nullptr || indices == nullptr || vertex_size == 0 ||
            vertex_stride == 0 || index_size == 0)
            return false;
        const std::uint32_t index_stride = index_format == DXGI_FORMAT_R16_UINT
            ? 2u : index_format == DXGI_FORMAT_R32_UINT ? 4u : 0u;
        if (index_stride == 0 || index_size % index_stride != 0)
            return false;
        if (!D3D12ResourceFactory::CreateVertexBuffer(device, uploader, vertices,
            vertex_size, vertex_stride, vertex_buffer_, vertex_view_))
            return false;
        if (!D3D12ResourceFactory::CreateIndexBuffer(device, uploader, indices,
            index_size, index_format, index_buffer_, index_view_))
        {
            Reset();
            return false;
        }
        index_count_ = index_size / index_stride;
        return true;
    }

    void D3D12MeshBuffer::SetDebugName(std::string_view key) noexcept
    {
        SetD3D12ObjectNameUtf8(vertex_buffer_.Get(), L"Mesh.VB", key);
        SetD3D12ObjectNameUtf8(index_buffer_.Get(), L"Mesh.IB", key);
    }

    void D3D12MeshBuffer::Reset() noexcept
    {
        vertex_buffer_.Reset();
        index_buffer_.Reset();
        vertex_view_ = {};
        index_view_ = {};
        index_count_ = 0;
    }
}
