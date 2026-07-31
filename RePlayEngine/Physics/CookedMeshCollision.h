#pragma once

#include "SphereCast.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ReplayEngine::Physics
{
    // Cook 済みの衝突形状。
    //
    // 【最重要】保持するのは「ローカル座標」の三角形と加速構造。
    //   ワールド空間へ変換済みの三角形は持たない。
    //   同じ Asset を別の Transform で 2 か所へ置いても、この 1 つを共有できる。
    //
    //   問い合わせ側は「クエリをこのデータのローカル空間へ変換 → 判定 →
    //   結果をワールドへ戻す」という流れになる。
    //
    // 参考プロジェクト（StageCollisionMesh）は
    // 「ワールド空間の三角形を Transform 変更時に作り直す」方式だった。
    // 単体 Stage なら成り立つが、AssetGUID 単位で共有する設計とは両立しない
    // （Transform が違う 2 体で同じ配列を使えない）ので採用していない。
    // 空間分割の考え方（XZ グリッド + 訪問スタンプ）だけを引き継いでいる。
    //
    // 不変オブジェクトとして扱う。Build() 後は誰も書き換えない。
    // そのため const 参照を複数の Collider で安全に共有できる。
    class CookedMeshCollisionData final
    {
    public:
        struct Settings
        {
            // XZ グリッド 1 セルのローカル空間での大きさ。
            float cell_size = 4.0f;

            // 裏面にも当たるか。false なら表面のみ。
            bool double_sided = true;

            bool operator==(const Settings& other) const noexcept
            {
                return cell_size == other.cell_size && double_sided == other.double_sided;
            }
        };

        // ローカル座標の三角形から Cook する。失敗しても nullptr は返さず、
        // 三角形 0 個の「空の Cook データ」を返す（Valid() が false になる）。
        static std::shared_ptr<const CookedMeshCollisionData> Build(
            std::string asset_guid, std::vector<Triangle> local_triangles,
            const Settings& settings);

        const std::string& AssetGuid() const noexcept { return asset_guid_; }
        const Settings& CookSettings() const noexcept { return settings_; }

        bool Valid() const noexcept { return !triangles_.empty(); }
        std::size_t TriangleCount() const noexcept { return triangles_.size(); }
        const Triangle* Triangles() const noexcept { return triangles_.data(); }

        // ローカル空間の AABB。
        const DirectX::XMFLOAT3& LocalBoundsMin() const noexcept { return bounds_min_; }
        const DirectX::XMFLOAT3& LocalBoundsMax() const noexcept { return bounds_max_; }

        // ローカル空間の AABB に掛かるセルの三角形添字を、重複なく集める。
        // out はクリアされる。スレッド安全ではない（メインスレッド専用）。
        void CollectTriangles(const DirectX::XMFLOAT3& local_aabb_min,
            const DirectX::XMFLOAT3& local_aabb_max,
            std::vector<std::uint32_t>& out) const;

    private:
        CookedMeshCollisionData() = default;

        void BuildGrid();
        void CellRange(float min_x, float max_x, float min_z, float max_z,
            int& x0, int& x1, int& z0, int& z1) const noexcept;

        std::string asset_guid_;
        Settings settings_;

        std::vector<Triangle> triangles_;                 // ローカル座標
        std::vector<std::vector<std::uint32_t>> cells_;   // XZ グリッド

        DirectX::XMFLOAT3 bounds_min_{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 bounds_max_{ 0.0f, 0.0f, 0.0f };

        int cell_count_x_ = 0;
        int cell_count_z_ = 0;

        // 三角形が複数セルへ跨るので、列挙時の重複を訪問スタンプで防ぐ。
        mutable std::vector<std::uint32_t> visit_stamps_;
        mutable std::uint32_t current_stamp_ = 0;
    };

    // AssetGUID -> Cook データの共有キャッシュ。
    //
    // 同じ Asset を使う MeshCollider が何体あっても、Cook は 1 回だけ。
    // 三角形の実体も 1 つだけ。各 Collider は自分の Transform を持ち、
    // 共有参照だけを保持する。
    //
    // Singleton にはしない。framework が値メンバとして 1 つ所有する。
    class CookedMeshCollisionCache final
    {
    public:
        // ローカル三角形を読み込む関数。キャッシュに無いときだけ呼ばれる。
        // 読み込めなければ false を返す。
        using Loader = std::function<bool(const std::string& asset_guid,
            std::vector<Triangle>& out_local_triangles)>;

        // Cook データを取得する。無ければ loader で読み込んで Cook する。
        // 読み込めなかった場合は nullptr を返す（無効な Collider を作らせない）。
        std::shared_ptr<const CookedMeshCollisionData> Acquire(
            const std::string& asset_guid,
            const CookedMeshCollisionData::Settings& settings,
            const Loader& loader);

        // 特定の Asset を捨てる。再インポート後の再 Cook に使う。
        void Invalidate(const std::string& asset_guid);

        void Clear() noexcept;

        std::size_t EntryCount() const noexcept { return entries_.size(); }

        // 読み込みに失敗した Asset。同じものを毎フレーム試さないための記録。
        bool Failed(const std::string& asset_guid) const;

    private:
        struct Entry
        {
            CookedMeshCollisionData::Settings settings;
            std::shared_ptr<const CookedMeshCollisionData> data;
        };

        std::unordered_map<std::string, Entry> entries_;
        std::unordered_map<std::string, bool> failures_;
    };
}
