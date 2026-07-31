#pragma once

#include "IPhysicsQueryService.h"
#include "LegacyStageMigrationState.h"
#include "../../Components/Physics/ColliderComponent.h"
#include "../../Physics/CookedMeshCollision.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ReplayEngine::Scene
{
    class Scene;

    // Scene 上の Collider と、移行前の旧 Stage をまとめて扱う問い合わせ窓口。
    //
    // CharacterMotorComponent は IPhysicsQueryService しか見ないので、
    // 「どちらの経路から返ったか」を意識しない。統合はここだけが行う。
    //
    // ---------------------------------------------------------------------
    // 【登録表の持ち方】
    //   毎フレーム Scene 全体を走査して Collider を探す方式は採らない。
    //   Scene の構成世代（StructureGeneration）が変わったフレームだけ
    //   走査して登録表を突き合わせ、それ以外のフレームは
    //   登録済みの Collider しか触らない。
    //
    //   登録表が持つのは ObjectID / ColliderID / 形状 / Layer / Mask /
    //   Trigger / ワールド AABB だけ。Component の生ポインタは一切持たない。
    //   実体が必要になったときだけ Scene から引き直す。
    //   これにより「削除済み Component を指したまま問い合わせる」ことが
    //   構造的に起こらない。
    //
    //   Scene を切り替えたとき（Play 開始・終了・Scene 読み込み）は
    //   AttachScene が登録表と接触ペアを丸ごと捨てる。
    //   古い Scene の ObjectID / ColliderID は 1 件も残らない。
    //
    // ---------------------------------------------------------------------
    // 【二重衝突を防ぐ仕組み】
    //   同じ地形を新旧両方へ登録しないことで防ぐ。「片方が外れたらもう片方」
    //   という順序依存の方式は採らない（MeshCollider の隙間から旧地形が
    //   反応してしまうため）。
    //
    //   移行済みかどうかは「移行元 ID の集合」で管理する。単一 bool ではない。
    //   一部の配置物だけを移行しても、未移行のぶんだけが Legacy へ回る。
    class SceneCollisionWorld final : public IPhysicsQueryService
    {
    public:
        // 問い合わせ先の構成。Scene 単位の設定として保存される。
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
        static BackendMode BackendModeFromInt(int value) noexcept;

        using RevisionProvider = std::function<std::string(const std::string& asset_guid)>;

        // ---- 接続（framework が設定する）------------------------------------

        // Scene を差し替える。登録表と接触ペアは必ず捨てられる。
        // Play 開始・終了・Scene 読み込みのたびに呼ぶこと。
        void AttachScene(Scene* scene);

        // Scene から切り離す。
        void DetachScene() { AttachScene(nullptr); }

        const Scene* AttachedScene() const noexcept { return scene_; }

        // 未移行の旧 Stage を補うための実装。移行完了後は nullptr にできる。
        //   legacy_source … 移行済み集合と突き合わせるための移行元 ID。
        void AttachLegacy(const IPhysicsQueryService* legacy,
            LegacyStageMigrationState::SourceID legacy_source =
                LegacyStageMigrationState::stage_source_id) noexcept
        {
            legacy_ = legacy;
            legacy_source_ = legacy_source;
        }

        void DetachLegacy() noexcept { legacy_ = nullptr; }

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

        // Asset の中身が変わったかを見分けるための文字列を返す関数。
        // 更新時刻 + サイズや content hash など、実体が変われば必ず変わるもの。
        // 未設定なら空文字が使われ、再インポート検出は行われない。
        void SetRevisionProvider(RevisionProvider provider)
        {
            revision_provider_ = std::move(provider);
        }

        void SetBackendMode(BackendMode mode) noexcept { mode_ = mode; }
        BackendMode GetBackendMode() const noexcept { return mode_; }

        // 旧 Stage の移行状態。framework が Scene の状態と同期させる。
        LegacyStageMigrationState& LegacyMigration() noexcept { return legacy_migration_; }
        const LegacyStageMigrationState& LegacyMigration() const noexcept
        {
            return legacy_migration_;
        }

        // ---- 毎フレームの更新 ------------------------------------------------

        // 登録表を最新にし、Cook と Transform を必要なぶんだけ更新する。
        // Scene の構成が変わっていなければ全走査は行わない。
        void Refresh();

        // Trigger の接触を判定し、Enter / Stay / Exit を配送する。
        // FixedUpdate の直後、位置が確定してから呼ぶこと。
        void DispatchTriggerEvents();

        // ---- 診断 -----------------------------------------------------------

        std::size_t RegisteredColliderCount() const noexcept { return entries_.size(); }
        std::size_t ActiveColliderCount() const noexcept { return active_collider_count_; }
        std::size_t BlockingColliderCount() const noexcept { return blocking_collider_count_; }
        std::size_t TriggerColliderCount() const noexcept { return trigger_collider_count_; }
        std::size_t MeshColliderCount() const noexcept { return mesh_collider_count_; }
        std::size_t ActiveTriggerPairCount() const noexcept { return pairs_.size(); }
        std::size_t RescanCount() const noexcept { return rescan_count_; }
        bool LegacyConsulted() const noexcept { return legacy_consulted_; }
        bool LegacyAvailable() const noexcept { return legacy_ != nullptr; }
        bool LegacyMigrated() const noexcept
        {
            return legacy_migration_.IsMigrated(legacy_source_);
        }

        // 直近に返した Hit の出所。診断表示用。
        const CollisionSourceInfo& LastGroundSource() const noexcept { return last_ground_source_; }
        const CollisionSourceInfo& LastSweepSource() const noexcept { return last_sweep_source_; }

        // 登録表 1 件分。Debug Draw と診断 UI が読む。
        struct Registration
        {
            Core::ObjectID object;
            ColliderID collider = invalid_collider_id;
            Components::ColliderShape shape = Components::ColliderShape::Sphere;
            int layer = 0;
            int mask = -1;
            bool trigger = false;
            bool active = false;
            bool bounds_valid = false;
            DirectX::XMFLOAT3 bounds_min{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 bounds_max{ 0.0f, 0.0f, 0.0f };
        };
        const std::vector<Registration>& Registrations() const noexcept { return entries_; }

        // ---- IPhysicsQueryService -------------------------------------------

        bool CollisionAvailable() const override;

        bool QueryGround(const DirectX::XMFLOAT3& origin, float radius,
            float up_offset, float down_distance, float walkable_normal_y,
            GroundHit& hit) const override;

        bool SweepSphere(const DirectX::XMFLOAT3& start, const DirectX::XMFLOAT3& end,
            float radius, float maximum_normal_y, SphereSweepHit& hit) const override;

        // Layer / Mask を考慮した版。Character Motor はこちらを使う。
        bool SweepSphereFiltered(const DirectX::XMFLOAT3& start, const DirectX::XMFLOAT3& end,
            float radius, float maximum_normal_y, int layer, int mask,
            SphereSweepHit& hit) const override;

        bool QueryGroundFiltered(const DirectX::XMFLOAT3& origin, float radius,
            float up_offset, float down_distance, float walkable_normal_y,
            int layer, int mask, GroundHit& hit) const override;

    private:
        // Legacy へ問い合わせてよいか。
        // Hybrid かつ「その移行元が未移行」のときだけ true。
        bool ShouldConsultLegacy() const noexcept;

        // Scene の構成世代が変わっていたら登録表を作り直す。
        void ReconcileRegistrations();

        // 登録済みの Collider を Scene から引き直す。見つからなければ nullptr。
        Components::ColliderComponent* Resolve(const Registration& entry) const;

        // Scene 上の Collider へスイープする。最も手前の Hit を返す。
        bool SweepSceneColliders(const DirectX::XMFLOAT3& start,
            const DirectX::XMFLOAT3& end, float radius,
            float minimum_normal_y, float maximum_normal_y,
            int layer, int mask, SphereSweepHit& hit) const;

        // Collider 1 つに対するスイープ。形状ごとの分岐はここだけ。
        bool SweepSingleCollider(const Components::ColliderComponent& collider,
            const DirectX::XMFLOAT3& start, const DirectX::XMFLOAT3& end, float radius,
            Physics::SphereCastHit& hit) const;

        // 三角形群（ローカル空間）へスイープして、結果をワールドへ戻す共通処理。
        bool SweepLocalTriangles(const DirectX::XMFLOAT4X4& world,
            const DirectX::XMFLOAT4X4& inverse_world, bool negative_scale,
            float local_radius_scale, const Physics::Triangle* triangles,
            std::size_t triangle_count, const DirectX::XMFLOAT3& start,
            const DirectX::XMFLOAT3& end, float radius,
            Physics::SphereCastHit& hit) const;

        // 2 つの Collider が重なっているか。Trigger 判定に使う。
        bool Overlaps(const Components::ColliderComponent& trigger,
            const Components::ColliderComponent& other) const;

        enum class ContactPhase { Enter, Stay, Exit };

        // Trigger 側と入った側の両方へ配送する。
        void DispatchToPair(const Core::TriggerContact& contact, ContactPhase phase) const;

        // 1 つの GameObject の全 Component へ配送する。
        // ObjectID から引き直すので、削除済みなら何も起きない。
        void DispatchToObject(Core::ObjectID target,
            const Core::TriggerContact& contact, ContactPhase phase) const;

        // ---- 接続先（すべて非所有）------------------------------------------

        Scene* scene_ = nullptr;
        const IPhysicsQueryService* legacy_ = nullptr;
        LegacyStageMigrationState::SourceID legacy_source_ =
            LegacyStageMigrationState::stage_source_id;
        Physics::CookedMeshCollisionCache* cook_cache_ = nullptr;
        Physics::CookedMeshCollisionCache::Loader loader_;
        RevisionProvider revision_provider_;

        BackendMode mode_ = BackendMode::Hybrid;
        LegacyStageMigrationState legacy_migration_;

        // ---- 登録表 -----------------------------------------------------------

        std::vector<Registration> entries_;

        // 最後に突き合わせた Scene の構成世代。
        // これが Scene 側と一致している間は全走査しない。
        std::uint32_t last_generation_ = 0;
        bool has_generation_ = false;

        // ---- Trigger の接触ペア ------------------------------------------------

        struct Pair
        {
            Core::ObjectID trigger_object;
            ColliderID trigger_collider = invalid_collider_id;
            Core::ObjectID other_object;
            ColliderID other_collider = invalid_collider_id;

            // 直近に接触を確認したフレーム番号。
            // 現フレームより古ければ「離れた」と判断して Exit を出す。
            std::uint64_t last_seen = 0;
        };
        std::vector<Pair> pairs_;
        std::uint64_t trigger_frame_ = 0;

        // ---- 診断 -------------------------------------------------------------

        std::size_t active_collider_count_ = 0;
        std::size_t blocking_collider_count_ = 0;
        std::size_t trigger_collider_count_ = 0;
        std::size_t mesh_collider_count_ = 0;
        std::size_t rescan_count_ = 0;
        mutable bool legacy_consulted_ = false;
        mutable CollisionSourceInfo last_ground_source_;
        mutable CollisionSourceInfo last_sweep_source_;

        // クエリごとのヒープ確保を避けるための作業領域。
        // 参考プロジェクトの ScratchIndices と同じ考え方。
        mutable std::vector<std::uint32_t> scratch_indices_;
        mutable std::vector<Physics::Triangle> scratch_triangles_;
    };
}
