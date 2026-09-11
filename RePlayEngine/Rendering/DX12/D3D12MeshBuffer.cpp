#include "D3D12MeshBuffer.h"
#include "D3D12ObjectName.h"

namespace ReplayEngine::Rendering::DX12
{
    bool D3D12MeshBuffer::Upload(ID3D12Device* device,
        D3D12UploadContext& uploader, const void* vertices,
        std::uint32_t vertex_size, std::uint32_t vertex_stride,
        const void* indices, std::uint32_t index_size,
        DXGI_FORMAT index_format, const Assets::VertexColorRgba8* colors) noexcept
    {
        Reset();
        if (vertices == nullptr || indices == nullptr || vertex_size == 0 ||
            vertex_stride == 0 || index_size == 0 || vertex_size % vertex_stride != 0)
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
        try
        {
            const std::uint32_t count = vertex_size / vertex_stride;
            if (count > UINT32_MAX / sizeof(Assets::VertexColorRgba8)) { Reset(); return false; }
            std::vector<Assets::VertexColorRgba8> white;
            if (colors == nullptr)
            {
                white.resize(count);
                colors = white.data();
            }
            if (!D3D12ResourceFactory::CreateVertexBuffer(device, uploader, colors,
                count * 4u, 4u,
                color_buffer_, color_view_)) { Reset(); return false; }
        }
        catch (...) { Reset(); return false; }
        index_count_ = index_size / index_stride;
        return true;
    }

    bool D3D12MeshBuffer::UploadVerticesSharingIndices(ID3D12Device* device,
        D3D12UploadContext& uploader, const D3D12MeshBuffer& previous,
        const void* vertices, std::uint32_t vertex_size, std::uint32_t vertex_stride) noexcept
    {
        if (!previous.IsValid() || vertex_size != previous.vertex_view_.SizeInBytes ||
            vertex_stride != previous.vertex_view_.StrideInBytes) return false;
        if (!D3D12ResourceFactory::CreateVertexBuffer(device, uploader, vertices,
            vertex_size, vertex_stride, vertex_buffer_, vertex_view_)) return false;
        color_revision_ = previous.color_revision_;
        color_buffer_ = previous.color_buffer_;
        color_view_ = previous.color_view_;
        index_buffer_ = previous.index_buffer_;
        index_view_ = previous.index_view_;
        index_count_ = previous.index_count_;
        return true;
    }

    bool D3D12MeshBuffer::UploadColorsSharingGeometry(ID3D12Device* device,
        D3D12UploadContext& uploader, const D3D12MeshBuffer& previous,
        const Assets::VertexColorRgba8* colors, std::uint32_t count, std::uint64_t revision) noexcept
    {
        if (!previous.IsValid() || !colors || count == 0 || count > UINT32_MAX / 4u ||
            count * 4u != previous.color_view_.SizeInBytes) return false;
        if (!D3D12ResourceFactory::CreateVertexBuffer(device, uploader, colors,
            count * 4u, 4u, color_buffer_, color_view_)) return false;
        vertex_buffer_ = previous.vertex_buffer_;
        vertex_view_ = previous.vertex_view_;
        index_buffer_ = previous.index_buffer_;
        index_view_ = previous.index_view_;
        index_count_ = previous.index_count_;
        color_revision_ = revision;
        return true;
    }
    void D3D12MeshBuffer::SetDebugName(std::string_view key) noexcept
    {
        SetD3D12ObjectNameUtf8(vertex_buffer_.Get(), L"Mesh.VB", key);
        SetD3D12ObjectNameUtf8(index_buffer_.Get(), L"Mesh.IB", key);
        SetD3D12ObjectNameUtf8(color_buffer_.Get(), L"Mesh.ColorVB", key);
    }

    void D3D12MeshBuffer::Reset() noexcept
    {
        vertex_buffer_.Reset();
        index_buffer_.Reset();
        color_buffer_.Reset();
        color_view_ = {};
        vertex_view_ = {};
        index_view_ = {};
        index_count_ = 0;
        color_revision_ = 0;
    }
}
