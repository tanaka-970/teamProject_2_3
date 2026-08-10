#include "UILayout.h"

#include "../Components/UI/CanvasComponent.h"
#include "../Components/UI/RectTransformComponent.h"
#include "../Components/UI/UIButtonComponent.h"
#include "../Components/UI/UIImageComponent.h"
#include "../Components/Motion/MotionPlayerComponent.h"
#include "../Object/GameObject/GameObject.h"
#include "../Scene/Runtime/Scene.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ReplayEngine::UI
{
    namespace
    {
        using Components::CanvasComponent;
        using Components::MotionPlayerComponent;
        using Components::RectTransformComponent;
        using Components::UIButtonComponent;
        using Components::UIImageComponent;

        constexpr int maximum_ui_depth = 64;

        bool NearlyEqual(float a, float b) noexcept
        {
            return std::fabs(a - b) <= 0.0001f;
        }

        DirectX::XMFLOAT4 ResolveRect(const RectTransformComponent& rect,
            const DirectX::XMFLOAT4& parent)
        {
            const float parent_min_x = parent.x;
            const float parent_min_y = parent.y;
            const float parent_w = parent.z;
            const float parent_h = parent.w;

            const float ax0 = parent_min_x + parent_w * rect.anchor_min.x;
            const float ay0 = parent_min_y + parent_h * rect.anchor_min.y;
            const float ax1 = parent_min_x + parent_w * rect.anchor_max.x;
            const float ay1 = parent_min_y + parent_h * rect.anchor_max.y;

            float min_x = 0.0f;
            float min_y = 0.0f;
            float size_x = 0.0f;
            float size_y = 0.0f;

            if (NearlyEqual(rect.anchor_min.x, rect.anchor_max.x))
            {
                size_x = rect.size_delta.x;
                const float center = ax0 + rect.anchored_position.x;
                min_x = center - size_x * rect.pivot.x;
            }
            else
            {
                min_x = ax0 + rect.anchored_position.x - rect.size_delta.x * rect.pivot.x;
                const float max_x = ax1 + rect.anchored_position.x +
                    rect.size_delta.x * (1.0f - rect.pivot.x);
                size_x = max_x - min_x;
            }

            if (NearlyEqual(rect.anchor_min.y, rect.anchor_max.y))
            {
                size_y = rect.size_delta.y;
                const float center = ay0 + rect.anchored_position.y;
                min_y = center - size_y * rect.pivot.y;
            }
            else
            {
                min_y = ay0 + rect.anchored_position.y - rect.size_delta.y * rect.pivot.y;
                const float max_y = ay1 + rect.anchored_position.y +
                    rect.size_delta.y * (1.0f - rect.pivot.y);
                size_y = max_y - min_y;
            }

            return { min_x, min_y, size_x, size_y };
        }

        DirectX::XMFLOAT4X4 ResolveMatrix(const RectTransformComponent& rect,
            const DirectX::XMFLOAT4& resolved)
        {
            const float pivot_x = resolved.x + resolved.z * rect.pivot.x;
            const float pivot_y = resolved.y + resolved.w * rect.pivot.y;

            const DirectX::XMMATRIX matrix =
                DirectX::XMMatrixTranslation(-pivot_x, -pivot_y, 0.0f) *
                DirectX::XMMatrixScaling(rect.scale.x, rect.scale.y, 1.0f) *
                DirectX::XMMatrixRotationZ(DirectX::XMConvertToRadians(rect.rotation)) *
                DirectX::XMMatrixTranslation(pivot_x, pivot_y, 0.0f);

            DirectX::XMFLOAT4X4 stored{};
            DirectX::XMStoreFloat4x4(&stored, matrix);
            return stored;
        }

        void ResolveChildren(Core::GameObject& object,
            const DirectX::XMFLOAT4& parent_rect, int depth)
        {
            if (depth > maximum_ui_depth || object.PendingDestroy()) return;

            DirectX::XMFLOAT4 current_parent = parent_rect;
            if (RectTransformComponent* rect = object.GetComponent<RectTransformComponent>())
            {
                const DirectX::XMFLOAT4 resolved = ResolveRect(*rect, parent_rect);
                rect->SetResolvedRect(resolved);
                rect->SetResolvedMatrix(ResolveMatrix(*rect, resolved));
                current_parent = resolved;
            }

            const std::vector<Core::GameObject*> children = object.Children();
            for (Core::GameObject* child : children)
            {
                if (child == nullptr || child->PendingDestroy()) continue;
                ResolveChildren(*child, current_parent, depth + 1);
            }
        }

        void GatherCanvases(Scene::Scene& scene, std::vector<Core::GameObject*>& canvases)
        {
            canvases.clear();
            for (std::size_t index = 0; index < scene.GameObjectCount(); ++index)
            {
                Core::GameObject* object = scene.GameObjectAt(index);
                if (object == nullptr || object->PendingDestroy()) continue;
                if (object->GetComponent<CanvasComponent>() != nullptr) canvases.push_back(object);
            }
            std::stable_sort(canvases.begin(), canvases.end(),
                [](const Core::GameObject* lhs, const Core::GameObject* rhs)
                {
                    const CanvasComponent* a = lhs != nullptr
                        ? lhs->GetComponent<CanvasComponent>() : nullptr;
                    const CanvasComponent* b = rhs != nullptr
                        ? rhs->GetComponent<CanvasComponent>() : nullptr;
                    const int ao = a != nullptr ? a->sort_order : 0;
                    const int bo = b != nullptr ? b->sort_order : 0;
                    return ao < bo;
                });
        }

        bool HitTest(const RectTransformComponent& rect, float x, float y) noexcept
        {
            const DirectX::XMFLOAT4 resolved = rect.ResolvedRect();
            const DirectX::XMMATRIX matrix = DirectX::XMLoadFloat4x4(&rect.ResolvedMatrix());
            DirectX::XMVECTOR determinant{};
            const DirectX::XMMATRIX inverse = DirectX::XMMatrixInverse(&determinant, matrix);
            const DirectX::XMVECTOR point = DirectX::XMVectorSet(x, y, 0.0f, 1.0f);
            const DirectX::XMVECTOR local = DirectX::XMVector2Transform(point, inverse);
            const float lx = DirectX::XMVectorGetX(local);
            const float ly = DirectX::XMVectorGetY(local);
            return lx >= resolved.x && lx <= resolved.x + resolved.z &&
                ly >= resolved.y && ly <= resolved.y + resolved.w;
        }

        UIImageComponent* ResolveTargetImage(UIButtonComponent& button)
        {
            Core::GameObject* owner = button.Owner();
            if (owner == nullptr) return nullptr;

            if (button.target_image.IsAssigned())
            {
                if (Scene::Scene* scene = owner->GetScene())
                {
                    if (Core::GameObject* target_owner =
                        scene->FindGameObjectByID(button.target_image.owner))
                    {
                        if (Core::Component* component =
                            target_owner->FindComponentByStableID(button.target_image.component))
                        {
                            if (auto* image = dynamic_cast<UIImageComponent*>(component))
                                return image;
                        }
                    }
                }
            }
            return owner->GetComponent<UIImageComponent>();
        }

        void ApplyButtonVisual(UIButtonComponent& button)
        {
            UIImageComponent* image = ResolveTargetImage(button);
            if (image == nullptr) return;

            switch (button.state)
            {
            case UIButtonComponent::Hover:    image->color = button.hover_color; break;
            case UIButtonComponent::Pressed:  image->color = button.pressed_color; break;
            case UIButtonComponent::Disabled: image->color = button.disabled_color; break;
            default:                          image->color = button.normal_color; break;
            }
        }

        const Reflection::AssetReference* MotionForState(
            const UIButtonComponent& button, int state) noexcept
        {
            switch (state)
            {
            case UIButtonComponent::Hover: return &button.hover_motion;
            case UIButtonComponent::Pressed: return &button.pressed_motion;
            case UIButtonComponent::Disabled: return &button.disabled_motion;
            default: return &button.normal_motion;
            }
        }

        void PlayButtonMotion(UIButtonComponent& button, int state)
        {
            const Reflection::AssetReference* motion = MotionForState(button, state);
            if (motion == nullptr || !motion->IsAssigned()) return;

            Core::GameObject* owner = button.Owner();
            if (owner == nullptr) return;

            MotionPlayerComponent* player = owner->GetComponent<MotionPlayerComponent>();
            if (player == nullptr)
            {
                player = owner->AddComponent<MotionPlayerComponent>();
            }
            if (player == nullptr) return;

            player->motion = *motion;
            player->play_on_start = true;
            player->loop = false;
            player->wrap_mode = MotionPlayerComponent::ClampForever;
            player->auto_stop_on_end = false;
            player->blend_in_seconds = (std::max)(0.0f, button.state_blend_seconds);
            player->weight = 1.0f;
            player->PlayFrom(0.0f);
        }

        void UpdateButtonsInTree(Core::GameObject& object,
            float mouse_x, float mouse_y,
            bool mouse_down, bool mouse_released,
            bool input_captured, bool play_state_motions, int depth)
        {
            if (depth > maximum_ui_depth || object.PendingDestroy()) return;

            if (auto* button = object.GetComponent<UIButtonComponent>())
            {
                int next_state = UIButtonComponent::Normal;
                if (!button->interactable || !button->ActiveInHierarchy())
                {
                    next_state = UIButtonComponent::Disabled;
                }
                else if (!input_captured)
                {
                    if (const auto* rect = object.GetComponent<RectTransformComponent>())
                    {
                        const bool hovered = HitTest(*rect, mouse_x, mouse_y);
                        if (hovered && mouse_down) next_state = UIButtonComponent::Pressed;
                        else if (hovered)          next_state = UIButtonComponent::Hover;
                    }
                }

                // Phase 1 は通知の入口だけをここへ置く。C# 連携は Phase 7。
                // release 時に Hover に戻るので、状態遷移は後段の MotionPlayer へ接続できる。
                (void)mouse_released;
                const int previous_state = button->state;
                button->state = next_state;
                ApplyButtonVisual(*button);
                if (play_state_motions && previous_state != next_state)
                {
                    PlayButtonMotion(*button, next_state);
                }
            }

            const std::vector<Core::GameObject*> children = object.Children();
            for (Core::GameObject* child : children)
            {
                if (child == nullptr) continue;
                UpdateButtonsInTree(*child, mouse_x, mouse_y, mouse_down,
                    mouse_released, input_captured, play_state_motions, depth + 1);
            }
        }
    }

    float UILayout::CanvasScale(const CanvasComponent& canvas,
        float screen_width, float screen_height) noexcept
    {
        if (canvas.scale_mode == CanvasComponent::ConstantPixelSize) return 1.0f;
        const float ref_w = canvas.reference_resolution.x > 0.0f
            ? canvas.reference_resolution.x : 1920.0f;
        const float ref_h = canvas.reference_resolution.y > 0.0f
            ? canvas.reference_resolution.y : 1080.0f;
        const float log_w = std::log2((std::max)(1.0f, screen_width) / ref_w);
        const float log_h = std::log2((std::max)(1.0f, screen_height) / ref_h);
        const float match = (std::min)((std::max)(canvas.match_width_or_height, 0.0f), 1.0f);
        return std::pow(2.0f, log_w + (log_h - log_w) * match);
    }

    void UILayout::Resolve(Scene::Scene& scene, float screen_width, float screen_height)
    {
        std::vector<Core::GameObject*> canvases;
        GatherCanvases(scene, canvases);
        for (Core::GameObject* canvas : canvases)
        {
            if (canvas != nullptr) ResolveCanvas(*canvas, screen_width, screen_height);
        }
    }

    void UILayout::ResolveCanvas(Core::GameObject& canvas_object,
        float screen_width, float screen_height)
    {
        CanvasComponent* canvas = canvas_object.GetComponent<CanvasComponent>();
        if (canvas == nullptr) return;

        const float scale = CanvasScale(*canvas, screen_width, screen_height);
        const float safe_scale = scale > 0.0001f ? scale : 1.0f;
        const DirectX::XMFLOAT4 canvas_rect{
            0.0f, 0.0f, screen_width / safe_scale, screen_height / safe_scale };

        if (RectTransformComponent* rect =
            canvas_object.GetComponent<RectTransformComponent>())
        {
            rect->SetResolvedRect(canvas_rect);
            DirectX::XMFLOAT4X4 identity{};
            DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
            rect->SetResolvedMatrix(identity);
        }

        const std::vector<Core::GameObject*> children = canvas_object.Children();
        for (Core::GameObject* child : children)
        {
            if (child == nullptr || child->PendingDestroy()) continue;
            ResolveChildren(*child, canvas_rect, 1);
        }
    }

    void UILayout::UpdateButtons(Scene::Scene& scene,
        float screen_width, float screen_height,
        float mouse_x, float mouse_y,
        bool mouse_down, bool mouse_pressed, bool mouse_released,
        bool input_captured, bool play_state_motions)
    {
        (void)mouse_pressed;

        std::vector<Core::GameObject*> canvases;
        GatherCanvases(scene, canvases);
        for (Core::GameObject* canvas_object : canvases)
        {
            if (canvas_object == nullptr) continue;
            const CanvasComponent* canvas = canvas_object->GetComponent<CanvasComponent>();
            if (canvas == nullptr) continue;

            const float scale = CanvasScale(*canvas, screen_width, screen_height);
            const float safe_scale = scale > 0.0001f ? scale : 1.0f;
            const float canvas_mouse_x = mouse_x / safe_scale;
            const float canvas_mouse_y = mouse_y / safe_scale;

            UpdateButtonsInTree(*canvas_object, canvas_mouse_x, canvas_mouse_y,
                mouse_down, mouse_released, input_captured, play_state_motions, 0);
        }
    }
}
