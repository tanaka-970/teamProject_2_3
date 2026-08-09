#include "framework.h"

#include "../../RePlayEngine/Components/UI/CanvasComponent.h"
#include "../../RePlayEngine/Components/UI/RectTransformComponent.h"
#include "../../RePlayEngine/Components/UI/UIImageComponent.h"
#include "../../RePlayEngine/Components/UI/UITextComponent.h"
#include "../../RePlayEngine/Components/UI/UIButtonComponent.h"
#include "../../RePlayEngine/Components/UI/UIMaskComponent.h"
#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Scene/Runtime/Scene.h"
#include "../../RePlayEngine/UI/UILayout.h"

// PushItemFlag / ImGuiItemFlags_Disabled を使うため。
// 同梱の ImGui は 1.80 WIP で BeginDisabled / EndDisabled がまだ無い。
// 既存の InspectorPanel.cpp / PropertyDrawer.cpp と同じ取り込み方に合わせる。
#include "imgui/imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace
{
    // 同梱の ImGui (1.80 WIP) には BeginDisabled / EndDisabled がまだ無い。
    // 操作を実際に止める必要がある場所はこちらを使う。
    // 見た目だけ淡くしたい場合は PushStyleVar(Alpha) だけで足りる。
    void BeginDisabledCompat()
    {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
    }

    void EndDisabledCompat()
    {
        ImGui::PopStyleVar();
        ImGui::PopItemFlag();
    }

    using ReplayEngine::Components::CanvasComponent;
    using ReplayEngine::Components::RectTransformComponent;
    using ReplayEngine::Components::UIImageComponent;
    using ReplayEngine::Components::UITextComponent;
    using ReplayEngine::Components::UIButtonComponent;
    using ReplayEngine::Components::UIMaskComponent;
    namespace Core = ReplayEngine::Core;
    namespace Scene = ReplayEngine::Scene;

    enum class UIElementKind
    {
        Canvas,
        Image,
        Text,
        Button,
        Mask,
    };

    bool HasUIComponent(const Core::GameObject& object)
    {
        return object.GetComponent<CanvasComponent>() != nullptr ||
            object.GetComponent<RectTransformComponent>() != nullptr ||
            object.GetComponent<UIImageComponent>() != nullptr ||
            object.GetComponent<UITextComponent>() != nullptr ||
            object.GetComponent<UIButtonComponent>() != nullptr ||
            object.GetComponent<UIMaskComponent>() != nullptr;
    }

    bool ContainsUI(const Core::GameObject& object)
    {
        if (HasUIComponent(object)) return true;
        for (const Core::GameObject* child : object.Children())
        {
            if (child != nullptr && ContainsUI(*child)) return true;
        }
        return false;
    }

    Core::GameObject* FindFirstCanvas(Scene::Scene& scene)
    {
        for (std::size_t index = 0; index < scene.GameObjectCount(); ++index)
        {
            Core::GameObject* object = scene.GameObjectAt(index);
            if (object != nullptr && !object->PendingDestroy() &&
                object->GetComponent<CanvasComponent>() != nullptr)
                return object;
        }
        return nullptr;
    }

    std::vector<Core::GameObject*> SortedCanvases(Scene::Scene& scene)
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

    Core::GameObject* SelectedUIParent(ReplayEngine::Editor::EditorContext& context)
    {
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr) return nullptr;
        Core::GameObject* selected =
            context.Selection().ResolvePrimary(*scene);
        if (selected != nullptr && ContainsUI(*selected)) return selected;
        return FindFirstCanvas(*scene);
    }

    Core::GameObject* CreateCanvasObject(Scene::Scene& scene)
    {
        Core::GameObject* canvas = scene.CreateGameObject("Canvas");
        if (canvas == nullptr) return nullptr;
        canvas->AddComponent<CanvasComponent>();
        RectTransformComponent* rect = canvas->GetComponent<RectTransformComponent>();
        if (rect != nullptr)
        {
            rect->anchor_min = { 0.0f, 0.0f };
            rect->anchor_max = { 1.0f, 1.0f };
            rect->anchored_position = { 0.0f, 0.0f };
            rect->size_delta = { 0.0f, 0.0f };
            rect->pivot = { 0.5f, 0.5f };
        }
        return canvas;
    }

    Core::GameObject* CreateUIElement(ReplayEngine::Editor::EditorContext& context,
        UIElementKind kind)
    {
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr || !context.CanEdit()) return nullptr;

        context.BeginEdit("UI 要素を作成");
        Core::GameObject* parent = nullptr;
        Core::GameObject* created = nullptr;

        if (kind == UIElementKind::Canvas)
        {
            created = CreateCanvasObject(*scene);
        }
        else
        {
            parent = SelectedUIParent(context);
            if (parent == nullptr) parent = CreateCanvasObject(*scene);
            const char* name = kind == UIElementKind::Text ? "Text" :
                kind == UIElementKind::Button ? "Button" :
                kind == UIElementKind::Mask ? "Mask" : "Image";
            created = scene->CreateGameObject(name);
            if (created != nullptr)
            {
                created->SetParent(parent, false);
                RectTransformComponent* rect = created->AddComponent<RectTransformComponent>();
                if (rect != nullptr)
                {
                    rect->size_delta = kind == UIElementKind::Button
                        ? DirectX::XMFLOAT2{ 180.0f, 52.0f }
                        : DirectX::XMFLOAT2{ 160.0f, 80.0f };
                }

                if (kind == UIElementKind::Image)
                {
                    created->AddComponent<UIImageComponent>();
                }
                else if (kind == UIElementKind::Text)
                {
                    UITextComponent* text = created->AddComponent<UITextComponent>();
                    if (text != nullptr) text->text = "Text";
                }
                else if (kind == UIElementKind::Button)
                {
                    created->AddComponent<UIImageComponent>();
                    created->AddComponent<UIButtonComponent>();

                    Core::GameObject* label = scene->CreateGameObject("Button Text");
                    if (label != nullptr)
                    {
                        label->SetParent(created, false);
                        RectTransformComponent* label_rect =
                            label->AddComponent<RectTransformComponent>();
                        if (label_rect != nullptr)
                        {
                            label_rect->anchor_min = { 0.0f, 0.0f };
                            label_rect->anchor_max = { 1.0f, 1.0f };
                            label_rect->size_delta = { 0.0f, 0.0f };
                        }
                        UITextComponent* text = label->AddComponent<UITextComponent>();
                        if (text != nullptr)
                        {
                            text->text = "Button";
                            text->font_size = 24.0f;
                        }
                    }
                }
                else if (kind == UIElementKind::Mask)
                {
                    UIImageComponent* image = created->AddComponent<UIImageComponent>();
                    if (image != nullptr)
                        image->color = { 0.2f, 0.45f, 0.8f, 0.18f };
                    created->AddComponent<UIMaskComponent>();
                }
            }
        }

        if (created != nullptr)
            context.Selection().Select(created->ID(), false);
        context.CommitEdit();
        return created;
    }

    DirectX::XMFLOAT2 TransformPoint(const DirectX::XMFLOAT4X4& matrix,
        float x, float y)
    {
        const DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(&matrix);
        const DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(
            DirectX::XMVectorSet(x, y, 0.0f, 1.0f), m);
        return { DirectX::XMVectorGetX(p), DirectX::XMVectorGetY(p) };
    }

    ImVec2 ToPreviewPoint(const DirectX::XMFLOAT2& canvas_point,
        const ImVec2& origin, float canvas_height, float zoom)
    {
        return ImVec2(origin.x + canvas_point.x * zoom,
            origin.y + (canvas_height - canvas_point.y) * zoom);
    }

    ImU32 ColorToU32(const DirectX::XMFLOAT4& color, float fallback_alpha = 1.0f)
    {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(
            color.x, color.y, color.z, color.w * fallback_alpha));
    }

    void PreviewQuad(const RectTransformComponent& rect, const ImVec2& origin,
        float canvas_height, float zoom, ImVec2 out[4])
    {
        const DirectX::XMFLOAT4 r = rect.ResolvedRect();
        const DirectX::XMFLOAT4X4 m = rect.ResolvedMatrix();
        out[0] = ToPreviewPoint(TransformPoint(m, r.x, r.y), origin, canvas_height, zoom);
        out[1] = ToPreviewPoint(TransformPoint(m, r.x + r.z, r.y), origin, canvas_height, zoom);
        out[2] = ToPreviewPoint(TransformPoint(m, r.x + r.z, r.y + r.w), origin, canvas_height, zoom);
        out[3] = ToPreviewPoint(TransformPoint(m, r.x, r.y + r.w), origin, canvas_height, zoom);
    }

    bool RectHit(const RectTransformComponent& rect, float x, float y)
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

    Core::GameObject* PickUIObject(Core::GameObject& object, float x, float y)
    {
        std::vector<Core::GameObject*> children = object.Children();
        for (auto it = children.rbegin(); it != children.rend(); ++it)
        {
            if (*it == nullptr) continue;
            if (Core::GameObject* picked = PickUIObject(**it, x, y))
                return picked;
        }

        const RectTransformComponent* rect = object.GetComponent<RectTransformComponent>();
        if (rect != nullptr && HasUIComponent(object) && RectHit(*rect, x, y))
            return &object;
        return nullptr;
    }

    void DrawPreviewObject(ImDrawList* draw_list, Core::GameObject& object,
        const ImVec2& origin, float canvas_height, float zoom,
        Core::ObjectID selected)
    {
        RectTransformComponent* rect = object.GetComponent<RectTransformComponent>();
        if (rect != nullptr && HasUIComponent(object))
        {
            ImVec2 p[4]{};
            PreviewQuad(*rect, origin, canvas_height, zoom, p);

            if (const UIImageComponent* image = object.GetComponent<UIImageComponent>())
                draw_list->AddQuadFilled(p[0], p[1], p[2], p[3], ColorToU32(image->color, image->opacity));
            else if (object.GetComponent<CanvasComponent>() == nullptr)
                draw_list->AddQuadFilled(p[0], p[1], p[2], p[3],
                    ImGui::ColorConvertFloat4ToU32(ImVec4(0.18f, 0.20f, 0.22f, 0.18f)));

            const bool selected_object = object.ID() == selected;
            const bool mask_object = object.GetComponent<UIMaskComponent>() != nullptr;
            const bool button_object = object.GetComponent<UIButtonComponent>() != nullptr;
            const ImU32 outline = selected_object
                ? IM_COL32(255, 210, 80, 255)
                : (mask_object ? IM_COL32(90, 170, 255, 220)
                    : (button_object ? IM_COL32(120, 210, 170, 200)
                        : IM_COL32(190, 195, 205, 125)));
            draw_list->AddQuad(p[0], p[1], p[2], p[3], outline, selected_object ? 2.0f : 1.0f);

            if (const UITextComponent* text = object.GetComponent<UITextComponent>())
            {
                const DirectX::XMFLOAT4 r = rect->ResolvedRect();
                const ImVec2 top_left = ToPreviewPoint(
                    { r.x, r.y + r.w }, origin, canvas_height, zoom);
                draw_list->AddText(top_left, ColorToU32(text->color, text->opacity),
                    text->text.c_str());
            }
        }

        for (Core::GameObject* child : object.Children())
        {
            if (child != nullptr) DrawPreviewObject(draw_list, *child,
                origin, canvas_height, zoom, selected);
        }
    }

    void DrawUINode(ReplayEngine::Editor::EditorContext& context,
        Core::GameObject& object)
    {
        if (!ContainsUI(object)) return;

        const bool selected = context.Selection().IsSelected(object.ID());
        bool has_ui_child = false;
        for (const Core::GameObject* child : object.Children())
        {
            if (child != nullptr && ContainsUI(*child))
            {
                has_ui_child = true;
                break;
            }
        }

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_SpanAvailWidth;
        if (selected) flags |= ImGuiTreeNodeFlags_Selected;
        if (!has_ui_child) flags |= ImGuiTreeNodeFlags_Leaf;

        std::string label = object.Name() + "##UI" + object.ID().ToString();
        const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            context.Selection().Select(object.ID(), false);
        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("Image を追加")) CreateUIElement(context, UIElementKind::Image);
            if (ImGui::MenuItem("Text を追加")) CreateUIElement(context, UIElementKind::Text);
            if (ImGui::MenuItem("Button を追加")) CreateUIElement(context, UIElementKind::Button);
            if (ImGui::MenuItem("Mask を追加")) CreateUIElement(context, UIElementKind::Mask);
            ImGui::EndPopup();
        }

        if (open)
        {
            for (Core::GameObject* child : object.Children())
            {
                if (child != nullptr) DrawUINode(context, *child);
            }
            ImGui::TreePop();
        }
    }
}

