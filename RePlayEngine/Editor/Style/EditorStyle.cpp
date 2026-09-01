#include "EditorStyle.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <random>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace ReplayEngine::Editor
{
    namespace
    {
        ImVec4 DefaultComponentCategoryColor(const std::string& category) noexcept
        {
            if (category == "Rendering") return ImVec4(0.30f, 0.52f, 0.78f, 1.0f);
            if (category == "Lighting") return ImVec4(0.52f, 0.48f, 0.24f, 1.0f);
            if (category == "Camera") return ImVec4(0.32f, 0.48f, 0.62f, 1.0f);
            if (category == "Landscape") return ImVec4(0.30f, 0.55f, 0.40f, 1.0f);
            if (category == "Scripting") return ImVec4(0.30f, 0.62f, 0.40f, 1.0f);
            if (category == "Gameplay") return ImVec4(0.35f, 0.58f, 0.42f, 1.0f);
            if (category == "Motion") return ImVec4(0.48f, 0.42f, 0.68f, 1.0f);
            if (category == "Physics") return ImVec4(0.60f, 0.42f, 0.32f, 1.0f);
            if (category == "Navigation") return ImVec4(0.34f, 0.58f, 0.56f, 1.0f);
            if (category == "Audio") return ImVec4(0.58f, 0.40f, 0.56f, 1.0f);
            if (category == "UI") return ImVec4(0.50f, 0.42f, 0.62f, 1.0f);
            if (category == "Scene") return ImVec4(0.42f, 0.50f, 0.58f, 1.0f);
            if (category == "Core") return ImVec4(0.40f, 0.44f, 0.50f, 1.0f);
            if (category == "Editor") return ImVec4(0.45f, 0.45f, 0.45f, 1.0f);
            if (category == "Internal") return ImVec4(0.36f, 0.38f, 0.42f, 1.0f);
            return ImVec4(0.38f, 0.46f, 0.56f, 1.0f);
        }

        std::unordered_map<std::string, ImVec4>& MutableComponentCategoryColors()
        {
            static std::unordered_map<std::string, ImVec4> colors;
            return colors;
        }

        EditorStyleTokens DefaultEditorStyleTokens() noexcept
        {
            return {
                ImVec4(0.045f, 0.050f, 0.060f, 1.0f), ImVec4(0.070f, 0.078f, 0.092f, 1.0f),
                ImVec4(0.090f, 0.102f, 0.122f, 1.0f), ImVec4(0.055f, 0.064f, 0.078f, 1.0f),
                ImVec4(0.150f, 0.170f, 0.200f, 1.0f), ImVec4(0.900f, 0.920f, 0.950f, 1.0f),
                ImVec4(0.650f, 0.690f, 0.740f, 1.0f), ImVec4(0.420f, 0.450f, 0.490f, 1.0f),
                ImVec4(0.100f, 0.530f, 0.900f, 1.0f), ImVec4(0.090f, 0.360f, 0.640f, 1.0f),
                ImVec4(0.125f, 0.290f, 0.450f, 1.0f), ImVec4(0.090f, 0.430f, 0.760f, 1.0f),
                ImVec4(0.250f, 0.780f, 0.430f, 1.0f), ImVec4(0.950f, 0.670f, 0.180f, 1.0f),
                ImVec4(0.920f, 0.260f, 0.230f, 1.0f), ImVec4(1.000f, 0.180f, 0.240f, 1.0f),
                ImVec4(0.250f, 0.580f, 0.950f, 1.0f), ImVec4(0.250f, 0.820f, 0.420f, 1.0f),
                ImVec4(0.180f, 0.780f, 0.860f, 1.0f), ImVec4(1.000f, 0.520f, 0.160f, 1.0f) };
        }

        EditorStyleTokens& MutableTokens() noexcept
        {
            static EditorStyleTokens tokens = DefaultEditorStyleTokens();
            return tokens;
        }

        ImVec4 Blend(const ImVec4& left, const ImVec4& right, float amount) noexcept
        {
            return ImVec4(left.x + (right.x - left.x) * amount,
                left.y + (right.y - left.y) * amount,
                left.z + (right.z - left.z) * amount, 1.0f);
        }

        constexpr const char* style_preset_magic = "REPLAY_EDITOR_STYLE_PRESET";
        constexpr const char* style_user_magic = "REPLAY_EDITOR_STYLE_USER";

        std::string StylePresetScopeName(EditorStylePresetScope scope)
        {
            return scope == EditorStylePresetScope::Shared ? "SHARED" : "PERSONAL";
        }

        void WriteTokens(std::ofstream& stream, const EditorStyleTokens& token)
        {
            const auto color = [&stream](const char* name, const ImVec4& value)
            {
                stream << "TOKEN_COLOR " << name << ' ' << value.x << ' ' << value.y << ' ' << value.z << '\n';
            };
            const auto value = [&stream](const char* name, float number)
            {
                stream << "TOKEN_VALUE " << name << ' ' << number << '\n';
            };
            color("main_background", token.main_background); color("panel_background", token.panel_background);
            color("header_background", token.header_background); color("toolbar_background", token.toolbar_background);
            color("border", token.border); color("text", token.text); color("secondary_text", token.secondary_text);
            color("disabled_text", token.disabled_text); color("accent", token.accent); color("selection", token.selection);
            color("hover", token.hover); color("active", token.active); color("success", token.success);
            color("warning", token.warning); color("error", token.error); color("missing", token.missing);
            color("prefab", token.prefab); color("normal_collider", token.normal_collider);
            color("trigger_collider", token.trigger_collider); color("primary_collider", token.primary_collider);
            value("padding", token.padding); value("item_spacing", token.item_spacing);
            value("panel_spacing", token.panel_spacing); value("header_height", token.header_height);
            value("toolbar_height", token.toolbar_height); value("input_height", token.input_height);
            value("border_thickness", token.border_thickness); value("corner_radius", token.corner_radius);
            value("font_size", token.font_size);
        }

        bool ReadTokenColor(EditorStyleTokens& token, const std::string& name, ImVec4 value)
        {
            value.w = 1.0f;
            if (name == "main_background") token.main_background = value; else if (name == "panel_background") token.panel_background = value;
            else if (name == "header_background") token.header_background = value; else if (name == "toolbar_background") token.toolbar_background = value;
            else if (name == "border") token.border = value; else if (name == "text") token.text = value;
            else if (name == "secondary_text") token.secondary_text = value; else if (name == "disabled_text") token.disabled_text = value;
            else if (name == "accent") token.accent = value; else if (name == "selection") token.selection = value;
            else if (name == "hover") token.hover = value; else if (name == "active") token.active = value;
            else if (name == "success") token.success = value; else if (name == "warning") token.warning = value;
            else if (name == "error") token.error = value; else if (name == "missing") token.missing = value;
            else if (name == "prefab") token.prefab = value; else if (name == "normal_collider") token.normal_collider = value;
            else if (name == "trigger_collider") token.trigger_collider = value; else if (name == "primary_collider") token.primary_collider = value;
            else return false;
            return true;
        }

        bool ReadTokenValue(EditorStyleTokens& token, const std::string& name, float value)
        {
            if (name == "padding") token.padding = value; else if (name == "item_spacing") token.item_spacing = value;
            else if (name == "panel_spacing") token.panel_spacing = value; else if (name == "header_height") token.header_height = value;
            else if (name == "toolbar_height") token.toolbar_height = value; else if (name == "input_height") token.input_height = value;
            else if (name == "border_thickness") token.border_thickness = value; else if (name == "corner_radius") token.corner_radius = value;
            else if (name == "font_size") token.font_size = value; else return false;
            return true;
        }

        bool SaveStylePresetFile(const EditorStylePreset& preset,
            const std::filesystem::path& path, std::string& error)
        {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec)
            {
                error = "見た目プリセットの保存フォルダーを作れません。";
                return false;
            }
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            if (!stream)
            {
                error = "見た目プリセットを書き出せません。";
                return false;
            }
            stream.imbue(std::locale::classic());
            stream << std::setprecision(std::numeric_limits<float>::max_digits10);
            stream << style_preset_magic << ' ' << EditorStylePreset::current_version << '\n';
            stream << "ID " << std::quoted(preset.id) << '\n';
            stream << "NAME " << std::quoted(preset.name) << '\n';
            stream << "SCOPE " << StylePresetScopeName(preset.scope) << '\n';
            stream << "BUTTON_SCALE " << preset.button_scale << '\n';
            stream << "FONT_SCALE " << preset.font_scale << '\n';
            stream << "TEXT_COLOR " << preset.text_color.x << ' '
                << preset.text_color.y << ' ' << preset.text_color.z << '\n';
            WriteTokens(stream, preset.tokens);
            std::vector<std::pair<std::string, ImVec4>> colors(
                preset.category_colors.begin(), preset.category_colors.end());
            std::sort(colors.begin(), colors.end(),
                [](const auto& left, const auto& right) { return left.first < right.first; });
            for (const auto& entry : colors)
            {
                stream << "CATEGORY_COLOR " << std::quoted(entry.first) << ' '
                    << entry.second.x << ' ' << entry.second.y << ' '
                    << entry.second.z << '\n';
            }
            if (!stream)
            {
                error = "見た目プリセットの書き込みに失敗しました。";
                return false;
            }
            return true;
        }

        bool LoadStylePresetFile(const std::filesystem::path& path,
            EditorStylePresetScope scope, EditorStylePreset& preset, std::string& error)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                error = "見た目プリセットを開けません: " + path.generic_string();
                return false;
            }
            stream.imbue(std::locale::classic());
            std::string header;
            int version = 0;
            if (!(stream >> header >> version) || header != style_preset_magic ||
                version <= 0 || version > EditorStylePreset::current_version)
            {
                error = "見た目プリセット形式が不正です: " + path.generic_string();
                return false;
            }
            std::string ignored;
            std::getline(stream, ignored);
            preset = EditorStylePreset{};
            preset.scope = scope;
            preset.source_path = path;
            std::string line_text;
            while (std::getline(stream, line_text))
            {
                if (line_text.empty()) continue;
                std::istringstream line(line_text);
                line.imbue(std::locale::classic());
                std::string keyword;
                line >> keyword;
                if (keyword == "ID") line >> std::quoted(preset.id);
                else if (keyword == "NAME") line >> std::quoted(preset.name);
                else if (keyword == "SCOPE") line >> ignored;
                else if (keyword == "BUTTON_SCALE") line >> preset.button_scale;
                else if (keyword == "FONT_SCALE") line >> preset.font_scale;
                else if (keyword == "TEXT_COLOR")
                {
                    if (!(line >> preset.text_color.x >> preset.text_color.y >> preset.text_color.z))
                    {
                        error = "見た目プリセットの文字色を読めません: " + path.generic_string();
                        return false;
                    }
                }
                else if (keyword == "TOKEN_COLOR")
                {
                    std::string name;
                    ImVec4 color{};
                    if (!(line >> name >> color.x >> color.y >> color.z) ||
                        !ReadTokenColor(preset.tokens, name, color))
                    {
                        error = "見た目プリセットの色設定を読めません: " + path.generic_string();
                        return false;
                    }
                }
                else if (keyword == "TOKEN_VALUE")
                {
                    std::string name;
                    float value = 0.0f;
                    if (!(line >> name >> value) || !ReadTokenValue(preset.tokens, name, value))
                    {
                        error = "見た目プリセットの大きさ設定を読めません: " + path.generic_string();
                        return false;
                    }
                }
                else if (keyword == "CATEGORY_COLOR")
                {
                    std::string category;
                    ImVec4 color{};
                    if (!(line >> std::quoted(category) >> color.x >> color.y >> color.z) ||
                        category.empty())
                    {
                        error = "見た目プリセットのカテゴリ色を読めません: " + path.generic_string();
                        return false;
                    }
                    color.w = 1.0f;
                    preset.category_colors[category] = color;
                }
            }
            if (preset.id.empty()) preset.id = path.stem().string();
            if (preset.name.empty()) preset.name = preset.id;
            preset.Sanitize();
            return true;
        }

        std::filesystem::path PersonalStylePresetPath(const EditorStylePreset& preset)
        {
            return EditorStylePresetStore::PersonalDirectory() /
                (preset.id + ".replaystylepreset");
        }
    }

    const EditorStyleTokens& EditorStyle::Tokens() noexcept
    {
        return MutableTokens();
    }

    EditorStyleTokens EditorStyle::DefaultTokens() noexcept
    {
        return DefaultEditorStyleTokens();
    }

    void EditorStyle::SetTokens(const EditorStyleTokens& tokens) noexcept
    {
        MutableTokens() = tokens;
    }

    void EditorStyle::ResetTokens() noexcept
    {
        MutableTokens() = DefaultEditorStyleTokens();
    }

    void EditorStyle::Apply(float dpi_scale)
    {
        const EditorStyleTokens& token = Tokens();
        const float scale = (std::max)(dpi_scale, 0.5f);

        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowPadding = ImVec2(token.padding * scale, token.padding * scale);
        const float frame_height = (std::max)(token.header_height,
            (std::max)(token.toolbar_height, token.input_height));
        style.FramePadding = ImVec2(7.0f * scale,
            (std::max)(2.0f, (frame_height - token.font_size) * 0.5f) * scale);
        style.ItemSpacing = ImVec2(token.panel_spacing * scale, token.item_spacing * scale);
        style.ItemInnerSpacing = ImVec2(5.0f * scale, token.item_spacing * 0.5f * scale);
        style.IndentSpacing = token.header_height * scale;
        style.ScrollbarSize = 13.0f * scale;
        style.GrabMinSize = 10.0f * scale;
        style.WindowBorderSize = token.border_thickness * scale;
        style.ChildBorderSize = token.border_thickness * scale;
        style.PopupBorderSize = token.border_thickness * scale;
        style.FrameBorderSize = token.border_thickness * scale;
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
        colors[ImGuiCol_PlotLines] = token.secondary_text;
        colors[ImGuiCol_PlotLinesHovered] = token.success;
        colors[ImGuiCol_WindowBg] = token.panel_background;
        colors[ImGuiCol_ChildBg] = token.panel_background;
        colors[ImGuiCol_PopupBg] = token.header_background;
        colors[ImGuiCol_Border] = token.border;
        colors[ImGuiCol_FrameBg] = Blend(token.header_background, token.border, 0.25f);
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
        colors[ImGuiCol_SeparatorHovered] = token.trigger_collider;
        colors[ImGuiCol_SeparatorActive] = token.primary_collider;
        colors[ImGuiCol_ResizeGrip] = ImVec4(token.accent.x, token.accent.y, token.accent.z, 0.20f);
        colors[ImGuiCol_ResizeGripHovered] = token.hover;
        colors[ImGuiCol_ResizeGripActive] = token.active;
        colors[ImGuiCol_Tab] = token.header_background;
        colors[ImGuiCol_TabHovered] = token.hover;
        colors[ImGuiCol_TabActive] = token.selection;
        colors[ImGuiCol_DockingPreview] = ImVec4(token.prefab.x, token.prefab.y, token.prefab.z, 0.70f);
        colors[ImGuiCol_DockingEmptyBg] = token.main_background;
        colors[ImGuiCol_NavHighlight] = token.normal_collider;
        colors[ImGuiCol_NavWindowingHighlight] = token.error;
        colors[ImGuiCol_DragDropTarget] = token.warning;
        colors[ImGuiCol_TextSelectedBg] = ImVec4(token.missing.x, token.missing.y,
            token.missing.z, 0.35f);
    }

    void EditorStyle::PushPanelTabColors(const std::string& category)
    {
        const EditorStyleTokens& token = Tokens();
        const ImVec4 category_color = ComponentCategoryColor(category);
        ImGui::PushStyleColor(ImGuiCol_Tab, Blend(token.header_background, category_color, 0.14f));
        ImGui::PushStyleColor(ImGuiCol_TabHovered, Blend(token.hover, category_color, 0.30f));
        ImGui::PushStyleColor(ImGuiCol_TabActive, Blend(token.selection, category_color, 0.38f));
    }

    void EditorStyle::PopPanelTabColors() noexcept
    {
        ImGui::PopStyleColor(3);
    }

    ImVec4 EditorStyle::ComponentCategoryColor(const std::string& category) noexcept
    {
        const auto& colors = MutableComponentCategoryColors();
        const auto found = colors.find(category);
        return found != colors.end() ? found->second : DefaultComponentCategoryColor(category);
    }

    const std::unordered_map<std::string, ImVec4>&
        EditorStyle::ComponentCategoryColors() noexcept
    {
        return MutableComponentCategoryColors();
    }

    void EditorStyle::SetComponentCategoryColor(const std::string& category,
        const ImVec4& color)
    {
        MutableComponentCategoryColors()[category] = ImVec4(
            std::clamp(color.x, 0.0f, 1.0f),
            std::clamp(color.y, 0.0f, 1.0f),
            std::clamp(color.z, 0.0f, 1.0f), 1.0f);
    }

    void EditorStyle::ReplaceComponentCategoryColors(
        const std::unordered_map<std::string, ImVec4>& colors)
    {
        MutableComponentCategoryColors() = colors;
    }

    void EditorStyle::ResetComponentCategoryColors() noexcept
    {
        MutableComponentCategoryColors().clear();
    }

    void EditorStylePreset::Sanitize() noexcept
    {
        if (name.empty()) name = "RePlay Default";
        if (!std::isfinite(button_scale)) button_scale = 1.0f;
        if (!std::isfinite(font_scale)) font_scale = 1.0f;
        button_scale = std::clamp(button_scale, 0.6f, 3.0f);
        font_scale = std::clamp(font_scale, 0.7f, 2.5f);
        if (!std::isfinite(text_color.x)) text_color.x = 1.0f;
        if (!std::isfinite(text_color.y)) text_color.y = 1.0f;
        if (!std::isfinite(text_color.z)) text_color.z = 1.0f;
        text_color.x = std::clamp(text_color.x, 0.0f, 1.0f);
        text_color.y = std::clamp(text_color.y, 0.0f, 1.0f);
        text_color.z = std::clamp(text_color.z, 0.0f, 1.0f);
        text_color.w = 1.0f;
        const EditorStyleTokens defaults = EditorStyle::DefaultTokens();
        const auto sanitize_color = [](ImVec4& color, const ImVec4& fallback)
        {
            if (!std::isfinite(color.x)) color.x = fallback.x;
            if (!std::isfinite(color.y)) color.y = fallback.y;
            if (!std::isfinite(color.z)) color.z = fallback.z;
            color.x = std::clamp(color.x, 0.0f, 1.0f);
            color.y = std::clamp(color.y, 0.0f, 1.0f);
            color.z = std::clamp(color.z, 0.0f, 1.0f);
            color.w = 1.0f;
        };
        sanitize_color(tokens.main_background, defaults.main_background); sanitize_color(tokens.panel_background, defaults.panel_background);
        sanitize_color(tokens.header_background, defaults.header_background); sanitize_color(tokens.toolbar_background, defaults.toolbar_background);
        sanitize_color(tokens.border, defaults.border); sanitize_color(tokens.text, defaults.text);
        sanitize_color(tokens.secondary_text, defaults.secondary_text); sanitize_color(tokens.disabled_text, defaults.disabled_text);
        sanitize_color(tokens.accent, defaults.accent); sanitize_color(tokens.selection, defaults.selection);
        sanitize_color(tokens.hover, defaults.hover); sanitize_color(tokens.active, defaults.active);
        sanitize_color(tokens.success, defaults.success); sanitize_color(tokens.warning, defaults.warning);
        sanitize_color(tokens.error, defaults.error); sanitize_color(tokens.missing, defaults.missing);
        sanitize_color(tokens.prefab, defaults.prefab); sanitize_color(tokens.normal_collider, defaults.normal_collider);
        sanitize_color(tokens.trigger_collider, defaults.trigger_collider); sanitize_color(tokens.primary_collider, defaults.primary_collider);
        const auto sanitize_value = [](float& value, float fallback, float minimum, float maximum)
        {
            if (!std::isfinite(value)) value = fallback;
            value = std::clamp(value, minimum, maximum);
        };
        sanitize_value(tokens.padding, defaults.padding, 0.0f, 40.0f); sanitize_value(tokens.item_spacing, defaults.item_spacing, 0.0f, 30.0f);
        sanitize_value(tokens.panel_spacing, defaults.panel_spacing, 0.0f, 40.0f); sanitize_value(tokens.header_height, defaults.header_height, 12.0f, 80.0f);
        sanitize_value(tokens.toolbar_height, defaults.toolbar_height, 12.0f, 80.0f); sanitize_value(tokens.input_height, defaults.input_height, 12.0f, 80.0f);
        sanitize_value(tokens.border_thickness, defaults.border_thickness, 0.0f, 4.0f); sanitize_value(tokens.corner_radius, defaults.corner_radius, 0.0f, 20.0f);
        sanitize_value(tokens.font_size, defaults.font_size, 8.0f, 40.0f);
        for (auto& entry : category_colors)
        {
            if (!std::isfinite(entry.second.x)) entry.second.x = 0.0f;
            if (!std::isfinite(entry.second.y)) entry.second.y = 0.0f;
            if (!std::isfinite(entry.second.z)) entry.second.z = 0.0f;
            entry.second.x = std::clamp(entry.second.x, 0.0f, 1.0f);
            entry.second.y = std::clamp(entry.second.y, 0.0f, 1.0f);
            entry.second.z = std::clamp(entry.second.z, 0.0f, 1.0f);
            entry.second.w = 1.0f;
        }
    }

    std::filesystem::path EditorStylePresetStore::SharedDirectory()
    {
        return std::filesystem::path("Editor") / "StylePresets";
    }

    std::filesystem::path EditorStylePresetStore::PersonalDirectory()
    {
        return std::filesystem::path("Saved") / "Editor" / "StylePresets";
    }

    std::filesystem::path EditorStylePresetStore::UserSelectionPath()
    {
        return std::filesystem::path("Saved") / "Editor" /
            "StyleUserSettings.replaystyleuser";
    }

    std::vector<EditorStylePreset> EditorStylePresetStore::LoadAll(std::string& error)
    {
        std::map<std::string, EditorStylePreset> by_id;
        const auto load_directory = [&](const std::filesystem::path& directory,
            EditorStylePresetScope scope)
        {
            std::error_code ec;
            if (!std::filesystem::exists(directory, ec) || ec) return;
            for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
            {
                if (ec) break;
                std::error_code entry_error;
                if (!entry.is_regular_file(entry_error) || entry_error ||
                    entry.path().extension() != ".replaystylepreset") continue;
                EditorStylePreset preset;
                std::string local_error;
                if (LoadStylePresetFile(entry.path(), scope, preset, local_error))
                    by_id[preset.id] = std::move(preset);
                else if (error.empty()) error = local_error;
            }
        };

        load_directory(SharedDirectory(), EditorStylePresetScope::Shared);
        load_directory(PersonalDirectory(), EditorStylePresetScope::Personal);

        std::vector<EditorStylePreset> result;
        result.reserve(by_id.size());
        for (auto& entry : by_id) result.push_back(std::move(entry.second));
        std::sort(result.begin(), result.end(), [](const EditorStylePreset& left,
            const EditorStylePreset& right)
        {
            if (left.scope != right.scope)
                return left.scope == EditorStylePresetScope::Personal;
            return left.name < right.name;
        });
        return result;
    }

    bool EditorStylePresetStore::Save(EditorStylePreset& preset, std::string& error)
    {
        if (!preset.Editable())
        {
            error = "共有見た目プリセットは直接変更しません。複製して自分用にしてください。";
            return false;
        }
        if (preset.id.empty()) preset.id = MakeUniqueId();
        preset.Sanitize();
        preset.source_path = PersonalStylePresetPath(preset);
        return SaveStylePresetFile(preset, preset.source_path, error);
    }

    bool EditorStylePresetStore::DeletePersonal(const EditorStylePreset& preset,
        std::string& error)
    {
        if (!preset.Editable())
        {
            error = "共有見た目プリセットは削除できません。";
            return false;
        }
        const std::filesystem::path path = preset.source_path.empty()
            ? PersonalStylePresetPath(preset) : preset.source_path;
        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || ec) return true;
        if (!std::filesystem::remove(path, ec) || ec)
        {
            error = "個人見た目プリセットを削除できません。";
            return false;
        }
        return true;
    }

    EditorStylePreset EditorStylePresetStore::DuplicateAsPersonal(
        const EditorStylePreset& source, const std::string& preferred_name)
    {
        EditorStylePreset copy = source;
        copy.id = MakeUniqueId();
        copy.name = preferred_name.empty() ? source.name + " Copy" : preferred_name;
        copy.scope = EditorStylePresetScope::Personal;
        copy.source_path.clear();
        return copy;
    }

    bool EditorStylePresetStore::PublishSharedCopy(const EditorStylePreset& source,
        const std::string& preferred_name, EditorStylePreset& published,
        std::string& error)
    {
        published = source;
        published.id = MakeUniqueId();
        published.name = preferred_name.empty() ? source.name + " Shared" : preferred_name;
        published.scope = EditorStylePresetScope::Shared;
        published.source_path = SharedDirectory() /
            (published.id + ".replaystylepreset");
        published.Sanitize();
        return SaveStylePresetFile(published, published.source_path, error);
    }

    bool EditorStylePresetStore::SaveActivePresetId(const std::string& id,
        std::string& error)
    {
        const std::filesystem::path path = UserSelectionPath();
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            error = "見た目のユーザー設定フォルダーを作れません。";
            return false;
        }
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            error = "見た目のユーザー設定を書き出せません。";
            return false;
        }
        stream << style_user_magic << " 1\n";
        stream << "ACTIVE " << std::quoted(id) << '\n';
        return static_cast<bool>(stream);
    }

    bool EditorStylePresetStore::LoadActivePresetId(std::string& id,
        std::string& error)
    {
        std::ifstream stream(UserSelectionPath(), std::ios::binary);
        if (!stream)
        {
            error = "見た目のユーザー設定がありません。";
            return false;
        }
        std::string header;
        int version = 0;
        if (!(stream >> header >> version) || header != style_user_magic || version != 1)
        {
            error = "見た目のユーザー設定形式が不正です。";
            return false;
        }
        std::string keyword;
        while (stream >> keyword)
        {
            if (keyword == "ACTIVE")
            {
                stream >> std::quoted(id);
                return !id.empty();
            }
            std::string ignored;
            std::getline(stream, ignored);
        }
        error = "使用中の見た目プリセットが記録されていません。";
        return false;
    }

    std::string EditorStylePresetStore::MakeUniqueId()
    {
        const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        std::random_device random_device;
        const std::uint64_t random_value =
            (static_cast<std::uint64_t>(random_device()) << 32) ^ random_device();
        std::ostringstream stream;
        stream << std::hex << static_cast<std::uint64_t>(now) << random_value;
        return stream.str();
    }
}
