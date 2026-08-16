#include "RenderStats.h"

#include <d3d11sdklayers.h>

#include <cstring>

namespace
{
    // Debug Build でだけ D3D オブジェクトへ名前を付ける。
    // Live Object Report に名前が出ないと、どの生成箇所か追えない。
    void SetDebugName(ID3D11DeviceChild* object, const char* name)
    {
#if defined(_DEBUG) || defined(DEBUG)
        if (object == nullptr || name == nullptr) return;
        object->SetPrivateData(WKPDID_D3DDebugObjectName,
            static_cast<UINT>(std::strlen(name)), name);
#else
        (void)object;
        (void)name;
#endif
    }
}

namespace ReplayEngine::Rendering
{
    bool RenderStats::Initialize(ID3D11Device* device)
    {
        initialized_ = false;
        frame_open_ = false;
        write_index_ = 0;
        query_pending_.fill(false);
        for (auto& query : queries_) query.Reset();
        for (auto& query : disjoint_queries_) query.Reset();
        for (auto& query : timestamp_begin_queries_) query.Reset();
        for (auto& query : timestamp_end_queries_) query.Reset();
        for (auto& frame : phase_timestamp_begin_queries_)
            for (auto& query : frame) query.Reset();
        for (auto& frame : phase_timestamp_end_queries_)
            for (auto& query : frame) query.Reset();
        for (auto& recorded : phase_recorded_) recorded.fill(false);
        cpu_phase_open_.fill(false);
        gpu_phase_open_.fill(false);
        if (!device) return false;

        D3D11_QUERY_DESC pipeline_desc{};
        pipeline_desc.Query = D3D11_QUERY_PIPELINE_STATISTICS;
        D3D11_QUERY_DESC disjoint_desc{};
        disjoint_desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        D3D11_QUERY_DESC timestamp_desc{};
        timestamp_desc.Query = D3D11_QUERY_TIMESTAMP;
        static const char* const query_names[kQueryCount] = {
            "RenderStats.PipelineQuery[0]",
            "RenderStats.PipelineQuery[1]",
            "RenderStats.PipelineQuery[2]",
        };
        for (std::size_t index = 0; index < queries_.size(); ++index)
        {
            queries_[index].Reset();
            disjoint_queries_[index].Reset();
            timestamp_begin_queries_[index].Reset();
            timestamp_end_queries_[index].Reset();
            if (FAILED(device->CreateQuery(&pipeline_desc, queries_[index].GetAddressOf())) ||
                FAILED(device->CreateQuery(&disjoint_desc, disjoint_queries_[index].GetAddressOf())) ||
                FAILED(device->CreateQuery(&timestamp_desc, timestamp_begin_queries_[index].GetAddressOf())) ||
                FAILED(device->CreateQuery(&timestamp_desc, timestamp_end_queries_[index].GetAddressOf())))
            {
                Release();
                return false;
            }
            for (std::size_t phase = 0; phase < phase_count; ++phase)
            {
                phase_timestamp_begin_queries_[index][phase].Reset();
                phase_timestamp_end_queries_[index][phase].Reset();
                if (FAILED(device->CreateQuery(&timestamp_desc,
                        phase_timestamp_begin_queries_[index][phase].GetAddressOf())) ||
                    FAILED(device->CreateQuery(&timestamp_desc,
                        phase_timestamp_end_queries_[index][phase].GetAddressOf())))
                {
                    Release();
                    return false;
                }
            }
            SetDebugName(queries_[index].Get(), query_names[index]);
        }

        initialized_ = true;
        return true;
    }

    void RenderStats::Release() noexcept
    {
        for (auto& query : queries_) query.Reset();
        for (auto& query : disjoint_queries_) query.Reset();
        for (auto& query : timestamp_begin_queries_) query.Reset();
        for (auto& query : timestamp_end_queries_) query.Reset();
        for (auto& frame : phase_timestamp_begin_queries_)
            for (auto& query : frame) query.Reset();
        for (auto& frame : phase_timestamp_end_queries_)
            for (auto& query : frame) query.Reset();
        for (auto& recorded : phase_recorded_) recorded.fill(false);
        cpu_phase_open_.fill(false);
        gpu_phase_open_.fill(false);
        query_pending_.fill(false);
        write_index_ = 0;
        initialized_ = false;
        frame_open_ = false;
        resolved_gpu_ = {};
    }

    void RenderStats::BeginFrame(ID3D11DeviceContext* context)
    {
        current_cpu_ = {};
        cpu_frame_begin_ = std::chrono::steady_clock::now();
        cpu_phase_open_.fill(false);
        gpu_phase_open_.fill(false);
        counting_enabled_ = true;
        if (!initialized_ || !context) return;

        // 書き込み先のクエリがまだ回収されていない場合は、そのフレームの
        // 計測を諦める(上書きすると結果が壊れるため)。
        if (query_pending_[write_index_]) return;

        phase_recorded_[write_index_].fill(false);
        context->Begin(disjoint_queries_[write_index_].Get());
        context->End(timestamp_begin_queries_[write_index_].Get());
        context->Begin(queries_[write_index_].Get());
        frame_open_ = true;
    }

    void RenderStats::BeginPhase(Phase phase, ID3D11DeviceContext* context)
    {
        const std::size_t index = static_cast<std::size_t>(phase);
        if (index >= phase_count || cpu_phase_open_[index]) return;
        cpu_phase_begin_[index] = std::chrono::steady_clock::now();
        cpu_phase_open_[index] = true;
        if (!initialized_ || context == nullptr || !frame_open_ || gpu_phase_open_[index])
            return;
        context->End(phase_timestamp_begin_queries_[write_index_][index].Get());
        gpu_phase_open_[index] = true;
    }

