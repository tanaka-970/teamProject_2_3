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

#ifdef USE_IMGUI
    if (!editor_mode) return;
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    imgui_frame_active = true;
    draw_editor();
#endif
}
