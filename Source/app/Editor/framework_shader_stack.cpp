#include "framework.h"
#include "../../../RePlayEngine/Editor/ShaderEditing/ShaderStackEditor.h"

void framework::draw_shader_stack(const char* id, int& base_shader, bool& outline_pass,
    ReplayEngine::Rendering::ShaderLayerStack& layers, float& pixel_grid,
    float& pixelate_strength)
{
    const auto result = ReplayEngine::Editor::ShaderStackEditor::Draw(id, base_shader,
        outline_pass, layers, shader_stack_advanced_mode, toon.outline.outline_color,
        toon.outline.outline_params, pixel_grid, pixelate_strength);
    if (result.requires_pbr) use_pbr_skin = true;
    if (result.requires_toon) enable_toon_shader = true;
    if (result.requires_unlit) enable_unlit_shader = true;
    if (result.requires_outline) enable_outline_shader = true;
}
