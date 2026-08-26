#pragma once

#include "../../Object/Component/Component.h"

#include <DirectXMath.h>

#include <cstddef>
#include <vector>

namespace ReplayEngine::Components
{
    // 指定されたワールド座標へ移動する、ジャンル非依存の Navigation Agent。
    //
    // Phase 2 では既存の衝突世界をグリッドで標本化し、A* で得た通過点を追う。
    // CharacterMotor が同じ GameObject にあれば必ずその Move() を使い、重力・接地・
    // 衝突解決を二重実装しない。Motor が無い場合だけ Phase 1 と同じ Transform
    // 直接移動へフォールバックする。
    //
    // MoveTo / Stop / Arrived の公開 API は、将来 NavMesh 実装へ差し替えても変えない。
    class NavAgentComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(NavAgentComponent)

    public:
        NavAgentComponent() = default;

        void OnStart() override;
        void OnUpdate(float delta_time) override;
        void OnDisable() override;

        void MoveTo(const DirectX::XMFLOAT3& world_position) noexcept;
        void Stop() noexcept;
        bool Arrived() const noexcept { return arrived_; }

        // Editor の Debug Draw が読む実行時状態。保存対象にはしない。
        bool HasDestination() const noexcept { return has_destination_; }
        const DirectX::XMFLOAT3& Destination() const noexcept { return destination_; }
        const std::vector<DirectX::XMFLOAT3>& RecentTrail() const noexcept { return recent_trail_; }
        void BuildDebugPath(std::vector<DirectX::XMFLOAT3>& out_points) const;

        // ---- 保存される設定 -------------------------------------------------
        float move_speed = 3.0f;
        float turn_speed_degrees = 360.0f;
        float stopping_distance = 0.5f;

        // Phase 2 のグリッド経路探索。既定 1.0 なら 1 単位ごとに衝突世界を標本化する。
        float path_grid_size = 1.0f;

        // 開始地点から X/Z 各方向へ探索できる最大距離。
        // 小さすぎる値で巨大な遠回りを探し続けないための安全上限でもある。
        float path_max_range = 24.0f;

        // A* が生成してよい升目数。壊れた設定や複雑な地形でも Editor を固めないため、
        // 範囲上限とは別に個数でも打ち切る。
        int path_max_search_cells = 4096;

    private:
        void RotateTowards(const DirectX::XMFLOAT3& direction, float delta_time);
        void RecordTrail(float delta_time);

        DirectX::XMFLOAT3 destination_{ 0.0f, 0.0f, 0.0f };
        bool has_destination_ = false;
        bool arrived_ = true;

        // path_[0] は探索開始時点、path_index_ は次に追う通過点。
        // destination_ は公開 API 用の最終目的地として別に保持する。
        std::vector<DirectX::XMFLOAT3> path_;
        std::size_t path_index_ = 0;

        // Inspector で探索設定を変えた場合、同じ目的地でも次の MoveTo で再探索する。
        float planned_grid_size_ = 0.0f;
        float planned_max_range_ = 0.0f;
        int planned_max_search_cells_ = 0;

        // Scene View の「実際に通った跡」。0.05 秒間隔 × 120 点で約 6 秒を上限にする。
        // 無制限に伸ばさないことが重要なので、固定上限をコード側で持つ。
        std::vector<DirectX::XMFLOAT3> recent_trail_;
        float trail_sample_accumulator_ = 0.0f;
    };
}
