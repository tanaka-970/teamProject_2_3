#include "D3D12DescriptorHeapAllocator.h"

#include <algorithm>

namespace ReplayEngine::Rendering::DX12
{
    bool D3D12DescriptorHeapAllocator::Initialize(ID3D12Device* device,
        D3D12_DESCRIPTOR_HEAP_TYPE type, std::uint32_t capacity,
        bool shader_visible) noexcept
    {
        Reset();
        if (device == nullptr || capacity == 0) return false;

        D3D12_DESCRIPTOR_HEAP_DESC description{};
        description.NumDescriptors = capacity;
        description.Type = type;
        description.Flags = shader_visible
            ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
            : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(device->CreateDescriptorHeap(&description,
            IID_PPV_ARGS(&heap_))))
            return false;

        type_ = type;
        capacity_ = capacity;
        descriptor_size_ = device->GetDescriptorHandleIncrementSize(type);
        shader_visible_ = shader_visible;
        cpu_start_ = heap_->GetCPUDescriptorHandleForHeapStart();
        if (shader_visible_) gpu_start_ = heap_->GetGPUDescriptorHandleForHeapStart();
        free_ranges_.push_back({ 0, capacity_ });
        allocated_.assign(capacity_, 0);
        return true;
    }

    void D3D12DescriptorHeapAllocator::Reset() noexcept
    {
        heap_.Reset();
        free_ranges_.clear();
        allocated_.clear();
        cpu_start_ = {};
        gpu_start_ = {};
        capacity_ = 0;
        used_ = 0;
        descriptor_size_ = 0;
        shader_visible_ = false;
    }

    bool D3D12DescriptorHeapAllocator::Allocate(std::uint32_t count,
        D3D12DescriptorAllocation& allocation) noexcept
    {
        allocation = {};
        if (!IsInitialized() || count == 0) return false;

        for (auto range = free_ranges_.begin(); range != free_ranges_.end(); ++range)
        {
            if (range->count < count) continue;
            const std::uint32_t index = range->begin;
            range->begin += count;
            range->count -= count;
            if (range->count == 0) free_ranges_.erase(range);
            std::fill(allocated_.begin() + index,
                allocated_.begin() + index + count, static_cast<std::uint8_t>(1));
            used_ += count;
            allocation.index = index;
            allocation.count = count;
            allocation.cpu = CpuHandle(index);
            allocation.gpu = GpuHandle(index);
            return true;
        }
        return false;
    }

    bool D3D12DescriptorHeapAllocator::Free(
        const D3D12DescriptorAllocation& allocation) noexcept
    {
        if (!IsInitialized() || !allocation.IsValid() ||
            allocation.index >= capacity_ || allocation.count > capacity_ - allocation.index)
            return false;
        const auto first = allocated_.begin() + allocation.index;
        const auto last = first + allocation.count;
        if (std::find(first, last, static_cast<std::uint8_t>(0)) != last) return false;
        std::fill(first, last, static_cast<std::uint8_t>(0));
        used_ -= allocation.count;

        const auto insert_at = std::lower_bound(free_ranges_.begin(), free_ranges_.end(),
            allocation.index, [](const FreeRange& range, std::uint32_t index)
            { return range.begin < index; });
        const std::size_t inserted_index = static_cast<std::size_t>(
            insert_at - free_ranges_.begin());
        free_ranges_.insert(insert_at, { allocation.index, allocation.count });
        std::size_t current_index = inserted_index;
        if (current_index > 0)
        {
            auto& previous = free_ranges_[current_index - 1];
            auto& current = free_ranges_[current_index];
            if (previous.begin + previous.count == current.begin)
            {
                previous.count += current.count;
                free_ranges_.erase(free_ranges_.begin() + current_index);
                --current_index;
            }
        }
        if (current_index + 1 < free_ranges_.size())
        {
            auto& current = free_ranges_[current_index];
            auto& next = free_ranges_[current_index + 1];
            if (current.begin + current.count == next.begin)
            {
                current.count += next.count;
                free_ranges_.erase(free_ranges_.begin() + current_index + 1);
            }
        }
        return true;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12DescriptorHeapAllocator::CpuHandle(
        std::uint32_t index) const noexcept
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle{};
        if (!IsInitialized() || index >= capacity_) return handle;
        handle.ptr = cpu_start_.ptr + static_cast<SIZE_T>(index) * descriptor_size_;
        return handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE D3D12DescriptorHeapAllocator::GpuHandle(
        std::uint32_t index) const noexcept
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle{};
        if (!IsInitialized() || !shader_visible_ || index >= capacity_) return handle;
        handle.ptr = gpu_start_.ptr + static_cast<UINT64>(index) * descriptor_size_;
        return handle;
    }
}
