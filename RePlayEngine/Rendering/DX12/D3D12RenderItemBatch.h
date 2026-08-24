#pragma once

#include "D3D12DescriptorHeapAllocator.h"
#include "D3D12FrameResource.h"

#include "../Adapter/RenderItem.h"

#include <d3d12.h>

#include <cstdint>
#include <vector>

namespace ReplayEngine::Rendering::DX12
{
    enum D3D12RenderItemFlags : std::uint32_t
    {
        D3D12RenderItemFlagSkinned = 1u << 0,
        D3D12RenderItemFlagCastShadow = 1u << 1,
        D3D12RenderItemFlagReceiveShadow = 1u << 2,
        D3D12RenderItemFlagOutline = 1u << 3,
    };

    // GPUへ渡す最小の描画提出データ。Asset文字列は既存解決層に残し、
    // DX12境界では行列・見た目・描画フラグだけを構造化バッファへ詰める。
    struct D3D12RenderItemGpuData final
    {
        DirectX::XMFLOAT4X4 world{};
        DirectX::XMFLOAT4 tint{};
        std::uint64_t owner = 0;
        std::uint32_t flags = 0;
        std::uint32_t reserved = 0;
    };

    static_assert(sizeof(D3D12RenderItemGpuData) % 16 == 0);

    class D3D12RenderItemBatch final
    {
    public:
        D3D12RenderItemBatch() = default;
        ~D3D12RenderItemBatch() = default;

        D3D12RenderItemBatch(const D3D12RenderItemBatch&) = delete;
        D3D12RenderItemBatch& operator=(const D3D12RenderItemBatch&) = delete;

        bool Upload(ID3D12Device* device, D3D12LinearUploadAllocator& upload_allocator,
            D3D12DescriptorHeapAllocator& descriptor_allocator,
            const RenderItemList& items) noexcept;
        void Reset(D3D12DescriptorHeapAllocator* descriptor_allocator) noexcept;

        std::size_t Size() const noexcept { return gpu_items_.size(); }
        bool Empty() const noexcept { return gpu_items_.empty(); }
        const std::vector<D3D12RenderItemGpuData>& Items() const noexcept
        {
            return gpu_items_;
        }
        ID3D12Resource* GpuBuffer() const noexcept { return gpu_buffer_; }
        std::uint64_t GpuBufferOffset() const noexcept { return gpu_buffer_offset_; }
        const D3D12DescriptorAllocation& ShaderResourceAllocation() const noexcept
        {
            return shader_resource_allocation_;
        }

    private:
        std::vector<D3D12RenderItemGpuData> gpu_items_;
        ID3D12Resource* gpu_buffer_ = nullptr; // FrameResource の upload heap を非所有参照。
        std::uint64_t gpu_buffer_offset_ = 0;
        D3D12DescriptorAllocation shader_resource_allocation_{};
    };
}
