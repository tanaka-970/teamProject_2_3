#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <DirectXMath.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ReplayEngine::Rendering::DX12
{
    enum class D3D12GpuPass : std::uint32_t
    {
        ShadowDirectional = 0,
        ShadowLocal,
        GBuffer,
        Lighting,
        Forward,
        PostProcess,
        RuntimeUI,
        UIEffect,
        UIPreview,
        ImGui,
        Present,
        ModelEffect,
        ScreenEffect,
        Count
    };

    constexpr std::size_t D3D12GpuPassCount =
        static_cast<std::size_t>(D3D12GpuPass::Count);

    const char* D3D12GpuPassName(D3D12GpuPass pass) noexcept;

    struct D3D12GpuTimingSnapshot final
    {
        std::array<double, D3D12GpuPassCount> milliseconds{};
        std::array<bool, D3D12GpuPassCount> valid{};
        std::uint64_t frame_id = 0;
    };


    struct D3D12RuntimeStats final
    {
        std::uint32_t resource_descriptor_capacity = 0;
        std::uint32_t resource_descriptor_used = 0;
        std::uint32_t resource_descriptor_peak = 0;
        float resource_descriptor_fragmentation = 0.0f;
        std::uint64_t resource_descriptor_failures = 0;
        std::uint32_t sampler_descriptor_capacity = 0;
        std::uint32_t sampler_descriptor_used = 0;
        std::uint32_t sampler_descriptor_peak = 0;
        float sampler_descriptor_fragmentation = 0.0f;
        std::uint64_t sampler_descriptor_failures = 0;
        std::uint64_t frame_upload_used = 0;
        std::uint64_t frame_upload_capacity = 0;
        std::uint64_t frame_upload_peak = 0;
        std::uint64_t upload_wait_count = 0;
        std::uint64_t upload_wait_nanoseconds = 0;
        std::uint64_t fence_wait_count = 0;
        std::uint64_t fence_wait_nanoseconds = 0;
        std::uint64_t mesh_resident = 0;
        std::uint64_t texture_resident = 0;
        std::uint64_t pso_count = 0;
        std::uint64_t pso_hits = 0;
        std::uint64_t pso_misses = 0;
        std::uint64_t dxc_compile_count = 0;
        std::uint64_t dxc_failure_count = 0;
        double dxc_total_milliseconds = 0.0;
        std::array<std::uint64_t, D3D12GpuPassCount> draw_calls{};
        std::array<std::uint64_t, D3D12GpuPassCount> instances{};
        std::array<std::uint64_t, D3D12GpuPassCount> indices{};
        std::array<std::uint64_t, D3D12GpuPassCount> barriers{};
        std::uint64_t frame_id = 0;
    };

    struct D3D12FrameDumpDraw final
    {
        D3D12GpuPass pass = D3D12GpuPass::GBuffer;
        std::string mesh_key;
        std::string material_key;
        std::uint64_t index_count = 0;
        std::uint64_t instance_count = 0;
        DirectX::XMFLOAT4 world_summary{};
    };

    struct D3D12FrameDumpBarrier final
    {
        std::string resource_name;
        std::uint32_t before = 0;
        std::uint32_t after = 0;
    };

    struct D3D12DebugMessage final
    {
        D3D12_MESSAGE_SEVERITY severity = D3D12_MESSAGE_SEVERITY_INFO;
        // Debug Layer の Message ID。除外指定と原因特定に使う。
        std::uint32_t id = 0;
        std::string text;
        std::uint32_t repeat_count = 1;
    };

    class D3D12Diagnostics final
    {
    public:
        static constexpr std::uint32_t FrameCount = 2;

        bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue) noexcept;
        void Shutdown() noexcept;
        void PrepareLiveObjectReport() noexcept;

        void SetDebugLogPath(std::filesystem::path path);
        void SetBreakOnError(bool enabled) noexcept;
        void SetStatsCsvPath(std::filesystem::path path);
        void SetFrameDumpCount(std::uint32_t count) noexcept;
        void SetRuntimeStats(const D3D12RuntimeStats& stats) noexcept;
        const D3D12RuntimeStats& RuntimeStats() const noexcept { return runtime_stats_; }
        void RecordDraw(D3D12GpuPass pass, std::uint64_t index_count,
            std::uint64_t instance_count, std::string_view mesh_key = {},
            std::string_view material_key = {},
            const DirectX::XMFLOAT4X4* world = nullptr) noexcept;
        void RecordBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
            D3D12_RESOURCE_STATES after) noexcept;
        void BeginFrame(std::uint32_t frame_slot, std::uint64_t completed_fence) noexcept;
        void BeginPass(ID3D12GraphicsCommandList* list, D3D12GpuPass pass) noexcept;
        void EndPass(ID3D12GraphicsCommandList* list, D3D12GpuPass pass) noexcept;
        void ResolveQueries(ID3D12GraphicsCommandList* list) noexcept;
        void MarkSubmitted(std::uint32_t frame_slot, std::uint64_t fence_value) noexcept;
        void DrainInfoQueue() noexcept;
        void ConsumeMessages(std::vector<D3D12DebugMessage>& out);
        void PushMessage(D3D12_MESSAGE_SEVERITY severity, std::string text) noexcept;
        const D3D12GpuTimingSnapshot& LatestTiming() const noexcept { return latest_timing_; }
        std::uint32_t PassSequence(D3D12GpuPass pass) const noexcept
        {
            const std::size_t index = static_cast<std::size_t>(pass);
            if (index >= D3D12GpuPassCount || current_slot_ >= FrameCount) return 0;
            return frames_[current_slot_].sequence[index];
        }
        bool ReportLiveObjects(std::uint32_t& live_lines,
            std::uint32_t& detail_lines) noexcept;

    private:
        struct TimestampFrame final
        {
            Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap;
            Microsoft::WRL::ComPtr<ID3D12Resource> readback;
            std::array<bool, D3D12GpuPassCount> began{};
            std::array<bool, D3D12GpuPassCount> ended{};
            // BeginEvent を積んだ Command List。別の List で EndEvent を積むと
            // Queue 単位で Begin/End の数が合わなくなるため記録して照合する。
            std::array<ID3D12GraphicsCommandList*, D3D12GpuPassCount> event_list{};
            std::array<std::uint32_t, D3D12GpuPassCount> sequence{};
            std::uint32_t next_sequence = 1;
            std::uint64_t fence_value = 0;
            std::uint64_t frame_id = 0;
            bool pending = false;
        };

        void ResolveFrame(TimestampFrame& frame, std::uint64_t completed_fence) noexcept;
        void AppendLog(const D3D12DebugMessage& message) noexcept;
        void AppendStatsCsv() noexcept;
        void WriteFrameDump() noexcept;
        static std::string ObjectName(ID3D12Object* object) noexcept;

        Microsoft::WRL::ComPtr<ID3D12Device> device_;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> info_queue_;
        std::array<TimestampFrame, FrameCount> frames_{};
        D3D12GpuTimingSnapshot latest_timing_{};
        std::vector<D3D12DebugMessage> pending_messages_;
        std::filesystem::path debug_log_path_{ L"Saved/Logs/dx12_debug_layer.log" };
        std::filesystem::path stats_csv_path_{};
        D3D12RuntimeStats runtime_stats_{};
        std::vector<D3D12FrameDumpDraw> current_draws_;
        std::vector<D3D12FrameDumpBarrier> current_barriers_;
        std::uint32_t frame_dump_remaining_ = 0;
        bool stats_csv_header_written_ = false;
        std::uint64_t timestamp_frequency_ = 0;
        std::uint64_t next_frame_id_ = 1;
        std::uint32_t current_slot_ = 0;
        D3D12GpuPass active_pass_ = D3D12GpuPass::GBuffer;
        bool pass_active_ = false;
        bool initialized_ = false;
    };
}
