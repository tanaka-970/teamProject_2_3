#include "PostProcessEditor.h"

#include "imgui/imgui.h"

namespace ReplayEngine::Editor
{
    void PostProcessEditor::Draw(Rendering::PostProcessPass::Settings& settings,
        float& luminance_threshold, bool& luminance_enabled,
        bool& bloom_enabled, bool& vignette_enabled,
        bool& fxaa_enabled, bool& final_pass_enabled)
    {
        ImGui::TextUnformatted("画面エフェクトスタック");
        ImGui::TextDisabled("上から順に処理し、最後に画面へ合成します");

        ImGui::Checkbox("1. 輝度抽出", &luminance_enabled);
        if (ImGui::Checkbox("2. Bloom", &bloom_enabled) && bloom_enabled)
        {
            luminance_enabled = true;
            final_pass_enabled = true;
        }
        if (ImGui::Checkbox("3. ビネット", &vignette_enabled) && vignette_enabled)
            final_pass_enabled = true;
        if (ImGui::Checkbox("4. FXAA", &fxaa_enabled) && fxaa_enabled)
            final_pass_enabled = true;
        ImGui::Checkbox("5. 最終合成", &final_pass_enabled);

        ImGui::Separator();
        ImGui::SliderFloat("輝度しきい値", &luminance_threshold, 0.0f, 1.0f);
        ImGui::SliderFloat("露出", &settings.exposure, 0.0f, 4.0f);
        ImGui::SliderFloat("Bloom強度", &settings.bloom_intensity, 0.0f, 4.0f);
        ImGui::SliderFloat("ビネット強度", &settings.vignette_strength, 0.0f, 1.0f);
        ImGui::SliderFloat("FXAA強度", &settings.fxaa_enable, 0.0f, 1.0f);
    }
}