void framework::draw_ui_hierarchy()
{
    if (!show_ui_hierarchy_panel) return;
    if (!ImGui::Begin("UI 階層", &show_ui_hierarchy_panel))
    {
        ImGui::End();
        return;
    }

    Scene::Scene* scene = object_editor_context.GetScene();
    const bool can_edit = object_editor_context.CanEdit();
    if (!can_edit) BeginDisabledCompat();
    if (ImGui::Button("Canvas")) CreateUIElement(object_editor_context, UIElementKind::Canvas);
    ImGui::SameLine();
    if (ImGui::Button("Image")) CreateUIElement(object_editor_context, UIElementKind::Image);
    ImGui::SameLine();
    if (ImGui::Button("Text")) CreateUIElement(object_editor_context, UIElementKind::Text);
    ImGui::SameLine();
    if (ImGui::Button("Button")) CreateUIElement(object_editor_context, UIElementKind::Button);
    ImGui::SameLine();
    if (ImGui::Button("Mask")) CreateUIElement(object_editor_context, UIElementKind::Mask);
    if (!can_edit) EndDisabledCompat();

    ImGui::Separator();
    if (scene == nullptr)
    {
        ImGui::TextDisabled("Scene がありません");
        ImGui::End();
        return;
    }

    bool any_canvas = false;
    for (Core::GameObject* root : scene->RootGameObjects())
    {
        if (root == nullptr || !ContainsUI(*root)) continue;
        any_canvas = true;
        DrawUINode(object_editor_context, *root);
    }
    if (!any_canvas)
        ImGui::TextDisabled("Canvas はまだありません");

    ImGui::End();
}

