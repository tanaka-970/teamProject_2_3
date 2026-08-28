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

        // GPU が Descriptor 範囲を参照しなくなったことを呼び出し側が保証できる場合
        // （所有フレームの Fence 完了後など）だけ即時解放を使う。
        bool Free(const D3D12DescriptorAllocation& allocation) noexcept;

        // 通常の実行時破棄では、Queue の単調増加する Fence 時系列に対して遅延解放し、
        // 完了値に到達してから再利用する。
        bool Retire(const D3D12DescriptorAllocation& allocation,
            std::uint64_t fence_value) noexcept;
        void ReleaseCompleted(std::uint64_t completed_fence_value) noexcept;

        bool IsInitialized() const noexcept { return heap_ != nullptr; }
        std::uint32_t Capacity() const noexcept { return capacity_; }
        std::uint32_t Used() const noexcept { return used_; }
        std::uint32_t RetiredCount() const noexcept
        {
            return static_cast<std::uint32_t>(retired_.size());
        }
        std::uint32_t DescriptorSize() const noexcept { return descriptor_size_; }
        std::uint32_t PeakUsed() const noexcept { return peak_used_; }
        std::uint64_t AllocationFailures() const noexcept { return allocation_failures_; }
        std::uint32_t FreeRangeCount() const noexcept
        {
            return static_cast<std::uint32_t>(free_ranges_.size());
        }
        float FragmentationRatio() const noexcept;
        D3D12_DESCRIPTOR_HEAP_TYPE Type() const noexcept { return type_; }
        bool ShaderVisible() const noexcept { return shader_visible_; }
        ID3D12DescriptorHeap* Heap() const noexcept { return heap_.Get(); }
        D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle(std::uint32_t index) const noexcept;
        D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle(std::uint32_t index) const noexcept;

    private:
        struct DescriptorFreeRange final
        {
            std::uint32_t begin = 0;
            std::uint32_t count = 0;
        };
        struct RetiredAllocation final
        {
            D3D12DescriptorAllocation allocation{};
            std::uint64_t fence_value = 0;
        };

        bool FreeRange(const D3D12DescriptorAllocation& allocation) noexcept;
        bool IsAllocatedRange(const D3D12DescriptorAllocation& allocation) const noexcept;

        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
        D3D12_DESCRIPTOR_HEAP_TYPE type_ = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_start_{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_start_{};
        std::vector<DescriptorFreeRange> free_ranges_;
        std::vector<RetiredAllocation> retired_;
        std::vector<std::uint8_t> allocated_;
        std::uint32_t capacity_ = 0;
        std::uint32_t used_ = 0;
        std::uint32_t descriptor_size_ = 0;
        std::uint32_t peak_used_ = 0;
        std::uint64_t allocation_failures_ = 0;
        bool shader_visible_ = false;
    };
}
