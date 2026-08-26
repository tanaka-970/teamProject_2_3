#include "RenderStats.h"

// psapi.h は windows.h の型に依存するため、順序を入れ替えない。
#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>

#pragma comment(lib, "psapi.lib")

namespace
{
    std::string EscapeCsv(const std::string& text)
    {
        if (text.find_first_of(",\"\r\n") == std::string::npos) return text;
        std::string result = "\"";
        for (const char c : text)
        {
            if (c == '\"') result += "\"\"";
            else result += c;
        }
        result += '\"';
        return result;
    }

    std::string EscapeJson(const std::string& text)
    {
        std::string result;
        result.reserve(text.size() + 8);
        for (const unsigned char c : text)
        {
            switch (c)
            {
            case '\\': result += "\\\\"; break;
            case '\"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (c < 0x20u)
                {
                    char buffer[8]{};
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    result += buffer;
                }
                else result += static_cast<char>(c);
                break;
            }
        }
        return result;
    }

    const char* PhaseName(ReplayEngine::Rendering::RenderStats::Phase phase) noexcept
    {
        using Phase = ReplayEngine::Rendering::RenderStats::Phase;
        switch (phase)
        {
        case Phase::Scene3D: return "3D+Post";
        case Phase::GameUI: return "GameUI";
        case Phase::EditorUI: return "EditorUI";
        default: return "UnknownPhase";
        }
    }
}

namespace ReplayEngine::Rendering
{
    RenderStats::ScopedCpu::ScopedCpu(const char* name) noexcept
    {
        token_ = Stats().BeginScope(name);
    }

    RenderStats::ScopedCpu::~ScopedCpu() noexcept
    {
        if (token_ != 0) Stats().EndScope(token_);
    }

    bool RenderStats::Initialize() noexcept
    {
        Release();
        initialized_ = true;
        enabled_ = true;
        paused_ = false;
        return true;
    }

    void RenderStats::Release() noexcept
    {
        active_scopes_.clear();
        current_scope_nodes_.clear();
        current_trace_events_.clear();
        current_scope_lookup_.clear();
        history_.clear();
        phase_tokens_.fill(0);
        last_state_values_.fill(nullptr);
        last_state_valid_.fill(false);
        latest_sample_ = {};
        current_cpu_ = {};
        resolved_cpu_ = {};
        resolved_gpu_ = {};
        frame_open_ = false;
        initialized_ = false;
        current_scope_depth_limit_hit_ = false;
        current_trace_event_limit_hit_ = false;
        pending_external_gpu_frames_ = 0;
        latest_external_gpu_source_frame_ = 0;
        pending_enabled_change_ = false;
        pending_paused_change_ = false;
    }

    void RenderStats::SetEnabled(bool enabled) noexcept
    {
        if (frame_open_)
        {
            pending_enabled_change_ = true;
            pending_enabled_value_ = enabled;
            return;
        }
        enabled_ = enabled;
        if (!enabled)
        {
            active_scopes_.clear();
            current_scope_nodes_.clear();
            current_scope_lookup_.clear();
            phase_tokens_.fill(0);
        }
    }

    void RenderStats::SetPaused(bool paused) noexcept
    {
        if (frame_open_)
        {
            pending_paused_change_ = true;
            pending_paused_value_ = paused;
            return;
        }
        paused_ = paused;
    }

    void RenderStats::SetOutputDirectory(std::filesystem::path directory)
    {
        if (!directory.empty()) output_directory_ = std::move(directory);
    }

    void RenderStats::BeginFrame()
    {
        if (!initialized_) Initialize();
        if (!enabled_ || paused_) return;
        ++frame_id_;
        current_cpu_ = {};
        current_scope_nodes_.clear();
        current_trace_events_.clear();
        current_scope_lookup_.clear();
        active_scopes_.clear();
        phase_tokens_.fill(0);
        current_scope_depth_limit_hit_ = false;
        current_trace_event_limit_hit_ = false;
        last_state_values_.fill(nullptr);
        last_state_valid_.fill(false);
        cpu_frame_begin_ = std::chrono::steady_clock::now();
        counting_enabled_ = true;
        frame_open_ = true;
    }