void framework::draw_ui_preview()
{
    if (!show_ui_preview_panel) return;
    if (!ImGui::Begin("Canvas プレビュー", &show_ui_preview_panel))
    {
        ImGui::End();
        return;
    }

    Scene::Scene* scene = object_editor_context.GetScene();
    if (scene == nullptr)
    {
        ImGui::TextDisabled("Scene がありません");
        ImGui::End();
        return;
    }

    const char* resolutions[] = { "1920 x 1080", "1280 x 720", "1080 x 1920", "Custom" };
    ImGui::SetNextItemWidth(140.0f);
    ImGui::Combo("##UIResolution", &ui_preview_resolution_index,
        resolutions, IM_ARRAYSIZE(resolutions));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::SliderFloat("Zoom", &ui_preview_zoom, 0.10f, 2.0f, "%.2f");
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &ui_preview_grid);

    int preview_width = 1920;
    int preview_height = 1080;
    if (ui_preview_resolution_index == 1) { preview_width = 1280; preview_height = 720; }
    else if (ui_preview_resolution_index == 2) { preview_width = 1080; preview_height = 1920; }
    else if (ui_preview_resolution_index == 3)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        ImGui::InputInt("W", &ui_preview_custom_width, 0, 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        ImGui::InputInt("H", &ui_preview_custom_height, 0, 0);
        preview_width = (std::max)(1, ui_preview_custom_width);
        preview_height = (std::max)(1, ui_preview_custom_height);
    }

    ReplayEngine::UI::UILayout::Resolve(*scene,
        static_cast<float>(preview_width), static_cast<float>(preview_height));

    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.x = (std::max)(avail.x, 64.0f);
    avail.y = (std::max)(avail.y, 64.0f);
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##CanvasPreviewSurface", avail);
    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();
    if (hovered && ImGui::GetIO().MouseWheel != 0.0f)
    {
        ui_preview_zoom = (std::min)(2.0f, (std::max)(0.1f,
            ui_preview_zoom + ImGui::GetIO().MouseWheel * 0.05f));
    }

    const ImVec2 canvas_size(
        static_cast<float>(preview_width) * ui_preview_zoom,
        static_cast<float>(preview_height) * ui_preview_zoom);
    ImVec2 origin(
        cursor.x + (avail.x - canvas_size.x) * 0.5f + ui_preview_pan.x,
        cursor.y + (avail.y - canvas_size.y) * 0.5f + ui_preview_pan.y);

    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right))
    {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        ui_preview_pan.x += delta.x;
        ui_preview_pan.y += delta.y;
        origin.x += delta.x;
        origin.y += delta.y;
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float canvas_mouse_x = (mouse.x - origin.x) / ui_preview_zoom;
    const float canvas_mouse_y = static_cast<float>(preview_height) -
        (mouse.y - origin.y) / ui_preview_zoom;

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        Core::GameObject* picked = nullptr;
        std::vector<Core::GameObject*> canvases = SortedCanvases(*scene);
        for (auto it = canvases.rbegin(); it != canvases.rend() && picked == nullptr; ++it)
        {
            if (*it != nullptr) picked = PickUIObject(**it, canvas_mouse_x, canvas_mouse_y);
        }
        if (picked != nullptr)
        {
            object_editor_context.Selection().Select(picked->ID(), false);
            selected_editor_object = editor_selection::game_object;
            if (RectTransformComponent* rect = picked->GetComponent<RectTransformComponent>())
            {
                ui_preview_drag_object = picked->ID();
                ui_preview_drag_start_mouse = mouse;
                ui_preview_drag_start_position = rect->anchored_position;
            }
        }
    }

    Core::GameObject* selected =
        object_editor_context.Selection().ResolvePrimary(*scene);
    RectTransformComponent* selected_rect = selected != nullptr
        ? selected->GetComponent<RectTransformComponent>() : nullptr;
    if (active && selected_rect != nullptr &&
        ui_preview_drag_object == selected->ID() &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
    {
        if (!ui_preview_dragging && object_editor_context.CanEdit())
        {
            object_editor_context.BeginEdit("UI 要素を移動");
            ui_preview_dragging = true;
        }
        if (ui_preview_dragging)
        {
            const ImVec2 delta(mouse.x - ui_preview_drag_start_mouse.x,
                mouse.y - ui_preview_drag_start_mouse.y);
            selected_rect->anchored_position = {
                ui_preview_drag_start_position.x + delta.x / ui_preview_zoom,
                ui_preview_drag_start_position.y - delta.y / ui_preview_zoom
            };
            ReplayEngine::UI::UILayout::Resolve(*scene,
                static_cast<float>(preview_width), static_cast<float>(preview_height));
        }
    }
    if (ui_preview_dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        object_editor_context.CommitEdit();
        ui_preview_dragging = false;
        ui_preview_drag_object = Core::ObjectID::Invalid();
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 clip_min = cursor;
    const ImVec2 clip_max(cursor.x + avail.x, cursor.y + avail.y);
    draw_list->PushClipRect(clip_min, clip_max, true);
    draw_list->AddRectFilled(clip_min, clip_max, IM_COL32(23, 26, 30, 255));
    draw_list->AddRectFilled(origin, ImVec2(origin.x + canvas_size.x, origin.y + canvas_size.y),
        IM_COL32(36, 38, 42, 255));

    if (ui_preview_grid && ui_preview_grid_size > 1.0f)
    {
        const float step = ui_preview_grid_size * ui_preview_zoom;
        for (float x = origin.x; x <= origin.x + canvas_size.x; x += step)
            draw_list->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + canvas_size.y),
                IM_COL32(255, 255, 255, 24));
        for (float y = origin.y; y <= origin.y + canvas_size.y; y += step)
            draw_list->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + canvas_size.x, y),
                IM_COL32(255, 255, 255, 24));
    }

    for (Core::GameObject* canvas : SortedCanvases(*scene))
    {
        if (canvas != nullptr)
            DrawPreviewObject(draw_list, *canvas, origin,
                static_cast<float>(preview_height), ui_preview_zoom,
                object_editor_context.Selection().Primary());
    }
    draw_list->AddRect(origin, ImVec2(origin.x + canvas_size.x, origin.y + canvas_size.y),
        IM_COL32(230, 230, 235, 180), 0.0f, 0, 1.5f);
    draw_list->PopClipRect();

    ImGui::End();
}

