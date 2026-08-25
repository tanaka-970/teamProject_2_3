#include "D3D12Diagnostics.h"
#include "D3D12ObjectName.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string_view>

namespace ReplayEngine::Rendering::DX12
{
    namespace
    {
        constexpr std::uint32_t QueryCount =
            static_cast<std::uint32_t>(D3D12GpuPassCount * 2u);

        const char* SeverityName(D3D12_MESSAGE_SEVERITY severity) noexcept
        {
            switch (severity)
            {
            case D3D12_MESSAGE_SEVERITY_CORRUPTION: return "CORRUPTION";
            case D3D12_MESSAGE_SEVERITY_ERROR: return "ERROR";
            case D3D12_MESSAGE_SEVERITY_WARNING: return "WARNING";
            case D3D12_MESSAGE_SEVERITY_INFO: return "INFO";
            default: return "MESSAGE";
            }
        }

        bool ContainsLiveObjectText(std::string_view text) noexcept
        {
            return text.find("Live") != std::string_view::npos ||
                text.find("live") != std::string_view::npos;
        }
    }

    const char* D3D12GpuPassName(D3D12GpuPass pass) noexcept
    {
        switch (pass)
        {
        case D3D12GpuPass::ShadowDirectional: return "ShadowDirectional";
        case D3D12GpuPass::ShadowLocal: return "ShadowLocal";
        case D3D12GpuPass::GBuffer: return "GBuffer";
        case D3D12GpuPass::Lighting: return "Lighting";
        case D3D12GpuPass::Forward: return "Forward";
        case D3D12GpuPass::PostProcess: return "PostProcess";
        case D3D12GpuPass::RuntimeUI: return "RuntimeUI";
        case D3D12GpuPass::UIEffect: return "UIEffect";
        case D3D12GpuPass::UIPreview: return "UIPreview";
        case D3D12GpuPass::ImGui: return "ImGui";
        case D3D12GpuPass::Present: return "Present";
        case D3D12GpuPass::ModelEffect: return "ModelEffect";
        case D3D12GpuPass::ScreenEffect: return "ScreenEffect";
        default: return "Unknown";
        }
    }

    bool D3D12Diagnostics::Initialize(ID3D12Device* device,
        ID3D12CommandQueue* queue) noexcept
    {
        Shutdown();
        if (device == nullptr || queue == nullptr) return false;
        device_ = device;
        queue_ = queue;
        if (FAILED(queue_->GetTimestampFrequency(&timestamp_frequency_)) ||
            timestamp_frequency_ == 0) return false;
        device_.As(&info_queue_);
        if (info_queue_)
        {
            info_queue_->SetMessageCountLimit(static_cast<UINT64>(-1));
            info_queue_->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE);
            info_queue_->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
        }

