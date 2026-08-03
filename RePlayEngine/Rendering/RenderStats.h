#pragma once

#include <d3d11.h>
#include <wrl.h>

#include <array>
#include <cstdint>

namespace ReplayEngine::Rendering
{
    // 描画統計。
    //
    // 2系統の数字を持つ:
    //   - CPU側カウンタ: 各メッシュがDrawIndexedへ投入した三角形/頂点/ドローコール数。
    //     「何を描こうとしたか」が分かるので、LODやカリングの効果が直接見える。
    //   - GPUパイプライン統計: D3D11_QUERY_PIPELINE_STATISTICS の実測値。
    //     クリッピング後に実際にラスタライズされた三角形数やピクセルシェーダー
    //     呼び出し回数が取れるため、「画面内に何ポリゴンあるか」はこちらが正確。
    //
    // クエリは結果が出るまで数フレームかかるので、リングバッファで回して
    // GetDataがブロックしないようにする(ストールするとFPSが崩れて計測にならない)。
    class RenderStats final
    {
    public:
        struct CpuCounters
        {
            std::uint64_t draw_calls = 0;
            std::uint64_t triangles = 0;
            std::uint64_t vertices = 0;
        };

        struct GpuCounters
        {
            std::uint64_t input_vertices = 0;      // IAへ入った頂点数
            std::uint64_t input_primitives = 0;    // IAへ入ったプリミティブ数
            std::uint64_t rasterized_primitives = 0; // クリップ後にラスタライズされた数
            std::uint64_t clipper_invocations = 0; // クリッパへ入った数
            std::uint64_t pixel_shader_invocations = 0;
            std::uint64_t vertex_shader_invocations = 0;
            std::uint64_t compute_shader_invocations = 0;
            bool valid = false;
        };

        bool Initialize(ID3D11Device* device);

        // フレーム先頭で呼ぶ。CPUカウンタを0に戻し、GPUクエリを開始する。
        void BeginFrame(ID3D11DeviceContext* context);
        // 描画の最後(Present直前)に呼ぶ。GPUクエリを閉じ、揃った結果を回収する。
        void EndFrame(ID3D11DeviceContext* context);

        // メッシュ側から呼ぶ。index_countは三角形リスト前提。
        void CountDrawIndexed(std::uint32_t index_count, std::uint32_t vertex_count = 0) noexcept
        {
            if (!counting_enabled_) return;
            current_cpu_.draw_calls += 1;
            current_cpu_.triangles += index_count / 3;
            current_cpu_.vertices += vertex_count;
        }

        // 影パスなど、統計に混ぜたくない描画の前後で false にする。
        void SetCountingEnabled(bool enabled) noexcept { counting_enabled_ = enabled; }
        bool CountingEnabled() const noexcept { return counting_enabled_; }

        // 直前フレームの確定値。UIから読む。
        const CpuCounters& Cpu() const noexcept { return resolved_cpu_; }
        const GpuCounters& Gpu() const noexcept { return resolved_gpu_; }
        bool Initialized() const noexcept { return initialized_; }

        // Query を明示的に手放す。
        //
        // 【なぜ必要か】
        //   Stats() は関数内 static なので、破棄されるのは main() が返ったあと。
        //   ID3D11Debug::ReportLiveDeviceObjects はそれより前に走るため、
        //   ここで解放しないと ID3D11Query が必ず Live Object として残る。
        //   Device より先に、終了処理の中から呼ぶこと。
        void Release() noexcept;

    private:
        // 3フレーム分あればGetDataが待たされない。
        static constexpr size_t kQueryCount = 3;

        std::array<Microsoft::WRL::ComPtr<ID3D11Query>, kQueryCount> queries_;
        std::array<bool, kQueryCount> query_pending_{};
        size_t write_index_ = 0;
        CpuCounters current_cpu_{};
        CpuCounters resolved_cpu_{};
        GpuCounters resolved_gpu_{};
        bool counting_enabled_ = true;
        bool initialized_ = false;
        bool frame_open_ = false;
    };

    // フレーム統計はレンダラー全体から触るためグローバルに1つ持つ。
    // メッシュクラスへ参照を配るより呼び出し側の変更が少なく済む。
    inline RenderStats& Stats()
    {
        static RenderStats stats{};
        return stats;
    }
}
