#include "framework.h"
#include "../../../RePlayEngine/Editor/ShaderEditing/CharacterMaterialEditor.h"
#include "../../../RePlayEngine/Editor/ShaderEditing/ShaderPresetEditor.h"

void framework::draw_character_material_controls(const char* id, int& base_shader,
    bool& outline_pass, ReplayEngine::Rendering::ShaderLayerStack& layers,
    ReplayEngine::Rendering::CharacterMaterialProfile& profile, float& pixel_grid,
    float& pixelate_strength)
{
   
    ImGui::PushID(id);
    if (!ImGui::CollapsingHeader("キャラクター材質", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::PopID();
        return;
    }
    ImGui::TextDisabled("型付きMaterial Instance。任意コードやシェーダーグラフは使用しません");
    ReplayEngine::Editor::ShaderPresetEditor::Draw(hwnd, base_shader, outline_pass,
        layers, profile, pixel_grid, pixelate_strength, shader_preset_status);
    ReplayEngine::Editor::CharacterMaterialEditor::Draw(profile);
    ImGui::PopID();
}
