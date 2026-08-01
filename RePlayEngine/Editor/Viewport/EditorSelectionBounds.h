#pragma once

#include "../../Core/ObjectID/ObjectID.h"

#include <DirectXMath.h>

#include <vector>

namespace ReplayEngine::Scene { class Scene; }
namespace ReplayEngine::Core { class GameObject; }

namespace ReplayEngine::Editor
{
    // 選択中の GameObject を画面へ収めるための World Bounds を求める。
    //
    // 【取得の優先順位】
    //   1. Collider の World Bounds（ColliderComponent::ComputeWorldBounds）
    //   2. 子 GameObject も含めた結合 Bounds
    //   3. どれも無ければ Transform のワールド位置
    //
    //   Renderer の World Bounds は現時点では使えない。
    //   メッシュ実体 (skinned_mesh / static_mesh) は Source 側にあり、
    //   RePlayEngine から参照すると依存方向が逆転するため。
    //   Collider を持つ対象なら 1 で正確に収まり、持たない対象でも
    //   3 のフォールバックで「近づきすぎず遠すぎない」位置になる。
    struct WorldBounds
    {
        DirectX::XMFLOAT3 minimum{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 maximum{ 0.0f, 0.0f, 0.0f };

        // 実体のある範囲が 1 つでも取れたか。
        bool valid = false;

        void Encapsulate(const DirectX::XMFLOAT3& point) noexcept;
        void Encapsulate(const DirectX::XMFLOAT3& other_minimum,
            const DirectX::XMFLOAT3& other_maximum) noexcept;
        void Encapsulate(const WorldBounds& other) noexcept;

        DirectX::XMFLOAT3 Center() const noexcept;
    };

    class EditorSelectionBounds final
    {
    public:
        EditorSelectionBounds() = delete;

        // 1 体ぶん（子孫を含む）。
        static WorldBounds Compute(const Core::GameObject& object);

        // 複数選択。すべてを含む結合 Bounds を返す。
        // 対象が 1 つも見つからなければ valid = false。
        static WorldBounds Compute(const Scene::Scene& scene,
            const std::vector<Core::ObjectID>& selection);
    };
}
