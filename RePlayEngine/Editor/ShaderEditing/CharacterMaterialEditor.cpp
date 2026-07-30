#include "CharacterMaterialEditor.h"

#include "imgui/imgui.h"

#include <cstring>

namespace ReplayEngine::Editor
{
    void CharacterMaterialEditor::Draw(Rendering::CharacterMaterialProfile& profile)
    {
        char profile_name[256]{};
        strncpy_s(profile_name, profile.name.c_str(), _TRUNCATE);
        if (ImGui::InputText("プロファイル名", profile_name, IM_ARRAYSIZE(profile_name)))
            profile.name = profile_name;
        ImGui::SliderFloat("陰影しきい値", &profile.toon_threshold, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("陰影の柔らかさ", &profile.toon_softness, 0.001f, 0.5f, "%.3f");
        ImGui::SliderFloat("影の強さ", &profile.shadow_strength, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("彩度", &profile.saturation, 0.0f, 2.0f, "%.2f");

        if (ImGui::TreeNodeEx("表現調整", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SliderFloat("陰影段階数", &profile.artistic.shadow_bands, 1.0f, 8.0f, "%.0f");
            ImGui::SliderFloat("コントラスト", &profile.artistic.contrast, 0.25f, 2.5f, "%.2f");
            ImGui::SliderFloat("色相", &profile.artistic.hue_shift, -0.5f, 0.5f, "%.2f");
            ImGui::SliderFloat("上下グラデーション", &profile.artistic.gradient_strength,
                0.0f, 1.0f, "%.2f");
            ImGui::ColorEdit3("上側の色", &profile.artistic.top_color.x);
            ImGui::ColorEdit3("下側の色", &profile.artistic.bottom_color.x);
            ImGui::DragFloat("グラデーション倍率", &profile.artistic.gradient_scale,
                0.01f, -4.0f, 4.0f);
            ImGui::DragFloat("グラデーション位置", &profile.artistic.gradient_offset,
                0.01f, -4.0f, 4.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("肌・疑似SSS", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("肌補正を有効化", &profile.skin.enabled);
            ImGui::ColorEdit3("肌色補正", &profile.skin.tint.x);
            ImGui::ColorEdit3("肌の影色", &profile.skin.shadow_tint.x);
            ImGui::SliderFloat("光の回り込み", &profile.skin.wrap, 0.0f, 1.0f);
            ImGui::SliderFloat("散乱光", &profile.skin.scatter, 0.0f, 1.0f);
            ImGui::SliderFloat("境界ぼかし", &profile.skin.softness, 0.0f, 0.5f);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("顔影補正", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("顔影補正を有効化", &profile.face.enabled);
            ImGui::ColorEdit3("顔の影色", &profile.face.shadow_tint.x);
            ImGui::SliderFloat("ライト方向補正", &profile.face.light_bias, -0.5f, 0.5f);
            ImGui::SliderFloat("顔影の柔らかさ", &profile.face.shadow_softness, 0.0f, 0.5f);
            ImGui::SliderFloat("正面補助光", &profile.face.front_fill, 0.0f, 1.0f);
            ImGui::TextDisabled("SDF顔影テクスチャはMaterial Slot対応後に割り当てます");
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("髪・異方性反射", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("髪ハイライトを有効化", &profile.hair.enabled);
            ImGui::ColorEdit3("髪ハイライト色", &profile.hair.highlight_color.x);
            ImGui::SliderFloat("ハイライト硬度", &profile.hair.power, 1.0f, 128.0f);
            ImGui::SliderFloat("ハイライト強度", &profile.hair.intensity, 0.0f, 2.0f);
            ImGui::SliderFloat("異方性", &profile.hair.anisotropy, 0.0f, 1.0f);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("鏡面反射", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("鏡面反射を有効化", &profile.specular.enabled);
            ImGui::ColorEdit3("鏡面色", &profile.specular.color.x);
            ImGui::SliderFloat("鏡面硬度", &profile.specular.power, 1.0f, 256.0f);
            ImGui::SliderFloat("鏡面しきい値", &profile.specular.threshold, 0.0f, 1.0f);
            ImGui::SliderFloat("鏡面強度", &profile.specular.intensity, 0.0f, 4.0f);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("リムライト", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("リムライトを有効化", &profile.rim.enabled);
            ImGui::ColorEdit3("リム色", &profile.rim.color.x);
            ImGui::SliderFloat("リム範囲", &profile.rim.power, 0.1f, 12.0f);
            ImGui::SliderFloat("リムしきい値", &profile.rim.threshold, 0.0f, 1.0f);
            ImGui::SliderFloat("リム強度", &profile.rim.intensity, 0.0f, 4.0f);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("透明・結晶", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("結晶表現を有効化", &profile.crystal.enabled);
            ImGui::ColorEdit3("結晶色", &profile.crystal.tint.x);
            ImGui::SliderFloat("透明度", &profile.crystal.transparency, 0.0f, 1.0f);
            ImGui::SliderFloat("フレネル", &profile.crystal.fresnel_power, 0.1f, 12.0f);
            ImGui::SliderFloat("色分散", &profile.crystal.dispersion, 0.0f, 1.0f);
            ImGui::SliderFloat("内部発光", &profile.crystal.internal_emission, 0.0f, 4.0f);
            ImGui::TextDisabled("背景屈折とOITは透明専用RenderPassで追加予定です");
            ImGui::TreePop();
        }
    }
}
