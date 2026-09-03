#include "framework.h"
#include "gltf_model.h"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <vector>

void framework::draw_render_stats_overlay()
{
#ifdef USE_IMGUI
    if (!show_render_stats) return;

    auto& stats = ReplayEngine::Rendering::Stats();
    const auto& cpu = stats.Cpu();
    const auto& gpu = stats.Gpu();
    const auto& latest = stats.LatestSample();

    const auto separated = [](std::uint64_t value)
    {
        std::string text = std::to_string(value);
        for (int position = static_cast<int>(text.size()) - 3; position > 0; position -= 3)
            text.insert(static_cast<std::size_t>(position), ",");
        return text;
    };
    const auto mib = [](std::uint64_t bytes)
    {
        return static_cast<double>(bytes) / (1024.0 * 1024.0);
    };

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    constexpr float kWindowWidth = 620.0f;
    constexpr float kWindowHeight = 720.0f;
    constexpr float kMargin = 12.0f;
    constexpr float kHierarchyWidth = 300.0f;
    const ImGuiCond placement = stats_window_placed_ ? ImGuiCond_FirstUseEver : ImGuiCond_Always;
    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x + kHierarchyWidth + kMargin,
            viewport->Pos.y + viewport->Size.y - kMargin),
        placement, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(kWindowWidth, kWindowHeight), placement);
    ImGui::SetNextWindowBgAlpha(0.92f);
    stats_window_placed_ = true;

    if (ImGui::Begin(u8"Profiler / 描画統計 (F4)", &show_render_stats,
        ImGuiWindowFlags_NoFocusOnAppearing))
    {
        bool enabled = stats.Enabled();
        if (ImGui::Checkbox(u8"計測する", &enabled)) stats.SetEnabled(enabled);
        ImGui::SameLine();
        bool paused = stats.Paused();
        if (ImGui::Checkbox(u8"Pause", &paused)) stats.SetPaused(paused);
        ImGui::SameLine();
        ImGui::TextDisabled(u8"OFF時はScope/Queryを発行しません");

        static float frame_budget_ms = 16.6f;
        ImGui::SetNextItemWidth(90.0f);
        ImGui::InputFloat(u8"Frame Budget ms", &frame_budget_ms, 0.1f, 1.0f, "%.2f");
        frame_budget_ms = (std::max)(0.0f, frame_budget_ms);

        const float frame_time = ImGui::GetIO().DeltaTime * 1000.0f;
        const bool cpu_over_budget = frame_budget_ms > 0.0f && cpu.frame_ms > frame_budget_ms;
        const bool gpu_over_budget = frame_budget_ms > 0.0f && gpu.timing_valid &&
            gpu.frame_ms > frame_budget_ms;
        ImGui::Text(u8"FPS %.1f / UI dt %.2f ms", ImGui::GetIO().Framerate, frame_time);

        // VSync 待ちを除いた実力値。BeginFrame は前フレームの GPU 完了待ちで、仕事ではない。
        double gpu_wait_ms = 0.0;
        for (const auto& scope : stats.Scopes())
        {
            if (scope.name.size() >= 10 &&
                scope.name.compare(scope.name.size() - 10, 10, "BeginFrame") == 0)
            {
                gpu_wait_ms = scope.cpu_ms;
                break;
            }
        }
        const double cpu_work_ms = (std::max)(0.0, cpu.frame_ms - gpu_wait_ms);
        const double limiting_ms = (std::max)(cpu_work_ms,
            gpu.timing_valid ? gpu.frame_ms : 0.0);
        if (limiting_ms > 0.0)
        {
            ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f),
                u8"上限なし %.1f FPS（CPU実仕事 %.2f ms / GPU待ち %.2f ms）",
                1000.0 / limiting_ms, cpu_work_ms, gpu_wait_ms);
        }
        ImGui::TextColored(cpu_over_budget ? ImVec4(1.0f, 0.35f, 0.25f, 1.0f)
            : ImVec4(0.75f, 1.0f, 0.75f, 1.0f), u8"CPU %.2f ms", cpu.frame_ms);
        if (gpu.timing_valid)
        {
            ImGui::SameLine();
            ImGui::TextColored(gpu_over_budget ? ImVec4(1.0f, 0.35f, 0.25f, 1.0f)
                : ImVec4(0.75f, 1.0f, 0.75f, 1.0f), u8"GPU %.2f ms", gpu.frame_ms);
        }
        else
        {
            ImGui::SameLine();
            ImGui::TextDisabled(u8"GPU --");
        }

        if (latest.gpu_disjoint)
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f),
                u8"GPU: disjoint frame。クロック変動のため数値を破棄しました");
        if (latest.gpu_query_ring_busy)
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f),
                u8"GPU: Query ringが未回収。CPUのみ記録しました");
        if (latest.gpu_scope_limit_hit)
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.2f, 1.0f),
                u8"GPU Scope上限 %u に到達。一部GPU Scopeは未計測です",
                stats.MaxGpuScopesPerFrame());
        if (latest.scope_depth_limit_hit)
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.2f, 1.0f),
                u8"Scope階層上限に到達。一部の深いScopeを破棄しました");
        if (latest.trace_event_limit_hit)
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.2f, 1.0f),
                u8"Trace event上限に到達。UI集計は継続、Trace時系列の一部だけ省略しました");
        const std::size_t pending_gpu_frames = stats.PendingGpuFrames();
        if (pending_gpu_frames > 0)
            ImGui::TextDisabled(u8"GPU Query pending: %llu frame(s)（CPUは待機しません）",
                static_cast<unsigned long long>(pending_gpu_frames));

        const auto cpu_history = stats.CpuFrameHistoryStats();
        const auto gpu_history = stats.GpuFrameHistoryStats();
        if (ImGui::CollapsingHeader(u8"Frame履歴", ImGuiTreeNodeFlags_DefaultOpen))
        {
            std::vector<float> cpu_values;
            std::vector<float> gpu_values;
            cpu_values.reserve(stats.History().size());
            gpu_values.reserve(stats.History().size());
            for (const auto& frame : stats.History())
            {
                cpu_values.push_back(static_cast<float>(frame.cpu_frame_ms));
                gpu_values.push_back(frame.gpu_valid
                    ? static_cast<float>(frame.gpu_frame_ms) : 0.0f);
            }
            if (!cpu_values.empty())
            {
                ImGui::PlotLines(u8"CPU ms", cpu_values.data(),
                    static_cast<int>(cpu_values.size()), 0, nullptr, 0.0f,
                    (std::max)(33.3f, frame_budget_ms * 2.0f), ImVec2(0, 72));
                ImGui::PlotLines(u8"GPU ms", gpu_values.data(),
                    static_cast<int>(gpu_values.size()), 0, nullptr, 0.0f,
                    (std::max)(33.3f, frame_budget_ms * 2.0f), ImVec2(0, 72));
            }
            ImGui::Text(u8"CPU  min %.2f / avg %.2f / median %.2f / p95 %.2f / max %.2f ms",
                cpu_history.minimum, cpu_history.average, cpu_history.median,
                cpu_history.p95, cpu_history.maximum);
            ImGui::Text(u8"GPU  min %.2f / avg %.2f / median %.2f / p95 %.2f / max %.2f ms",
                gpu_history.minimum, gpu_history.average, gpu_history.median,
                gpu_history.p95, gpu_history.maximum);
            ImGui::TextDisabled(u8"履歴はRAMリングのみ（上限 %llu frames）。ディスクへ常時追記しません",
                static_cast<unsigned long long>(stats.HistoryLimit()));
        }

        if (ImGui::CollapsingHeader(u8"Scope Tree / Hotspots", ImGuiTreeNodeFlags_DefaultOpen))
        {
            static int scope_view = 0;
            const char* const view_names[]{ u8"階層", u8"CPU重い順", u8"GPU重い順" };
            ImGui::SetNextItemWidth(150.0f);
            ImGui::Combo(u8"表示", &scope_view, view_names, 3);

            static std::string selected_path;
            std::unordered_map<std::uint32_t, const ReplayEngine::Rendering::RenderStats::ScopeSnapshot*>
                by_id;
            for (const auto& scope : stats.Scopes()) by_id[scope.id] = &scope;

            const auto draw_scope_row = [&](const auto& scope, double parent_cpu, double parent_gpu)
            {
                const double cpu_percent = parent_cpu > 0.0 ? scope.cpu_ms * 100.0 / parent_cpu : 0.0;
                const double gpu_percent = parent_gpu > 0.0 && scope.gpu_valid
                    ? scope.gpu_ms * 100.0 / parent_gpu : 0.0;
                const bool over_cpu = scope.cpu_budget_ms > 0.0 && scope.cpu_ms > scope.cpu_budget_ms;
                const bool over_gpu = scope.gpu_budget_ms > 0.0 && scope.gpu_valid &&
                    scope.gpu_ms > scope.gpu_budget_ms;
                ImGui::PushID(scope.path.c_str());
                if (over_cpu || over_gpu)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.25f, 1.0f));
                const bool selected = selected_path == scope.path;
                if (ImGui::Selectable(scope.name.c_str(), selected, 0, ImVec2(220.0f, 0.0f)))
                    selected_path = scope.path;
                if (over_cpu || over_gpu) ImGui::PopStyleColor();
                ImGui::SameLine(235.0f);
                ImGui::Text("CPU %6.2f ms %5.1f%%", scope.cpu_ms, cpu_percent);
                ImGui::SameLine(365.0f);
                if (scope.gpu_valid)
                    ImGui::Text("GPU %6.2f ms %5.1f%%", scope.gpu_ms, gpu_percent);
                else ImGui::TextDisabled("GPU --");
                ImGui::SameLine(510.0f);
                ImGui::Text("x%u", scope.calls);
                ImGui::PopID();
            };

            if (scope_view == 0)
            {
                std::unordered_map<std::uint32_t, std::vector<const ReplayEngine::Rendering::RenderStats::ScopeSnapshot*>> children;
                for (const auto& scope : stats.Scopes()) children[scope.parent_id].push_back(&scope);
                std::function<void(std::uint32_t, double, double)> draw_tree;
                draw_tree = [&](std::uint32_t parent, double parent_cpu, double parent_gpu)
                {
                    const auto found = children.find(parent);
                    if (found == children.end()) return;
                    for (const auto* scope : found->second)
                    {
                        const bool has_children = children.find(scope->id) != children.end();
                        if (has_children)
                        {
                            ImGui::PushID(scope->path.c_str());
                            const bool open = ImGui::TreeNodeEx("##scope_tree",
                                ImGuiTreeNodeFlags_DefaultOpen |
                                ImGuiTreeNodeFlags_OpenOnArrow |
                                ImGuiTreeNodeFlags_SpanAvailWidth);
                            ImGui::SameLine();
                            draw_scope_row(*scope, parent_cpu, parent_gpu);
                            if (open)
                            {
                                draw_tree(scope->id, scope->cpu_ms,
                                    scope->gpu_valid ? scope->gpu_ms : parent_gpu);
                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }
                        else
                        {
                            ImGui::Indent(18.0f);
                            draw_scope_row(*scope, parent_cpu, parent_gpu);
                            ImGui::Unindent(18.0f);
                        }
                    }
                };
                draw_tree(0u, cpu.frame_ms, gpu.timing_valid ? gpu.frame_ms : 0.0);
            }
            else
            {
                std::vector<const ReplayEngine::Rendering::RenderStats::ScopeSnapshot*> sorted;
                for (const auto& scope : stats.Scopes()) sorted.push_back(&scope);
                // scope_view は static なので捕捉できない（C3495）。直接参照する。
                std::sort(sorted.begin(), sorted.end(), [](const auto* a, const auto* b)
                {
                    return scope_view == 1 ? a->cpu_ms > b->cpu_ms : a->gpu_ms > b->gpu_ms;
                });
                for (const auto* scope : sorted)
                {
                    const auto parent = by_id.find(scope->parent_id);
                    const double parent_cpu = parent != by_id.end() ? parent->second->cpu_ms : cpu.frame_ms;
                    const double parent_gpu = parent != by_id.end() && parent->second->gpu_valid
                        ? parent->second->gpu_ms : gpu.frame_ms;
                    draw_scope_row(*scope, parent_cpu, parent_gpu);
                    ImGui::TextDisabled("  %s", scope->path.c_str());
                }
            }

            if (!selected_path.empty())
            {
                const auto found = std::find_if(stats.Scopes().begin(), stats.Scopes().end(),
                    [&](const auto& scope) { return scope.path == selected_path; });
                if (found != stats.Scopes().end())
                {
                    ImGui::Separator();
                    ImGui::Text(u8"Budget: %s", selected_path.c_str());
                    static float cpu_budget = 0.0f;
                    static float gpu_budget = 0.0f;
                    static std::string last_budget_path;
                    if (last_budget_path != selected_path)
                    {
                        cpu_budget = static_cast<float>(found->cpu_budget_ms);
                        gpu_budget = static_cast<float>(found->gpu_budget_ms);
                        last_budget_path = selected_path;
                    }
                    ImGui::SetNextItemWidth(100.0f);
                    ImGui::InputFloat("CPU ms##budget", &cpu_budget, 0.1f, 0.5f, "%.2f");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(100.0f);
                    ImGui::InputFloat("GPU ms##budget", &gpu_budget, 0.1f, 0.5f, "%.2f");
                    ImGui::SameLine();
                    if (ImGui::Button(u8"Budget適用"))
                        stats.SetBudget(selected_path, (std::max)(0.0f, cpu_budget),
                            (std::max)(0.0f, gpu_budget));
                    ReplayEngine::Editor::EditorHelp::Item("button.render_stats.budget_apply");
                    ImGui::SameLine();
                    if (ImGui::Button(u8"解除")) stats.ClearBudget(selected_path);
                    ReplayEngine::Editor::EditorHelp::Item("button.render_stats.budget_clear");
                }
            }
        }

        if (ImGui::CollapsingHeader(u8"出力 / ログ", ImGuiTreeNodeFlags_DefaultOpen))
        {
            static char output_name[64]{ "manual_profile" };
            ImGui::SetNextItemWidth(180.0f);
            ImGui::InputText(u8"名前", output_name, sizeof(output_name));
            if (ImGui::Button(u8"計測ログを保存 (CSV + Trace)"))
                stats.ExportCsvAndTrace(output_name);
            ReplayEngine::Editor::EditorHelp::Item("button.render_stats.export");
            ImGui::SameLine();
            ImGui::TextDisabled("Saved/Profile/");
            if (!stats.LastOutputStatus().empty())
                ImGui::TextWrapped("%s", stats.LastOutputStatus().c_str());
            if (!stats.LastCsvPath().empty())
                ImGui::TextDisabled("CSV: %s", stats.LastCsvPath().u8string().c_str());
            if (!stats.LastTracePath().empty())
                ImGui::TextDisabled("Trace: %s", stats.LastTracePath().u8string().c_str());

            auto& output = stats.OutputConfig();
            ImGui::Checkbox(u8"自動ログ", &output.auto_export);
            ImGui::TextDisabled(u8"常時ディスク追記はしません。RAMリングを間隔ごとにSnapshot出力します");
            int interval = static_cast<int>(output.auto_export_interval_frames);
            int generations = static_cast<int>(output.max_generations);
            int max_mib = static_cast<int>(output.max_total_megabytes);
            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::InputInt(u8"間隔(frames)", &interval))
                output.auto_export_interval_frames = static_cast<std::uint32_t>((std::max)(60, interval));
            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::InputInt(u8"最大世代", &generations))
                output.max_generations = static_cast<std::uint32_t>((std::max)(1, generations));
            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::InputInt(u8"自動ログ合計上限MiB", &max_mib))
                output.max_total_megabytes = static_cast<std::uint32_t>((std::max)(1, max_mib));
            ImGui::TextDisabled(u8"自動生成 profile_* のみローテーション。手動/Benchmarkログは削除しません");
        }

        if (ImGui::CollapsingHeader(u8"描画量 / State / Memory"))
        {
            ImGui::Text(u8"Draw %s / Tri %s / Vert %s / Instance %s",
                separated(cpu.draw_calls).c_str(), separated(cpu.triangles).c_str(),
                separated(cpu.vertices).c_str(), separated(cpu.instances).c_str());
            ImGui::Text(u8"Effect Pass %s / RT acquire %s (reuse %s / create %s)",
                separated(cpu.effect_passes).c_str(), separated(cpu.render_target_acquires).c_str(),
                separated(cpu.render_target_reuses).c_str(), separated(cpu.render_target_creates).c_str());
            ImGui::Text(u8"Runtime UI Cmd %s / Vert %s / Tex %s / Mask depth %s / Clipped %s",
                separated(cpu.ui_draw_commands).c_str(), separated(cpu.ui_vertices).c_str(),
                separated(cpu.ui_texture_count).c_str(), separated(cpu.ui_mask_depth).c_str(),
                separated(cpu.ui_clipped_commands).c_str());
            if (gpu.valid)
            {
                const double screen_pixels = static_cast<double>(SCREEN_WIDTH) *
                    static_cast<double>(SCREEN_HEIGHT);
                ImGui::Text(u8"GPU IA Vert %s / Prim %s / Raster Prim %s",
                    separated(gpu.input_vertices).c_str(), separated(gpu.input_primitives).c_str(),
                    separated(gpu.rasterized_primitives).c_str());
                ImGui::Text(u8"VS %s / PS %s / CS %s",
                    separated(gpu.vertex_shader_invocations).c_str(),
                    separated(gpu.pixel_shader_invocations).c_str(),
                    separated(gpu.compute_shader_invocations).c_str());
                ImGui::Text(u8"Overdraw %.2fx ※Postの全画面PSを含む",
                    static_cast<double>(gpu.pixel_shader_invocations) /
                    (std::max)(screen_pixels, 1.0));
            }

            static const char* const state_names[]{
                u8"Shader", u8"Blend", u8"Rasterizer", u8"Depth", u8"InputLayout", u8"RT", u8"SRV" };
            ImGui::Separator();
            ImGui::Text(u8"State Set %s / redundant %s",
                separated(cpu.state_set_calls).c_str(),
                separated(cpu.redundant_state_set_calls).c_str());
            for (std::size_t i = 0; i < ReplayEngine::Rendering::RenderStats::state_kind_count; ++i)
            {
                if (cpu.state_sets[i] == 0) continue;
                ImGui::Text("  %-12s %llu (redundant %llu)", state_names[i],
                    static_cast<unsigned long long>(cpu.state_sets[i]),
                    static_cast<unsigned long long>(cpu.redundant_state_sets[i]));
            }
            ImGui::TextDisabled(u8"Stateカウンタは計測を挿入した主要描画経路の値です");

            const auto& memory = stats.Memory();
            ImGui::Separator();
            if (memory.vram_valid)
                ImGui::Text(u8"VRAM %.1f / %.1f MiB (Usage / Budget)",
                    mib(memory.vram_usage_bytes), mib(memory.vram_budget_bytes));
            else ImGui::TextDisabled(u8"VRAM: IDXGIAdapter3で未取得");
            if (memory.process_memory_valid)
                ImGui::Text(u8"Working Set %.1f MiB", mib(memory.working_set_bytes));
            ImGui::Text(u8"Engine tracked Texture %.1f / Buffer %.1f / RT %.1f MiB",
                mib(memory.engine_texture_bytes), mib(memory.engine_buffer_bytes),
                mib(memory.render_target_bytes));
            ImGui::Text(u8"Duplicate GUID AssetDB %u / ShaderCatalog %u",
                memory.duplicate_asset_guids, memory.duplicate_shader_guids);
            ImGui::Text(u8"Resident Texture GUID refs %u / duplicate resource GUID %u",
                memory.resident_texture_guid_refs,
                memory.duplicate_resident_texture_guids);

            const auto& scene = stats.Scene();
            ImGui::Text(u8"Scene Object %s / Component %s / Culling visible %s / %s",
                separated(scene.object_count).c_str(), separated(scene.component_count).c_str(),
                separated(scene.culling_visible).c_str(), separated(scene.culling_tested).c_str());
            ImGui::Text(u8"Effect Stack %s",
                separated(scene.effect_stack_count).c_str());
        }

        if (ImGui::CollapsingHeader(u8"最適化 (カリング / LOD / プリパス)"))
        {
            auto& culling = ReplayEngine::Rendering::Culling();
            ImGui::TextUnformatted(u8"■ 視錐台カリング");
            ImGui::Checkbox(u8"  有効##culling", &culling.enabled);
            if (culling.tested > 0)
            {
                const float ratio = 100.0f * static_cast<float>(culling.culled) /
                    static_cast<float>(culling.tested);
                ImGui::Text(u8"  除外 %u / %u (%.1f%%)", culling.culled, culling.tested, ratio);
            }
            ImGui::Separator();
            ImGui::TextUnformatted(u8"■ 自動LOD (QEM簡略化)");
            ImGui::Checkbox(u8"  有効##lod", &culling.lod_enabled);
            ImGui::SliderFloat(u8"  切替の画面高さ(px)",
                &culling.lod_pixel_threshold, 40.0f, 1200.0f, "%.0f");
            const char* const forced_names[]{
                u8"自動", u8"LOD0固定", u8"LOD1固定", u8"LOD2固定", u8"LOD3固定" };
            int forced = culling.forced_lod + 1;
            if (ImGui::Combo(u8"  強制LOD", &forced, forced_names, 5)) culling.forced_lod = forced - 1;
            ImGui::Text(u8"  描画数 L0:%u L1:%u L2:%u L3:%u",
                culling.lod_draws[0], culling.lod_draws[1], culling.lod_draws[2], culling.lod_draws[3]);
            ImGui::Separator();
            // 切り替えたらプロジェクト設定へも残す。再起動で戻ると、
            // 測って決めた値を毎回入れ直すことになる。
            if (ImGui::Checkbox(u8"深度プリパス", &enable_depth_prepass))
            {
                project_settings.SetDepthPrepassEnabled(enable_depth_prepass);
                save_project_settings();
            }
            ImGui::TextDisabled(u8"  切り替えるとプロジェクト設定へ保存します");
        }

        if (stage_gltf_model && ImGui::CollapsingHeader(u8"ロード内訳"))
        {
            const auto& timings = stage_gltf_model->Timings();
            ImGui::Text(u8"合計 %.0f / parse %.0f / image %.0f / geometry %.0f / LOD %.0f ms",
                timings.total_ms, timings.parse_ms, timings.image_decode_ms,
                timings.geometry_ms, timings.lod_cache_ms);
        }
    }
    ImGui::End();
