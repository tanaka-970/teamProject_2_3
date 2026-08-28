#include "D3D12Diagnostics.h"
#include "D3D12ObjectName.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string_view>
#include <chrono>
#include <iomanip>
#include <sstream>

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

        std::string JsonEscape(std::string_view text)
        {
            std::string result;
            result.reserve(text.size() + 8);
            for (char c : text)
            {
                switch (c)
                {
                case '\\': result += "\\\\"; break;
                case '"': result += "\\\""; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c; break;
                }
            }
            return result;
        }

        // PIX の Metadata 値。0=UNICODE / 1=ANSI / 2=PIX3BLOB。名前は char* なので ANSI。
        constexpr UINT kPixAnsiEventMetadata = 1u;
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
        current_draws_.clear();
        current_barriers_.clear();
        runtime_stats_ = {};
        stats_csv_header_written_ = false;
        timestamp_frequency_ = 0;
        next_frame_id_ = 1;
        current_slot_ = 0;
        initialized_ = false;
    }

    void D3D12Diagnostics::SetDebugLogPath(std::filesystem::path path)
    {
        if (!path.empty()) debug_log_path_ = std::move(path);
    }

    void D3D12Diagnostics::SetStatsCsvPath(std::filesystem::path path)
    {
        stats_csv_path_ = std::move(path);
        stats_csv_header_written_ = false;
    }

    void D3D12Diagnostics::SetFrameDumpCount(std::uint32_t count) noexcept
    {
        frame_dump_remaining_ = count;
    }

    void D3D12Diagnostics::SetRuntimeStats(const D3D12RuntimeStats& stats) noexcept
    {
        const auto draw_calls = runtime_stats_.draw_calls;
        const auto instances = runtime_stats_.instances;
        const auto indices = runtime_stats_.indices;
        const auto barriers = runtime_stats_.barriers;
        const std::uint64_t frame_id = runtime_stats_.frame_id;
        runtime_stats_ = stats;
        runtime_stats_.draw_calls = draw_calls;
        runtime_stats_.instances = instances;
        runtime_stats_.indices = indices;
        runtime_stats_.barriers = barriers;
        runtime_stats_.frame_id = frame_id;
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
        frame.event_list.fill(nullptr);
        frame.sequence.fill(0);
        frame.next_sequence = 1;
        frame.frame_id = next_frame_id_++;
        frame.fence_value = 0;
        frame.pending = false;
        current_draws_.clear();
        current_barriers_.clear();
        runtime_stats_.draw_calls.fill(0);
        runtime_stats_.instances.fill(0);
        runtime_stats_.indices.fill(0);
        runtime_stats_.barriers.fill(0);
        runtime_stats_.frame_id = frame.frame_id;
    }

    void D3D12Diagnostics::BeginPass(ID3D12GraphicsCommandList* list,
        D3D12GpuPass pass) noexcept
    {
        if (!initialized_ || list == nullptr) return;
        const std::size_t index = static_cast<std::size_t>(pass);
        if (index >= D3D12GpuPassCount) return;
        TimestampFrame& frame = frames_[current_slot_];
        if (frame.began[index]) return;
        const char* event_name = D3D12GpuPassName(pass);
        list->BeginEvent(kPixAnsiEventMetadata, event_name,
            static_cast<UINT>(std::strlen(event_name) + 1u));
        list->EndQuery(frame.heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
            static_cast<UINT>(index * 2u));
        frame.began[index] = true;
        frame.event_list[index] = list;
        frame.sequence[index] = frame.next_sequence++;
        active_pass_ = pass;
        pass_active_ = true;
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
        // BeginEvent と同じ Command List のときだけ閉じる。
        if (frame.event_list[index] == list) list->EndEvent();
        frame.event_list[index] = nullptr;
        frame.ended[index] = true;
        pass_active_ = false;
    }

    void D3D12Diagnostics::ResolveQueries(ID3D12GraphicsCommandList* list) noexcept
    {
        if (!initialized_ || list == nullptr) return;
        TimestampFrame& frame = frames_[current_slot_];
        // 実行しなかったパスの Query を Resolve すると Debug Layer が
        // Cannot Resolve query that has never been performed を出すため、
        // Begin と End が揃ったパスだけを対象にする。
        for (std::size_t index = 0; index < D3D12GpuPassCount; ++index)
        {
            if (!frame.began[index] || !frame.ended[index]) continue;
            const UINT start = static_cast<UINT>(index * 2u);
            list->ResolveQueryData(frame.heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                start, 2u, frame.readback.Get(),
                static_cast<UINT64>(start) * sizeof(std::uint64_t));
        }
    }

    void D3D12Diagnostics::MarkSubmitted(std::uint32_t frame_slot,
        std::uint64_t fence_value) noexcept
    {
        if (!initialized_ || frame_slot >= FrameCount || fence_value == 0) return;
        TimestampFrame& frame = frames_[frame_slot];
        frame.fence_value = fence_value;
        frame.pending = true;
        AppendStatsCsv();
        WriteFrameDump();
    }

    std::string D3D12Diagnostics::ObjectName(ID3D12Object* object) noexcept
    {
        if (object == nullptr) return "unnamed";
        UINT bytes = 0;
        if (FAILED(object->GetPrivateData(WKPDID_D3DDebugObjectName, &bytes, nullptr)) || bytes == 0)
            return "unnamed";
        try
        {
            std::string result(bytes, '\0');
            if (FAILED(object->GetPrivateData(WKPDID_D3DDebugObjectName, &bytes, result.data())))
                return "unnamed";
            while (!result.empty() && result.back() == '\0') result.pop_back();
            return result.empty() ? "unnamed" : result;
        }
        catch (...)
        {
            return "unnamed";
        }
    }

    void D3D12Diagnostics::RecordDraw(D3D12GpuPass pass, std::uint64_t index_count,
        std::uint64_t instance_count, std::string_view mesh_key,
        std::string_view material_key, const DirectX::XMFLOAT4X4* world) noexcept
    {
        const std::size_t index = static_cast<std::size_t>(pass);
        if (index >= D3D12GpuPassCount) return;
        ++runtime_stats_.draw_calls[index];
        runtime_stats_.instances[index] += instance_count;
        runtime_stats_.indices[index] += index_count;
        if (frame_dump_remaining_ == 0) return;
        try
        {
            D3D12FrameDumpDraw draw{};
            draw.pass = pass;
            draw.mesh_key.assign(mesh_key.data(), mesh_key.size());
            draw.material_key.assign(material_key.data(), material_key.size());
            draw.index_count = index_count;
            draw.instance_count = instance_count;
            if (world != nullptr)
                draw.world_summary = { world->_41, world->_42, world->_43, 1.0f };
            current_draws_.push_back(std::move(draw));
        }
        catch (...) {}
    }

    void D3D12Diagnostics::RecordBarrier(ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) noexcept
    {
        if (pass_active_)
        {
            const std::size_t pass = static_cast<std::size_t>(active_pass_);
            if (pass < D3D12GpuPassCount) ++runtime_stats_.barriers[pass];
        }
        if (frame_dump_remaining_ == 0) return;
        try
        {
            D3D12FrameDumpBarrier barrier{};
            barrier.resource_name = ObjectName(resource);
            barrier.before = static_cast<std::uint32_t>(before);
            barrier.after = static_cast<std::uint32_t>(after);
            current_barriers_.push_back(std::move(barrier));
        }
        catch (...) {}
    }

    void D3D12Diagnostics::AppendStatsCsv() noexcept
    {
        if (stats_csv_path_.empty()) return;
        std::error_code error;
        std::filesystem::create_directories(stats_csv_path_.parent_path(), error);
        std::ofstream output(stats_csv_path_, std::ios::binary | std::ios::app);
        if (!output) return;
        if (!stats_csv_header_written_)
        {
            output << "frame,descriptor_used,descriptor_peak,descriptor_fragmentation,descriptor_failures,"
                "sampler_used,sampler_peak,upload_used,upload_peak,mesh_resident,texture_resident,pso_count,"
                "dxc_count,dxc_failures,dxc_ms,fence_waits,fence_wait_ms";
            for (std::size_t i = 0; i < D3D12GpuPassCount; ++i)
                output << ',' << D3D12GpuPassName(static_cast<D3D12GpuPass>(i)) << "_draws";
            output << "\r\n";
            stats_csv_header_written_ = true;
        }
        output << runtime_stats_.frame_id << ',' << runtime_stats_.resource_descriptor_used << ','
            << runtime_stats_.resource_descriptor_peak << ',' << runtime_stats_.resource_descriptor_fragmentation << ','
            << runtime_stats_.resource_descriptor_failures << ',' << runtime_stats_.sampler_descriptor_used << ','
            << runtime_stats_.sampler_descriptor_peak << ',' << runtime_stats_.frame_upload_used << ','
            << runtime_stats_.frame_upload_peak << ',' << runtime_stats_.mesh_resident << ','
            << runtime_stats_.texture_resident << ',' << runtime_stats_.pso_count << ','
            << runtime_stats_.dxc_compile_count << ',' << runtime_stats_.dxc_failure_count << ','
            << runtime_stats_.dxc_total_milliseconds << ',' << runtime_stats_.fence_wait_count << ','
            << static_cast<double>(runtime_stats_.fence_wait_nanoseconds) / 1000000.0;
        for (std::uint64_t value : runtime_stats_.draw_calls) output << ',' << value;
        output << "\r\n";
    }

    void D3D12Diagnostics::WriteFrameDump() noexcept
    {
        if (frame_dump_remaining_ == 0) return;
        const std::filesystem::path path = std::filesystem::path("Saved/Logs") /
            (std::string("dx12_frame_") + std::to_string(runtime_stats_.frame_id) + ".json");
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) return;
        output << "{\r\n  \"frame\": " << runtime_stats_.frame_id << ",\r\n  \"passes\": [";
        bool first_pass = true;
        for (std::size_t i = 0; i < D3D12GpuPassCount; ++i)
        {
            if (runtime_stats_.draw_calls[i] == 0 && runtime_stats_.barriers[i] == 0) continue;
            if (!first_pass) output << ',';
            first_pass = false;
            output << "\r\n    {\"name\": \"" << D3D12GpuPassName(static_cast<D3D12GpuPass>(i))
                << "\", \"draws\": " << runtime_stats_.draw_calls[i]
                << ", \"instances\": " << runtime_stats_.instances[i]
                << ", \"indices\": " << runtime_stats_.indices[i] << '}';
        }
        output << "\r\n  ],\r\n  \"draws\": [";
        for (std::size_t i = 0; i < current_draws_.size(); ++i)
        {
            const auto& draw = current_draws_[i];
            if (i != 0) output << ',';
            output << "\r\n    {\"pass\": \"" << D3D12GpuPassName(draw.pass)
                << "\", \"mesh\": \"" << JsonEscape(draw.mesh_key)
                << "\", \"material\": \"" << JsonEscape(draw.material_key)
                << "\", \"index_count\": " << draw.index_count
                << ", \"instances\": " << draw.instance_count
                << ", \"translation\": [" << draw.world_summary.x << ',' << draw.world_summary.y << ','
                << draw.world_summary.z << "]}";
        }
        output << "\r\n  ],\r\n  \"barriers\": [";
        for (std::size_t i = 0; i < current_barriers_.size(); ++i)
        {
            const auto& barrier = current_barriers_[i];
            if (i != 0) output << ',';
            output << "\r\n    {\"resource\": \"" << JsonEscape(barrier.resource_name)
                << "\", \"before\": " << barrier.before << ", \"after\": " << barrier.after << '}';
        }
        output << "\r\n  ],\r\n  \"descriptors\": {\"used\": " << runtime_stats_.resource_descriptor_used
            << ", \"capacity\": " << runtime_stats_.resource_descriptor_capacity << "},\r\n"
            << "  \"upload\": {\"used\": " << runtime_stats_.frame_upload_used
            << ", \"capacity\": " << runtime_stats_.frame_upload_capacity << "},\r\n"
            << "  \"fence\": {\"wait_count\": " << runtime_stats_.fence_wait_count << "}\r\n}\r\n";
        --frame_dump_remaining_;
    }

    void D3D12Diagnostics::AppendLog(const D3D12DebugMessage& message) noexcept
    {
        if (debug_log_path_.empty()) return;
        std::error_code error;
        std::filesystem::create_directories(debug_log_path_.parent_path(), error);
        std::ofstream output(debug_log_path_, std::ios::binary | std::ios::app);
        if (!output) return;
        output << '[' << SeverityName(message.severity) << "][id " << message.id << "] "
            << message.text;
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
            // パスマーカーの BeginEvent/EndEvent は Debug Layer が毎回 CORRUPTION として
            // 助言を出す。ID は汎用の CORRUPTED_PARAMETER1 なので ID では切れない。
            // 本物の破損を消さないよう、この助言文だけを除外する。
            if (message->pDescription != nullptr &&
                std::strstr(message->pDescription,
                    "is a diagnostic API used by debugging tools") != nullptr)
                continue;
            D3D12DebugMessage next{};
            next.severity = message->Severity;
            next.id = static_cast<std::uint32_t>(message->ID);
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

    void D3D12Diagnostics::PushMessage(D3D12_MESSAGE_SEVERITY severity,
        std::string text) noexcept
    {
        if (text.empty()) return;
        try
        {
            D3D12DebugMessage message{};
            message.severity = severity;
            message.text = std::move(text);
            AppendLog(message);
            pending_messages_.push_back(std::move(message));
        }
        catch (...) {}
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
