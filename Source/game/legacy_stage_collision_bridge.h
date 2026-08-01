#pragma once

#include "../../RePlayEngine/Scene/Services/IPhysicsQueryService.h"

class Stage;

// 【移行用】既存 Stage の衝突メッシュを、汎用の地形問い合わせとして見せる橋渡し。
//
// これは最終設計ではない。
//   現状 CharacterMotorComponent は IPhysicsQueryService しか知らず、
//   Stage 具象型にも GameRaycast にも依存していない。
//   その具体的な実装だけをこのクラスが引き受けている。
//
// 【削除条件】
//   Stage を地形 GameObject 群（Transform + MeshRenderer + MeshCollider）へ移行し、
//   Scene 上の Collider を走査する CollisionWorld を用意した時点で、
//   このクラスを削除して差し替える。
//   その際 CharacterMotorComponent は 1 行も変更しなくてよい。
//
// 責任は「Stage への問い合わせを汎用インターフェイスへ変換する」ことだけ。
// プレイヤーのことも移動のことも知らない。
class LegacyStageCollisionBridge final : public ReplayEngine::Scene::IPhysicsQueryService
{
public:
    // stage は非所有参照。実体は SceneGame が持つ。
    void Attach(const Stage* stage) noexcept { stage_ = stage; }
    void Detach() noexcept { stage_ = nullptr; }

    // Stage の衝突メッシュが有効なときだけ問い合わせを受け付ける。
    void SetActive(bool active) noexcept { active_ = active; }

    bool CollisionAvailable() const override;

    bool QueryGround(const DirectX::XMFLOAT3& origin, float radius,
        float up_offset, float down_distance, float walkable_normal_y,
        ReplayEngine::Scene::GroundHit& hit) const override;

    bool SweepSphere(const DirectX::XMFLOAT3& start, const DirectX::XMFLOAT3& end,
        float radius, float maximum_normal_y,
        ReplayEngine::Scene::SphereSweepHit& hit) const override;

private:
    const Stage* stage_ = nullptr;
    bool active_ = false;
};
