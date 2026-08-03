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
        if (!device) return false;

        D3D11_QUERY_DESC desc{};
        desc.Query = D3D11_QUERY_PIPELINE_STATISTICS;
        static const char* const query_names[kQueryCount] = {
            "RenderStats.PipelineQuery[0]",
            "RenderStats.PipelineQuery[1]",
            "RenderStats.PipelineQuery[2]",
        };
        for (std::size_t index = 0; index < queries_.size(); ++index)
        {
            if (FAILED(device->CreateQuery(&desc, queries_[index].GetAddressOf())))
            {
                return false;
            }
            SetDebugName(queries_[index].Get(), query_names[index]);
        }

        initialized_ = true;
        return true;
    }

    void RenderStats::Release() noexcept
    {
        for (auto& query : queries_) query.Reset();
        query_pending_.fill(false);
        write_index_ = 0;
        initialized_ = false;
        frame_open_ = false;
        resolved_gpu_ = {};
    }

    void RenderStats::BeginFrame(ID3D11DeviceContext* context)
    {
        current_cpu_ = {};
        counting_enabled_ = true;
        if (!initialized_ || !context) return;

        // 書き込み先のクエリがまだ回収されていない場合は、そのフレームの
        // 計測を諦める(上書きすると結果が壊れるため)。
        if (query_pending_[write_index_]) return;

        context->Begin(queries_[write_index_].Get());
        frame_open_ = true;
    }

    void RenderStats::EndFrame(ID3D11DeviceContext* context)
    {
        resolved_cpu_ = current_cpu_;
        if (!initialized_ || !context) return;

        if (frame_open_)
        {
            context->End(queries_[write_index_].Get());
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
            const HRESULT result = context->GetData(queries_[index].Get(),
                &statistics, sizeof(statistics), D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (result != S_OK) continue; // S_FALSE = まだ未完了

            resolved_gpu_.input_vertices = statistics.IAVertices;
            resolved_gpu_.input_primitives = statistics.IAPrimitives;
            resolved_gpu_.vertex_shader_invocations = statistics.VSInvocations;
            resolved_gpu_.clipper_invocations = statistics.CInvocations;
            resolved_gpu_.rasterized_primitives = statistics.CPrimitives;
            resolved_gpu_.pixel_shader_invocations = statistics.PSInvocations;
            resolved_gpu_.compute_shader_invocations = statistics.CSInvocations;
            resolved_gpu_.valid = true;
            query_pending_[index] = false;
        }
    }
}
