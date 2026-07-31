#include "framework.h"

void framework::update(float elapsed_time)
{
    async_asset_manager.PumpMainThread();
    if (game_scene)
    {
        game_scene->Gameplay().SetLegacyStageActive(stage_asset_placed);
    }

    if (scene_manager.IsExclusive())
    {
        scene_manager.Update(elapsed_time);
        return;
    }

    if (!editor_mode || !edit_mode_active)
    {
        scene_manager.Update(elapsed_time);
    }

    // GameObject / Component 基盤の更新。
    // 既存の scene_manager (画面遷移) とは別系統で、二重更新にはならない。
    // 内部で Edit Mode 中は Component を止める判定を行っている。
    update_object_scene(elapsed_time);

#ifdef USE_IMGUI
    if (!editor_mode) return;
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    draw_editor();
#endif
}
