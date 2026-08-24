#include "D3D12FrameResource.h"

#include <limits>

namespace ReplayEngine::Rendering::DX12
{
    namespace
    {
        bool AlignUp(std::uint64_t value, std::uint64_t alignment,
            std::uint64_t& aligned) noexcept
        {
            aligned = 0;
            if (alignment == 0) return false;
            const std::uint64_t remainder = value % alignment;
            if (remainder == 0)
            {
                aligned = value;
                return true;
            }
            const std::uint64_t add = alignment - remainder;
            if (value > (std::numeric_limits<std::uint64_t>::max)() - add)
                return false;
            aligned = value + add;
            return true;
        }
    }

    bool D3D12LinearUploadAllocator::Initialize(ID3D12Device* device,
        std::uint64_t capacity) noexcept
    {
        Shutdown();
        if (device == nullptr || capacity == 0) return false;

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        description.Width = capacity;
        description.Height = 1;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.SampleDesc.Count = 1;
        description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
            &description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&resource_))))
            return false;

        void* mapped = nullptr;
        D3D12_RANGE no_read{ 0, 0 };
        if (FAILED(resource_->Map(0, &no_read, &mapped)) || mapped == nullptr)
        {
            resource_.Reset();
            return false;
        }
        mapped_ = static_cast<std::uint8_t*>(mapped);
        capacity_ = capacity;
        used_ = 0;
        return true;
    }

    void D3D12LinearUploadAllocator::Shutdown() noexcept
    {
        if (resource_ != nullptr && mapped_ != nullptr)
            resource_->Unmap(0, nullptr);
        mapped_ = nullptr;
        resource_.Reset();
        capacity_ = 0;
        used_ = 0;
    }

    bool D3D12LinearUploadAllocator::Allocate(std::uint64_t size,
        std::uint64_t alignment, D3D12UploadAllocation& allocation) noexcept
    {
        allocation = {};
        if (!IsInitialized() || size == 0 || alignment == 0) return false;

        std::uint64_t offset = 0;
        if (!AlignUp(used_, alignment, offset)) return false;
        if (offset > capacity_ || size > capacity_ - offset) return false;

        allocation.resource = resource_.Get();
        allocation.cpu = mapped_ + static_cast<std::size_t>(offset);
        allocation.gpu = resource_->GetGPUVirtualAddress() + offset;
        allocation.offset = offset;
        allocation.size = size;
        used_ = offset + size;
        return true;
    }

    bool D3D12FrameResource::Initialize(ID3D12Device* device,
        std::uint64_t upload_capacity) noexcept
    {
        Shutdown();
        if (device == nullptr || upload_capacity == 0) return false;
        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&command_allocator))))
            return false;
        if (!upload_allocator.Initialize(device, upload_capacity))
        {
            command_allocator.Reset();
            return false;
        }
        return true;
    }

    void D3D12FrameResource::ResetAfterGpu() noexcept
    {
        upload_allocator.Reset();
        frame_constants_gpu = 0;
    }

    void D3D12FrameResource::Shutdown() noexcept
    {
        upload_allocator.Shutdown();
        command_allocator.Reset();
        fence_value = 0;
        frame_constants_gpu = 0;
    }
}
