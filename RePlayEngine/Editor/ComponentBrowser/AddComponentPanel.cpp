#include "AddComponentPanel.h"

#include "../Core/EditorContext.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Object/Registry/ComponentRegistry.h"

#include "imgui/imgui.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

namespace ReplayEngine::Editor
{
    using Core::ComponentRegistry;
    using Core::ComponentTypeInfo;

    namespace
    {
        constexpr const char* popup_id = "RePlayAddComponentPopup";

        std::string ToLower(const std::string& text)
        {
            std::string lowered = text;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                [](unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });
            return lowered;
        }

        // 検索は型名と表示名の両方に対して部分一致で行う。
        bool Matches(const ComponentTypeInfo& info, const std::string& lowered_query)
        {
            if (lowered_query.empty()) return true;
            return ToLower(info.type_name).find(lowered_query) != std::string::npos ||
                ToLower(info.DisplayName()).find(lowered_query) != std::string::npos;
        }
    }

    void AddComponentPanel::Close() noexcept
    {
        open_requested_ = false;
        search_text_[0] = '\0';
    }

    bool AddComponentPanel::Draw(EditorContext& context, Core::GameObject& target)
    {
        if (open_requested_)
        {
            open_requested_ = false;
            focus_search_ = true;
            search_text_[0] = '\0';
            ImGui::OpenPopup(popup_id);
        }

        if (!ImGui::BeginPopup(popup_id)) return false;

        ImGui::TextDisabled("コンポーネントを追加");
        ImGui::Separator();

        if (focus_search_)
        {
            ImGui::SetKeyboardFocusHere();
            focus_search_ = false;
        }
        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputTextWithHint("##AddComponentSearch", "Search Components...",
            search_text_, search_buffer_size);

        const std::string query = ToLower(std::string(search_text_));

        bool added = false;
        for (const std::string& category : ComponentRegistry::Categories())
        {
            // このカテゴリに表示すべき型があるかを先に数える。
            // 空のカテゴリ見出しだけが残らないようにするため。
            int visible_in_category = 0;
            for (const ComponentTypeInfo& info : ComponentRegistry::All())
            {
                if (!info.editor_visible) continue;
                if (info.category != category) continue;
                if (!Matches(info, query)) continue;
                ++visible_in_category;
            }
            if (visible_in_category == 0) continue;

            ImGui::TextDisabled("%s", category.c_str());
            ImGui::Separator();

            for (const ComponentTypeInfo& info : ComponentRegistry::All())
            {
                if (!info.editor_visible) continue;
                if (info.category != category) continue;
                if (!Matches(info, query)) continue;

                // 重複禁止の型が既に付いている場合は追加させない。
                // 押せてしまってから失敗するのではなく、押せないことを見た目で示す。
                const bool already_present =
                    !info.allow_multiple && target.FindComponent(info.type_id) != nullptr;

                ImGui::PushID(info.type_name.c_str());
                if (already_present)
                {
                    ImGui::TextDisabled("  %s  (追加済み)", info.DisplayName().c_str());
                }
                else if (ImGui::Selectable(("  " + info.DisplayName()).c_str()))
                {
                    context.BeginEdit(info.DisplayName() + " を追加");
                    if (target.AddComponent(info.type_id) != nullptr)
                    {
                        context.CommitEdit();
                        context.SetStatus(info.DisplayName() + " を追加しました");
                        added = true;
                    }
                    else
                    {
                        context.CancelEdit();
                        context.SetStatus(info.DisplayName() + " を追加できませんでした");
                    }
                    ImGui::CloseCurrentPopup();
                }
                if (!info.tooltip.empty() && ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(info.tooltip.c_str());
                    ImGui::EndTooltip();
                }
                ImGui::PopID();

                if (added) break;
            }
            if (added) break;
            ImGui::Spacing();
        }

        if (query.empty() && ComponentRegistry::All().empty())
        {
            ImGui::TextDisabled("登録された Component がありません");
        }

        ImGui::EndPopup();
        return added;
    }
}
