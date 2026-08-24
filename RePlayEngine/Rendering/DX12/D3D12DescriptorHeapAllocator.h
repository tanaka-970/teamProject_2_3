#pragma once

#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <vector>

namespace ReplayEngine::Rendering::DX12
{
    struct D3D12DescriptorAllocation final
    {
        static constexpr std::uint32_t InvalidIndex = UINT32_MAX;

        std::uint32_t index = InvalidIndex;
        std::uint32_t count = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu{};

        bool IsValid() const noexcept
        {
            return index != InvalidIndex && count != 0;
        }
    };

    class D3D12DescriptorHeapAllocator final
    {
    public:
        D3D12DescriptorHeapAllocator() = default;
        ~D3D12DescriptorHeapAllocator() { Reset(); }

        D3D12DescriptorHeapAllocator(const D3D12DescriptorHeapAllocator&) = delete;
        D3D12DescriptorHeapAllocator& operator=(const D3D12DescriptorHeapAllocator&) = delete;

        bool Initialize(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
            std::uint32_t capacity, bool shader_visible) noexcept;
        void Reset() noexcept;

        bool Allocate(std::uint32_t count, D3D12DescriptorAllocation& allocation) noexcept;
        bool Free(const D3D12DescriptorAllocation& allocation) noexcept;

        bool IsInitialized() const noexcept { return heap_ != nullptr; }
        std::uint32_t Capacity() const noexcept { return capacity_; }
        std::uint32_t Used() const noexcept { return used_; }
        std::uint32_t DescriptorSize() const noexcept { return descriptor_size_; }
        ID3D12DescriptorHeap* Heap() const noexcept { return heap_.Get(); }
        D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle(std::uint32_t index) const noexcept;
        D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle(std::uint32_t index) const noexcept;

    private:
        struct FreeRange final
        {
            std::uint32_t begin = 0;
            std::uint32_t count = 0;
        };

        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
        D3D12_DESCRIPTOR_HEAP_TYPE type_ = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_start_{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_start_{};
        std::vector<FreeRange> free_ranges_;
        std::vector<std::uint8_t> allocated_;
        std::uint32_t capacity_ = 0;
        std::uint32_t used_ = 0;
        std::uint32_t descriptor_size_ = 0;
        bool shader_visible_ = false;
    };
}
