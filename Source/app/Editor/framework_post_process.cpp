#include "framework.h"
#include "../../../RePlayEngine/Editor/Rendering/PostProcessEditor.h"

void framework::draw_screen_effect_stack()
{
    const float old_luminance_threshold = luminance_threshold;
    const bool old_luminance_enabled = enable_luminance_shader;
    const bool old_final_pass_enabled = enable_final_pass_shader;

    ReplayEngine::Editor::PostProcessEditor::Draw(post_process.GetSettings(),
        luminance_threshold, enable_luminance_shader, enable_bloom_shader,
        enable_vignette_shader, enable_fxaa_shader, enable_final_pass_shader);
    if (old_luminance_threshold != luminance_threshold ||
        old_luminance_enabled != enable_luminance_shader ||
        old_final_pass_enabled != enable_final_pass_shader)
    {
        project_settings.SetLuminanceThreshold(luminance_threshold);
        project_settings.SetLuminanceEnabled(enable_luminance_shader);
        project_settings.SetFinalPassEnabled(enable_final_pass_shader);
        save_project_settings();
    }
}
