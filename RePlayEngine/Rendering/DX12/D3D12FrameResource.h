#pragma once

#include <d3d12.h>
#include <wrl.h>

#include <cstddef>
#include <cstdint>

namespace ReplayEngine::Rendering::DX12
{
    struct D3D12UploadAllocation final
    {
        ID3D12Resource* resource = nullptr;
        void* cpu = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
        std::uint64_t offset = 0;
        std::uint64_t size = 0;

        bool IsValid() const noexcept
        {
            return resource != nullptr && cpu != nullptr && gpu != 0 && size != 0;
        }
    };

    class D3D12LinearUploadAllocator final
    {
    public:
        D3D12LinearUploadAllocator() = default;
        ~D3D12LinearUploadAllocator() { Shutdown(); }

        D3D12LinearUploadAllocator(const D3D12LinearUploadAllocator&) = delete;
        D3D12LinearUploadAllocator& operator=(const D3D12LinearUploadAllocator&) = delete;

        bool Initialize(ID3D12Device* device, std::uint64_t capacity) noexcept;
        void Shutdown() noexcept;
        void Reset() noexcept { used_ = 0; }
        bool Allocate(std::uint64_t size, std::uint64_t alignment,
            D3D12UploadAllocation& allocation) noexcept;

        bool IsInitialized() const noexcept
        {
            return resource_ != nullptr && mapped_ != nullptr;
        }
        std::uint64_t Capacity() const noexcept { return capacity_; }
        std::uint64_t Used() const noexcept { return used_; }
        ID3D12Resource* Resource() const noexcept { return resource_.Get(); }

    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
        std::uint8_t* mapped_ = nullptr;
        std::uint64_t capacity_ = 0;
        std::uint64_t used_ = 0;
    };

    struct D3D12FrameResource final
    {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> command_allocator;
        D3D12LinearUploadAllocator upload_allocator;
        std::uint64_t fence_value = 0;
        D3D12_GPU_VIRTUAL_ADDRESS frame_constants_gpu = 0;

        bool Initialize(ID3D12Device* device, std::uint64_t upload_capacity) noexcept;
        void ResetAfterGpu() noexcept;
        void Shutdown() noexcept;
    };
}