    std::uint32_t RenderStats::FindOrCreateScopeNode(const char* name)
    {
        const std::string safe_name = name != nullptr && *name != '\0' ? name : "Unnamed";
        const std::uint32_t parent_id = active_scopes_.empty()
            ? 0u : current_scope_nodes_[active_scopes_.back().node_index].id;
        const std::string parent_path = active_scopes_.empty()
            ? std::string{} : current_scope_nodes_[active_scopes_.back().node_index].path;
        const std::string path = parent_path.empty() ? safe_name : parent_path + "/" + safe_name;
        const auto found = current_scope_lookup_.find(path);
        if (found != current_scope_lookup_.end()) return found->second;
        ScopeSnapshot node{};
        node.id = static_cast<std::uint32_t>(current_scope_nodes_.size() + 1u);
        node.parent_id = parent_id;
        node.depth = static_cast<std::uint16_t>((std::min)(active_scopes_.size(), kMaxScopeDepth));
        node.name = safe_name;
        node.path = path;
        const auto budget = budgets_.find(path);
        if (budget != budgets_.end())
        {
            node.cpu_budget_ms = budget->second.cpu_ms;
            node.gpu_budget_ms = budget->second.gpu_ms;
        }
        current_scope_nodes_.push_back(std::move(node));
        const std::uint32_t index = static_cast<std::uint32_t>(current_scope_nodes_.size() - 1u);
        current_scope_lookup_.emplace(path, index);
        return index;
    }

    std::uint64_t RenderStats::BeginScope(const char* name) noexcept
    {
        if (!enabled_ || paused_ || !frame_open_) return 0;
        if (active_scopes_.size() >= kMaxScopeDepth)
        {
            current_scope_depth_limit_hit_ = true;
            last_output_status_ = "Profiler scope depth limit reached";
            return 0;
        }
        const auto now = std::chrono::steady_clock::now();
        const std::uint32_t node_index = FindOrCreateScopeNode(name);
        ScopeSnapshot& node = current_scope_nodes_[node_index];
        const double start_us = std::chrono::duration<double, std::micro>(now - cpu_frame_begin_).count();
        if (node.calls == 0 || start_us < node.cpu_start_us) node.cpu_start_us = start_us;
        ActiveScope active{};
        active.token = next_scope_token_++;
        if (active.token == 0) active.token = next_scope_token_++;
        active.node_index = node_index;
        active.cpu_begin = now;
        active.cpu_start_us = start_us;
        if (current_trace_events_.size() < kMaxTraceEventsPerFrame)
        {
            TraceEvent event{};
            event.path = node.path;
            event.depth = node.depth;
            event.cpu_start_us = start_us;
            active.trace_event_index = static_cast<std::uint32_t>(current_trace_events_.size());
            current_trace_events_.push_back(std::move(event));
        }
        else current_trace_event_limit_hit_ = true;
        active_scopes_.push_back(active);
        return active.token;
    }

