#include "EditorStyle.h"

#include <algorithm>

namespace ReplayEngine::Editor
{
    const EditorStyleTokens& EditorStyle::Tokens() noexcept
    {
        static const EditorStyleTokens tokens{
            ImVec4(0.045f, 0.050f, 0.060f, 1.0f), // main_background
            ImVec4(0.070f, 0.078f, 0.092f, 1.0f), // panel_background
            ImVec4(0.090f, 0.102f, 0.122f, 1.0f), // header_background
            ImVec4(0.055f, 0.064f, 0.078f, 1.0f), // toolbar_background
            ImVec4(0.150f, 0.170f, 0.200f, 1.0f), // border
            ImVec4(0.900f, 0.920f, 0.950f, 1.0f), // text
            ImVec4(0.650f, 0.690f, 0.740f, 1.0f), // secondary_text
            ImVec4(0.420f, 0.450f, 0.490f, 1.0f), // disabled_text
            ImVec4(0.100f, 0.530f, 0.900f, 1.0f), // accent
            ImVec4(0.090f, 0.360f, 0.640f, 1.0f), // selection
            ImVec4(0.125f, 0.290f, 0.450f, 1.0f), // hover
            ImVec4(0.090f, 0.430f, 0.760f, 1.0f), // active
            ImVec4(0.250f, 0.780f, 0.430f, 1.0f), // success
            ImVec4(0.950f, 0.670f, 0.180f, 1.0f), // warning
            ImVec4(0.920f, 0.260f, 0.230f, 1.0f), // error
            ImVec4(1.000f, 0.180f, 0.240f, 1.0f), // missing
            ImVec4(0.250f, 0.580f, 0.950f, 1.0f), // prefab
            ImVec4(0.250f, 0.820f, 0.420f, 1.0f), // normal_collider
            ImVec4(0.180f, 0.780f, 0.860f, 1.0f), // trigger_collider
            ImVec4(1.000f, 0.520f, 0.160f, 1.0f)  // primary_collider
        };
        return tokens;
    }

    void EditorStyle::Apply(float dpi_scale)
    {
        const EditorStyleTokens& token = Tokens();
        const float scale = (std::max)(dpi_scale, 0.5f);

        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowPadding = ImVec2(token.padding * scale, token.padding * scale);
        style.FramePadding = ImVec2(7.0f * scale, 4.0f * scale);
        style.ItemSpacing = ImVec2(token.panel_spacing * scale, token.item_spacing * scale);
        style.ItemInnerSpacing = ImVec2(5.0f * scale, 4.0f * scale);
        style.ScrollbarSize = 13.0f * scale;
        style.GrabMinSize = 10.0f * scale;
        style.WindowBorderSize = token.border_thickness * scale;
        style.ChildBorderSize = token.border_thickness * scale;
        style.PopupBorderSize = token.border_thickness * scale;
        style.FrameBorderSize = 0.0f;
        style.WindowRounding = token.corner_radius * scale;
        style.ChildRounding = token.corner_radius * scale;
        style.FrameRounding = token.corner_radius * scale;
        style.PopupRounding = token.corner_radius * scale;
        style.ScrollbarRounding = token.corner_radius * scale;
        style.GrabRounding = token.corner_radius * scale;
        style.TabRounding = token.corner_radius * scale;

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text] = token.text;
        colors[ImGuiCol_TextDisabled] = token.disabled_text;
        colors[ImGuiCol_WindowBg] = token.panel_background;
        colors[ImGuiCol_ChildBg] = token.panel_background;
        colors[ImGuiCol_PopupBg] = token.header_background;
        colors[ImGuiCol_Border] = token.border;
        colors[ImGuiCol_FrameBg] = token.header_background;
        colors[ImGuiCol_FrameBgHovered] = token.hover;
        colors[ImGuiCol_FrameBgActive] = token.active;
        colors[ImGuiCol_TitleBg] = token.toolbar_background;
        colors[ImGuiCol_TitleBgActive] = token.header_background;
        colors[ImGuiCol_MenuBarBg] = token.toolbar_background;
        colors[ImGuiCol_ScrollbarBg] = token.main_background;
        colors[ImGuiCol_ScrollbarGrab] = token.header_background;
        colors[ImGuiCol_CheckMark] = token.accent;
        colors[ImGuiCol_SliderGrab] = token.accent;
        colors[ImGuiCol_SliderGrabActive] = token.active;
        colors[ImGuiCol_Button] = token.header_background;
        colors[ImGuiCol_ButtonHovered] = token.hover;
        colors[ImGuiCol_ButtonActive] = token.active;
        colors[ImGuiCol_Header] = token.selection;
        colors[ImGuiCol_HeaderHovered] = token.hover;
        colors[ImGuiCol_HeaderActive] = token.active;
        colors[ImGuiCol_Separator] = token.border;
        colors[ImGuiCol_SeparatorHovered] = token.accent;
        colors[ImGuiCol_SeparatorActive] = token.active;
        colors[ImGuiCol_ResizeGrip] = ImVec4(token.accent.x, token.accent.y, token.accent.z, 0.20f);
        colors[ImGuiCol_ResizeGripHovered] = token.hover;
        colors[ImGuiCol_ResizeGripActive] = token.active;
        colors[ImGuiCol_Tab] = token.header_background;
        colors[ImGuiCol_TabHovered] = token.hover;
        colors[ImGuiCol_TabActive] = token.selection;
        colors[ImGuiCol_DockingPreview] = ImVec4(token.accent.x, token.accent.y, token.accent.z, 0.70f);
        colors[ImGuiCol_DockingEmptyBg] = token.main_background;
        colors[ImGuiCol_NavHighlight] = token.accent;
    }
}
