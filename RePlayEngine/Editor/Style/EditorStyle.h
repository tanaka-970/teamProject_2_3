#pragma once

#include "imgui/imgui.h"

#include <string>
#include <unordered_map>

namespace ReplayEngine::Editor
{
    // Editor 全体で共有する Design Token。個々の Window は色や余白を
    // ハードコードせず、この表を ImGuiStyle へ適用した結果を使う。
    struct EditorStyleTokens
    {
        ImVec4 main_background;
        ImVec4 panel_background;
        ImVec4 header_background;
        ImVec4 toolbar_background;
        ImVec4 border;
        ImVec4 text;
        ImVec4 secondary_text;
        ImVec4 disabled_text;
        ImVec4 accent;
        ImVec4 selection;
        ImVec4 hover;
        ImVec4 active;
        ImVec4 success;
        ImVec4 warning;
        ImVec4 error;
        ImVec4 missing;
        ImVec4 prefab;
        ImVec4 normal_collider;
        ImVec4 trigger_collider;
        ImVec4 primary_collider;

        float padding = 8.0f;
        float item_spacing = 6.0f;
        float panel_spacing = 8.0f;
        float header_height = 24.0f;
        float toolbar_height = 30.0f;
        float input_height = 24.0f;
        float border_thickness = 1.0f;
        float corner_radius = 3.0f;
        float font_size = 15.0f;
    };

    class EditorStyle final
    {
    public:
        EditorStyle() = delete;

        static const EditorStyleTokens& Tokens() noexcept;
        static void Apply(float dpi_scale = 1.0f);
        static ImVec4 ComponentCategoryColor(const std::string& category) noexcept;
        static const std::unordered_map<std::string, ImVec4>& ComponentCategoryColors() noexcept;
        static void SetComponentCategoryColor(const std::string& category, const ImVec4& color);
        static void ReplaceComponentCategoryColors(
            const std::unordered_map<std::string, ImVec4>& colors);
        static void ResetComponentCategoryColors() noexcept;
    };
}
