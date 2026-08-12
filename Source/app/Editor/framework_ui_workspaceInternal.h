#pragma once

// UI workspace の分割実装だけが共有する内部ヘルパ。
// 外部の Editor コードから include するものではない。

#include "framework.h"
#include "../../RePlayEngine/Components/UI/CanvasComponent.h"
#include "../../RePlayEngine/Components/UI/RectTransformComponent.h"
#include "../../RePlayEngine/Components/UI/UIImageComponent.h"
#include "../../RePlayEngine/Components/UI/UITextComponent.h"
#include "../../RePlayEngine/Components/UI/UIButtonComponent.h"
#include "../../RePlayEngine/Components/UI/UIMaskComponent.h"
#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Scene/Runtime/Scene.h"

#include <algorithm>
#include <vector>

namespace framework_ui_workspace_detail
{
    using ReplayEngine::Components::CanvasComponent;
    using ReplayEngine::Components::RectTransformComponent;
    using ReplayEngine::Components::UIImageComponent;
    using ReplayEngine::Components::UITextComponent;
    using ReplayEngine::Components::UIButtonComponent;
    using ReplayEngine::Components::UIMaskComponent;
    namespace Core = ReplayEngine::Core;
    namespace Scene = ReplayEngine::Scene;

    inline bool HasUIComponent(const Core::GameObject& object)
    {
        return object.GetComponent<CanvasComponent>() != nullptr ||
            object.GetComponent<RectTransformComponent>() != nullptr ||
            object.GetComponent<UIImageComponent>() != nullptr ||
            object.GetComponent<UITextComponent>() != nullptr ||
            object.GetComponent<UIButtonComponent>() != nullptr ||
            object.GetComponent<UIMaskComponent>() != nullptr;
    }

    inline bool ContainsUI(const Core::GameObject& object)
    {
        if (HasUIComponent(object)) return true;
        for (const Core::GameObject* child : object.Children())
        {
            if (child != nullptr && ContainsUI(*child)) return true;
        }
        return false;
    }

    inline std::vector<Core::GameObject*> SortedCanvases(Scene::Scene& scene)
    {
        std::vector<Core::GameObject*> canvases;
        for (std::size_t index = 0; index < scene.GameObjectCount(); ++index)
        {
            Core::GameObject* object = scene.GameObjectAt(index);
            if (object != nullptr && !object->PendingDestroy() &&
                object->GetComponent<CanvasComponent>() != nullptr)
                canvases.push_back(object);
        }
        std::stable_sort(canvases.begin(), canvases.end(),
            [](const Core::GameObject* lhs, const Core::GameObject* rhs)
            {
                const CanvasComponent* a = lhs != nullptr
                    ? lhs->GetComponent<CanvasComponent>() : nullptr;
                const CanvasComponent* b = rhs != nullptr
                    ? rhs->GetComponent<CanvasComponent>() : nullptr;
                return (a != nullptr ? a->sort_order : 0) <
                    (b != nullptr ? b->sort_order : 0);
            });
        return canvases;
    }

    inline DirectX::XMFLOAT2 TransformPoint(const DirectX::XMFLOAT4X4& matrix,
        float x, float y)
    {
        const DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(&matrix);
        const DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(
            DirectX::XMVectorSet(x, y, 0.0f, 1.0f), m);
        return { DirectX::XMVectorGetX(p), DirectX::XMVectorGetY(p) };
    }

    inline bool RectHit(const RectTransformComponent& rect, float x, float y)
    {
        const DirectX::XMFLOAT4 r = rect.ResolvedRect();
        const DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(&rect.ResolvedMatrix());
        DirectX::XMVECTOR determinant{};
        const DirectX::XMMATRIX inverse = DirectX::XMMatrixInverse(&determinant, m);
        const DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(
            DirectX::XMVectorSet(x, y, 0.0f, 1.0f), inverse);
        const float lx = DirectX::XMVectorGetX(p);
        const float ly = DirectX::XMVectorGetY(p);
        return lx >= r.x && lx <= r.x + r.z && ly >= r.y && ly <= r.y + r.w;
    }
}