    void RenderStats::EndScope(std::uint64_t token) noexcept
    {
        if (token == 0 || active_scopes_.empty()) return;
        if (active_scopes_.back().token != token)
        {
            last_output_status_ = "Profiler scope close order mismatch";
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        const ActiveScope active = active_scopes_.back();
        active_scopes_.pop_back();
        if (active.node_index >= current_scope_nodes_.size()) return;
        ScopeSnapshot& node = current_scope_nodes_[active.node_index];
        const double duration_ms = std::chrono::duration<double, std::milli>(now - active.cpu_begin).count();
        node.cpu_ms += duration_ms;
        ++node.calls;
        if (active.trace_event_index != UINT32_MAX && active.trace_event_index < current_trace_events_.size())
            current_trace_events_[active.trace_event_index].cpu_duration_us = duration_ms * 1000.0;
    }

    void RenderStats::BeginPhase(Phase phase)
    {
        const std::size_t index = static_cast<std::size_t>(phase);
        if (index >= phase_count || phase_tokens_[index] != 0) return;
        phase_tokens_[index] = BeginScope(PhaseName(phase));
    }

    void RenderStats::EndPhase(Phase phase)
    {
        const std::size_t index = static_cast<std::size_t>(phase);
        if (index >= phase_count) return;
        const std::uint64_t token = phase_tokens_[index];
        if (token == 0) return;
        EndScope(token);
        phase_tokens_[index] = 0;
        for (const ScopeSnapshot& scope : current_scope_nodes_)
        {
            if (scope.parent_id == 0 && scope.name == PhaseName(phase))
            {
                current_cpu_.phase_ms[index] = scope.cpu_ms;
                break;
            }
        }
    }

    void RenderStats::SampleMemoryCounters()
    {
        if (++frames_since_memory_sample_ < 30u) return;
        frames_since_memory_sample_ = 0;
        sampled_memory_.vram_valid = false;
        sampled_memory_.process_memory_valid = false;
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
        {
            sampled_memory_.working_set_bytes = counters.WorkingSetSize;
            sampled_memory_.process_memory_valid = true;
        }
    }

    void RenderStats::EndFrame()
    {
        if (!enabled_ || paused_ || !frame_open_) return;
        while (!active_scopes_.empty()) EndScope(active_scopes_.back().token);
        phase_tokens_.fill(0);
        const auto cpu_end = std::chrono::steady_clock::now();
        current_cpu_.frame_ms = std::chrono::duration<double, std::milli>(cpu_end - cpu_frame_begin_).count();
        resolved_cpu_ = current_cpu_;
        SampleMemoryCounters();
        FrameSample sample{};
        sample.frame_id = frame_id_;
        sample.cpu_frame_ms = current_cpu_.frame_ms;
        sample.cpu = current_cpu_;
        sample.memory = sampled_memory_;
        sample.memory.engine_texture_bytes = current_engine_memory_.engine_texture_bytes;
        sample.memory.engine_buffer_bytes = current_engine_memory_.engine_buffer_bytes;
        sample.memory.render_target_bytes = current_engine_memory_.render_target_bytes;
        sample.memory.duplicate_asset_guids = current_engine_memory_.duplicate_asset_guids;
        sample.memory.duplicate_shader_guids = current_engine_memory_.duplicate_shader_guids;
        sample.memory.resident_texture_guid_refs = current_engine_memory_.resident_texture_guid_refs;
        sample.memory.duplicate_resident_texture_guids = current_engine_memory_.duplicate_resident_texture_guids;
        sample.scene = current_scene_;
        sample.scopes = current_scope_nodes_;
        sample.scope_depth_limit_hit = current_scope_depth_limit_hit_;
        sample.trace_event_limit_hit = current_trace_event_limit_hit_;
        sample.trace_events = current_trace_events_;
        ApplyBudgets(sample);
        PushHistory(std::move(sample));
        frame_open_ = false;
        if (pending_paused_change_)
        {
            paused_ = pending_paused_value_;
            pending_paused_change_ = false;
        }
        if (pending_enabled_change_)
        {
            const bool next_enabled = pending_enabled_value_;
            pending_enabled_change_ = false;
            SetEnabled(next_enabled);
        }
    }

    void RenderStats::ApplyExternalGpuTiming(std::uint64_t source_frame_id,
        const std::vector<ExternalGpuPassTiming>& timings) noexcept
    {
        if (history_.empty() || timings.empty()) return;
        if (source_frame_id != 0 && source_frame_id == latest_external_gpu_source_frame_) return;
        latest_external_gpu_source_frame_ = source_frame_id;
        FrameSample* sample = &history_.back();
        GpuCounters gpu{};
        double gpu_cursor_us = 0.0;
        for (const auto& timing : timings)
        {
            if (!timing.valid || timing.milliseconds < 0.0) continue;
            gpu.valid = true;
            gpu.timing_valid = true;
            gpu.frame_ms += timing.milliseconds;
            const std::size_t phase_index = static_cast<std::size_t>(timing.phase);
            if (phase_index < phase_count)
            {
                gpu.phase_ms[phase_index] += timing.milliseconds;
                gpu.phase_timing_valid[phase_index] = true;
            }
            ScopeSnapshot scope{};
            scope.id = static_cast<std::uint32_t>(sample->scopes.size() + 1u);
            scope.name = timing.name;
            scope.path = std::string("GPU/") + timing.name;
            scope.gpu_ms = timing.milliseconds;
            scope.gpu_start_us = gpu_cursor_us;
            scope.calls = 1;
            scope.gpu_valid = true;
            const auto budget = budgets_.find(scope.path);
            if (budget != budgets_.end()) scope.gpu_budget_ms = budget->second.gpu_ms;
            sample->scopes.push_back(scope);
            TraceEvent event{};
            event.path = scope.path;
            event.gpu_start_us = gpu_cursor_us;
            event.gpu_duration_us = timing.milliseconds * 1000.0;
            event.gpu_valid = true;
            sample->trace_events.push_back(std::move(event));
            gpu_cursor_us += timing.milliseconds * 1000.0;
        }
        sample->gpu = gpu;
        sample->gpu_frame_ms = gpu.frame_ms;
        sample->gpu_valid = gpu.timing_valid;
        sample->gpu_disjoint = false;
        resolved_gpu_ = gpu;
        latest_sample_ = *sample;
        pending_external_gpu_frames_ = source_frame_id != 0 && source_frame_id + 1 < frame_id_
            ? static_cast<std::size_t>(frame_id_ - source_frame_id - 1) : 0;
        ApplyBudgets(*sample);
    }

    void RenderStats::PushHistory(FrameSample sample)
    {
        if (sample.frame_id >= latest_sample_.frame_id) latest_sample_ = sample;
        const auto insert_at = std::upper_bound(history_.begin(), history_.end(), sample.frame_id,
            [](std::uint64_t frame_id, const FrameSample& existing) { return frame_id < existing.frame_id; });
        history_.insert(insert_at, std::move(sample));
        while (history_.size() > history_limit_) history_.pop_front();
        MaybeAutoExport();
    }

    void RenderStats::SetBudget(const std::string& path, double cpu_ms, double gpu_ms)
    {
        if (path.empty()) return;
        budgets_[path] = { (std::max)(0.0, cpu_ms), (std::max)(0.0, gpu_ms) };
        ApplyBudgets(latest_sample_);
    }

    void RenderStats::ClearBudget(const std::string& path)
    {
        budgets_.erase(path);
        ApplyBudgets(latest_sample_);
    }

    void RenderStats::ApplyBudgets(FrameSample& sample) const
    {
        for (ScopeSnapshot& scope : sample.scopes)
        {
            const auto found = budgets_.find(scope.path);
            if (found == budgets_.end())
            {
                scope.cpu_budget_ms = 0.0;
                scope.gpu_budget_ms = 0.0;
            }
            else
            {
                scope.cpu_budget_ms = found->second.cpu_ms;
                scope.gpu_budget_ms = found->second.gpu_ms;
            }
        }
    }

    void RenderStats::TrackStateSet(StateKind kind, const void* identity, std::uint64_t count) noexcept
    {
        if (!counting_enabled_ || !enabled_ || paused_) return;
        const std::size_t index = static_cast<std::size_t>(kind);
        if (index >= state_kind_count)
        {
            CountStateSet(count);
            return;
        }
        const bool redundant = last_state_valid_[index] && last_state_values_[index] == identity;
        last_state_values_[index] = identity;
        last_state_valid_[index] = true;
        CountStateSet(kind, redundant, count);
    }

    void RenderStats::SetSceneCounters(std::uint64_t objects, std::uint64_t components,
        std::uint64_t culling_tested, std::uint64_t culling_visible,
        std::uint64_t effect_stacks) noexcept
    {
        current_scene_.object_count = objects;
        current_scene_.component_count = components;
        current_scene_.culling_tested = culling_tested;
        current_scene_.culling_visible = culling_visible;
        current_scene_.effect_stack_count = effect_stacks;
    }

    void RenderStats::SetUICounters(std::uint64_t draw_commands,
        std::uint64_t vertices, std::uint64_t texture_count, std::uint64_t mask_depth,
        std::uint64_t clipped_commands) noexcept
    {
        current_cpu_.ui_draw_commands = draw_commands;
        current_cpu_.ui_vertices = vertices;
        current_cpu_.ui_texture_count = texture_count;
        current_cpu_.ui_mask_depth = mask_depth;
        current_cpu_.ui_clipped_commands = clipped_commands;
    }

    void RenderStats::SetEngineMemoryBytes(std::uint64_t texture_bytes,
        std::uint64_t buffer_bytes, std::uint64_t render_target_bytes) noexcept
    {
        current_engine_memory_.engine_texture_bytes = texture_bytes;
        current_engine_memory_.engine_buffer_bytes = buffer_bytes;
        current_engine_memory_.render_target_bytes = render_target_bytes;
    }

    void RenderStats::SetDuplicateAssetGuids(std::uint32_t assets, std::uint32_t shaders) noexcept
    {
        current_engine_memory_.duplicate_asset_guids = assets;
        current_engine_memory_.duplicate_shader_guids = shaders;
    }

    void RenderStats::SetResidentTextureDuplicates(std::uint32_t references,
        std::uint32_t duplicate_guids) noexcept
    {
        current_engine_memory_.resident_texture_guid_refs = references;
        current_engine_memory_.duplicate_resident_texture_guids = duplicate_guids;
    }

    RenderStats::HistoryStats RenderStats::ComputeHistoryStats(std::vector<double> values)
    {
        HistoryStats result{};
        if (values.empty()) return result;
        std::sort(values.begin(), values.end());
        result.minimum = values.front();
        result.maximum = values.back();
        result.average = std::accumulate(values.begin(), values.end(), 0.0) /
            static_cast<double>(values.size());
        const std::size_t middle = values.size() / 2u;
        result.median = values.size() % 2u != 0u ? values[middle]
            : (values[middle - 1u] + values[middle]) * 0.5;
        const std::size_t p95_index = (std::min)(values.size() - 1u,
            static_cast<std::size_t>(std::ceil(values.size() * 0.95)) - 1u);
        result.p95 = values[p95_index];
        return result;
    }

    RenderStats::HistoryStats RenderStats::CpuFrameHistoryStats() const
    {
        std::vector<double> values;
        values.reserve(history_.size());
        for (const FrameSample& sample : history_) values.push_back(sample.cpu_frame_ms);
        return ComputeHistoryStats(std::move(values));
    }

    RenderStats::HistoryStats RenderStats::GpuFrameHistoryStats() const
    {
        std::vector<double> values;
        values.reserve(history_.size());
        for (const FrameSample& sample : history_)
        {
            if (sample.gpu_valid) values.push_back(sample.gpu_frame_ms);
        }
        return ComputeHistoryStats(std::move(values));
    }

    std::string RenderStats::SanitizeOutputName(std::string name)
    {
        if (name.empty())
        {
            SYSTEMTIME time{};
            GetLocalTime(&time);
            char buffer[64]{};
            std::snprintf(buffer, sizeof(buffer), "profile_%04u%02u%02u_%02u%02u%02u_%03u",
                time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
                time.wMilliseconds);
            name = buffer;
        }
        for (char& c : name)
        {
            const unsigned char value = static_cast<unsigned char>(c);
            if (!(std::isalnum(value) || c == '_' || c == '-')) c = '_';
        }
        if (name.size() > 80u) name.resize(80u);
        return name;
    }

    bool RenderStats::ExportCsvAndTrace(const std::string& base_name)
    {
        if (history_.empty())
        {
            last_output_status_ = "No profiler history to export";
            return false;
        }
        std::error_code error;
        std::filesystem::create_directories(output_directory_, error);
        if (error)
        {
            last_output_status_ = "Could not create Saved/Profile";
            return false;
        }

        const std::string safe_name = SanitizeOutputName(base_name);
        last_csv_path_ = output_directory_ / (safe_name + ".csv");
        last_trace_path_ = output_directory_ / (safe_name + ".trace.json");

        std::ofstream csv(last_csv_path_, std::ios::binary | std::ios::trunc);
        if (!csv)
        {
            last_output_status_ = "Could not open profiler CSV";
            return false;
        }
        csv << "frame_id,cpu_frame_ms,gpu_frame_ms,gpu_valid,gpu_disjoint,query_ring_busy,"
            "gpu_scope_limit,scope_depth_limit,trace_event_limit,draw_calls,triangles,vertices,instances,effect_passes,"
            "ui_draw_commands,ui_vertices,ui_texture_count,ui_mask_depth,ui_clipped_commands,"
            "rt_acquires,rt_reuses,rt_creates,rt_binds,state_sets,state_redundant,"
            "shader_sets,shader_redundant,blend_sets,blend_redundant,raster_sets,raster_redundant,"
            "depth_sets,depth_redundant,input_layout_sets,input_layout_redundant,"
            "rt_state_sets,rt_state_redundant,srv_sets,srv_redundant,"
            "ia_vertices,ia_primitives,clipper_invocations,rasterized_primitives,"
            "vs_invocations,ps_invocations,cs_invocations,working_set_bytes,"
            "vram_usage_bytes,vram_budget_bytes,engine_texture_bytes,engine_buffer_bytes,"
            "render_target_bytes,duplicate_asset_guids,duplicate_shader_guids,"
            "resident_texture_guid_refs,duplicate_resident_texture_guids,"
            "objects,components,culling_tested,culling_visible,effect_stacks,"
            "scope_path,scope_depth,scope_calls,scope_cpu_ms,scope_gpu_ms,"
            "scope_gpu_valid,cpu_budget_ms,gpu_budget_ms\n";
        csv << std::setprecision(9);

        const auto write_frame_prefix = [&csv](const FrameSample& frame)
        {
            const auto& state_sets = frame.cpu.state_sets;
            const auto& redundant = frame.cpu.redundant_state_sets;
            const auto index = [](StateKind kind) noexcept
            {
                return static_cast<std::size_t>(kind);
            };
            csv << frame.frame_id << ',' << frame.cpu_frame_ms << ','
                << frame.gpu_frame_ms << ',' << (frame.gpu_valid ? 1 : 0) << ','
                << (frame.gpu_disjoint ? 1 : 0) << ','
                << (frame.gpu_query_ring_busy ? 1 : 0) << ','
                << (frame.gpu_scope_limit_hit ? 1 : 0) << ','
                << (frame.scope_depth_limit_hit ? 1 : 0) << ','
                << (frame.trace_event_limit_hit ? 1 : 0) << ','
                << frame.cpu.draw_calls << ',' << frame.cpu.triangles << ','
                << frame.cpu.vertices << ',' << frame.cpu.instances << ','
                << frame.cpu.effect_passes << ','
                << frame.cpu.ui_draw_commands << ','
                << frame.cpu.ui_vertices << ','
                << frame.cpu.ui_texture_count << ','
                << frame.cpu.ui_mask_depth << ','
                << frame.cpu.ui_clipped_commands << ','
                << frame.cpu.render_target_acquires << ','
                << frame.cpu.render_target_reuses << ','
                << frame.cpu.render_target_creates << ','
                << frame.cpu.render_target_binds << ','
                << frame.cpu.state_set_calls << ','
                << frame.cpu.redundant_state_set_calls << ','
                << state_sets[index(StateKind::Shader)] << ','
                << redundant[index(StateKind::Shader)] << ','
                << state_sets[index(StateKind::Blend)] << ','
                << redundant[index(StateKind::Blend)] << ','
                << state_sets[index(StateKind::Rasterizer)] << ','
                << redundant[index(StateKind::Rasterizer)] << ','
                << state_sets[index(StateKind::DepthStencil)] << ','
                << redundant[index(StateKind::DepthStencil)] << ','
                << state_sets[index(StateKind::InputLayout)] << ','
                << redundant[index(StateKind::InputLayout)] << ','
                << state_sets[index(StateKind::RenderTarget)] << ','
                << redundant[index(StateKind::RenderTarget)] << ','
                << state_sets[index(StateKind::ShaderResource)] << ','
                << redundant[index(StateKind::ShaderResource)] << ','
                << frame.gpu.input_vertices << ','
                << frame.gpu.input_primitives << ','
                << frame.gpu.clipper_invocations << ','
                << frame.gpu.rasterized_primitives << ','
                << frame.gpu.vertex_shader_invocations << ','
                << frame.gpu.pixel_shader_invocations << ','
                << frame.gpu.compute_shader_invocations << ','
                << frame.memory.working_set_bytes << ','
                << frame.memory.vram_usage_bytes << ','
                << frame.memory.vram_budget_bytes << ','
                << frame.memory.engine_texture_bytes << ','
                << frame.memory.engine_buffer_bytes << ','
                << frame.memory.render_target_bytes << ','
                << frame.memory.duplicate_asset_guids << ','
                << frame.memory.duplicate_shader_guids << ','
                << frame.memory.resident_texture_guid_refs << ','
                << frame.memory.duplicate_resident_texture_guids << ','
                << frame.scene.object_count << ','
                << frame.scene.component_count << ','
                << frame.scene.culling_tested << ','
                << frame.scene.culling_visible << ','
                << frame.scene.effect_stack_count << ',';
        };

        for (const FrameSample& frame : history_)
        {
            if (frame.scopes.empty())
            {
                write_frame_prefix(frame);
                csv << ",,,,,,,\n";
                continue;
            }
            for (const ScopeSnapshot& scope : frame.scopes)
            {
                write_frame_prefix(frame);
                csv << EscapeCsv(scope.path) << ',' << scope.depth << ','
                    << scope.calls << ',' << scope.cpu_ms << ',' << scope.gpu_ms << ','
                    << (scope.gpu_valid ? 1 : 0) << ',' << scope.cpu_budget_ms << ','
                    << scope.gpu_budget_ms << '\n';
            }
        }
        csv.close();

        std::ofstream trace(last_trace_path_, std::ios::binary | std::ios::trunc);
        if (!trace)
        {
            last_output_status_ = "CSV written, but trace JSON could not be opened";
            return false;
        }
        trace << "{\"traceEvents\":[\n";
        bool first = true;
        double frame_base_us = 0.0;
        const auto emit = [&](std::ofstream& stream, bool& first_event,
            const std::string& name, const char* category, int tid,
            double timestamp_us, double duration_us)
        {
            if (!first_event) stream << ",\n";
            first_event = false;
            stream << "{\"name\":\"" << EscapeJson(name) << "\",\"cat\":\"" << category
                << "\",\"ph\":\"X\",\"pid\":1,\"tid\":" << tid << ",\"ts\":"
                << std::fixed << std::setprecision(3) << timestamp_us << ",\"dur\":"
                << duration_us << '}';
        };
        for (const FrameSample& frame : history_)
        {
            emit(trace, first, "Frame", "CPU", 1, frame_base_us, frame.cpu_frame_ms * 1000.0);
            if (frame.gpu_valid)
                emit(trace, first, "GPU Frame", "GPU", 2, frame_base_us, frame.gpu_frame_ms * 1000.0);
            if (!frame.trace_events.empty())
            {
                for (const TraceEvent& event : frame.trace_events)
                {
                    emit(trace, first, event.path, "CPU", 1,
                        frame_base_us + event.cpu_start_us, event.cpu_duration_us);
                    if (event.gpu_valid)
                    {
                        emit(trace, first, event.path, "GPU", 2,
                            frame_base_us + event.gpu_start_us,
                            event.gpu_duration_us);
                    }
                }
            }
            else
            {
                // 旧/CPU-onlyサンプル互換。Trace occurrence が無い場合だけ
                // aggregate Scope を1イベントとして出す。
                for (const ScopeSnapshot& scope : frame.scopes)
                {
                    emit(trace, first, scope.path, "CPU", 1,
                        frame_base_us + scope.cpu_start_us, scope.cpu_ms * 1000.0);
                    if (scope.gpu_valid)
                    {
                        emit(trace, first, scope.path, "GPU", 2,
                            frame_base_us + scope.gpu_start_us,
                            scope.gpu_ms * 1000.0);
                    }
                }
            }
            frame_base_us += (std::max)(1000.0, frame.cpu_frame_ms * 1000.0);
        }
        trace << "\n],\"displayTimeUnit\":\"ms\"}\n";
        trace.close();

        last_output_status_ = "Profiler CSV + trace exported";
        PruneProfileFiles();
        return true;
    }

    void RenderStats::MaybeAutoExport()
    {
        if (!output_settings_.auto_export || paused_ || !enabled_) return;
        ++frames_since_auto_export_;
        const std::uint32_t interval = (std::max)(60u,
            output_settings_.auto_export_interval_frames);
        if (frames_since_auto_export_ < interval) return;
        frames_since_auto_export_ = 0;
        ExportCsvAndTrace();
    }

    void RenderStats::PruneProfileFiles()
    {
        std::error_code error;
        if (!std::filesystem::is_directory(output_directory_, error) || error) return;

        struct Pair final
        {
            std::filesystem::path csv;
            std::filesystem::path trace;
            std::filesystem::file_time_type time{};
            std::uintmax_t bytes = 0;
        };
        std::unordered_map<std::string, Pair> pairs;
        for (std::filesystem::directory_iterator it(output_directory_, error), end;
            !error && it != end; it.increment(error))
        {
            if (!it->is_regular_file(error)) continue;
            const std::filesystem::path path = it->path();
            const std::string filename = path.filename().string();
            std::string stem;
            bool is_csv = false;
            bool is_trace = false;
            if (path.extension() == ".csv")
            {
                stem = path.stem().string();
                is_csv = true;
            }
            else if (filename.size() > 11u &&
                filename.substr(filename.size() - 11u) == ".trace.json")
            {
                stem = filename.substr(0, filename.size() - 11u);
                is_trace = true;
            }
            else continue;

            // 自動ログだけをローテーション対象にする。明示名で保存した手動ログや
            // benchmark結果を容量制限で勝手に消さない。
            if (stem.rfind("profile_", 0) != 0) continue;

            Pair& pair = pairs[stem];
            if (is_csv) pair.csv = path;
            if (is_trace) pair.trace = path;
            const auto write_time = it->last_write_time(error);
            if (!error && write_time > pair.time) pair.time = write_time;
            error.clear();
            pair.bytes += it->file_size(error);
            error.clear();
        }

        std::vector<Pair> ordered;
        ordered.reserve(pairs.size());
        std::uintmax_t total_bytes = 0;
        for (auto& entry : pairs)
        {
            total_bytes += entry.second.bytes;
            ordered.push_back(std::move(entry.second));
        }
        std::sort(ordered.begin(), ordered.end(), [](const Pair& a, const Pair& b)
        {
            return a.time > b.time;
        });

        const std::size_t keep_count = (std::max)(std::size_t{ 1 },
            static_cast<std::size_t>(output_settings_.max_generations));
        const std::uintmax_t byte_limit = static_cast<std::uintmax_t>((std::max)(1u,
            output_settings_.max_total_megabytes)) * 1024u * 1024u;

        const auto remove_pair = [&](Pair& pair)
        {
            if (!pair.csv.empty()) std::filesystem::remove(pair.csv, error);
            error.clear();
            if (!pair.trace.empty()) std::filesystem::remove(pair.trace, error);
            error.clear();
            total_bytes = total_bytes > pair.bytes ? total_bytes - pair.bytes : 0;
            pair.bytes = 0;
        };

        // 世代上限は古いものから落とす。
        for (std::size_t index = keep_count; index < ordered.size(); ++index)
            remove_pair(ordered[index]);

        // 容量上限も古いものから落とす。最新の1世代だけは必ず残す。
        // 1回の明示Exportが上限より大きかったときまで消してしまうと、
        // 「保存成功」と表示した直後にファイルが無くなるため。
        for (std::size_t index = ordered.size(); total_bytes > byte_limit && index > 1u;)
        {
            --index;
            if (ordered[index].bytes == 0) continue;
            remove_pair(ordered[index]);
        }
        if (total_bytes > byte_limit && !ordered.empty())
        {
            last_output_status_ +=
                " (latest profile alone exceeds configured disk limit)";
        }
    }
}
