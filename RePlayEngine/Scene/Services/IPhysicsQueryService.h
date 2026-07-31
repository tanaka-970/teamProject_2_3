#pragma once

#include "../../Core/ObjectID/ObjectID.h"

#include <DirectXMath.h>

#include <cstdint>

namespace ReplayEngine::Scene
{
    // Hit がどの経路から返ってきたか。
    //
    // 移行期間中は「Scene 上の Collider」と「旧 Stage」の 2 経路がある。
    // どちらから返ったかを Hit へ記録しておくと、
    // 診断表示で「今の接地はどっち由来か」を画面から確認できる。
    enum class CollisionBackend
    {
        None = 0,
        SceneCollider,   // Scene 上の MeshCollider などから
        LegacyStage,     // 旧 Stage の衝突メッシュから（移行完了後に消える）
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

        // SceneCollider の場合のみ有効。LegacyStage では無効 ID になる。
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

    // 地形との問い合わせ窓口。
    //
    // なぜこれを挟むか:
    //   旧 SceneGame は GameRaycast::SphereCastStageDown(const Stage&, ...) を
    //   直接呼んでおり、移動処理が Stage 具象型に縛られていた。
    //
    //   CharacterMotorComponent はこのインターフェイスだけを見る。
    //   Scene 上の Collider を使うのか旧 Stage を使うのかは
    //   実装（SceneCollisionWorld）の中だけで決まり、Motor は違いを認識しない。
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

        // ---- Layer / Mask を考慮した版 --------------------------------------
        //
        // 既定の実装は layer / mask を無視して、上のフィルタ無し版へ委譲する。
        // 旧 Stage のように Layer の概念を持たない実装は、この既定のままでよい。
        //
        // 純粋仮想にしないのは、Layer に対応していない実装へ
        // 「対応しているふり」を強制しないため。
        virtual bool SweepSphereFiltered(const DirectX::XMFLOAT3& start,
            const DirectX::XMFLOAT3& end, float radius, float maximum_normal_y,
            int /*layer*/, int /*mask*/, SphereSweepHit& hit) const
        {
            return SweepSphere(start, end, radius, maximum_normal_y, hit);
        }

        virtual bool QueryGroundFiltered(const DirectX::XMFLOAT3& origin, float radius,
            float up_offset, float down_distance, float walkable_normal_y,
            int /*layer*/, int /*mask*/, GroundHit& hit) const
        {
            return QueryGround(origin, radius, up_offset, down_distance,
                walkable_normal_y, hit);
        }
    };
}
