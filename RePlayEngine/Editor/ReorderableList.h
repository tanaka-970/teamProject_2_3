#pragma once

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include <cstddef>
#include <string>
#include <cstdint>

namespace ReplayEngine::Editor
{
    inline constexpr std::size_t invalid_reorder_index = static_cast<std::size_t>(-1);

    struct ReorderRequest final
    {
        std::size_t source = invalid_reorder_index;
        std::size_t destination = invalid_reorder_index;

        bool Valid() const noexcept
        {
            return source != invalid_reorder_index &&
                destination != invalid_reorder_index && source != destination;
        }
    };

    struct ReorderableItemResult final
    {
        ReorderRequest request{};
        bool opened = false;
        bool dragging = false;
        bool clicked = false;
        bool hovered = false;
    };

    struct ReorderPayload final
    {
        std::uintptr_t list = 0;
        std::size_t index = 0;
    };

    inline std::size_t ReorderDestination(std::size_t source, std::size_t target,
        bool after, std::size_t count) noexcept
    {
        if (source >= count || target >= count || source == target) return source;
        if (after) return source < target ? target : target + 1;
        return source < target ? target - 1 : target;
    }

    // ドラッグ中に「何を動かしているか」を一覧の外へ出すための控え。
    inline std::string g_active_reorder_label;
    inline const void* g_active_reorder_list = nullptr;
    inline std::size_t g_active_reorder_index = static_cast<std::size_t>(-1);

    inline const char* ActiveReorderLabel(const void* list_identity)
    {
        return g_active_reorder_list == list_identity && !g_active_reorder_label.empty()
            ? g_active_reorder_label.c_str() : nullptr;
    }

    inline bool IsReorderDragging(const void* list_identity, std::size_t index)
    {
        return g_active_reorder_list == list_identity && g_active_reorder_index == index;
    }

