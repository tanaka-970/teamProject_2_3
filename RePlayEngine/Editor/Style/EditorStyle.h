#pragma once

#include "imgui/imgui.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

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
        static EditorStyleTokens DefaultTokens() noexcept;
        static void SetTokens(const EditorStyleTokens& tokens) noexcept;
        static void ResetTokens() noexcept;
        static void Apply(float dpi_scale = 1.0f);
        static void PushPanelTabColors(const std::string& category);
        static void PopPanelTabColors() noexcept;
        static ImVec4 ComponentCategoryColor(const std::string& category) noexcept;
        static const std::unordered_map<std::string, ImVec4>& ComponentCategoryColors() noexcept;
        static void SetComponentCategoryColor(const std::string& category, const ImVec4& color);
        static void ReplaceComponentCategoryColors(
            const std::unordered_map<std::string, ImVec4>& colors);
        static void ResetComponentCategoryColors() noexcept;
    };

    class PanelTabColorScope final
    {
    public:
        explicit PanelTabColorScope(const std::string& category)
        {
            EditorStyle::PushPanelTabColors(category);
        }
        ~PanelTabColorScope()
        {
            EditorStyle::PopPanelTabColors();
        }
        PanelTabColorScope(const PanelTabColorScope&) = delete;
        PanelTabColorScope& operator=(const PanelTabColorScope&) = delete;
    };

    enum class EditorStylePresetScope : std::uint8_t
    {
        Personal = 0,
        Shared,
    };

    struct EditorStylePreset final
    {
        static constexpr int current_version = 2;

        std::string id;
        std::string name = "RePlay Default";
        EditorStylePresetScope scope = EditorStylePresetScope::Personal;
        std::filesystem::path source_path;
        float button_scale = 1.0f;
        float font_scale = 1.0f;
        ImVec4 text_color{ 1.0f, 1.0f, 1.0f, 1.0f };
        EditorStyleTokens tokens{};
        std::unordered_map<std::string, ImVec4> category_colors;

        bool Editable() const noexcept
        {
            return scope == EditorStylePresetScope::Personal;
        }

        void Sanitize() noexcept;
    };

    class EditorStylePresetStore final
    {
    public:
        EditorStylePresetStore() = delete;

        static std::filesystem::path SharedDirectory();
        static std::filesystem::path PersonalDirectory();
        static std::filesystem::path UserSelectionPath();
        static std::vector<EditorStylePreset> LoadAll(std::string& error);
        static bool Save(EditorStylePreset& preset, std::string& error);
        static bool DeletePersonal(const EditorStylePreset& preset, std::string& error);
        static EditorStylePreset DuplicateAsPersonal(const EditorStylePreset& source,
            const std::string& preferred_name);
        static bool PublishSharedCopy(const EditorStylePreset& source,
            const std::string& preferred_name, EditorStylePreset& published,
            std::string& error);
        static bool SaveActivePresetId(const std::string& id, std::string& error);
        static bool LoadActivePresetId(std::string& id, std::string& error);
        static std::string MakeUniqueId();
    };
}
