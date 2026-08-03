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
    // Cook 結果に影響する設定。キーの一部になる。
    struct CookSettings
    {
        // XZ グリッド 1 セルのローカル空間での大きさ。
        float cell_size = 4.0f;

        // 裏面にも当たるか。false なら表面のみ。
        bool double_sided = true;

        // メッシュの一部だけを衝突に使う場合の添字。-1 で全体。
        int sub_mesh_index = -1;

        bool operator==(const CookSettings& other) const noexcept
        {
            return cell_size == other.cell_size &&
                double_sided == other.double_sided &&
                sub_mesh_index == other.sub_mesh_index;
        }
        bool operator!=(const CookSettings& other) const noexcept { return !(*this == other); }
    };

    // Cook キャッシュの主キー。
    //
    // 【AssetGUID だけにしない理由】
    //   1. 同じ GUID でも Asset を再インポートすれば中身が変わる。
    //      GUID だけをキーにすると、古い形で当たり続ける。
    //   2. cell_size / double_sided / sub_mesh_index が変われば別物になる。
    //      同じ Cook 結果を使い回してはいけない。
    //
    //   content_revision には「更新時刻 + ファイルサイズ」や content hash など、
    //   実体が変われば必ず変わる文字列を入れる。空文字は「不明」を意味し、
    //   その場合キャッシュは効くが再インポート検出はできない（呼び出し側が
    //   Invalidate を呼ぶ必要がある）。
    struct CookKey
    {
        std::string asset_guid;
        std::string content_revision;
        CookSettings settings;

        bool operator==(const CookKey& other) const noexcept
        {
            return asset_guid == other.asset_guid &&
                content_revision == other.content_revision &&
                settings == other.settings;
        }
    };

    struct CookKeyHash
    {
        std::size_t operator()(const CookKey& key) const noexcept;
    };

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
        // ローカル座標の三角形から Cook する。
        static std::shared_ptr<const CookedMeshCollisionData> Build(
            CookKey key, std::vector<Triangle> local_triangles);

        const CookKey& Key() const noexcept { return key_; }
        const std::string& AssetGuid() const noexcept { return key_.asset_guid; }

        // 名前を Settings() にしてある。
        // CookSettings() にすると「型名と同じ名前のメンバ関数」になり、
        // C++ の規則で名前の意味が変わってしまうためコンパイルが通らない。
        const CookSettings& Settings() const noexcept { return key_.settings; }

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

        CookKey key_;

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

    // Cook データの共有キャッシュ。
    //
    // 【所有権】
    //   キャッシュは所有しない。weak_ptr しか持たない。
    //   実体の所有者は MeshColliderComponent（shared_ptr）だけ。
    //
    //   こうしている理由:
    //     shared_ptr を永久保持すると、Scene を切り替えても Cook 済み三角形が
    //     解放されず、使っていない Asset のぶんだけメモリが増え続ける。
    //     weak_ptr なら「最後の Collider が消えた瞬間」に実体も消える。
    //     参照が生きている間は Acquire が同じ実体を返すので、共有は保たれる。
    //
    //   Collect() で期限切れのエントリを掃除し、Clear() で表ごと捨てる。
    //   Scene の切り替えと Project の終了では Clear() を呼ぶこと。
    //
    // Singleton にはしない。framework が値メンバとして 1 つ所有する。
    class CookedMeshCollisionCache final
    {
    public:
        // ローカル三角形を読み込む関数。生存中の実体が無いときだけ呼ばれる。
        // 読み込めなければ false を返す。
        using Loader = std::function<bool(const CookKey& key,
            std::vector<Triangle>& out_local_triangles)>;

        // Cook データを取得する。無ければ loader で読み込んで Cook する。
        // 読み込めなかった場合は nullptr を返す（無効な Collider を作らせない）。
        std::shared_ptr<const CookedMeshCollisionData> Acquire(
            const CookKey& key, const Loader& loader);

        // 特定の Asset を捨てる。再インポート後の再 Cook に使う。
        // 同じ GUID を持つエントリを設定違い・revision 違いも含めてすべて消す。
        void Invalidate(const std::string& asset_guid);

        void Clear() noexcept;

        // 期限切れ（参照が 0 になった）エントリを取り除く。
        // 戻り値は取り除いた件数。
        std::size_t Collect();

        // 生存しているエントリの数。期限切れは数えない。
        std::size_t LiveEntryCount() const;

        // 表に載っているエントリの数（期限切れを含む）。診断用。
        std::size_t TableSize() const noexcept { return entries_.size(); }

        // Cook を実行した回数。「共有できているか」の確認に使う。
        std::size_t CookCount() const noexcept { return cook_count_; }

        // 読み込みに失敗したキー。同じものを毎フレーム試さないための記録。
        bool Failed(const CookKey& key) const;

    private:
        // 所有しない。実体は MeshColliderComponent が持つ。
        std::unordered_map<CookKey, std::weak_ptr<const CookedMeshCollisionData>,
            CookKeyHash> entries_;

        // 失敗もキー単位で覚える。
        // Asset を作り直せば content_revision が変わるので、自然に再試行される。
        std::unordered_map<CookKey, bool, CookKeyHash> failures_;

        std::size_t cook_count_ = 0;
    };
}
