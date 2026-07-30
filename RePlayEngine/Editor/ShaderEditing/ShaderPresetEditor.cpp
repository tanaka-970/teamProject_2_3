#include "ShaderPresetEditor.h"
#include "../../Rendering/ShaderPresets/BuiltInShaderPresets.h"
#include "../../Rendering/ShaderPresets/ShaderPresetSerializer.h"

#include "imgui/imgui.h"

#include <commdlg.h>

#include <filesystem>
#include <utility>

namespace ReplayEngine::Editor
{
    namespace
    {
        std::filesystem::path BrowsePreset(HWND owner, bool save)
        {
            wchar_t filename[32768]{};
            const std::wstring initial_directory = std::filesystem::absolute(
                std::filesystem::path("resources") / "Shaders" / "Presets").wstring();
            OPENFILENAMEW dialog{};
            dialog.lStructSize = sizeof(dialog);
            dialog.hwndOwner = owner;
            dialog.lpstrFile = filename;
            dialog.nMaxFile = static_cast<DWORD>(_countof(filename));
            dialog.lpstrFilter = L"RePlayシェーダープリセット (*.replayshader)\0*.replayshader\0\0";
            dialog.lpstrDefExt = L"replayshader";
            dialog.lpstrTitle = save ? L"シェーダープリセットを保存" : L"シェーダープリセットを開く";
            dialog.lpstrInitialDir = initial_directory.c_str();
            dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
                (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
            const BOOL accepted = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
            return accepted ? std::filesystem::path(filename) : std::filesystem::path{};
        }

        void ApplyPreset(Rendering::ShaderPreset preset, int& base_shader,
            bool& outline_pass, Rendering::ShaderLayerStack& layers,
            Rendering::CharacterMaterialProfile& profile)
        {
            base_shader = preset.base_shader;
            outline_pass = preset.outline;
            layers = std::move(preset.layers);
            profile = std::move(preset.character);
        }
    }

    void ShaderPresetEditor::Draw(HWND owner, int& base_shader, bool& outline_pass,
        Rendering::ShaderLayerStack& layers,
        Rendering::CharacterMaterialProfile& profile, std::string& status)
    {
        using namespace Rendering;
        if (ImGui::Button("新規プリセット（空）"))
        {
            base_shader = 0;
            outline_pass = false;
            layers.Clear();
            profile = CharacterMaterialProfile{};
            profile.name = "新規シェーダープリセット";
            status = "新規プリセット: 追加パスなし";
        }
        ImGui::SameLine();
        ImGui::TextDisabled("追加パスを選び、必要な表現だけ積み上げます");
        ImGui::Separator();
        ImGui::TextUnformatted("既存のシェーダープリセットから開始");

        if (ImGui::Button("鳴潮風"))
        {
            ApplyPreset(BuiltInShaderPresets::WutheringStylized(),
                base_shader, outline_pass, layers, profile);
            status = "内蔵プリセット: 鳴潮風Stylized PBR";
        }
        ImGui::SameLine();
        if (ImGui::Button("エンドフィールド風"))
        {
            ApplyPreset(BuiltInShaderPresets::EndfieldLayered(),
                base_shader, outline_pass, layers, profile);
            status = "内蔵プリセット: エンドフィールド風Layered PBR";
        }
        ImGui::SameLine();
        if (ImGui::Button("Crystal Toon"))
        {
            ApplyPreset(BuiltInShaderPresets::CrystalToon(),
                base_shader, outline_pass, layers, profile);
            status = "内蔵プリセット: Crystal Toon";
        }
        if (ImGui::Button("柔光アニメ"))
        {
            ApplyPreset(BuiltInShaderPresets::SoftAnime(),
                base_shader, outline_pass, layers, profile);
            status = "内蔵プリセット: 柔光アニメ";
        }
        ImGui::SameLine();
        if (ImGui::Button("硬質グラフィックセル"))
        {
            ApplyPreset(BuiltInShaderPresets::GraphicCel(),
                base_shader, outline_pass, layers, profile);
            status = "内蔵プリセット: 硬質グラフィックセル";
        }
        ImGui::SameLine();
        if (ImGui::Button("月光クリスタル"))
        {
            ApplyPreset(BuiltInShaderPresets::MoonlitCrystal(),
                base_shader, outline_pass, layers, profile);
            status = "内蔵プリセット: 月光クリスタル";
        }

        if (ImGui::Button("プリセットを保存..."))
        {
            const auto path = BrowsePreset(owner, true);
            if (!path.empty())
            {
                ShaderPreset preset{};
                preset.name = profile.name;
                preset.base_shader = base_shader;
                preset.outline = outline_pass;
                preset.layers = layers;
                preset.character = profile;
                std::string error;
                status = ShaderPresetSerializer::Save(preset, path, error)
                    ? "保存しました: " + path.generic_u8string() : "保存失敗: " + error;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("既存プリセットから開始..."))
        {
            const auto path = BrowsePreset(owner, false);
            if (!path.empty())
            {
                ShaderPreset preset{};
                std::string error;
                if (ShaderPresetSerializer::Load(preset, path, error))
                {
                    ApplyPreset(std::move(preset), base_shader, outline_pass, layers, profile);
                    status = "読み込みました: " + path.generic_u8string();
                }
                else status = "読込失敗: " + error;
            }
        }
        ImGui::TextWrapped("%s", status.c_str());
    }
}
