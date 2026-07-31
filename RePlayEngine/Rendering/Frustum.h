#pragma once

#include <DirectXMath.h>

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace ReplayEngine::Rendering
{
    // 視錐台カリング用の6平面と、AABBとの交差判定。
    //
    // ビュー射影行列から平面を直接取り出す(Gribb/Hartmannの方法)。
    // 行優先・mul(vector, matrix) 規約に合わせているので、
    // このエンジンの view_projection をそのまま渡せる。
    //
    // AABB判定は「最も平面の外側に寄る頂点」だけを見る定番の高速版。
    // 8頂点を回す必要がないため、数百プリミティブでも負荷にならない。
    class Frustum final
    {
    public:
        void BuildFromViewProjection(const DirectX::XMFLOAT4X4& view_projection) noexcept
        {
            const DirectX::XMFLOAT4X4& m = view_projection;

            // 左: w + x > 0
            planes_[0] = { m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41 };
            // 右: w - x > 0
            planes_[1] = { m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41 };
            // 下: w + y > 0
            planes_[2] = { m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42 };
            // 上: w - y > 0
            planes_[3] = { m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42 };
            // 近: z > 0 (Direct3Dの深度範囲は0..1)
            planes_[4] = { m._13, m._23, m._33, m._43 };
            // 遠: w - z > 0
            planes_[5] = { m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43 };

            // 法線を正規化しておくと、距離の比較に一貫性が出る。
            for (auto& plane : planes_)
            {
                const float length = std::sqrt(
                    plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
                if (length > 1.0e-8f)
                {
                    const float inverse = 1.0f / length;
                    plane.x *= inverse; plane.y *= inverse;
                    plane.z *= inverse; plane.w *= inverse;
                }
            }
            valid_ = true;
        }

        // AABBが視錐台の外にあるなら false。境界上や内側は true。
        bool IntersectsAabb(const DirectX::XMFLOAT3& minimum,
                            const DirectX::XMFLOAT3& maximum) const noexcept
        {
            if (!valid_) return true;
            for (const DirectX::XMFLOAT4& plane : planes_)
            {
                // 平面の法線方向に最も進んだ頂点(サポート点)を選ぶ。
                // それでも負側なら、8頂点すべてが外側にあると確定する。
                const float x = plane.x >= 0.0f ? maximum.x : minimum.x;
                const float y = plane.y >= 0.0f ? maximum.y : minimum.y;
                const float z = plane.z >= 0.0f ? maximum.z : minimum.z;
                if (plane.x * x + plane.y * y + plane.z * z + plane.w < 0.0f)
                    return false;
            }
            return true;
        }

        // ワールド行列で変換したAABBを判定する。回転を含む場合は
        // 変換後のAABBを取り直してから判定する(保守的だが安全)。
        bool IntersectsTransformedAabb(const DirectX::XMFLOAT3& local_minimum,
                                       const DirectX::XMFLOAT3& local_maximum,
                                       const DirectX::XMFLOAT4X4& world) const noexcept
        {
            if (!valid_) return true;

            const DirectX::XMMATRIX matrix = DirectX::XMLoadFloat4x4(&world);
            DirectX::XMFLOAT3 minimum{ FLT_MAX, FLT_MAX, FLT_MAX };
            DirectX::XMFLOAT3 maximum{ -FLT_MAX, -FLT_MAX, -FLT_MAX };

            for (int corner = 0; corner < 8; ++corner)
            {
                const DirectX::XMVECTOR local = DirectX::XMVectorSet(
                    (corner & 1) ? local_maximum.x : local_minimum.x,
                    (corner & 2) ? local_maximum.y : local_minimum.y,
                    (corner & 4) ? local_maximum.z : local_minimum.z, 1.0f);
                DirectX::XMFLOAT3 transformed{};
                DirectX::XMStoreFloat3(&transformed,
                    DirectX::XMVector3TransformCoord(local, matrix));
                minimum.x = (std::min)(minimum.x, transformed.x);
                minimum.y = (std::min)(minimum.y, transformed.y);
                minimum.z = (std::min)(minimum.z, transformed.z);
                maximum.x = (std::max)(maximum.x, transformed.x);
                maximum.y = (std::max)(maximum.y, transformed.y);
                maximum.z = (std::max)(maximum.z, transformed.z);
            }

            return IntersectsAabb(minimum, maximum);
        }

        bool Valid() const noexcept { return valid_; }
        void Invalidate() noexcept { valid_ = false; }

    private:
        DirectX::XMFLOAT4 planes_[6]{};
        bool valid_ = false;
    };

    // カリングはフレーム単位で共有する。メッシュ側は Frustum() を参照すれば
    // 描画直前に判定できるため、render() の引数を増やさずに済む。
    struct CullingContext
    {
        Frustum frustum;
        bool enabled = true;
        // 統計表示用。1フレームで何プリミティブ落としたか。
        unsigned int tested = 0;
        unsigned int culled = 0;

        // --- 自動LOD ---------------------------------------------------------
        // 画面上でどれだけ小さく写るかでLODを選ぶ。距離ベースだと
        // 画角やオブジェクトの大小で最適点がずれるため、投影サイズで判断する。
        bool lod_enabled = true;
        // ビュー射影行列と画面の高さ。LOD選択の投影計算に使う。
        DirectX::XMFLOAT4X4 view_projection{};
        float screen_height = 1080.0f;
        DirectX::XMFLOAT3 camera_position{};
        // このピクセル高さを下回るごとにLODを1段落とす。
        // 例: 320 なら 320px未満でLOD1、160px未満でLOD2、80px未満でLOD3。
        float lod_pixel_threshold = 320.0f;
        // LOD選択を強制する (-1 で自動)。見比べ用。
        int forced_lod = -1;
        // 統計表示用。LODごとに何プリミティブ描いたか。
        unsigned int lod_draws[4]{};
        // LODを生成中のモデルがあるか。描画側が毎フレーム申告する。
        bool lod_building = false;
        // 生成済みLOD段数の最大値。0ならLODが1つも無い。
        unsigned int lod_available = 0;

        void BeginFrame() noexcept
        {
            tested = 0;
            culled = 0;
            lod_building = false;
            // lod_available はモデルごとに固定の情報なのでリセットしない。
            // 毎フレーム0に戻すとUI(描画より前に走る)が0を読んでしまう。
            for (unsigned int& count : lod_draws) count = 0;
        }

        // AABBの画面上の高さ(ピクセル)からLOD番号を返す。
        // available_lods はLOD1以降の数。
        int SelectLod(const DirectX::XMFLOAT3& bounds_minimum,
                      const DirectX::XMFLOAT3& bounds_maximum,
                      const DirectX::XMFLOAT4X4& world,
                      size_t available_lods) const noexcept
        {
            if (!lod_enabled || available_lods == 0) return 0;
            if (forced_lod >= 0)
                return (std::min)(forced_lod, static_cast<int>(available_lods));

            // AABBの中心とワールド半径を求める。
            const DirectX::XMMATRIX matrix = DirectX::XMLoadFloat4x4(&world);
            const DirectX::XMVECTOR local_center = DirectX::XMVectorSet(
                (bounds_minimum.x + bounds_maximum.x) * 0.5f,
                (bounds_minimum.y + bounds_maximum.y) * 0.5f,
                (bounds_minimum.z + bounds_maximum.z) * 0.5f, 1.0f);
            const DirectX::XMVECTOR center =
                DirectX::XMVector3TransformCoord(local_center, matrix);

            const DirectX::XMVECTOR extent = DirectX::XMVectorSet(
                (bounds_maximum.x - bounds_minimum.x) * 0.5f,
                (bounds_maximum.y - bounds_minimum.y) * 0.5f,
                (bounds_maximum.z - bounds_minimum.z) * 0.5f, 0.0f);
            // スケール込みの半径。等方スケールでなくても最大成分で見積もる。
            const DirectX::XMVECTOR scaled_extent =
                DirectX::XMVector3TransformNormal(extent, matrix);
            const float radius = DirectX::XMVectorGetX(DirectX::XMVector3Length(scaled_extent));
            if (radius <= 0.0f) return 0;

            // カメラからの距離。近すぎる場合はLOD0。
            DirectX::XMFLOAT3 center_position{};
            DirectX::XMStoreFloat3(&center_position, center);
            const float dx = center_position.x - camera_position.x;
            const float dy = center_position.y - camera_position.y;
            const float dz = center_position.z - camera_position.z;
            const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (distance <= radius) return 0;

            // 画面上の高さ(ピクセル)。projection._22 = 1/tan(fovY/2) を使う。
            const float projection_scale = view_projection._22 != 0.0f
                ? std::abs(view_projection._22) : 1.0f;
            const float pixel_height =
                (2.0f * radius) * projection_scale * 0.5f * screen_height / distance;

            // 閾値を半分ずつ下げながら段を決める。
            int level = 0;
            float threshold = (std::max)(lod_pixel_threshold, 1.0f);
            while (level < static_cast<int>(available_lods) && pixel_height < threshold)
            {
                ++level;
                threshold *= 0.5f;
            }
            return level;
        }

        void CountLodDraw(int level) noexcept
        {
            if (level >= 0 && level < 4) ++lod_draws[level];
        }
    };

    inline CullingContext& Culling()
    {
        static CullingContext context{};
        return context;
    }
}
