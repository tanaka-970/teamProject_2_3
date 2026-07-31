#pragma once

#include "IPhysicsQueryService.h"
#include "../../Physics/CookedMeshCollision.h"

#include <vector>

namespace ReplayEngine::Scene
{
    class Scene;

    // Scene 上の Collider と、移行前の旧 Stage をまとめて扱う問い合わせ窓口。
    //
    // CharacterMotorComponent は IPhysicsQueryService しか見ないので、
    // 「どちらの経路から返ったか」を意識しない。統合はここだけが行う。
    //
    // 【二重衝突を防ぐ仕組み】
    //   同じ地形を新旧両方へ登録しないことで防ぐ。「片方が外れたらもう片方」
    //   という順序依存の方式は採らない（MeshCollider の隙間から旧地形が
    //   反応してしまうため）。
    //
    //   移行済みかどうかは Stage 単位のフラグ（legacy_stage_migrated_）で管理する。
    //   移行済みなら Hybrid でも Legacy へは一切問い合わせない。
    class SceneCollisionWorld final : public IPhysicsQueryService
    {
    public:
        // 問い合わせ先の構成。
        enum class BackendMode
        {
            // 旧 Stage だけ。MeshCollider があっても使わない。
            LegacyOnly = 0,

            // Scene 上の Collider を使い、未移行の旧 Stage だけ補う。
            // 既存 Scene の初期値。
            Hybrid = 1,

            // Scene 上の Collider だけ。旧 Stage へは一切問い合わせない。
            // 新規 Scene の初期値であり、移行完了後の最終形。
            SceneCollidersOnly = 2,
        };

        static const char* ToString(BackendMode mode) noexcept;

        // ---- 接続（framework が設定する）------------------------------------

        void AttachScene(Scene* scene) noexcept { scene_ = scene; }

        // 未移行の旧 Stage を補うための実装。移行完了後は nullptr にできる。
        void AttachLegacy(const IPhysicsQueryService* legacy) noexcept { legacy_ = legacy; }

        // Cook データの共有キャッシュ。実体は framework が所有する。
        void AttachCookCache(Physics::CookedMeshCollisionCache* cache) noexcept
        {
            cook_cache_ = cache;
        }

        // Asset GUID からローカル三角形を読み込む関数。
        void SetMeshLoader(Physics::CookedMeshCollisionCache::Loader loader)
        {
            loader_ = std::move(loader);
        }

        void SetBackendMode(BackendMode mode) noexcept { mode_ = mode; }
        BackendMode GetBackendMode() const noexcept { return mode_; }

        // 旧 Stage が MeshCollider GameObject へ移行済みか。
        // true の間は Hybrid でも Legacy へ問い合わせない（＝二重衝突しない）。
        void SetLegacyStageMigrated(bool migrated) noexcept { legacy_stage_migrated_ = migrated; }
        bool LegacyStageMigrated() const noexcept { return legacy_stage_migrated_; }

        // 毎フレーム 1 回。Scene 上の MeshCollider の Cook と Transform を更新する。
        // ここで無効な Collider は問い合わせ対象から外れる。
        void Refresh();

        // ---- 診断 -----------------------------------------------------------

        std::size_t ActiveColliderCount() const noexcept { return active_collider_count_; }
        bool LegacyConsulted() const noexcept { return legacy_consulted_; }

        // ---- IPhysicsQueryService -------------------------------------------

        bool CollisionAvailable() const override;

        bool QueryGround(const DirectX::XMFLOAT3& origin, float radius,
            float up_offset, float down_distance, float walkable_normal_y,
            GroundHit& hit) const override;

        bool SweepSphere(const DirectX::XMFLOAT3& start, const DirectX::XMFLOAT3& end,
            float radius, float maximum_normal_y, SphereSweepHit& hit) const override;

    private:
        // Legacy へ問い合わせてよいか。
        // Hybrid かつ「旧 Stage が未移行」のときだけ true。
        bool ShouldConsultLegacy() const noexcept;

        // Scene 上の Collider へスイープする。最も手前の Hit を返す。
        bool SweepSceneColliders(const DirectX::XMFLOAT3& start,
            const DirectX::XMFLOAT3& end, float radius,
            float minimum_normal_y, float maximum_normal_y,
            SphereSweepHit& hit) const;

        Scene* scene_ = nullptr;
        const IPhysicsQueryService* legacy_ = nullptr;
        Physics::CookedMeshCollisionCache* cook_cache_ = nullptr;
        Physics::CookedMeshCollisionCache::Loader loader_;

        BackendMode mode_ = BackendMode::Hybrid;
        bool legacy_stage_migrated_ = false;

        std::size_t active_collider_count_ = 0;
        mutable bool legacy_consulted_ = false;

        // クエリごとのヒープ確保を避けるための作業領域。
        // 参考プロジェクトの ScratchIndices と同じ考え方。
        mutable std::vector<std::uint32_t> scratch_indices_;
        mutable std::vector<Physics::Triangle> scratch_triangles_;
    };
}
