#pragma once

#include <d3d12.h>
#include <wrl.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
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

    struct D3D12DebugMessage final
    {
        D3D12_MESSAGE_SEVERITY severity = D3D12_MESSAGE_SEVERITY_INFO;
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
        void BeginFrame(std::uint32_t frame_slot, std::uint64_t completed_fence) noexcept;
        void BeginPass(ID3D12GraphicsCommandList* list, D3D12GpuPass pass) noexcept;
        void EndPass(ID3D12GraphicsCommandList* list, D3D12GpuPass pass) noexcept;
        void ResolveQueries(ID3D12GraphicsCommandList* list) noexcept;
        void MarkSubmitted(std::uint32_t frame_slot, std::uint64_t fence_value) noexcept;
        void DrainInfoQueue() noexcept;
        void ConsumeMessages(std::vector<D3D12DebugMessage>& out);
        const D3D12GpuTimingSnapshot& LatestTiming() const noexcept { return latest_timing_; }
        bool ReportLiveObjects(std::uint32_t& live_lines,
            std::uint32_t& detail_lines) noexcept;

    private:
        struct TimestampFrame final
        {
            Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap;
            Microsoft::WRL::ComPtr<ID3D12Resource> readback;
            std::array<bool, D3D12GpuPassCount> began{};
            std::array<bool, D3D12GpuPassCount> ended{};
            std::uint64_t fence_value = 0;
            std::uint64_t frame_id = 0;
            bool pending = false;
        };

        void ResolveFrame(TimestampFrame& frame, std::uint64_t completed_fence) noexcept;
        void AppendLog(const D3D12DebugMessage& message) noexcept;

        Microsoft::WRL::ComPtr<ID3D12Device> device_;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> info_queue_;
        std::array<TimestampFrame, FrameCount> frames_{};
        D3D12GpuTimingSnapshot latest_timing_{};
        std::vector<D3D12DebugMessage> pending_messages_;
        std::filesystem::path debug_log_path_{ L"Saved/Logs/dx12_debug_layer.log" };
        std::uint64_t timestamp_frequency_ = 0;
        std::uint64_t next_frame_id_ = 1;
        std::uint32_t current_slot_ = 0;
        bool initialized_ = false;
    };
}
