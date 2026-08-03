#pragma once

#include "../../Core/ObjectID/ObjectID.h"

#include <DirectXMath.h>

#include <cstdint>

namespace ReplayEngine::Scene
{
    // Hit がScene Colliderから返ったことを診断へ伝える。
    enum class CollisionBackend
    {
        None = 0,
        SceneCollider,
    };

    const char* ToString(CollisionBackend backend) noexcept;

    // Collider を一意に識別する番号。
    // ObjectID だけだと、同じ GameObject に Collider が複数ある場合を区別できない。
    using ColliderID = std::uint32_t;
    inline constexpr ColliderID invalid_collider_id = 0;

    // Hit に共通で付く「どこから来たか」の情報。
    struct CollisionSourceInfo
    {
        CollisionBackend backend = CollisionBackend::None;

        Core::ObjectID object;
        ColliderID collider = invalid_collider_id;
    };

    // 接地判定の結果。
    struct GroundHit
    {
        DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };   // 接地点（ワールド）
        DirectX::XMFLOAT3 normal{ 0.0f, 1.0f, 0.0f };     // 面法線（ワールド）
        CollisionSourceInfo source;
        bool valid = false;
    };

    struct SphereSweepHit
    {
        DirectX::XMFLOAT3 center{ 0.0f, 0.0f, 0.0f };     // 衝突時の球中心（ワールド）
        DirectX::XMFLOAT3 normal{ 0.0f, 1.0f, 0.0f };
        float fraction = 1.0f;
        CollisionSourceInfo source;
        bool valid = false;
    };

    // 問い合わせの絞り込み条件。
    //
    // 【ignore_object があるのはなぜか】
    //   Character Motor は自分の Collider を持ったまま、自分の周りへ球を飛ばす。
    //   何も除外しないと「自分の Collider に当たった」という結果が返り、
    //   その面から押し戻されて毎フレーム宙へ持ち上がる。
    //   実際にこの不具合が起きたので、除外を問い合わせ条件として明示している。
    //
    //   除外の単位は GameObject。同じ GameObject に付いた
    //   本体 Collider・攻撃判定・検知範囲は、まとめて自分自身として扱う。
    struct CollisionQueryFilter
    {
        int layer = 0;
        int mask = -1;

        // この GameObject に属する Collider は結果に含めない。
        // 無効 ID なら何も除外しない。
        Core::ObjectID ignore_object;
    };

    // 地形との問い合わせ窓口。
    //
    // なぜこれを挟むか:
    //   CharacterMotorComponent はこのインターフェイスだけを見る。
    //   実装はSceneCollisionWorldへ一本化されている。
    //
    // 実装は Scene の外側（framework）が用意し、
    // SceneServices 経由で非所有参照として渡す。
    class IPhysicsQueryService
    {
    public:
        virtual ~IPhysicsQueryService() = default;

        // 問い合わせ可能な地形があるか。無ければ Motor は重力・接地を適用しない。
        virtual bool CollisionAvailable() const = 0;

        // 足元へ球を落として接地点を探す。
        //   origin        … 球の中心（ワールド）
        //   radius        … 球半径
        //   up_offset     … origin からどれだけ上へ持ち上げてから落とすか
        //   down_distance … 落とす距離
        //   walkable_normal_y … これ以上の上向き成分を持つ面だけを地面とみなす
        virtual bool QueryGround(const DirectX::XMFLOAT3& origin, float radius,
            float up_offset, float down_distance, float walkable_normal_y,
            GroundHit& hit) const = 0;

        // 移動前後の球を掃引して壁を探す。
        //   maximum_normal_y … これ以下の上向き成分の面だけを壁とみなす
        //                      （床は QueryGround が扱うので除外する）
        virtual bool SweepSphere(const DirectX::XMFLOAT3& start,
            const DirectX::XMFLOAT3& end, float radius,
            float maximum_normal_y, SphereSweepHit& hit) const = 0;

        // ---- 絞り込み付きの問い合わせ ----------------------------------------
        //
        // 既定の実装はフィルタを無視して、上のフィルタ無し版へ委譲する。
        // 純粋仮想にしないのは、対応していない実装へ
        // 「対応しているふり」を強制しないため。
        virtual bool SweepSphereFiltered(const DirectX::XMFLOAT3& start,
            const DirectX::XMFLOAT3& end, float radius, float maximum_normal_y,
            const CollisionQueryFilter& /*filter*/, SphereSweepHit& hit) const
        {
            return SweepSphere(start, end, radius, maximum_normal_y, hit);
        }

        virtual bool QueryGroundFiltered(const DirectX::XMFLOAT3& origin, float radius,
            float up_offset, float down_distance, float walkable_normal_y,
            const CollisionQueryFilter& /*filter*/, GroundHit& hit) const
        {
            return QueryGround(origin, radius, up_offset, down_distance,
                walkable_normal_y, hit);
        }
    };
}
