#pragma once

#include "IPhysicsQueryService.h"
#include "../../Components/Physics/ColliderComponent.h"
#include "../../Physics/CookedMeshCollision.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ReplayEngine::Scene
{
    class Scene;

    // Scene上のColliderを扱う唯一の問い合わせ窓口。
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
    // 衝突問い合わせはSceneへ登録されたCollider Componentだけを参照する。
    // 別経路へのフォールバックを持たないため、同じ地形への二重衝突は起こらない。
    class SceneCollisionWorld final : public IPhysicsQueryService
    {
    public:
        using RevisionProvider = std::function<std::string(const std::string& asset_guid)>;

        // ---- 接続（framework が設定する）------------------------------------

        // Scene を差し替える。登録表と接触ペアは必ず捨てられる。
        // Play 開始・終了・Scene 読み込みのたびに呼ぶこと。
        void AttachScene(Scene* scene);

        // Scene から切り離す。
        void DetachScene() { AttachScene(nullptr); }

        const Scene* AttachedScene() const noexcept { return scene_; }

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
        // 直近に返した Hit の出所。診断表示用。
        const CollisionSourceInfo& LastGroundSource() const noexcept { return last_ground_source_; }
        const CollisionSourceInfo& LastSweepSource() const noexcept { return last_sweep_source_; }
        const CollisionSourceInfo& LastRaySource() const noexcept { return last_ray_source_; }

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

        // 絞り込み付き。Character Motor はこちらを使う。
        // filter.ignore_object で自分自身の Collider を除外できる。
        bool SweepSphereFiltered(const DirectX::XMFLOAT3& start, const DirectX::XMFLOAT3& end,
            float radius, float maximum_normal_y, const CollisionQueryFilter& filter,
            SphereSweepHit& hit) const override;

        bool QueryGroundFiltered(const DirectX::XMFLOAT3& origin, float radius,
            float up_offset, float down_distance, float walkable_normal_y,
            const CollisionQueryFilter& filter, GroundHit& hit) const override;

        bool Raycast(const DirectX::XMFLOAT3& origin,
            const DirectX::XMFLOAT3& direction, float max_distance,
            RaycastHit& hit) const override;

        bool RaycastFiltered(const DirectX::XMFLOAT3& origin,
            const DirectX::XMFLOAT3& direction, float max_distance,
            const CollisionQueryFilter& filter, RaycastHit& hit) const override;

        bool RaycastAllFiltered(const DirectX::XMFLOAT3& origin,
            const DirectX::XMFLOAT3& direction, float max_distance,
            const CollisionQueryFilter& filter,
            std::vector<PhysicsQueryHit>& hits) const override;
        bool OverlapSphere(const DirectX::XMFLOAT3& center, float radius,
            const CollisionQueryFilter& filter,
            std::vector<PhysicsQueryHit>& hits) const override;
        bool OverlapBox(const DirectX::XMFLOAT3& center,
            const DirectX::XMFLOAT3& half_extents,
            const DirectX::XMFLOAT4& rotation, const CollisionQueryFilter& filter,
            std::vector<PhysicsQueryHit>& hits) const override;
        bool OverlapCapsule(const DirectX::XMFLOAT3& point_a,
            const DirectX::XMFLOAT3& point_b, float radius,
            const CollisionQueryFilter& filter,
            std::vector<PhysicsQueryHit>& hits) const override;
        bool SphereCastAll(const DirectX::XMFLOAT3& origin,
            const DirectX::XMFLOAT3& direction, float radius, float max_distance,
            const CollisionQueryFilter& filter,
            std::vector<PhysicsQueryHit>& hits) const override;
        bool BoxCastAll(const DirectX::XMFLOAT3& center,
            const DirectX::XMFLOAT3& half_extents,
            const DirectX::XMFLOAT4& rotation, const DirectX::XMFLOAT3& direction,
            float max_distance, const CollisionQueryFilter& filter,
            std::vector<PhysicsQueryHit>& hits) const override;
        bool CapsuleCastAll(const DirectX::XMFLOAT3& point_a,
            const DirectX::XMFLOAT3& point_b, float radius,
            const DirectX::XMFLOAT3& direction, float max_distance,
            const CollisionQueryFilter& filter,
            std::vector<PhysicsQueryHit>& hits) const override;

    private:
        // Scene の構成世代が変わっていたら登録表を作り直す。
        void ReconcileRegistrations();

        // 登録済みの Collider を Scene から引き直す。見つからなければ nullptr。
        Components::ColliderComponent* Resolve(const Registration& entry) const;

        // Scene 上の Collider へスイープする。最も手前の Hit を返す。
        bool SweepSceneColliders(const DirectX::XMFLOAT3& start,
            const DirectX::XMFLOAT3& end, float radius,
            float minimum_normal_y, float maximum_normal_y,
            const CollisionQueryFilter& filter, SphereSweepHit& hit) const;

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
        Physics::CookedMeshCollisionCache* cook_cache_ = nullptr;
        Physics::CookedMeshCollisionCache::Loader loader_;
        RevisionProvider revision_provider_;

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
        mutable CollisionSourceInfo last_ground_source_;
        mutable CollisionSourceInfo last_sweep_source_;
        mutable CollisionSourceInfo last_ray_source_;

        // クエリごとのヒープ確保を避けるための作業領域。
        // 参考プロジェクトの ScratchIndices と同じ考え方。
        mutable std::vector<std::uint32_t> scratch_indices_;
        mutable std::vector<Physics::Triangle> scratch_triangles_;
    };
}
