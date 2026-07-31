#include "framework.h"
#include "../../../RePlayEngine/Editor/Rendering/PostProcessEditor.h"

void framework::draw_screen_effect_stack()
{

    ReplayEngine::Editor::PostProcessEditor::Draw(post_process.GetSettings(),
        luminance_threshold, enable_luminance_shader, enable_bloom_shader,
        enable_vignette_shader, enable_fxaa_shader, enable_final_pass_shader);
}
