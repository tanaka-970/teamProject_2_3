#pragma once

#include "../../Object/Component/Component.h"

#include <DirectXMath.h>

#include <vector>

namespace ReplayEngine::Components
{
    // 指定されたワールド座標へ移動する、ジャンル非依存の最小 Navigation Agent。
    //
    // Phase 1 では経路探索を持たず、現在位置から目的地への直線だけを経路として扱う。
    // CharacterMotor が同じ GameObject にあれば必ずその Move() を使い、重力・接地・
    // 衝突解決を二重実装しない。Motor が無い場合だけ Transform 直接移動へフォールバックする。
    //
    // MoveTo / Stop / Arrived の公開 API は Phase 2 の NavMesh 導入後も変えない。
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

    private:
        void RotateTowards(const DirectX::XMFLOAT3& direction, float delta_time);
        void RecordTrail(float delta_time);

        DirectX::XMFLOAT3 destination_{ 0.0f, 0.0f, 0.0f };
        bool has_destination_ = false;
        bool arrived_ = true;

        // Scene View の「実際に通った跡」。0.05 秒間隔 × 120 点で約 6 秒を上限にする。
        // 無制限に伸ばさないことが重要なので、固定上限をコード側で持つ。
        std::vector<DirectX::XMFLOAT3> recent_trail_;
        float trail_sample_accumulator_ = 0.0f;
    };
}
