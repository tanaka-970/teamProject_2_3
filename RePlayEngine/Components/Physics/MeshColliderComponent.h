#pragma once

#include "../../Object/Component/Component.h"
#include "../../Physics/SphereCast.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ReplayEngine::Components
{
    // 三角形メッシュの衝突形状。
    //
    // 旧 ReplayEngine::Core::MeshColliderComponent（Stage へ値メンバとして埋め込まれていたもの）
    // を、新しい Component 基盤へ移したもの。名前空間が Core -> Components なので
    // 完全修飾名が重複せず、移行期間中は旧クラスと併存できる。
    //
    // 責任:
    //   クック済み三角形を保持し、XZ グリッドで絞り込んで返すことだけ。
    //   実際のスイープ判定は Physics::CastSphereAgainstTriangles が行い、
    //   移動の解決は CharacterMotorComponent が行う。
    //
    // Transform について:
    //   三角形は「ワールド空間へ変換済み」の状態で保持する。
    //   Owner の Transform が変わったら Rebuild が必要になるため、
    //   Transform のバージョンを控えて自動で作り直す。
    //   自前の座標データを持つわけではないので、二重所有にはならない。
    //
    // Asset について:
    //   クック結果のキャッシュパスを保存する。三角形そのものは保存しない。
    //   読み込み時にキャッシュから読み直す。
    class MeshColliderComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(MeshColliderComponent)

    public:
        MeshColliderComponent() = default;

        void OnStart() override;
        void OnPropertyChanged(const char* property_name) override;

        // ---- 形状の構築 -----------------------------------------------------

        // モデル座標系の三角形を渡す。内部で Owner の Transform を掛けて保持する。
        void Build(std::vector<Physics::Triangle> local_triangles);

        void Clear() noexcept;

        bool Valid() const noexcept { return !world_triangles_.empty(); }
        std::size_t TriangleCount() const noexcept { return world_triangles_.size(); }

        // Owner の Transform が変わっていたら作り直す。毎フレーム呼んでよい。
        void RefreshIfTransformChanged();

        // ---- 問い合わせ -----------------------------------------------------

        // AABB と重なるセルの三角形添字を集める。重複は除かれる。
        void CollectTriangles(const DirectX::XMFLOAT3& aabb_min,
            const DirectX::XMFLOAT3& aabb_max,
            std::vector<std::uint32_t>& indices) const;

        const Physics::Triangle& TriangleAt(std::size_t index) const
        {
            return world_triangles_.at(index);
        }

        const std::vector<Physics::Triangle>& Triangles() const noexcept
        {
            return world_triangles_;
        }

        // ---- 保存される設定 -------------------------------------------------

        // クック済み衝突データのキャッシュパス（プロジェクト相対）。
        std::string cooked_path;

        // XZ グリッドの 1 セルの大きさ。小さいほど絞り込みが効くがメモリを食う。
        float cell_size = 4.0f;

    private:
        void RebuildGrid();
        void CellRange(float min_x, float max_x, float min_z, float max_z,
            int& x0, int& x1, int& z0, int& z1) const noexcept;

        // 実行時のみの状態。保存しない（cooked_path から読み直す）。
        std::vector<Physics::Triangle> local_triangles_;
        std::vector<Physics::Triangle> world_triangles_;
        std::vector<std::vector<std::uint32_t>> cells_;

        float min_x_ = 0.0f;
        float min_z_ = 0.0f;
        int cell_count_x_ = 0;
        int cell_count_z_ = 0;

        // 重複除去用。const 関数から使うので mutable。
        mutable std::vector<std::uint32_t> visit_stamps_;
        mutable std::uint32_t current_stamp_ = 0;

        // 最後に構築したときの Transform。変化を検出して作り直す。
        DirectX::XMFLOAT3 built_position_{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 built_rotation_{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 built_scale_{ 1.0f, 1.0f, 1.0f };
        bool built_ = false;
    };
}
