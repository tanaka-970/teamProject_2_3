#include "Transform.h"

#include <cmath>

using namespace DirectX;

namespace ReplayEngine::Core
{
    namespace
    {
        // 行列から Pitch/Yaw/Roll (XMMatrixRotationRollPitchYaw と同じ順序) を取り出す。
        // ジンバル特異点 (Pitch が ±90 度付近) では Roll を 0 に倒して Yaw へ寄せる。
        //
        // XMMatrixRotationRollPitchYaw(p, y, r) を展開すると各成分はこうなる。
        //   _31 = cos(p)sin(y)   _32 = -sin(p)   _33 = cos(p)cos(y)
        //   _12 = sin(r)cos(p)   _22 = cos(r)cos(p)
        // 取り出す式はこれを逆に解いたもの。符号を反転させると LocalMatrix と
        // 往復せず、SetFromWorldMatrix が回転を裏返す。
        XMFLOAT3 ExtractEulerFromRotationMatrix(const XMFLOAT4X4& m) noexcept
        {
            XMFLOAT3 euler{ 0.0f, 0.0f, 0.0f };

            const float sin_pitch = -m._32;
            if (sin_pitch >= 1.0f - 1.0e-6f)
            {
                euler.x = XM_PIDIV2;
                euler.y = std::atan2(-m._13, m._11);
                euler.z = 0.0f;
            }
            else if (sin_pitch <= -1.0f + 1.0e-6f)
            {
                euler.x = -XM_PIDIV2;
                euler.y = std::atan2(-m._13, m._11);
                euler.z = 0.0f;
            }
            else
            {
                euler.x = std::asin(sin_pitch);
                euler.y = std::atan2(m._31, m._33);
                euler.z = std::atan2(m._12, m._22);
            }
            return euler;
        }
    }

    XMMATRIX Transform::LocalMatrix() const noexcept
    {
        const XMMATRIX s = XMMatrixScaling(local_scale_.x, local_scale_.y, local_scale_.z);
        const XMMATRIX r = XMMatrixRotationRollPitchYaw(
            local_rotation_.x, local_rotation_.y, local_rotation_.z);
        const XMMATRIX t = XMMatrixTranslation(
            local_position_.x, local_position_.y, local_position_.z);
        return s * r * t;
    }

    XMMATRIX Transform::ParentWorldMatrix() const noexcept
    {
        // 親チェーンが循環していた場合に無限再帰しないよう、深さに上限を設ける。
        // GameObject::SetParent 側でも循環を弾いているが、二重の安全網として残す。
        constexpr int maximum_depth = 64;

        XMMATRIX accumulated = XMMatrixIdentity();
        const Transform* current = parent_;
        for (int depth = 0; current != nullptr && depth < maximum_depth; ++depth)
        {
            accumulated = accumulated * current->LocalMatrix();
            current = current->parent_;
        }
        return accumulated;
    }

    XMMATRIX Transform::WorldMatrix() const noexcept
    {
        return LocalMatrix() * ParentWorldMatrix();
    }

    XMFLOAT4X4 Transform::WorldMatrixFloat4x4() const noexcept
    {
        XMFLOAT4X4 result{};
        XMStoreFloat4x4(&result, WorldMatrix());
        return result;
    }

    XMFLOAT3 Transform::WorldPosition() const noexcept
    {
        XMFLOAT4X4 world{};
        XMStoreFloat4x4(&world, WorldMatrix());
        return XMFLOAT3{ world._41, world._42, world._43 };
    }

    XMFLOAT4 Transform::WorldRotationQuaternion() const noexcept
    {
        XMVECTOR scale{};
        XMVECTOR rotation{};
        XMVECTOR translation{};
        XMFLOAT4 result{ 0.0f, 0.0f, 0.0f, 1.0f };
        if (XMMatrixDecompose(&scale, &rotation, &translation, WorldMatrix()))
        {
            XMStoreFloat4(&result, rotation);
        }
        return result;
    }

    XMFLOAT3 Transform::WorldScale() const noexcept
    {
        XMVECTOR scale{};
        XMVECTOR rotation{};
        XMVECTOR translation{};
        XMFLOAT3 result{ 1.0f, 1.0f, 1.0f };
        if (XMMatrixDecompose(&scale, &rotation, &translation, WorldMatrix()))
        {
            XMStoreFloat3(&result, scale);
        }
        return result;
    }

    void Transform::SetWorldPosition(const XMFLOAT3& value) noexcept
    {
        if (parent_ == nullptr)
        {
            local_position_ = value;
            return;
        }

        const XMMATRIX parent_world = ParentWorldMatrix();
        XMVECTOR determinant{};
        const XMMATRIX inverse = XMMatrixInverse(&determinant, parent_world);
        if (XMVector4Equal(determinant, XMVectorZero()))
        {
            // 親の拡縮に 0 が含まれると逆行列が作れない。既存値を保ったまま黙って諦める。
            return;
        }

        const XMVECTOR local = XMVector3TransformCoord(XMLoadFloat3(&value), inverse);
        XMStoreFloat3(&local_position_, local);
    }

    void Transform::SetFromWorldMatrix(FXMMATRIX world) noexcept
    {
        XMMATRIX local = world;
        if (parent_ != nullptr)
        {
            XMVECTOR determinant{};
            const XMMATRIX inverse = XMMatrixInverse(&determinant, ParentWorldMatrix());
            if (XMVector4Equal(determinant, XMVectorZero())) return;
            local = world * inverse;
        }

        XMVECTOR scale{};
        XMVECTOR rotation{};
        XMVECTOR translation{};
        if (!XMMatrixDecompose(&scale, &rotation, &translation, local))
        {
            // せん断を含むなど分解できない場合は平行移動だけ拾い、姿勢は現状維持とする。
            XMFLOAT4X4 stored{};
            XMStoreFloat4x4(&stored, local);
            local_position_ = XMFLOAT3{ stored._41, stored._42, stored._43 };
            return;
        }

        XMStoreFloat3(&local_scale_, scale);
        XMStoreFloat3(&local_position_, translation);

        XMFLOAT4X4 rotation_matrix{};
        XMStoreFloat4x4(&rotation_matrix, XMMatrixRotationQuaternion(rotation));
        local_rotation_ = ExtractEulerFromRotationMatrix(rotation_matrix);
    }
}