void framework::draw_ui_inspector()
{
    if (!show_ui_inspector_panel) return;
    if (!ImGui::Begin("UI インスペクター", &show_ui_inspector_panel))
    {
        ImGui::End();
        return;
    }

    Scene::Scene* scene = object_editor_context.GetScene();
    Core::GameObject* selected = scene != nullptr
        ? object_editor_context.Selection().ResolvePrimary(*scene) : nullptr;
    if (selected == nullptr || !HasUIComponent(*selected))
    {
        ImGui::TextDisabled("UI 要素を選択してください");
        if (ImGui::Button("Canvas を作成"))
            CreateUIElement(object_editor_context, UIElementKind::Canvas);
        ImGui::End();
        return;
    }

    if (RectTransformComponent* rect = selected->GetComponent<RectTransformComponent>())
    {
        ImGui::TextDisabled("アンカー");
        const auto preset = [&](const char* label,
            DirectX::XMFLOAT2 min_anchor, DirectX::XMFLOAT2 max_anchor,
            DirectX::XMFLOAT2 pivot)
        {
            if (ImGui::Button(label) && object_editor_context.CanEdit())
            {
                object_editor_context.BeginEdit("アンカーを変更");
                rect->anchor_min = min_anchor;
                rect->anchor_max = max_anchor;
                rect->pivot = pivot;
                object_editor_context.CommitEdit();
            }
        };
        preset("左上", { 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 1.0f });
        ImGui::SameLine();
        preset("中央", { 0.5f, 0.5f }, { 0.5f, 0.5f }, { 0.5f, 0.5f });
        ImGui::SameLine();
        preset("全体", { 0.0f, 0.0f }, { 1.0f, 1.0f }, { 0.5f, 0.5f });
        ImGui::Separator();
    }

    object_inspector_panel.DrawContents(object_editor_context);
    ImGui::Separator();
    // Motion Workspace への導線。UI 側の編集状態は変えず、
    // Motion Asset の作成と Workspace 切り替えだけを担当する。
    if (ImGui::Button("Motion を作成"))
    {
        if (!motion_editor_loaded)
            project_create_motion("UIMotion");
        set_editor_workspace(editor_workspace::motion);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("選択中の UI 要素を Motion Workspace で編集します。");

    ImGui::End();
}
