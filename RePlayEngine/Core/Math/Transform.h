#pragma once

#include <DirectXMath.h>

namespace ReplayEngine::Core
{
    // GameObject の位置・回転・拡縮。
    //
    // 所有権について:
    //   Transform の実体は GameObject が「値メンバとして 1 つだけ」持つ。
    //   TransformComponent はこの Transform を編集するためのビューであり、
    //   独自の座標データを持たない。これにより二重所有を構造的に防いでいる。
    //
    // 回転の表現について:
    //   ローカル回転はオイラー角 (ラジアン, XYZ = Pitch/Yaw/Roll) を正とする。
    //   既存の Player / Stage / TransformGizmo / 旧 TransformData がすべてオイラー角前提で
    //   書かれているため、クォータニオンへ切り替えるとギズモまで作り直しになる。
    //   ワールド回転はクォータニオンで取り出せるようにしてあるので、
    //   将来ローカル側をクォータニオン化する余地は残している。
    //
    // ワールド行列について:
    //   キャッシュせず、親をたどって毎回合成する。
    //   ダーティ伝播のバグ（親を付け替えた・親の親が動いた・削除予約中に読んだ 等）を
    //   構造的に発生させないことを優先した。階層の深さは実用上わずかなので費用は小さい。
    class Transform final
    {
    public:
        Transform() = default;

        Transform(const Transform&) = default;
        Transform& operator=(const Transform&) = default;
        Transform(Transform&&) noexcept = default;
        Transform& operator=(Transform&&) noexcept = default;

        // ---- ローカル ----------------------------------------------------

        const DirectX::XMFLOAT3& LocalPosition() const noexcept { return local_position_; }
        const DirectX::XMFLOAT3& LocalRotationEuler() const noexcept { return local_rotation_; }
        const DirectX::XMFLOAT3& LocalScale() const noexcept { return local_scale_; }

        void SetLocalPosition(const DirectX::XMFLOAT3& value) noexcept { local_position_ = value; }
        void SetLocalRotationEuler(const DirectX::XMFLOAT3& value) noexcept { local_rotation_ = value; }
        void SetLocalScale(const DirectX::XMFLOAT3& value) noexcept { local_scale_ = value; }

        void SetLocal(const DirectX::XMFLOAT3& position,
            const DirectX::XMFLOAT3& rotation,
            const DirectX::XMFLOAT3& scale) noexcept
        {
            local_position_ = position;
            local_rotation_ = rotation;
            local_scale_ = scale;
        }

        // S * R * T の順で合成する。既存 Player / Stage / 旧 TransformComponent と同じ順序。
        DirectX::XMMATRIX LocalMatrix() const noexcept;

        // ---- ワールド ----------------------------------------------------

        DirectX::XMMATRIX WorldMatrix() const noexcept;
        DirectX::XMFLOAT4X4 WorldMatrixFloat4x4() const noexcept;

        DirectX::XMFLOAT3 WorldPosition() const noexcept;
        DirectX::XMFLOAT4 WorldRotationQuaternion() const noexcept;
        DirectX::XMFLOAT3 WorldScale() const noexcept;

        // 親を考慮してローカル値を逆算する。
        void SetWorldPosition(const DirectX::XMFLOAT3& value) noexcept;

        // 親付け替え時にワールド姿勢を維持するために使う。
        // 与えられたワールド行列から親を打ち消したローカル値を復元する。
        // 拡縮に負の値や強いせん断が含まれる場合は完全には復元できないため、
        // 分解できたぶんだけを反映する。
        void SetFromWorldMatrix(DirectX::FXMMATRIX world) noexcept;

        // ---- 親 ----------------------------------------------------------
        // 親の設定は GameObject が行う。利用側から直接呼ばないこと。

        const Transform* Parent() const noexcept { return parent_; }

        // 親をたどったワールド行列。親が無ければ単位行列。
        DirectX::XMMATRIX ParentWorldMatrix() const noexcept;

        void Reset() noexcept
        {
            local_position_ = { 0.0f, 0.0f, 0.0f };
            local_rotation_ = { 0.0f, 0.0f, 0.0f };
            local_scale_ = { 1.0f, 1.0f, 1.0f };
        }

    private:
        friend class GameObject;

        // GameObject だけが親を張り替えられる。
        void SetParentInternal(const Transform* parent) noexcept { parent_ = parent; }

        DirectX::XMFLOAT3 local_position_{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 local_rotation_{ 0.0f, 0.0f, 0.0f };   // ラジアン
        DirectX::XMFLOAT3 local_scale_{ 1.0f, 1.0f, 1.0f };

        // 非所有参照。GameObject が親子関係を張り替えるたびに更新する。
        // 親 GameObject は Scene が unique_ptr で保持しており、要素追加でアドレスが動かないため安全。
        const Transform* parent_ = nullptr;
    };
}