    void RenderStats::EndPhase(Phase phase, ID3D11DeviceContext* context)
    {
        const std::size_t index = static_cast<std::size_t>(phase);
        if (index >= phase_count) return;
        const auto now = std::chrono::steady_clock::now();
        if (cpu_phase_open_[index])
        {
            current_cpu_.phase_ms[index] += std::chrono::duration<double, std::milli>(
                now - cpu_phase_begin_[index]).count();
            cpu_phase_open_[index] = false;
        }
        if (!initialized_ || context == nullptr || !frame_open_ || !gpu_phase_open_[index])
            return;
        context->End(phase_timestamp_end_queries_[write_index_][index].Get());
        gpu_phase_open_[index] = false;
        phase_recorded_[write_index_][index] = true;
    }

    void RenderStats::EndFrame(ID3D11DeviceContext* context)
    {
        const auto cpu_end = std::chrono::steady_clock::now();
        for (std::size_t phase = 0; phase < phase_count; ++phase)
        {
            if (cpu_phase_open_[phase])
            {
                current_cpu_.phase_ms[phase] += std::chrono::duration<double, std::milli>(
                    cpu_end - cpu_phase_begin_[phase]).count();
                cpu_phase_open_[phase] = false;
            }
        }
        current_cpu_.frame_ms = std::chrono::duration<double, std::milli>(
            cpu_end - cpu_frame_begin_).count();
        resolved_cpu_ = current_cpu_;
        if (!initialized_ || !context) return;

        if (frame_open_)
        {
            // 区間の End が例外経路で呼ばれなかった場合も Disjoint Query より先に
            // timestamp を閉じ、次フレームへ open 状態を持ち越さない。
            for (std::size_t phase = 0; phase < phase_count; ++phase)
            {
                if (!gpu_phase_open_[phase]) continue;
                context->End(phase_timestamp_end_queries_[write_index_][phase].Get());
                gpu_phase_open_[phase] = false;
                phase_recorded_[write_index_][phase] = true;
            }
            context->End(queries_[write_index_].Get());
            context->End(timestamp_end_queries_[write_index_].Get());
            context->End(disjoint_queries_[write_index_].Get());
            query_pending_[write_index_] = true;
            write_index_ = (write_index_ + 1) % kQueryCount;
            frame_open_ = false;
        }

        // 揃っているクエリだけを回収する。D3D11_ASYNC_GETDATA_DONOTFLUSH を付けて
        // GPUを待たせない(待つとフレームタイムが計測用途に使えなくなる)。
        for (size_t index = 0; index < kQueryCount; ++index)
        {
            if (!query_pending_[index]) continue;
            D3D11_QUERY_DATA_PIPELINE_STATISTICS statistics{};
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT timing{};
            UINT64 timestamp_begin = 0;
            UINT64 timestamp_end = 0;
            if (context->GetData(queries_[index].Get(), &statistics, sizeof(statistics),
                    D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
                context->GetData(disjoint_queries_[index].Get(), &timing, sizeof(timing),
                    D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
                context->GetData(timestamp_begin_queries_[index].Get(), &timestamp_begin,
                    sizeof(timestamp_begin), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
                context->GetData(timestamp_end_queries_[index].Get(), &timestamp_end,
                    sizeof(timestamp_end), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
            {
                continue;
            }

            std::array<UINT64, phase_count> phase_begin{};
            std::array<UINT64, phase_count> phase_end{};
            bool phase_queries_ready = true;
            for (std::size_t phase = 0; phase < phase_count; ++phase)
            {
                if (!phase_recorded_[index][phase]) continue;
                if (context->GetData(phase_timestamp_begin_queries_[index][phase].Get(),
                        &phase_begin[phase], sizeof(UINT64), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
                    context->GetData(phase_timestamp_end_queries_[index][phase].Get(),
                        &phase_end[phase], sizeof(UINT64), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
                {
                    phase_queries_ready = false;
                    break;
                }
            }
            if (!phase_queries_ready) continue;

            resolved_gpu_.input_vertices = statistics.IAVertices;
            resolved_gpu_.input_primitives = statistics.IAPrimitives;
            resolved_gpu_.vertex_shader_invocations = statistics.VSInvocations;
            resolved_gpu_.clipper_invocations = statistics.CInvocations;
            resolved_gpu_.rasterized_primitives = statistics.CPrimitives;
            resolved_gpu_.pixel_shader_invocations = statistics.PSInvocations;
            resolved_gpu_.compute_shader_invocations = statistics.CSInvocations;
            resolved_gpu_.timing_valid = !timing.Disjoint && timing.Frequency > 0 &&
                timestamp_end >= timestamp_begin;
            resolved_gpu_.frame_ms = resolved_gpu_.timing_valid
                ? (static_cast<double>(timestamp_end - timestamp_begin) * 1000.0 /
                    static_cast<double>(timing.Frequency))
                : 0.0;
            resolved_gpu_.phase_ms.fill(0.0);
            resolved_gpu_.phase_timing_valid.fill(false);
            if (resolved_gpu_.timing_valid)
            {
                for (std::size_t phase = 0; phase < phase_count; ++phase)
                {
                    if (!phase_recorded_[index][phase] || phase_end[phase] < phase_begin[phase])
                        continue;
                    resolved_gpu_.phase_ms[phase] =
                        static_cast<double>(phase_end[phase] - phase_begin[phase]) * 1000.0 /
                        static_cast<double>(timing.Frequency);
                    resolved_gpu_.phase_timing_valid[phase] = true;
                }
            }
            resolved_gpu_.valid = true;
            query_pending_[index] = false;
        }
    }
}