        D3D12_QUERY_HEAP_DESC query_desc{};
        query_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        query_desc.Count = QueryCount;
        const std::uint64_t readback_bytes = sizeof(std::uint64_t) * QueryCount;
        for (std::uint32_t slot = 0; slot < FrameCount; ++slot)
        {
            TimestampFrame& frame = frames_[slot];
            if (FAILED(device_->CreateQueryHeap(&query_desc, IID_PPV_ARGS(&frame.heap))))
            {
                Shutdown();
                return false;
            }
            SetD3D12ObjectName(frame.heap.Get(), L"Profiler.TimestampHeap", L"Frame", slot);

            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC resource{};
            resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            resource.Width = readback_bytes;
            resource.Height = 1;
            resource.DepthOrArraySize = 1;
            resource.MipLevels = 1;
            resource.SampleDesc.Count = 1;
            resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
                &resource, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                IID_PPV_ARGS(&frame.readback))))
            {
                Shutdown();
                return false;
            }
            SetD3D12ObjectName(frame.readback.Get(), L"Profiler.TimestampReadback", L"Frame", slot);
        }
        initialized_ = true;
        return true;
    }

    void D3D12Diagnostics::PrepareLiveObjectReport() noexcept
    {
        for (auto& frame : frames_)
        {
            frame.heap.Reset();
            frame.readback.Reset();
            frame = {};
        }
        queue_.Reset();
    }

    void D3D12Diagnostics::Shutdown() noexcept
    {
        PrepareLiveObjectReport();
        info_queue_.Reset();
        device_.Reset();
        latest_timing_ = {};
        pending_messages_.clear();
        timestamp_frequency_ = 0;
        next_frame_id_ = 1;
        current_slot_ = 0;
        initialized_ = false;
    }

    void D3D12Diagnostics::SetDebugLogPath(std::filesystem::path path)
    {
        if (!path.empty()) debug_log_path_ = std::move(path);
    }

    void D3D12Diagnostics::SetBreakOnError(bool enabled) noexcept
    {
        if (!info_queue_) return;
        info_queue_->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION,
            enabled ? TRUE : FALSE);
        info_queue_->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR,
            enabled ? TRUE : FALSE);
    }

    void D3D12Diagnostics::ResolveFrame(TimestampFrame& frame,
        std::uint64_t completed_fence) noexcept
    {
        if (!frame.pending || frame.fence_value == 0 ||
            completed_fence < frame.fence_value || !frame.readback) return;
        const std::uint64_t bytes = sizeof(std::uint64_t) * QueryCount;
        D3D12_RANGE read_range{ 0, static_cast<SIZE_T>(bytes) };
        void* mapped = nullptr;
        if (FAILED(frame.readback->Map(0, &read_range, &mapped)) || mapped == nullptr)
            return;
        const auto* values = static_cast<const std::uint64_t*>(mapped);
        D3D12GpuTimingSnapshot snapshot{};
        snapshot.frame_id = frame.frame_id;
        for (std::size_t index = 0; index < D3D12GpuPassCount; ++index)
        {
            if (!frame.began[index] || !frame.ended[index]) continue;
            const std::uint64_t begin = values[index * 2u];
            const std::uint64_t end = values[index * 2u + 1u];
            if (end < begin) continue;
            snapshot.milliseconds[index] = static_cast<double>(end - begin) * 1000.0 /
                static_cast<double>(timestamp_frequency_);
            snapshot.valid[index] = true;
        }
        D3D12_RANGE write_range{ 0, 0 };
        frame.readback->Unmap(0, &write_range);
        latest_timing_ = snapshot;
        frame.pending = false;
    }

    void D3D12Diagnostics::BeginFrame(std::uint32_t frame_slot,
        std::uint64_t completed_fence) noexcept
    {
        if (!initialized_ || frame_slot >= FrameCount) return;
        current_slot_ = frame_slot;
        TimestampFrame& frame = frames_[frame_slot];
        ResolveFrame(frame, completed_fence);
        frame.began.fill(false);
        frame.ended.fill(false);
        frame.frame_id = next_frame_id_++;
        frame.fence_value = 0;
        frame.pending = false;
    }

    void D3D12Diagnostics::BeginPass(ID3D12GraphicsCommandList* list,
        D3D12GpuPass pass) noexcept
    {
        if (!initialized_ || list == nullptr) return;
        const std::size_t index = static_cast<std::size_t>(pass);
        if (index >= D3D12GpuPassCount) return;
        TimestampFrame& frame = frames_[current_slot_];
        if (frame.began[index]) return;
        list->EndQuery(frame.heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
            static_cast<UINT>(index * 2u));
        frame.began[index] = true;
    }

    void D3D12Diagnostics::EndPass(ID3D12GraphicsCommandList* list,
        D3D12GpuPass pass) noexcept
    {
        if (!initialized_ || list == nullptr) return;
        const std::size_t index = static_cast<std::size_t>(pass);
        if (index >= D3D12GpuPassCount) return;
        TimestampFrame& frame = frames_[current_slot_];
        if (!frame.began[index] || frame.ended[index]) return;
        list->EndQuery(frame.heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
            static_cast<UINT>(index * 2u + 1u));
        frame.ended[index] = true;
    }

    void D3D12Diagnostics::ResolveQueries(ID3D12GraphicsCommandList* list) noexcept
    {
        if (!initialized_ || list == nullptr) return;
        TimestampFrame& frame = frames_[current_slot_];
        list->ResolveQueryData(frame.heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
            0, QueryCount, frame.readback.Get(), 0);
    }

    void D3D12Diagnostics::MarkSubmitted(std::uint32_t frame_slot,
        std::uint64_t fence_value) noexcept
    {
        if (!initialized_ || frame_slot >= FrameCount || fence_value == 0) return;
        TimestampFrame& frame = frames_[frame_slot];
        frame.fence_value = fence_value;
        frame.pending = true;
    }

    void D3D12Diagnostics::AppendLog(const D3D12DebugMessage& message) noexcept
    {
        if (debug_log_path_.empty()) return;
        std::error_code error;
        std::filesystem::create_directories(debug_log_path_.parent_path(), error);
        std::ofstream output(debug_log_path_, std::ios::binary | std::ios::app);
        if (!output) return;
        output << '[' << SeverityName(message.severity) << "] " << message.text;
        if (message.repeat_count > 1) output << " (x" << message.repeat_count << ')';
        output << "\r\n";
    }

    void D3D12Diagnostics::DrainInfoQueue() noexcept
    {
        if (!info_queue_) return;
        const UINT64 count = info_queue_->GetNumStoredMessagesAllowedByRetrievalFilter();
        D3D12DebugMessage aggregate{};
        bool has_aggregate = false;
        for (UINT64 index = 0; index < count; ++index)
        {
            SIZE_T bytes = 0;
            if (FAILED(info_queue_->GetMessage(index, nullptr, &bytes)) || bytes == 0) continue;
            std::vector<std::uint8_t> storage(bytes);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if (FAILED(info_queue_->GetMessage(index, message, &bytes))) continue;
            D3D12DebugMessage next{};
            next.severity = message->Severity;
            if (message->pDescription != nullptr) next.text = message->pDescription;
            if (has_aggregate && aggregate.severity == next.severity && aggregate.text == next.text)
            {
                ++aggregate.repeat_count;
                continue;
            }
            if (has_aggregate)
            {
                AppendLog(aggregate);
                pending_messages_.push_back(std::move(aggregate));
            }
            aggregate = std::move(next);
            has_aggregate = true;
        }
        if (has_aggregate)
        {
            AppendLog(aggregate);
            pending_messages_.push_back(std::move(aggregate));
        }
        info_queue_->ClearStoredMessages();
    }

    void D3D12Diagnostics::ConsumeMessages(std::vector<D3D12DebugMessage>& out)
    {
        out.insert(out.end(), std::make_move_iterator(pending_messages_.begin()),
            std::make_move_iterator(pending_messages_.end()));
        pending_messages_.clear();
    }

    bool D3D12Diagnostics::ReportLiveObjects(std::uint32_t& live_lines,
        std::uint32_t& detail_lines) noexcept
    {
        live_lines = 0;
        detail_lines = 0;
        if (!device_) return true;
        Microsoft::WRL::ComPtr<ID3D12DebugDevice> debug_device;
        if (FAILED(device_.As(&debug_device)) || !debug_device) return false;
        if (info_queue_) info_queue_->ClearStoredMessages();
        debug_device->ReportLiveDeviceObjects(
            D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL);
        if (!info_queue_) return true;
        const UINT64 count = info_queue_->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 index = 0; index < count; ++index)
        {
            SIZE_T bytes = 0;
            if (FAILED(info_queue_->GetMessage(index, nullptr, &bytes)) || bytes == 0) continue;
            std::vector<std::uint8_t> storage(bytes);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if (FAILED(info_queue_->GetMessage(index, message, &bytes)) ||
                message->pDescription == nullptr) continue;
            const std::string_view text(message->pDescription);
            if (!ContainsLiveObjectText(text)) continue;
            const bool own_interface =
                text.find("Live ID3D12Device") != std::string_view::npos ||
                text.find("Live ID3D12Debug") != std::string_view::npos ||
                text.find("Live ID3D12InfoQueue") != std::string_view::npos;
            if (!own_interface)
            {
                ++live_lines;
                if (text.find("Refcount") != std::string_view::npos ||
                    text.find("refcount") != std::string_view::npos) ++detail_lines;
            }
            D3D12DebugMessage copy{};
            copy.severity = message->Severity;
            copy.text = message->pDescription;
            AppendLog(copy);
            pending_messages_.push_back(std::move(copy));
        }
        info_queue_->ClearStoredMessages();
        std::fprintf(stderr, "D3D12_LIVE_OBJECT_LINES %u\n", live_lines);
        std::fprintf(stderr, "D3D12_LIVE_OBJECT_DETAIL_LINES %u\n", detail_lines);
        return true;
    }
}
