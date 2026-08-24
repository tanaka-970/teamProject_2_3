#include "D3D12RenderItemBatch.h"

#include "D3D12ResourceFactory.h"

namespace ReplayEngine::Rendering::DX12
{
    namespace
    {
        std::uint32_t RenderItemFlags(const RenderItem& item) noexcept
        {
            std::uint32_t flags = 0;
            if (item.skinned) flags |= D3D12RenderItemFlagSkinned;
            if (item.cast_shadow) flags |= D3D12RenderItemFlagCastShadow;
            if (item.receive_shadow) flags |= D3D12RenderItemFlagReceiveShadow;
            if (item.outline) flags |= D3D12RenderItemFlagOutline;
            return flags;
        }
    }

    bool D3D12RenderItemBatch::Upload(ID3D12Device* device,
        D3D12UploadContext& uploader,
        D3D12DescriptorHeapAllocator& descriptor_allocator,
        const RenderItemList& items) noexcept
    {
        Reset(&descriptor_allocator);
        if (device == nullptr || !uploader.IsInitialized() ||
            !descriptor_allocator.IsInitialized())
            return false;

        gpu_items_.reserve(items.Size());
        for (const RenderItem& item : items.Items())
        {
            D3D12RenderItemGpuData gpu_item{};
            gpu_item.world = item.world;
            gpu_item.tint = item.tint;
            gpu_item.owner = item.owner.Value();
            gpu_item.flags = RenderItemFlags(item);
            gpu_items_.push_back(gpu_item);
        }
        if (gpu_items_.empty()) return true;

        if (!D3D12ResourceFactory::CreateBufferWithData(device, uploader,
            gpu_items_.data(), sizeof(D3D12RenderItemGpuData) * gpu_items_.size(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, gpu_buffer_))
        {
            gpu_items_.clear();
            return false;
        }

        if (!descriptor_allocator.Allocate(1, shader_resource_allocation_))
        {
            gpu_buffer_.Reset();
            gpu_items_.clear();
            return false;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_UNKNOWN;
        view.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Buffer.NumElements = static_cast<UINT>(gpu_items_.size());
        view.Buffer.StructureByteStride = sizeof(D3D12RenderItemGpuData);
        device->CreateShaderResourceView(gpu_buffer_.Get(), &view,
            shader_resource_allocation_.cpu);
        return true;
    }

    void D3D12RenderItemBatch::Reset(
        D3D12DescriptorHeapAllocator* descriptor_allocator) noexcept
    {
        if (descriptor_allocator != nullptr &&
            shader_resource_allocation_.IsValid())
            descriptor_allocator->Free(shader_resource_allocation_);
        shader_resource_allocation_ = {};
        gpu_buffer_.Reset();
        gpu_items_.clear();
    }
}
