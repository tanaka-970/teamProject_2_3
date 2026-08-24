#include "D3D12UploadContext.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace ReplayEngine::Rendering::DX12
{
    D3D12UploadContext::~D3D12UploadContext()
    {
        Shutdown();
    }

    bool D3D12UploadContext::Initialize(ID3D12Device* device,
        ID3D12CommandQueue* queue) noexcept
    {
        Shutdown();
        if (device == nullptr || queue == nullptr) return false;
        device_ = device;
        queue_ = queue;
        if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&command_allocator_))))
            return false;
        if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator_.Get(), nullptr, IID_PPV_ARGS(&command_list_))))
            return false;
        if (FAILED(command_list_->Close())) return false;
        if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&fence_))))
            return false;
        fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (fence_event_ == nullptr) return false;
        try
        {
            pending_upload_resources_.reserve(64);
        }
        catch (...)
        {
            return false;
        }
        return true;
    }

    void D3D12UploadContext::Shutdown() noexcept
    {
        if (batch_open_) EndBatch();
        if (IsInitialized()) WaitForGpu();
        pending_upload_resources_.clear();
        if (fence_event_ != nullptr)
        {
            CloseHandle(fence_event_);
            fence_event_ = nullptr;
        }
        fence_.Reset();
        command_list_.Reset();
        command_allocator_.Reset();
        queue_.Reset();
        device_.Reset();
        fence_value_ = 0;
        batch_open_ = false;
        batch_has_commands_ = false;
    }

    bool D3D12UploadContext::BeginBatch() noexcept
    {
        if (!IsInitialized() || batch_open_) return false;
        // 直前に提出した Batch の Fence 待ちが失敗している可能性がある。
        // GPU が Command List の処理を終えたと確認できるまで、保持中の Upload 先・
        // 転送先 Resource を破棄してはいけない。
        if (!pending_upload_resources_.empty())
        {
            if (!WaitForGpu()) return false;
            pending_upload_resources_.clear();
        }
        if (FAILED(command_allocator_->Reset()) ||
            FAILED(command_list_->Reset(command_allocator_.Get(), nullptr)))
            return false;
        batch_open_ = true;
        batch_has_commands_ = false;
        return true;
    }

    bool D3D12UploadContext::EndBatch() noexcept
    {
        if (!IsInitialized() || !batch_open_) return false;
        const bool had_commands = batch_has_commands_;
        batch_open_ = false;
        batch_has_commands_ = false;
        if (FAILED(command_list_->Close()))
        {
            pending_upload_resources_.clear();
            return false;
        }
        if (!had_commands)
        {
            pending_upload_resources_.clear();
            return true;
        }
        ID3D12CommandList* lists[] = { command_list_.Get() };
        queue_->ExecuteCommandLists(1, lists);
        const bool waited = WaitForGpu();
        // Upload Resource と転送先 Resource は、Copy Command List の完了が確定するまで
        // 生存させる。Fence 待ち自体が失敗した場合は保持し、次の BeginBatch で
        // Queue を Drain してから解放する。
        if (waited)
            pending_upload_resources_.clear();
        return waited;
    }

    bool D3D12UploadContext::BeginSingleUploadIfNeeded(bool& opened_here) noexcept
    {
        opened_here = false;
        if (batch_open_) return true;
        if (!BeginBatch()) return false;
        opened_here = true;
        return true;
    }

    bool D3D12UploadContext::FinishSingleUploadIfNeeded(bool opened_here) noexcept
    {
        return !opened_here || EndBatch();
    }

    bool D3D12UploadContext::UploadBuffer(ID3D12Resource* destination,
        const void* data, std::uint64_t size,
        D3D12_RESOURCE_STATES final_state) noexcept
    {
        if (!IsInitialized() || destination == nullptr || data == nullptr || size == 0)
            return false;

        bool opened_here = false;
        if (!BeginSingleUploadIfNeeded(opened_here)) return false;

        D3D12_RESOURCE_DESC upload_description{};
        upload_description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        upload_description.Width = size;
        upload_description.Height = 1;
        upload_description.DepthOrArraySize = 1;
        upload_description.MipLevels = 1;
        upload_description.SampleDesc.Count = 1;
        upload_description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES upload_heap{};
        upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        Microsoft::WRL::ComPtr<ID3D12Resource> upload_resource;
        if (FAILED(device_->CreateCommittedResource(&upload_heap,
            D3D12_HEAP_FLAG_NONE, &upload_description,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&upload_resource))))
        {
            if (opened_here) EndBatch();
            return false;
        }

        void* mapped_data = nullptr;
        if (FAILED(upload_resource->Map(0, nullptr, &mapped_data)))
        {
            if (opened_here) EndBatch();
            return false;
        }
        std::memcpy(mapped_data, data, static_cast<std::size_t>(size));
        upload_resource->Unmap(0, nullptr);

        try
        {
            pending_upload_resources_.push_back(upload_resource);
            pending_upload_resources_.push_back(destination);
        }
        catch (...)
        {
            if (opened_here) EndBatch();
            return false;
        }
        command_list_->CopyBufferRegion(destination, 0, upload_resource.Get(), 0, size);
        if (final_state != D3D12_RESOURCE_STATE_COPY_DEST)
        {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = destination;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.StateAfter = final_state;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            command_list_->ResourceBarrier(1, &barrier);
        }
        batch_has_commands_ = true;
        return FinishSingleUploadIfNeeded(opened_here);
    }

    bool D3D12UploadContext::UploadTexture2D(ID3D12Resource* destination,
        const void* rgba_data, std::uint32_t width, std::uint32_t height,
        std::uint32_t source_row_pitch, D3D12_RESOURCE_STATES final_state) noexcept
    {
        if (!IsInitialized() || destination == nullptr || rgba_data == nullptr ||
            width == 0 || height == 0 || source_row_pitch < width * 4u)
            return false;

        bool opened_here = false;
        if (!BeginSingleUploadIfNeeded(opened_here)) return false;

        const D3D12_RESOURCE_DESC destination_desc = destination->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT row_count = 0;
        UINT64 row_size = 0;
        UINT64 total_size = 0;
        device_->GetCopyableFootprints(&destination_desc, 0, 1, 0,
            &footprint, &row_count, &row_size, &total_size);
        if (total_size == 0 || total_size == (std::numeric_limits<UINT64>::max)())
        {
            if (opened_here) EndBatch();
            return false;
        }

        D3D12_RESOURCE_DESC upload_desc{};
        upload_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        upload_desc.Width = total_size;
        upload_desc.Height = 1;
        upload_desc.DepthOrArraySize = 1;
        upload_desc.MipLevels = 1;
        upload_desc.SampleDesc.Count = 1;
        upload_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES upload_heap{};
        upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        Microsoft::WRL::ComPtr<ID3D12Resource> upload_resource;
        if (FAILED(device_->CreateCommittedResource(&upload_heap,
            D3D12_HEAP_FLAG_NONE, &upload_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&upload_resource))))
        {
            if (opened_here) EndBatch();
            return false;
        }

        std::uint8_t* mapped = nullptr;
        if (FAILED(upload_resource->Map(0, nullptr,
            reinterpret_cast<void**>(&mapped))))
        {
            if (opened_here) EndBatch();
            return false;
        }
        const auto* source = static_cast<const std::uint8_t*>(rgba_data);
        for (UINT row = 0; row < row_count; ++row)
        {
            std::memcpy(mapped + footprint.Offset +
                static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
                source + static_cast<std::size_t>(row) * source_row_pitch,
                static_cast<std::size_t>((std::min)(row_size,
                    static_cast<UINT64>(source_row_pitch))));
        }
        upload_resource->Unmap(0, nullptr);

        try
        {
            pending_upload_resources_.push_back(upload_resource);
            pending_upload_resources_.push_back(destination);
        }
        catch (...)
        {
            if (opened_here) EndBatch();
            return false;
        }
        D3D12_TEXTURE_COPY_LOCATION destination_location{};
        destination_location.pResource = destination;
        destination_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination_location.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION source_location{};
        source_location.pResource = upload_resource.Get();
        source_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source_location.PlacedFootprint = footprint;
        command_list_->CopyTextureRegion(&destination_location, 0, 0, 0,
            &source_location, nullptr);
        if (final_state != D3D12_RESOURCE_STATE_COPY_DEST)
        {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = destination;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.StateAfter = final_state;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            command_list_->ResourceBarrier(1, &barrier);
        }
        batch_has_commands_ = true;
        return FinishSingleUploadIfNeeded(opened_here);
    }

    bool D3D12UploadContext::UploadTextureSubresources(
        ID3D12Resource* destination,
        const std::vector<D3D12TextureSubresourceSource>& sources,
        D3D12_RESOURCE_STATES final_state) noexcept
    {
        if (!IsInitialized() || destination == nullptr || sources.empty()) return false;
        for (const D3D12TextureSubresourceSource& source : sources)
        {
            if (source.data == nullptr || source.row_pitch == 0 || source.slice_pitch == 0)
                return false;
        }

        bool opened_here = false;
        if (!BeginSingleUploadIfNeeded(opened_here)) return false;

        const D3D12_RESOURCE_DESC destination_desc = destination->GetDesc();
        const UINT subresource_count = static_cast<UINT>(sources.size());
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints;
        std::vector<UINT> row_counts;
        std::vector<UINT64> row_sizes;
        try
        {
            footprints.resize(subresource_count);
            row_counts.resize(subresource_count);
            row_sizes.resize(subresource_count);
        }
        catch (...)
        {
            if (opened_here) EndBatch();
            return false;
        }
        UINT64 total_size = 0;
        device_->GetCopyableFootprints(&destination_desc, 0, subresource_count, 0,
            footprints.data(), row_counts.data(), row_sizes.data(), &total_size);
        if (total_size == 0 || total_size == (std::numeric_limits<UINT64>::max)())
        {
            if (opened_here) EndBatch();
            return false;
        }

        D3D12_RESOURCE_DESC upload_desc{};
        upload_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        upload_desc.Width = total_size;
        upload_desc.Height = 1;
        upload_desc.DepthOrArraySize = 1;
        upload_desc.MipLevels = 1;
        upload_desc.SampleDesc.Count = 1;
        upload_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES upload_heap{};
        upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        Microsoft::WRL::ComPtr<ID3D12Resource> upload_resource;
        if (FAILED(device_->CreateCommittedResource(&upload_heap,
            D3D12_HEAP_FLAG_NONE, &upload_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&upload_resource))))
        {
            if (opened_here) EndBatch();
            return false;
        }

        std::uint8_t* mapped = nullptr;
        if (FAILED(upload_resource->Map(0, nullptr, reinterpret_cast<void**>(&mapped))))
        {
            if (opened_here) EndBatch();
            return false;
        }
        bool copy_valid = true;
        for (UINT subresource = 0; subresource < subresource_count && copy_valid;
            ++subresource)
        {
            const D3D12TextureSubresourceSource& source = sources[subresource];
            const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint = footprints[subresource];
            const auto* source_bytes = static_cast<const std::uint8_t*>(source.data);
            const UINT depth = (std::max)(1u, footprint.Footprint.Depth);
            if (source.slice_pitch < source.row_pitch * row_counts[subresource])
            {
                copy_valid = false;
                break;
            }
            for (UINT slice = 0; slice < depth; ++slice)
            {
                std::uint8_t* destination_slice = mapped + footprint.Offset +
                    static_cast<std::size_t>(slice) * footprint.Footprint.RowPitch *
                    row_counts[subresource];
                const std::uint8_t* source_slice = source_bytes +
                    static_cast<std::size_t>(slice) * source.slice_pitch;
                for (UINT row = 0; row < row_counts[subresource]; ++row)
                {
                    const std::size_t copy_size = static_cast<std::size_t>((std::min)(
                        row_sizes[subresource], source.row_pitch));
                    std::memcpy(destination_slice +
                        static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
                        source_slice + static_cast<std::size_t>(row) * source.row_pitch,
                        copy_size);
                }
            }
        }
        upload_resource->Unmap(0, nullptr);
        if (!copy_valid)
        {
            if (opened_here) EndBatch();
            return false;
        }
        try
        {
            pending_upload_resources_.push_back(upload_resource);
            pending_upload_resources_.push_back(destination);
        }
        catch (...)
        {
            if (opened_here) EndBatch();
            return false;
        }

        for (UINT subresource = 0; subresource < subresource_count; ++subresource)
        {
            D3D12_TEXTURE_COPY_LOCATION destination_location{};
            destination_location.pResource = destination;
            destination_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination_location.SubresourceIndex = subresource;
            D3D12_TEXTURE_COPY_LOCATION source_location{};
            source_location.pResource = upload_resource.Get();
            source_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            source_location.PlacedFootprint = footprints[subresource];
            command_list_->CopyTextureRegion(&destination_location, 0, 0, 0,
                &source_location, nullptr);
        }
        if (final_state != D3D12_RESOURCE_STATE_COPY_DEST)
        {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = destination;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.StateAfter = final_state;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            command_list_->ResourceBarrier(1, &barrier);
        }
        batch_has_commands_ = true;
        return FinishSingleUploadIfNeeded(opened_here);
    }

    bool D3D12UploadContext::WaitForGpu() noexcept
    {
        if (!IsInitialized() || fence_event_ == nullptr) return false;
        if (fence_value_ == (std::numeric_limits<std::uint64_t>::max)()) return false;
        const std::uint64_t value = ++fence_value_;
        if (FAILED(queue_->Signal(fence_.Get(), value))) return false;
        if (fence_->GetCompletedValue() < value)
        {
            if (FAILED(fence_->SetEventOnCompletion(value, fence_event_))) return false;
            if (WaitForSingleObject(fence_event_, INFINITE) != WAIT_OBJECT_0) return false;
        }
        return true;
    }
}
