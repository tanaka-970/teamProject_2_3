#include "framework.h"

void framework::draw_ui_focus_style_manager()
{
#ifdef USE_IMGUI
    if (!show_ui_focus_style_manager) return;

    if (!ImGui::Begin(u8"UI フォーカス表示", &show_ui_focus_style_manager))
    {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled(
        u8"ゲーム共通の既定値です。Scene/ユーザー設定ではなく Project Settings に保存されます。");
    ImGui::Separator();

    bool enabled = project_settings.FocusOutlineEnabled();
    DirectX::XMFLOAT4 color = project_settings.FocusOutlineColor();
    float width = project_settings.FocusOutlineWidth();
    float radius = project_settings.FocusCornerRadius();

    bool changed = false;
    changed |= ImGui::Checkbox(u8"輪郭線を表示", &enabled);
    changed |= ImGui::ColorEdit4(u8"輪郭線の色", &color.x);
    changed |= ImGui::DragFloat(u8"輪郭線の太さ", &width, 0.25f, 0.0f, 32.0f);
    changed |= ImGui::DragFloat(u8"角の丸み", &radius, 0.25f, 0.0f, 64.0f);

    if (changed)
    {
        project_settings.SetFocusOutlineEnabled(enabled);
        project_settings.SetFocusOutlineColor(color);
        project_settings.SetFocusOutlineWidth((std::max)(0.0f, width));
        project_settings.SetFocusCornerRadius((std::max)(0.0f, radius));
        save_project_settings();
    }

    ImGui::Separator();
    ImGui::TextWrapped(
        u8"個別 UI は Selectable の「Focus Style 上書き」を有効にすると、"
        u8"この Project 既定値を要素単位で上書きできます。");
    ImGui::TextDisabled("%s", project_settings_status.c_str());
    ImGui::End();
#endif
}
