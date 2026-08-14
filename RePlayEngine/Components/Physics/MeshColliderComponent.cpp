#include "MeshColliderComponent.h"

#include "../Rendering/MeshRendererComponent.h"
#include "../Rendering/SkinnedMeshRendererComponent.h"
#include "../../Object/GameObject/GameObject.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

using namespace DirectX;

namespace ReplayEngine::Components
{
    namespace
    {
        constexpr float scale_epsilon = 1.0e-4f;
    }

    void MeshColliderComponent::OnColliderDetach()
    {
        // 共有参照を手放す。他の Collider が同じ Cook データを使っていれば残る。
        // 誰も使っていなければ、ここで実体が解放される（キャッシュは weak_ptr のみ）。
        cooked_.reset();
        cooked_key_ = Physics::CookKey{};
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

    Physics::CookKey MeshColliderComponent::BuildCookKey(
        const std::string& content_revision) const
    {
        Physics::CookKey key;
        key.asset_guid = ResolveMeshAssetGuid();
        key.content_revision = content_revision;
        key.settings.cell_size = cook_cell_size;
        key.settings.double_sided = double_sided;
        key.settings.sub_mesh_index = -1;
        return key;
    }

    bool MeshColliderComponent::EnsureCooked(Physics::CookedMeshCollisionCache& cache,
        const Physics::CookedMeshCollisionCache::Loader& loader,
        const std::string& content_revision)
    {
        const Physics::CookKey key = BuildCookKey(content_revision);
        if (key.asset_guid.empty())
        {
            cooked_.reset();
            cooked_key_ = Physics::CookKey{};
            UpdateStatus();
            return false;
        }

        // 同じキーなら作り直さない。
        // ここに Transform は入っていないので、動かしただけでは Cook が走らない。
        if (cooked_ != nullptr && cooked_key_ == key) return cooked_->Valid();

        cooked_ = cache.Acquire(key, loader);
        cooked_key_ = cooked_ != nullptr ? key : Physics::CookKey{};

        // Cook が入れ替わったので Bounds も作り直す必要がある。
        transform_valid_ = false;
        RefreshTransformIfChanged();

        UpdateStatus();
        return cooked_ != nullptr && cooked_->Valid();
    }

    bool MeshColliderComponent::RefreshTransformIfChanged()
    {
        const Core::GameObject* owner = Owner();
        if (owner == nullptr) return false;

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
            return false;
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
        const float minimum = (std::max)(scale_epsilon, (std::min)({ ax, ay, az }));
        const float maximum = (std::max)({ ax, ay, az });

        uniform_scale_ = (maximum - minimum) <= scale_epsilon * (std::max)(1.0f, maximum);
        negative_scale_ = (scale.x * scale.y * scale.z) < 0.0f;

        // 半径をローカルへ移すときの倍率。
        // 非一様のときは「最も縮む軸」で割る = ローカル球を大きめに取る。
        // すり抜けるより、少し早めに当たる方が安全なため。
        local_radius_scale_ = 1.0f / minimum;

        // ワールド空間の AABB。Broad Phase の粗い絞り込みに使う。
        world_bounds_min_ = { 0.0f, 0.0f, 0.0f };
        world_bounds_max_ = { 0.0f, 0.0f, 0.0f };
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

                world_bounds_min_.x = (std::min)(world_bounds_min_.x, transformed.x);
                world_bounds_min_.y = (std::min)(world_bounds_min_.y, transformed.y);
                world_bounds_min_.z = (std::min)(world_bounds_min_.z, transformed.z);
                world_bounds_max_.x = (std::max)(world_bounds_max_.x, transformed.x);
                world_bounds_max_.y = (std::max)(world_bounds_max_.y, transformed.y);
                world_bounds_max_.z = (std::max)(world_bounds_max_.z, transformed.z);
            }
        }

        UpdateStatus();
        return true;
    }

    bool MeshColliderComponent::ComputeWorldBounds(XMFLOAT3& minimum, XMFLOAT3& maximum) const
    {
        if (cooked_ == nullptr || !cooked_->Valid()) return false;
        minimum = world_bounds_min_;
        maximum = world_bounds_max_;
        return true;
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
