#pragma once

#include "../../Object/Component/Component.h"
#include "../../Object/GameObject/GameObject.h"

namespace ReplayEngine::Components
{
    // GameObject の Transform を Inspector から編集するためのビュー。
    //
    // 重要: このクラスは座標データを一切持たない。
    //   実体は GameObject が値メンバとして所有する Core::Transform ただ 1 つ。
    //   ここに別の座標を持たせると二重所有になり、どちらが正か分からなくなる。
    //   すべてのアクセサは Owner()->GetTransform() へ委譲する。
    //
    // 登録上の扱い:
    //   built_in        … GameObject 生成時に自動で付く
    //   removable=false … Inspector から削除できない
    //   serializable=false
    //                   … Transform は GameObject 側の情報として Scene へ保存される。
    //                     ここでも保存すると同じ値が 2 箇所に出るため保存対象から外す。
    //
    // Inspector には度数法で見せ、内部はラジアンで保持する。
    // 既存の Player / Stage / TransformGizmo がラジアンのオイラー角で書かれているため、
    // 内部表現はそちらに合わせている。
    class TransformComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(TransformComponent)

    public:
        TransformComponent() = default;

        // 所有 GameObject が無い場合に備え、参照できないときは静的なダミーを返す。
        // Inspector 描画中に GameObject が消えても即座に落ちないようにするための保険。
        Core::Transform& Target() noexcept;
        const Core::Transform& Target() const noexcept;

        DirectX::XMFLOAT3 Position() const noexcept { return Target().LocalPosition(); }
        void SetPosition(const DirectX::XMFLOAT3& value) noexcept { Target().SetLocalPosition(value); }

        // 度数法。Inspector 表示用。
        DirectX::XMFLOAT3 RotationDegrees() const noexcept;
        void SetRotationDegrees(const DirectX::XMFLOAT3& value) noexcept;

        DirectX::XMFLOAT3 Scale() const noexcept { return Target().LocalScale(); }
        void SetScale(const DirectX::XMFLOAT3& value) noexcept { Target().SetLocalScale(value); }
    };
}
