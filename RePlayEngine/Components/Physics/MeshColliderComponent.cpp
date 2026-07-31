#include "MeshColliderComponent.h"

#include "../Rendering/MeshRendererComponent.h"
#include "../Rendering/SkinnedMeshRendererComponent.h"
#include "../../Object/GameObject/GameObject.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace ReplayEngine::Components
{
    namespace
    {
        // ColliderID の採番。ObjectID だけでは、同じ GameObject に
        // Collider が複数付いている場合を区別できないため別に振る。
        // プロセス内で一意であればよく、保存はしない。
        Scene::ColliderID NextColliderID() noexcept
        {
            static Scene::ColliderID next = 1;
            return next++;
        }

        constexpr float scale_epsilon = 1.0e-4f;
    }

    void MeshColliderComponent::OnAttach()
    {
        collider_id_ = NextColliderID();
        XMStoreFloat4x4(&world_, XMMatrixIdentity());
        XMStoreFloat4x4(&inverse_world_, XMMatrixIdentity());
        transform_valid_ = false;
    }

    void MeshColliderComponent::OnDetach()
    {
        // 共有参照を手放す。他の Collider が同じ Cook データを使っていれば残る。
        cooked_.reset();
        cooked_asset_guid_.clear();
        collider_id_ = Scene::invalid_collider_id;
    }

    std::string MeshColliderComponent::ResolveMeshAssetGuid() const
    {
        if (mesh_source == MeshSource_Custom) return mesh_asset;

        const Core::GameObject* owner = Owner();
        if (owner == nullptr) return std::string();

        // Renderer 参照モード。静的・スキンのどちらでも受け付ける。
        // 見つからない場合に暗黙で別の Asset へ切り替えることはしない。
        if (const auto* mesh_renderer = owner->GetComponent<MeshRendererComponent>())
        {
            if (!mesh_renderer->mesh_asset.empty()) return mesh_renderer->mesh_asset;
        }
        if (const auto* skinned = owner->GetComponent<SkinnedMeshRendererComponent>())
        {
            if (!skinned->mesh_asset.empty()) return skinned->mesh_asset;
        }
        return std::string();
    }

    bool MeshColliderComponent::EnsureCooked(Physics::CookedMeshCollisionCache& cache,
        const Physics::CookedMeshCollisionCache::Loader& loader)
    {
        const std::string guid = ResolveMeshAssetGuid();
        if (guid.empty())
        {
            cooked_.reset();
            cooked_asset_guid_.clear();
            UpdateStatus();
            return false;
        }

        Physics::CookedMeshCollisionData::Settings settings;
        settings.cell_size = cook_cell_size;
        settings.double_sided = double_sided;

        // 同じ Asset かつ同じ Cook 設定なら作り直さない。
        if (cooked_ != nullptr && cooked_asset_guid_ == guid &&
            cooked_->CookSettings() == settings)
        {
            return true;
        }

        cooked_ = cache.Acquire(guid, settings, loader);
        cooked_asset_guid_ = cooked_ != nullptr ? guid : std::string();
        UpdateStatus();
        return cooked_ != nullptr && cooked_->Valid();
    }

    void MeshColliderComponent::RefreshTransformIfChanged()
    {
        const Core::GameObject* owner = Owner();
        if (owner == nullptr) return;

        const Core::Transform& transform = owner->GetTransform();
        const XMFLOAT3 position = transform.LocalPosition();
        const XMFLOAT3 rotation = transform.LocalRotationEuler();
        const XMFLOAT3 scale = transform.LocalScale();

        const auto same = [](const XMFLOAT3& a, const XMFLOAT3& b) noexcept
        {
            return a.x == b.x && a.y == b.y && a.z == b.z;
        };

        if (transform_valid_ && same(position, cached_position_) &&
            same(rotation, cached_rotation_) && same(scale, cached_scale_))
        {
            return;
        }

        cached_position_ = position;
        cached_rotation_ = rotation;
        cached_scale_ = scale;
        transform_valid_ = true;

        // ワールド行列と、その逆行列を控える。
        // 逆行列はクエリをローカル空間へ持ち込むために毎回使う。
        const XMMATRIX world = transform.WorldMatrix();
        XMStoreFloat4x4(&world_, world);

        XMVECTOR determinant{};
        const XMMATRIX inverse = XMMatrixInverse(&determinant, world);
        XMStoreFloat4x4(&inverse_world_, inverse);

        // 拡縮の性質を調べる。
        //   一様      … 球の半径をそのまま割れば正確
        //   非一様    … 球がローカルでは楕円体になる。正確には扱えない
        //   負を含む  … 面の裏表が反転する
        const float ax = std::fabs(scale.x);
        const float ay = std::fabs(scale.y);
        const float az = std::fabs(scale.z);
        const float minimum = std::max(scale_epsilon, std::min({ ax, ay, az }));
        const float maximum = std::max({ ax, ay, az });

        uniform_scale_ = (maximum - minimum) <= scale_epsilon * std::max(1.0f, maximum);
        negative_scale_ = (scale.x * scale.y * scale.z) < 0.0f;

        // 半径をローカルへ移すときの倍率。
        // 非一様のときは「最も縮む軸」で割る = ローカル球を大きめに取る。
        // すり抜けるより、少し早めに当たる方が安全なため。
        local_radius_scale_ = 1.0f / minimum;

        // ワールド空間の AABB。Broad Phase の粗い絞り込みに使う。
        if (cooked_ != nullptr && cooked_->Valid())
        {
            const XMFLOAT3& local_min = cooked_->LocalBoundsMin();
            const XMFLOAT3& local_max = cooked_->LocalBoundsMax();

            world_bounds_min_ = { FLT_MAX, FLT_MAX, FLT_MAX };
            world_bounds_max_ = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

            // ローカル AABB の 8 頂点を変換して、その AABB を取る。
            for (int corner = 0; corner < 8; ++corner)
            {
                const XMFLOAT3 point{
                    (corner & 1) ? local_max.x : local_min.x,
                    (corner & 2) ? local_max.y : local_min.y,
                    (corner & 4) ? local_max.z : local_min.z };

                XMFLOAT3 transformed{};
                XMStoreFloat3(&transformed,
                    XMVector3TransformCoord(XMLoadFloat3(&point), world));

                world_bounds_min_.x = std::min(world_bounds_min_.x, transformed.x);
                world_bounds_min_.y = std::min(world_bounds_min_.y, transformed.y);
                world_bounds_min_.z = std::min(world_bounds_min_.z, transformed.z);
                world_bounds_max_.x = std::max(world_bounds_max_.x, transformed.x);
                world_bounds_max_.y = std::max(world_bounds_max_.y, transformed.y);
                world_bounds_max_.z = std::max(world_bounds_max_.z, transformed.z);
            }
        }

        UpdateStatus();
    }

    void MeshColliderComponent::UpdateStatus()
    {
        status_.clear();

        if (mesh_source == MeshSource_Renderer)
        {
            const Core::GameObject* owner = Owner();
            const bool has_renderer = owner != nullptr &&
                (owner->GetComponent<MeshRendererComponent>() != nullptr ||
                 owner->GetComponent<SkinnedMeshRendererComponent>() != nullptr);

            if (!has_renderer)
            {
                status_ = "Renderer のメッシュを使う設定ですが、"
                    "この GameObject に Mesh Renderer がありません。";
                return;
            }
            if (ResolveMeshAssetGuid().empty())
            {
                status_ = "Renderer の Asset が未指定です。"
                    "Renderer へメッシュを設定するか、衝突専用メッシュへ切り替えてください。";
                return;
            }
        }
        else if (mesh_asset.empty())
        {
            status_ = "衝突専用メッシュが未指定です。";
            return;
        }

        if (cooked_ == nullptr)
        {
            status_ = "衝突データを読み込めませんでした。Asset が欠損している可能性があります。";
            return;
        }
        if (!cooked_->Valid())
        {
            status_ = "衝突データに三角形がありません。";
            return;
        }

        // 拡縮の制限を明記する。黙って誤差を出さない。
        if (negative_scale_)
        {
            status_ = "負の拡大率が含まれています。面の裏表が反転するため法線を反転して扱います。";
        }
        else if (!uniform_scale_)
        {
            status_ = "拡大率が軸ごとに異なります。球の判定はローカル空間で真球にならないため、"
                "安全側（やや大きめ）に近似しています。正確さが必要な場合は一様な拡大率にしてください。";
        }
    }
}