    namespace Detail
    {
        inline void DrawDisabledSmallButton(const char* label, bool enabled,
            bool& pressed)
        {
            if (!enabled)
            {
                ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
                    ImGui::GetStyle().Alpha * 0.45f);
            }
            pressed = ImGui::SmallButton(label);
            if (!enabled)
            {
                ImGui::PopStyleVar();
                ImGui::PopItemFlag();
            }
        }
    }

    template<class ContextMenuFn>
    ReorderableItemResult DrawReorderableItem(const void* list_identity,
        const char* item_id, std::size_t index, std::size_t count,
        const char* title, bool selected, bool default_open, bool editable,
        ContextMenuFn&& draw_context_menu)
    {
        ReorderableItemResult result{};
        if (list_identity == nullptr || item_id == nullptr || title == nullptr)
            return result;

        ImGui::PushID(item_id);
        // 行ごと掴めるように、見出しと順序ボタンをひとまとまりにする。
        ImGui::BeginGroup();
        const bool handle_enabled = editable && count > 1;
        if (!handle_enabled)
        {
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
                ImGui::GetStyle().Alpha * 0.45f);
        }
        ImGui::SmallButton("◆");
        if (!handle_enabled)
        {
            ImGui::PopStyleVar();
            ImGui::PopItemFlag();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("この行を掴んでドラッグ、またはボタンで順序を変更");
        if (handle_enabled && ImGui::BeginDragDropSource(
            ImGuiDragDropFlags_SourceAllowNullID))
        {
            const ReorderPayload payload{
                reinterpret_cast<std::uintptr_t>(list_identity), index };
            ImGui::SetDragDropPayload("REPLAY_REORDERABLE_ITEM", &payload,
                sizeof(payload));
            ImGui::Text("移動: %s", title);
            ImGui::EndDragDropSource();
            g_active_reorder_label = title;
            g_active_reorder_list = list_identity;
            g_active_reorder_index = index;
        }
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextDisabled("%zu", index + 1);
        ImGui::SameLine(0.0f, 6.0f);

        // 掴んでいないフレームでは控えを捨てる。「移動中」表示が残らないように。
        if (ImGui::GetDragDropPayload() == nullptr)
        {
            g_active_reorder_list = nullptr;
            g_active_reorder_index = static_cast<std::size_t>(-1);
            g_active_reorder_label.clear();
        }
        const bool dragging_this = IsReorderDragging(list_identity, index);
        if (dragging_this)
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImGui::GetStyle().Colors[ImGuiCol_DragDropTarget]);
        ImGuiTreeNodeFlags header_flags = ImGuiTreeNodeFlags_AllowItemOverlap;
        if (selected) header_flags |= ImGuiTreeNodeFlags_Selected;
        if (default_open) header_flags |= ImGuiTreeNodeFlags_DefaultOpen;
        const std::string header_title = dragging_this
            ? (std::string("▶ ") + title + "  … 移動中") : std::string(title);
        result.opened = ImGui::CollapsingHeader(header_title.c_str(), header_flags);
        if (dragging_this) ImGui::PopStyleColor();
        // 見出しそのものを掴めるようにする。小さな印だけを掴ませない。
        if (editable && ImGui::BeginDragDropSource(
            ImGuiDragDropFlags_SourceAllowNullID))
        {
            const ReorderPayload payload{
                reinterpret_cast<std::uintptr_t>(list_identity), index };
            ImGui::SetDragDropPayload("REPLAY_REORDERABLE_ITEM", &payload,
                sizeof(payload));
            ImGui::Text("移動: %s", title);
            ImGui::EndDragDropSource();
            g_active_reorder_label = title;
            g_active_reorder_list = list_identity;
            g_active_reorder_index = index;
        }
        result.clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        result.hovered = ImGui::IsItemHovered();


        if (ImGui::BeginPopupContextItem("##ReorderContext"))
        {
            if (ImGui::MenuItem("先頭へ", nullptr, false,
                editable && index > 0) && !result.request.Valid())
                result.request = { index, 0 };
            if (ImGui::MenuItem("上へ", nullptr, false,
                editable && index > 0) && !result.request.Valid())
                result.request = { index, index - 1 };
            if (ImGui::MenuItem("下へ", nullptr, false,
                editable && index + 1 < count) && !result.request.Valid())
                result.request = { index, index + 1 };
            if (ImGui::MenuItem("末尾へ", nullptr, false,
                editable && index + 1 < count) && !result.request.Valid())
                result.request = { index, count - 1 };
            ImGui::Separator();
            draw_context_menu();
            ImGui::EndPopup();
        }

        if (count > 1)
        {
            ImGui::Indent();
            ImGui::TextDisabled("順序");
            ImGui::SameLine();
            bool pressed = false;
            Detail::DrawDisabledSmallButton("先頭", editable && index > 0, pressed);
            if (pressed && editable && index > 0)
            {
                result.request = { index, 0 };
            }
            ImGui::SameLine();
            Detail::DrawDisabledSmallButton("↑", editable && index > 0, pressed);
            if (pressed && editable && index > 0 && !result.request.Valid())
            {
                result.request = { index, index - 1 };
            }
            ImGui::SameLine();
            Detail::DrawDisabledSmallButton("↓", editable && index + 1 < count, pressed);
            if (pressed && editable && index + 1 < count && !result.request.Valid())
            {
                result.request = { index, index + 1 };
            }
            ImGui::SameLine();
            Detail::DrawDisabledSmallButton("末尾", editable && index + 1 < count, pressed);
            if (pressed && editable && index + 1 < count && !result.request.Valid())
            {
                result.request = { index, count - 1 };
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("ボタンまたは右クリックでも順序を変更できます");
            ImGui::Unindent();
        }

        ImGui::EndGroup();
        // 掴めることをカーソルで示す。触れば動かせると分かるように。
        if (editable && count > 1 && ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        // 判定は行全体。見出しだけでなく順序ボタンの帯まで落とせる。
        const ImVec2 item_min = ImGui::GetItemRectMin();
        const ImVec2 item_max = ImGui::GetItemRectMax();
        if (ImGui::BeginDragDropTarget())
        {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
                "REPLAY_REORDERABLE_ITEM", ImGuiDragDropFlags_AcceptBeforeDelivery);
            if (payload != nullptr && payload->Data != nullptr &&
                payload->DataSize == static_cast<int>(sizeof(ReorderPayload)))
            {
                const ReorderPayload dragged =
                    *static_cast<const ReorderPayload*>(payload->Data);
                if (dragged.list == reinterpret_cast<std::uintptr_t>(list_identity) &&
                    dragged.index < count)
                {
                    const bool after = ImGui::GetMousePos().y >
                        (item_min.y + item_max.y) * 0.5f;
                    const std::size_t destination = ReorderDestination(
                        dragged.index, index, after, count);
                    if (destination != dragged.index)
                    {
                        result.dragging = true;
                        const float line_y = after ? item_max.y : item_min.y;
                        ImGui::GetWindowDrawList()->AddLine(
                            ImVec2(item_min.x, line_y), ImVec2(item_max.x, line_y),
                            ImGui::GetColorU32(ImGuiCol_DragDropTarget), 4.0f);
                        if (payload->IsDelivery())
                        {
                            result.request.source = dragged.index;
                            result.request.destination = destination;
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::PopID();
        return result;
    }
}
