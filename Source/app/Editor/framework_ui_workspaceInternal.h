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
#include "../../RePlayEngine/Components/UI/UIShapeComponent.h"
#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Scene/Runtime/Scene.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace framework_ui_workspace_detail
{
    using ReplayEngine::Components::CanvasComponent;
    using ReplayEngine::Components::RectTransformComponent;
    using ReplayEngine::Components::UIImageComponent;
    using ReplayEngine::Components::UITextComponent;
    using ReplayEngine::Components::UIButtonComponent;
    using ReplayEngine::Components::UIMaskComponent;
    using ReplayEngine::Components::UIShapeComponent;
    namespace Core = ReplayEngine::Core;
    namespace Scene = ReplayEngine::Scene;

    inline bool HasUIComponent(const Core::GameObject& object)
    {
        return object.GetComponent<CanvasComponent>() != nullptr ||
            object.GetComponent<RectTransformComponent>() != nullptr ||
            object.GetComponent<UIImageComponent>() != nullptr ||
            object.GetComponent<UITextComponent>() != nullptr ||
            object.GetComponent<UIButtonComponent>() != nullptr ||
            object.GetComponent<UIMaskComponent>() != nullptr ||
            object.GetComponent<UIShapeComponent>() != nullptr;
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

    // 図形マスクの子 Image を選択していても、移動・拡縮は組になったマスク全体へ適用する。
    inline Core::GameObject* UITransformEditTarget(Core::GameObject* object) noexcept
    {
        if (object == nullptr) return nullptr;
        for (Core::GameObject* parent = object->Parent(); parent != nullptr;
            parent = parent->Parent())
        {
            const UIMaskComponent* mask = parent->GetComponent<UIMaskComponent>();
            if (mask != nullptr && mask->mask_mode == UIMaskComponent::Shape)
                return parent;
        }
        return object;
    }

    inline void ScaleUIShapeImageDescendants(Core::GameObject& object,
        const DirectX::XMFLOAT2& ratio)
    {
        for (Core::GameObject* child : object.Children())
        {
            if (child == nullptr) continue;
            if (RectTransformComponent* rect = child->GetComponent<RectTransformComponent>())
            {
                rect->anchored_position.x *= ratio.x;
                rect->anchored_position.y *= ratio.y;
                rect->size_delta.x *= ratio.x;
                rect->size_delta.y *= ratio.y;
            }
            ScaleUIShapeImageDescendants(*child, ratio);
        }
    }

    inline bool IsUIShapeMaskObject(const Core::GameObject* object) noexcept
    {
        const UIMaskComponent* mask = object != nullptr
            ? object->GetComponent<UIMaskComponent>() : nullptr;
        return mask != nullptr && mask->mask_mode == UIMaskComponent::Shape;
    }

    // Scene View と Canvas Preview で同じ Rect Tool の規則を使う。
    // 順番は左下から時計回りで、辺の中央を交互に挟む。
    enum UIResizeHandle : int
    {
        ResizeBottomLeft = 0,
        ResizeBottom = 1,
        ResizeBottomRight = 2,
        ResizeRight = 3,
        ResizeTopRight = 4,
        ResizeTop = 5,
        ResizeTopLeft = 6,
        ResizeLeft = 7,
    };

    inline ImVec2 ResizeMidpoint(const ImVec2& a, const ImVec2& b) noexcept
    {
        return ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    }

    inline void ResizeHandlePoints(const ImVec2 corners[4], ImVec2 out[8]) noexcept
    {
        out[ResizeBottomLeft] = corners[0];
        out[ResizeBottom] = ResizeMidpoint(corners[0], corners[1]);
        out[ResizeBottomRight] = corners[1];
        out[ResizeRight] = ResizeMidpoint(corners[1], corners[2]);
        out[ResizeTopRight] = corners[2];
        out[ResizeTop] = ResizeMidpoint(corners[2], corners[3]);
        out[ResizeTopLeft] = corners[3];
        out[ResizeLeft] = ResizeMidpoint(corners[3], corners[0]);
    }

    inline int HitResizeHandle(const ImVec2 points[8], const ImVec2& mouse,
        float radius = 8.0f) noexcept
    {
        const float radius_squared = radius * radius;
        for (int index = 0; index < 8; ++index)
        {
            const float dx = mouse.x - points[index].x;
            const float dy = mouse.y - points[index].y;
            if (dx * dx + dy * dy <= radius_squared) return index;
        }
        return -1;
    }

    inline float PointToSegmentDistanceSquared(const ImVec2& point,
        const ImVec2& start, const ImVec2& end) noexcept
    {
        const float segment_x = end.x - start.x;
        const float segment_y = end.y - start.y;
        const float length_squared = segment_x * segment_x + segment_y * segment_y;
        if (length_squared <= 0.0001f)
        {
            const float dx = point.x - start.x;
            const float dy = point.y - start.y;
            return dx * dx + dy * dy;
        }
        const float projection = ((point.x - start.x) * segment_x +
            (point.y - start.y) * segment_y) / length_squared;
        const float t = (std::max)(0.0f, (std::min)(projection, 1.0f));
        const float closest_x = start.x + segment_x * t;
        const float closest_y = start.y + segment_y * t;
        const float dx = point.x - closest_x;
        const float dy = point.y - closest_y;
        return dx * dx + dy * dy;
    }

    // 点だけでなく枠線の近くもリサイズとして拾う。
    // 小さい図形や高DPI環境でも、細いハンドルを厳密に狙わず操作できる。
    inline int HitResizeBorder(const ImVec2 corners[4], const ImVec2& mouse,
        float handle_radius = 14.0f, float edge_radius = 9.0f) noexcept
    {
        ImVec2 handles[8]{};
        ResizeHandlePoints(corners, handles);
        const int point_handle = HitResizeHandle(handles, mouse, handle_radius);
        if (point_handle >= 0) return point_handle;

        const int edge_handles[4]{ ResizeBottom, ResizeRight, ResizeTop, ResizeLeft };
        const int edge_starts[4]{ 0, 1, 2, 3 };
        const int edge_ends[4]{ 1, 2, 3, 0 };
        const float limit = edge_radius * edge_radius;
        int nearest = -1;
        float nearest_distance = limit;
        for (int index = 0; index < 4; ++index)
        {
            const float distance = PointToSegmentDistanceSquared(mouse,
                corners[edge_starts[index]], corners[edge_ends[index]]);
            if (distance <= nearest_distance)
            {
                nearest_distance = distance;
                nearest = edge_handles[index];
            }
        }
        return nearest;
    }

    inline bool InverseTransformPoint(const DirectX::XMFLOAT4X4& matrix,
        const DirectX::XMFLOAT2& point, DirectX::XMFLOAT2& output) noexcept
    {
        const DirectX::XMMATRIX source = DirectX::XMLoadFloat4x4(&matrix);
        DirectX::XMVECTOR determinant{};
        const DirectX::XMMATRIX inverse = DirectX::XMMatrixInverse(&determinant, source);
        const float determinant_value = DirectX::XMVectorGetX(determinant);
        if (!std::isfinite(determinant_value) ||
            std::fabs(determinant_value) < 1.0e-8f)
            return false;

        const DirectX::XMVECTOR transformed = DirectX::XMVector3TransformCoord(
            DirectX::XMVectorSet(point.x, point.y, 0.0f, 1.0f), inverse);
        const float x = DirectX::XMVectorGetX(transformed);
        const float y = DirectX::XMVectorGetY(transformed);
        if (!std::isfinite(x) || !std::isfinite(y)) return false;
        output = { x, y };
        return true;
    }

    inline DirectX::XMFLOAT4 ParentResolvedRect(const Core::GameObject& object,
        float canvas_width, float canvas_height) noexcept
    {
        const Core::GameObject* parent = object.Parent();
        const RectTransformComponent* parent_rect = parent != nullptr
            ? parent->GetComponent<RectTransformComponent>() : nullptr;
        if (parent_rect != nullptr) return parent_rect->ResolvedRect();
        return { 0.0f, 0.0f, canvas_width, canvas_height };
    }

    inline void ApplyResolvedRect(RectTransformComponent& rect,
        const DirectX::XMFLOAT4& parent, const DirectX::XMFLOAT4& desired) noexcept
    {
        const float width = (std::max)(1.0f, desired.z);
        const float height = (std::max)(1.0f, desired.w);
        const float anchor_span_x = parent.z * (rect.anchor_max.x - rect.anchor_min.x);
        const float anchor_span_y = parent.w * (rect.anchor_max.y - rect.anchor_min.y);
        const float anchor_min_x = parent.x + parent.z * rect.anchor_min.x;
        const float anchor_min_y = parent.y + parent.w * rect.anchor_min.y;

        rect.size_delta.x = width - anchor_span_x;
        rect.size_delta.y = height - anchor_span_y;
        rect.anchored_position.x = desired.x - anchor_min_x +
            rect.size_delta.x * rect.pivot.x;
        rect.anchored_position.y = desired.y - anchor_min_y +
            rect.size_delta.y * rect.pivot.y;
    }

    inline void ResizeDirections(int handle, int& horizontal, int& vertical) noexcept
    {
        horizontal = 0;
        vertical = 0;
        if (handle == ResizeBottomLeft || handle == ResizeLeft || handle == ResizeTopLeft)
            horizontal = -1;
        else if (handle == ResizeBottomRight || handle == ResizeRight ||
            handle == ResizeTopRight)
            horizontal = 1;
        if (handle == ResizeBottomLeft || handle == ResizeBottom ||
            handle == ResizeBottomRight)
            vertical = -1;
        else if (handle == ResizeTopLeft || handle == ResizeTop ||
            handle == ResizeTopRight)
            vertical = 1;
    }

    inline DirectX::XMFLOAT4 ResizedRectFromHandle(const DirectX::XMFLOAT4& start,
        int handle, const DirectX::XMFLOAT2& local_mouse, bool keep_aspect,
        bool from_center) noexcept
    {
        float left = start.x;
        float bottom = start.y;
        float right = start.x + start.z;
        float top = start.y + start.w;
        int horizontal = 0;
        int vertical = 0;
        ResizeDirections(handle, horizontal, vertical);

        if (horizontal < 0)
        {
            if (from_center)
            {
                const float delta = local_mouse.x - start.x;
                left = local_mouse.x;
                right = start.x + start.z - delta;
            }
            else left = local_mouse.x;
        }
        else if (horizontal > 0)
        {
            if (from_center)
            {
                const float delta = local_mouse.x - (start.x + start.z);
                right = local_mouse.x;
                left = start.x - delta;
            }
            else right = local_mouse.x;
        }

        if (vertical < 0)
        {
            if (from_center)
            {
                const float delta = local_mouse.y - start.y;
                bottom = local_mouse.y;
                top = start.y + start.w - delta;
            }
            else bottom = local_mouse.y;
        }
        else if (vertical > 0)
        {
            if (from_center)
            {
                const float delta = local_mouse.y - (start.y + start.w);
                top = local_mouse.y;
                bottom = start.y - delta;
            }
            else top = local_mouse.y;
        }

        constexpr float minimum_size = 1.0f;
        if (right - left < minimum_size)
        {
            if (horizontal < 0) left = right - minimum_size;
            else if (horizontal > 0) right = left + minimum_size;
        }
        if (top - bottom < minimum_size)
        {
            if (vertical < 0) bottom = top - minimum_size;
            else if (vertical > 0) top = bottom + minimum_size;
        }

        if (keep_aspect && horizontal != 0 && vertical != 0 &&
            start.z > minimum_size && start.w > minimum_size)
        {
            const float aspect = start.z / start.w;
            float width = right - left;
            float height = top - bottom;
            const float width_change = std::fabs(width - start.z) / start.z;
            const float height_change = std::fabs(height - start.w) / start.w;
            if (width_change >= height_change)
            {
                height = (std::max)(minimum_size, width / aspect);
                if (from_center)
                {
                    const float center = start.y + start.w * 0.5f;
                    bottom = center - height * 0.5f;
                    top = center + height * 0.5f;
                }
                else if (vertical < 0) bottom = top - height;
                else top = bottom + height;
            }
            else
            {
                width = (std::max)(minimum_size, height * aspect);
                if (from_center)
                {
                    const float center = start.x + start.z * 0.5f;
                    left = center - width * 0.5f;
                    right = center + width * 0.5f;
                }
                else if (horizontal < 0) left = right - width;
                else right = left + width;
            }
        }

        return { left, bottom, right - left, top - bottom };
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
