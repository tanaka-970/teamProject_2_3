#pragma once

#include "D3D12ResourceFactory.h"

#include <d3d12.h>
#include <wrl.h>

#include <cstdint>

namespace ReplayEngine::Rendering::DX12
{
    class D3D12MeshBuffer final
    {
    public:
        D3D12MeshBuffer() = default;
        ~D3D12MeshBuffer() = default;

        D3D12MeshBuffer(const D3D12MeshBuffer&) = delete;
        D3D12MeshBuffer& operator=(const D3D12MeshBuffer&) = delete;

        bool Upload(ID3D12Device* device, D3D12UploadContext& uploader,
            const void* vertices, std::uint32_t vertex_size,
            std::uint32_t vertex_stride, const void* indices,
            std::uint32_t index_size, DXGI_FORMAT index_format) noexcept;
        void Reset() noexcept;

        bool IsValid() const noexcept
        {
            return vertex_buffer_ != nullptr && index_buffer_ != nullptr &&
                vertex_view_.SizeInBytes != 0 && index_view_.SizeInBytes != 0;
        }
        const D3D12_VERTEX_BUFFER_VIEW& VertexView() const noexcept
        {
            return vertex_view_;
        }
        const D3D12_INDEX_BUFFER_VIEW& IndexView() const noexcept
        {
            return index_view_;
        }
        std::uint32_t IndexCount() const noexcept { return index_count_; }

    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> vertex_buffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> index_buffer_;
        D3D12_VERTEX_BUFFER_VIEW vertex_view_{};
        D3D12_INDEX_BUFFER_VIEW index_view_{};
        std::uint32_t index_count_ = 0;
    };
}
