#pragma once

#include "SphereCast.h"

#include <cstddef>

namespace ReplayEngine::Physics
{
    // 解析的な形状スイープ。
    //
    // Mesh / Box は三角形へ落として CastSphereAgainstTriangles を使い回すが、
    // Sphere と Capsule を三角形へ分割すると、分割の粗さがそのまま
    // 当たり判定の粗さになってしまう。この 2 つだけ解析解で扱う。
    //
    // 座標系:
    //   すべてワールド空間で受け取り、ワールド空間で返す。
    //   Mesh / Box のようにローカルへ持ち込む必要がない
    //   （回転しても球は球、カプセルは軸を回すだけで済むため）。

    // 動く球 vs 静止したカプセル。
    //
    // segment_a == segment_b を渡せば「動く球 vs 静止した球」になる。
    // 開始時点で既に重なっている場合は fraction 0 で、
    // 押し出す向きの法線を返す（すり抜けたまま貼り付くのを防ぐ）。
    bool SweepSphereAgainstCapsule(const DirectX::XMFLOAT3& start,
        const DirectX::XMFLOAT3& end, float radius,
        const DirectX::XMFLOAT3& segment_a, const DirectX::XMFLOAT3& segment_b,
        float capsule_radius, SphereCastHit& hit) noexcept;

    // 直方体をローカル空間の三角形 12 枚へ展開する。
    //
    // 半辺長を受け取り、原点中心の箱を作る。
    // 呼び出し側はクエリを箱のローカル空間へ移してから使うこと
    // （Mesh Collider と同じ経路になる）。
    // out は 12 要素以上の領域を指していること。
    void BuildBoxTriangles(const DirectX::XMFLOAT3& half_extents, Triangle* out) noexcept;

    inline constexpr std::size_t box_triangle_count = 12;

    // 2 つの AABB が重なるか。Broad Phase の共通処理。
    inline bool BoundsOverlap(const DirectX::XMFLOAT3& min_a, const DirectX::XMFLOAT3& max_a,
        const DirectX::XMFLOAT3& min_b, const DirectX::XMFLOAT3& max_b) noexcept
    {
        if (max_a.x < min_b.x || min_a.x > max_b.x) return false;
        if (max_a.y < min_b.y || min_a.y > max_b.y) return false;
        if (max_a.z < min_b.z || min_a.z > max_b.z) return false;
        return true;
    }
}