#endif
}


void framework::draw_dx12_debug_panel()
{
#ifdef USE_IMGUI
    if (!show_dx12_debug_panel || !dx12_framework_active) return;
    if (!ImGui::Begin("DX12 Debug", &show_dx12_debug_panel))
    {
        ImGui::End();
        return;
    }

    const auto& runtime = dx12_device_context.RuntimeStats();
    const auto& timing = dx12_device_context.GpuTiming();
    const auto mib = [](std::uint64_t bytes) noexcept
    {
        return static_cast<double>(bytes) / (1024.0 * 1024.0);
    };

    ImGui::Text("Frame %llu", static_cast<unsigned long long>(runtime.frame_id));
    if (ImGui::CollapsingHeader("Descriptor Heaps", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("CBV/SRV/UAV %u / %u  peak %u", runtime.resource_descriptor_used,
            runtime.resource_descriptor_capacity, runtime.resource_descriptor_peak);
        ImGui::Text("fragmentation %.2f%%  allocation failures %llu",
            runtime.resource_descriptor_fragmentation * 100.0f,
            static_cast<unsigned long long>(runtime.resource_descriptor_failures));
        ImGui::Text("Sampler %u / %u  peak %u", runtime.sampler_descriptor_used,
            runtime.sampler_descriptor_capacity, runtime.sampler_descriptor_peak);
        ImGui::Text("fragmentation %.2f%%  allocation failures %llu",
            runtime.sampler_descriptor_fragmentation * 100.0f,
            static_cast<unsigned long long>(runtime.sampler_descriptor_failures));
    }
    if (ImGui::CollapsingHeader("Upload / Fence", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Frame Upload %.2f / %.2f MiB  peak %.2f MiB",
            mib(runtime.frame_upload_used), mib(runtime.frame_upload_capacity),
            mib(runtime.frame_upload_peak));
        ImGui::Text("Upload waits %llu  %.3f ms",
            static_cast<unsigned long long>(runtime.upload_wait_count),
            static_cast<double>(runtime.upload_wait_nanoseconds) / 1000000.0);
        ImGui::Text("Fence waits %llu  %.3f ms",
            static_cast<unsigned long long>(runtime.fence_wait_count),
            static_cast<double>(runtime.fence_wait_nanoseconds) / 1000000.0);
    }
    if (ImGui::CollapsingHeader("Caches / DXC", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Mesh %llu  Texture %llu  PSO %llu",
            static_cast<unsigned long long>(runtime.mesh_resident),
            static_cast<unsigned long long>(runtime.texture_resident),
            static_cast<unsigned long long>(runtime.pso_count));
        ImGui::Text("PSO hit %llu  miss %llu",
            static_cast<unsigned long long>(runtime.pso_hits),
            static_cast<unsigned long long>(runtime.pso_misses));
        ImGui::Text("DXC compile %llu  failures %llu  total %.3f ms",
            static_cast<unsigned long long>(runtime.dxc_compile_count),
            static_cast<unsigned long long>(runtime.dxc_failure_count),
            runtime.dxc_total_milliseconds);
    }
    if (ImGui::CollapsingHeader("GPU Passes", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (std::size_t index = 0; index < ReplayEngine::Rendering::DX12::D3D12GpuPassCount; ++index)
        {
            const auto pass = static_cast<ReplayEngine::Rendering::DX12::D3D12GpuPass>(index);
            if (timing.valid[index])
            {
                ImGui::Text("%-18s %7.3f ms  draw %llu  inst %llu  index %llu  barrier %llu",
                    ReplayEngine::Rendering::DX12::D3D12GpuPassName(pass),
                    timing.milliseconds[index],
                    static_cast<unsigned long long>(runtime.draw_calls[index]),
                    static_cast<unsigned long long>(runtime.instances[index]),
                    static_cast<unsigned long long>(runtime.indices[index]),
                    static_cast<unsigned long long>(runtime.barriers[index]));
            }
            else
            {
                ImGui::TextDisabled("%-18s --  draw %llu  inst %llu  index %llu  barrier %llu",
                    ReplayEngine::Rendering::DX12::D3D12GpuPassName(pass),
                    static_cast<unsigned long long>(runtime.draw_calls[index]),
                    static_cast<unsigned long long>(runtime.instances[index]),
                    static_cast<unsigned long long>(runtime.indices[index]),
                    static_cast<unsigned long long>(runtime.barriers[index]));
            }
        }
    }
    ImGui::End();
#endif
}
