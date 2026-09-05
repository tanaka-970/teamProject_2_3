#pragma once

#include "ColliderComponent.h"
#include "../../Physics/CookedMeshCollision.h"

#include <memory>
#include <string>

namespace ReplayEngine::Components
{
    // 三角形メッシュの衝突形状（静的環境用）。
    //
    // 【所有と共有の分け方】
    //   Cook データ（三角形と加速構造）は CookKey 単位で共有される不変オブジェクト。
    //   ローカル座標で保持され、複数の MeshCollider が同じ実体を参照する。
    //   実体の所有者はこの Component（shared_ptr）。キャッシュは weak_ptr しか持たない。
    //
    //   このクラスが持つのは「そのインスタンス固有の情報」だけ。
    //     World Transform / Inverse World Transform / World Bounds /
    //     ColliderID / Layer / Mask / Trigger / enabled / Cook データへの共有参照
    //
    //   ワールド空間へ変換済みの三角形は一切持たない。
    //   持つと、同じ Asset を別の場所へ 2 つ置いたときに共有できなくなる。
    //
    // 【問い合わせの流れ】
    //   1. ワールドのクエリ（球の始点・終点・半径）を Inverse World で
    //      このコライダーのローカル空間へ変換する
    //   2. Cook データのローカル三角形へ判定する
    //   3. Hit の位置と法線を World へ戻す
    //
    // 【Transform について】
    //   自前の座標は持たない。Owner の Transform が変わったときだけ
    //   World / Inverse World / World Bounds を作り直す。
    //   Cook はやり直さない（ローカル形状は変わらないため）。
    //
    // 【Dirty の分け方】
    //   Transform が変わった         -> World / Inverse / Bounds だけ作り直す
    //   Asset / Cook 設定が変わった  -> Cook データを取り直す（次の必要時）
    //   この 2 つは独立している。片方の変更でもう片方をやり直さない。
    //
    // 【今回の範囲外】
    //   Dynamic Rigidbody の MeshCollider / 変形する MeshCollider / SkinnedMesh 衝突 /
    //   毎フレーム Cook / 剛体シミュレーション / Convex Decomposition。
    class MeshColliderComponent final : public ColliderComponent
    {
        REPLAY_COMPONENT_BODY(MeshColliderComponent)

    public:
        // 衝突用メッシュをどこから取るか。
        enum MeshSource
        {
            // 同じ GameObject の MeshRenderer / SkinnedMeshRenderer が指す Asset を使う。
            MeshSource_Renderer = 0,

            // このコンポーネントの mesh_asset を使う（衝突専用の低ポリメッシュなど）。
            MeshSource_Custom = 1,
        };

        MeshColliderComponent() = default;

        ColliderShape Shape() const noexcept override { return ColliderShape::Mesh; }

        bool ComputeWorldBounds(DirectX::XMFLOAT3& minimum,
            DirectX::XMFLOAT3& maximum) const override;

        // Character Motor ではワールド AABB の外接球へ安全側に近似する。
        bool UsableAsCharacterShape() const noexcept override { return true; }

        std::string StatusMessage() const override { return status_; }

        // ---- 実行時の準備（SceneCollisionWorld から呼ばれる）-----------------

        // 実際に使う Asset GUID を返す。
        // Renderer 参照モードで Renderer が無い / Asset 未指定なら空文字。
        std::string ResolveMeshAssetGuid() const;

        // 今の設定から Cook キーを組み立てる。revision は呼び出し側が解決する。
        Physics::CookKey BuildCookKey(const std::string& content_revision) const;

        // Cook データを取得または解決する。取得できなければ false。
        // 無効な状態のまま衝突対象へ登録させないための入口。
        //
        // Cook が必要かどうかはキーの一致で判断する。
        // Transform が変わっただけでは Cook は走らない。
        bool EnsureCooked(Physics::CookedMeshCollisionCache& cache,
            const Physics::CookedMeshCollisionCache::Loader& loader,
            const std::string& content_revision);

        // Owner の Transform が変わっていたら World / Inverse World / Bounds を更新する。
        // Cook はやり直さない。毎フレーム呼んでよい。
        // 実際に作り直したときだけ true を返す。
        bool RefreshTransformIfChanged();

        // Cook をやり直させる。Asset を差し替えたときに呼ぶ。
        void MarkCookDirty() noexcept
        {
            cooked_.reset();
            cooked_key_ = Physics::CookKey{};
        }

        // 衝突対象として使える状態か。
        bool ReadyForQuery() const noexcept
        {
            return cooked_ != nullptr && cooked_->Valid() && ActiveInHierarchy();
        }

        // ---- 読み取り -------------------------------------------------------

        const std::shared_ptr<const Physics::CookedMeshCollisionData>& Cooked() const noexcept
        {
            return cooked_;
        }

        const DirectX::XMFLOAT4X4& WorldMatrix() const noexcept { return world_; }
        const DirectX::XMFLOAT4X4& InverseWorldMatrix() const noexcept { return inverse_world_; }
        const DirectX::XMFLOAT3& WorldBoundsMin() const noexcept { return world_bounds_min_; }
        const DirectX::XMFLOAT3& WorldBoundsMax() const noexcept { return world_bounds_max_; }

        // 一様な拡縮か。非一様だと球の半径をローカルへ正確に移せない。
        bool UniformScale() const noexcept { return uniform_scale_; }

        // 負の拡縮が含まれるか。面の裏表が反転するため法線を反転する必要がある。
        bool NegativeScale() const noexcept { return negative_scale_; }

        // ローカルへ移すときの半径の倍率。非一様なら安全側（大きめ）に倒す。
        float LocalRadiusScale() const noexcept { return local_radius_scale_; }

        // ---- 保存される設定 -------------------------------------------------

        // MeshSource_Renderer / MeshSource_Custom
        int mesh_source = MeshSource_Renderer;

        // MeshSource_Custom のときに使う AssetGUID。
        // GameObject 名や Asset 名ではなく GUID で参照する。
        std::string mesh_asset;

        // Cook 時の XZ グリッドのセルサイズ（ローカル空間）。
        float cook_cell_size = 4.0f;

        // 判定は常に両面で行う。片面にすると裏から入った瞬間にすり抜けるため。
        // 値は Cook の識別にだけ使っており、false にしても当たり方は変わらない。
        bool double_sided = true;

        // 既定表示は Bounds のみ。必要なときだけ三角形を出す。
        bool debug_draw_wireframe = false;

    private:
        void OnColliderDetach() override;
        void UpdateStatus();

        // 実行時のみの状態。保存しない。
        std::shared_ptr<const Physics::CookedMeshCollisionData> cooked_;

        // 最後に Cook したキー。これと一致しないときだけ Cook をやり直す。
        Physics::CookKey cooked_key_;

        DirectX::XMFLOAT4X4 world_{};
        DirectX::XMFLOAT4X4 inverse_world_{};
        DirectX::XMFLOAT3 world_bounds_min_{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 world_bounds_max_{ 0.0f, 0.0f, 0.0f };

        DirectX::XMFLOAT4X4 cached_world_{};
        bool transform_valid_ = false;

        bool uniform_scale_ = true;
        bool negative_scale_ = false;
        float local_radius_scale_ = 1.0f;

        std::string status_;
    };
}
