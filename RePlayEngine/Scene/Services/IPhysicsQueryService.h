#pragma once

#include <DirectXMath.h>

namespace ReplayEngine::Scene
{
    // 接地判定とスイープ問い合わせの結果。
    struct GroundHit
    {
        DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };   // 接地点
        DirectX::XMFLOAT3 normal{ 0.0f, 1.0f, 0.0f };     // 面法線
        bool valid = false;
    };

    struct SphereSweepHit
    {
        DirectX::XMFLOAT3 center{ 0.0f, 0.0f, 0.0f };     // 衝突時の球中心
        DirectX::XMFLOAT3 normal{ 0.0f, 1.0f, 0.0f };
        float fraction = 1.0f;
        bool valid = false;
    };

    // 地形との問い合わせ窓口。
    //
    // なぜこれを挟むか:
    //   旧 SceneGame は GameRaycast::SphereCastStageDown(const Stage&, ...) を
    //   直接呼んでおり、移動処理が Stage 具象型に縛られていた。
    //
    //   CharacterMotorComponent はこのインターフェイスだけを見る。
    //   将来 Stage を地形 GameObject 群へ移行しても、
    //   実装（Bridge）を差し替えるだけで CharacterMotor は変更不要。
    //
    // 実装は Scene の外側（framework / SceneGame）が用意し、
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
    };
}
