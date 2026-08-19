#pragma once

#include <d3d11.h>
// ID3DUserDefinedAnnotation（PIX / RenderDoc 連携のマーカー）は d3d11_1.h の定義。
#include <d3d11_1.h>
#include <dxgi1_4.h>
#include <wrl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace ReplayEngine::Rendering
{
    class RenderStats final
    {
    public:
        enum class Phase : std::size_t
        {
            Scene3D = 0,
            GameUI,
            EditorUI,
            Count
        };
        static constexpr std::size_t phase_count =
            static_cast<std::size_t>(Phase::Count);

        enum class StateKind : std::size_t
        {
            Shader = 0,
            Blend,
            Rasterizer,
            DepthStencil,
            InputLayout,
            RenderTarget,
            ShaderResource,
            Count
        };
        static constexpr std::size_t state_kind_count =
            static_cast<std::size_t>(StateKind::Count);

        struct CpuCounters
        {
            std::uint64_t draw_calls = 0;
            std::uint64_t triangles = 0;
            std::uint64_t vertices = 0;
            std::uint64_t instances = 0;
            std::uint64_t effect_passes = 0;
            std::uint64_t render_target_acquires = 0;
            std::uint64_t render_target_reuses = 0;
            std::uint64_t render_target_creates = 0;
            std::uint64_t render_target_binds = 0;
            std::uint64_t state_set_calls = 0;
            std::uint64_t redundant_state_set_calls = 0;
            std::array<std::uint64_t, state_kind_count> state_sets{};
            std::array<std::uint64_t, state_kind_count> redundant_state_sets{};
            double frame_ms = 0.0;
            std::array<double, phase_count> phase_ms{};
        };

        struct GpuCounters
        {
            std::uint64_t input_vertices = 0;
            std::uint64_t input_primitives = 0;
            std::uint64_t rasterized_primitives = 0;
            std::uint64_t clipper_invocations = 0;
            std::uint64_t pixel_shader_invocations = 0;
            std::uint64_t vertex_shader_invocations = 0;
            std::uint64_t compute_shader_invocations = 0;
            double frame_ms = 0.0;
            std::array<double, phase_count> phase_ms{};
            std::array<bool, phase_count> phase_timing_valid{};
            bool timing_valid = false;
            bool valid = false;
            bool disjoint = false;
        };

        struct ScopeSnapshot
        {
            std::uint32_t id = 0;
            std::uint32_t parent_id = 0;
            std::uint16_t depth = 0;
            std::string name;
            std::string path;
            double cpu_ms = 0.0;
            double gpu_ms = 0.0;
            double cpu_start_us = 0.0;
            double gpu_start_us = 0.0;
            std::uint32_t calls = 0;
            bool gpu_valid = false;
            double cpu_budget_ms = 0.0;
            double gpu_budget_ms = 0.0;
        };

        struct TraceEvent
        {
            std::string path;
            std::uint16_t depth = 0;
            double cpu_start_us = 0.0;
            double cpu_duration_us = 0.0;
            double gpu_start_us = 0.0;
            double gpu_duration_us = 0.0;
            bool gpu_valid = false;
        };

        struct MemoryCounters
        {
            std::uint64_t vram_usage_bytes = 0;
            std::uint64_t vram_budget_bytes = 0;
            std::uint64_t working_set_bytes = 0;
            std::uint64_t engine_texture_bytes = 0;
            std::uint64_t engine_buffer_bytes = 0;
            std::uint64_t render_target_bytes = 0;
            std::uint32_t duplicate_asset_guids = 0;
            std::uint32_t duplicate_shader_guids = 0;
            std::uint32_t resident_texture_guid_refs = 0;
            std::uint32_t duplicate_resident_texture_guids = 0;
            bool vram_valid = false;
            bool process_memory_valid = false;
        };

        struct SceneCounters
        {
            std::uint64_t object_count = 0;
            std::uint64_t component_count = 0;
            std::uint64_t culling_tested = 0;
            std::uint64_t culling_visible = 0;
            std::uint64_t effect_stack_count = 0;
        };

        struct FrameSample
        {
            std::uint64_t frame_id = 0;
            double cpu_frame_ms = 0.0;
            double gpu_frame_ms = 0.0;
            bool gpu_valid = false;
            bool gpu_disjoint = false;
            bool gpu_query_ring_busy = false;
            bool gpu_scope_limit_hit = false;
            bool scope_depth_limit_hit = false;
            bool trace_event_limit_hit = false;
            CpuCounters cpu{};
            GpuCounters gpu{};
            MemoryCounters memory{};
            SceneCounters scene{};
            std::vector<ScopeSnapshot> scopes;
            std::vector<TraceEvent> trace_events;
        };

        struct HistoryStats
        {
            double minimum = 0.0;
            double maximum = 0.0;
            double average = 0.0;
            double median = 0.0;
            double p95 = 0.0;
        };

        struct OutputSettings
        {
            bool auto_export = false;
            std::uint32_t auto_export_interval_frames = 1800;
            std::uint32_t max_generations = 12;
            std::uint32_t max_total_megabytes = 128;
        };

        class ScopedCpu final
        {
        public:
            ScopedCpu(const char* name) noexcept;
            ~ScopedCpu() noexcept;
            ScopedCpu(const ScopedCpu&) = delete;
            ScopedCpu& operator=(const ScopedCpu&) = delete;
        private:
            std::uint64_t token_ = 0;
        };

        class ScopedGpu final
        {
        public:
            ScopedGpu(ID3D11DeviceContext* context, const char* name) noexcept;
            ~ScopedGpu() noexcept;
            ScopedGpu(const ScopedGpu&) = delete;
            ScopedGpu& operator=(const ScopedGpu&) = delete;
        private:
            ID3D11DeviceContext* context_ = nullptr;
            std::uint64_t token_ = 0;
        };

        bool Initialize(ID3D11Device* device);
        void Release() noexcept;

        void BeginFrame(ID3D11DeviceContext* context);
        void EndFrame(ID3D11DeviceContext* context);
        void BeginPhase(Phase phase, ID3D11DeviceContext* context);
        void EndPhase(Phase phase, ID3D11DeviceContext* context);

        std::uint64_t BeginScope(const char* name, ID3D11DeviceContext* context,
            bool gpu_timing) noexcept;
        void EndScope(std::uint64_t token, ID3D11DeviceContext* context) noexcept;

        void SetEnabled(bool enabled) noexcept;
        bool Enabled() const noexcept { return enabled_; }
        void SetPaused(bool paused) noexcept;
        bool Paused() const noexcept { return paused_; }

        void SetOutputDirectory(std::filesystem::path directory);
        const std::filesystem::path& OutputDirectory() const noexcept { return output_directory_; }
        OutputSettings& OutputConfig() noexcept { return output_settings_; }
        const OutputSettings& OutputConfig() const noexcept { return output_settings_; }
        bool ExportCsvAndTrace(const std::string& base_name = {});
        const std::string& LastOutputStatus() const noexcept { return last_output_status_; }
        const std::filesystem::path& LastCsvPath() const noexcept { return last_csv_path_; }
        const std::filesystem::path& LastTracePath() const noexcept { return last_trace_path_; }

        void SetBudget(const std::string& path, double cpu_ms, double gpu_ms);
        void ClearBudget(const std::string& path);

        const std::deque<FrameSample>& History() const noexcept { return history_; }
        void SetHistoryLimit(std::size_t frames) noexcept
        {
            history_limit_ = (std::max)(std::size_t{ 1 }, frames);
            while (history_.size() > history_limit_) history_.pop_front();
        }
        std::size_t HistoryLimit() const noexcept { return history_limit_; }
        const std::vector<ScopeSnapshot>& Scopes() const noexcept { return latest_sample_.scopes; }
        const FrameSample& LatestSample() const noexcept { return latest_sample_; }
        std::size_t PendingGpuFrames() const noexcept
        {
            std::size_t count = 0;
            for (const auto& slot : query_slots_)
                if (slot.pending) ++count;
            return count;
        }
        HistoryStats CpuFrameHistoryStats() const;
        HistoryStats GpuFrameHistoryStats() const;

        void SetSceneCounters(std::uint64_t objects, std::uint64_t components,
            std::uint64_t culling_tested, std::uint64_t culling_visible,
            std::uint64_t effect_stacks = 0) noexcept;
        void SetEngineMemoryBytes(std::uint64_t texture_bytes,
            std::uint64_t buffer_bytes, std::uint64_t render_target_bytes) noexcept;
        void SetDuplicateAssetGuids(std::uint32_t assets, std::uint32_t shaders) noexcept;
        void SetResidentTextureDuplicates(std::uint32_t references,
            std::uint32_t duplicate_guids) noexcept;

        void CountDrawIndexed(std::uint32_t index_count, std::uint32_t vertex_count = 0,
            std::uint32_t instance_count = 1) noexcept
        {
            if (!counting_enabled_ || !enabled_ || paused_) return;
            current_cpu_.draw_calls += 1;
            current_cpu_.triangles += (index_count / 3) * instance_count;
            current_cpu_.vertices += static_cast<std::uint64_t>(vertex_count) * instance_count;
            current_cpu_.instances += instance_count;
        }

        void CountDraw(std::uint32_t vertex_count, std::uint32_t instance_count = 1) noexcept
        {
            if (!counting_enabled_ || !enabled_ || paused_) return;
            current_cpu_.draw_calls += 1;
            current_cpu_.vertices += static_cast<std::uint64_t>(vertex_count) * instance_count;
            current_cpu_.instances += instance_count;
        }

        void CountEffectPass() noexcept
        {
            if (counting_enabled_ && enabled_ && !paused_) ++current_cpu_.effect_passes;
        }
        void CountRenderTargetAcquire(bool reused) noexcept
        {
            if (!counting_enabled_ || !enabled_ || paused_) return;
            ++current_cpu_.render_target_acquires;
            if (reused) ++current_cpu_.render_target_reuses;
            else ++current_cpu_.render_target_creates;
        }
        void CountRenderTargetBind(std::uint64_t count = 1) noexcept
        {
            if (counting_enabled_ && enabled_ && !paused_) current_cpu_.render_target_binds += count;
        }
        void CountStateSet(std::uint64_t count = 1) noexcept
        {
            if (counting_enabled_ && enabled_ && !paused_) current_cpu_.state_set_calls += count;
        }
        void TrackStateSet(StateKind kind, const void* identity, std::uint64_t count = 1) noexcept;
        void CountStateSet(StateKind kind, bool redundant, std::uint64_t count = 1) noexcept
        {
            if (!counting_enabled_ || !enabled_ || paused_) return;
            current_cpu_.state_set_calls += count;
            const std::size_t index = static_cast<std::size_t>(kind);
            if (index < state_kind_count) current_cpu_.state_sets[index] += count;
            if (redundant)
            {
                current_cpu_.redundant_state_set_calls += count;
                if (index < state_kind_count) current_cpu_.redundant_state_sets[index] += count;
            }
        }

        void SetCountingEnabled(bool enabled) noexcept { counting_enabled_ = enabled; }
        bool CountingEnabled() const noexcept { return counting_enabled_; }

        const CpuCounters& Cpu() const noexcept { return resolved_cpu_; }
        const GpuCounters& Gpu() const noexcept { return resolved_gpu_; }
        const MemoryCounters& Memory() const noexcept { return latest_sample_.memory; }
        const SceneCounters& Scene() const noexcept { return latest_sample_.scene; }
        bool Initialized() const noexcept { return initialized_; }
        bool GpuScopeLimitHit() const noexcept { return latest_sample_.gpu_scope_limit_hit; }
        bool QueryRingBusy() const noexcept { return latest_sample_.gpu_query_ring_busy; }
        std::uint32_t MaxGpuScopesPerFrame() const noexcept { return kMaxGpuScopesPerFrame; }

    private:
        static constexpr std::size_t kQueryCount = 5;
        static constexpr std::uint32_t kMaxGpuScopesPerFrame = 96;
        static constexpr std::size_t kHistoryFrames = 300;
        static constexpr std::size_t kMaxScopeDepth = 32;
        static constexpr std::size_t kMaxTraceEventsPerFrame = 2048;

        struct Budget final
        {
            double cpu_ms = 0.0;
            double gpu_ms = 0.0;
            double cpu_start_us = 0.0;
            double gpu_start_us = 0.0;
        };

        struct ActiveScope final
        {
            std::uint64_t token = 0;
            std::uint32_t node_index = 0;
            std::uint32_t gpu_query_index = UINT32_MAX;
            std::uint32_t trace_event_index = UINT32_MAX;
            std::chrono::steady_clock::time_point cpu_begin{};
            double cpu_start_us = 0.0;
            bool annotation_open = false;
        };

        struct GpuScopeMeta final
        {
            std::uint32_t node_index = 0;
            std::uint32_t trace_event_index = UINT32_MAX;
            double cpu_start_us = 0.0;
            double cpu_duration_us = 0.0;
        };

        struct GpuScopeQueries final
        {
            Microsoft::WRL::ComPtr<ID3D11Query> begin;
            Microsoft::WRL::ComPtr<ID3D11Query> end;
        };

        struct QuerySlot final
        {
            Microsoft::WRL::ComPtr<ID3D11Query> pipeline;
            Microsoft::WRL::ComPtr<ID3D11Query> disjoint;
            Microsoft::WRL::ComPtr<ID3D11Query> frame_begin;
            Microsoft::WRL::ComPtr<ID3D11Query> frame_end;
            std::array<GpuScopeQueries, kMaxGpuScopesPerFrame> scopes;
            std::array<GpuScopeMeta, kMaxGpuScopesPerFrame> scope_meta{};
            std::uint32_t scope_count = 0;
            bool pending = false;
            FrameSample sample;
        };

        std::uint32_t FindOrCreateScopeNode(const char* name);
        void ResolveAvailableQueries(ID3D11DeviceContext* context);
        void PushHistory(FrameSample sample);
        void SampleMemoryCounters();
        void ApplyBudgets(FrameSample& sample) const;
        void PruneProfileFiles();
        void MaybeAutoExport();
        static std::string SanitizeOutputName(std::string name);
        static HistoryStats ComputeHistoryStats(std::vector<double> values);

        std::array<QuerySlot, kQueryCount> query_slots_{};
        std::array<std::uint64_t, phase_count> phase_tokens_{};
        std::array<const void*, state_kind_count> last_state_values_{};
        std::array<bool, state_kind_count> last_state_valid_{};
        std::vector<ActiveScope> active_scopes_;
        std::vector<ScopeSnapshot> current_scope_nodes_;
        std::vector<TraceEvent> current_trace_events_;
        std::unordered_map<std::string, std::uint32_t> current_scope_lookup_;
        std::unordered_map<std::string, Budget> budgets_;
        std::deque<FrameSample> history_;
        std::size_t history_limit_ = kHistoryFrames;
        FrameSample latest_sample_{};
        CpuCounters current_cpu_{};
        CpuCounters resolved_cpu_{};
        GpuCounters resolved_gpu_{};
        MemoryCounters sampled_memory_{};
        SceneCounters current_scene_{};
        MemoryCounters current_engine_memory_{};
        std::filesystem::path output_directory_{ "Saved/Profile" };
        OutputSettings output_settings_{};
        std::filesystem::path last_csv_path_;
        std::filesystem::path last_trace_path_;
        std::string last_output_status_;
        Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> annotation_;
        Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3_;
        std::chrono::steady_clock::time_point cpu_frame_begin_{};
        std::uint64_t next_scope_token_ = 1;
        std::uint64_t frame_id_ = 0;
        std::size_t write_index_ = 0;
        std::uint32_t frames_since_memory_sample_ = 0;
        std::uint32_t frames_since_auto_export_ = 0;
        bool counting_enabled_ = true;
        bool enabled_ = true;
        bool paused_ = false;
        bool initialized_ = false;
        bool frame_open_ = false;
        bool gpu_frame_recording_ = false;
        bool current_gpu_scope_limit_hit_ = false;
        bool current_query_ring_busy_ = false;
        bool current_scope_depth_limit_hit_ = false;
        bool current_trace_event_limit_hit_ = false;
        bool pending_enabled_change_ = false;
        bool pending_enabled_value_ = true;
        bool pending_paused_change_ = false;
        bool pending_paused_value_ = false;
    };

    inline RenderStats& Stats()
    {
        static RenderStats stats{};
        return stats;
    }
}

#define REPLAY_PROFILE_CONCAT_INNER(a, b) a##b
#define REPLAY_PROFILE_CONCAT(a, b) REPLAY_PROFILE_CONCAT_INNER(a, b)
#define REPLAY_PROFILE_SCOPE(name) \
    ::ReplayEngine::Rendering::RenderStats::ScopedCpu \
    REPLAY_PROFILE_CONCAT(replay_profile_cpu_scope_, __LINE__)(name)
#define REPLAY_PROFILE_GPU_SCOPE(context, name) \
    ::ReplayEngine::Rendering::RenderStats::ScopedGpu \
    REPLAY_PROFILE_CONCAT(replay_profile_gpu_scope_, __LINE__)(context, name)
