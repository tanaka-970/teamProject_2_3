#include "framework.h"

#include "../../RePlayEngine/Components/UI/UIImageComponent.h"
#include "../../RePlayEngine/Motion/MotionBindingResolver.h"
#include "../../RePlayEngine/Motion/MotionEvaluator.h"
#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Object/Registry/ComponentRegistry.h"
#include "../../RePlayEngine/Reflection/Registry/PropertyRegistry.h"
#include "../../RePlayEngine/Scene/Runtime/Scene.h"

#include "imgui/imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "framework_motion_workspaceInternal.h"
using namespace framework_motion_workspace::Detail;

// Motion Preview 描画の関数本体

void framework::draw_motion_preview()
{
    if (!show_motion_preview_panel) return;
    if (!ImGui::Begin("Motion プレビュー", &show_motion_preview_panel))
    {
        ImGui::End();
        return;
    }

    if (!motion_editor_loaded)
    {
        ImGui::TextDisabled("Motion Asset が未選択です。");
        ImGui::End();
        return;
    }

    if (ImGui::Button(motion_preview_active ? "停止" : "再生"))
    {
        if (motion_preview_active) stop_motion_preview();
        else
        {
            capture_motion_preview_targets();
            motion_preview_active = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("復元")) stop_motion_preview();
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &motion_preview_loop);
    ImGui::SetNextItemWidth(140.0f);
    ImGui::DragFloat("Speed", &motion_preview_speed, 0.01f, -8.0f, 8.0f);

    if (motion_preview_active)
    {
        motion_preview_time += ImGui::GetIO().DeltaTime * motion_preview_speed;
        if (motion_editor_asset.duration > 0.0f)
        {
            if (motion_preview_loop)
            {
                while (motion_preview_time > motion_editor_asset.duration)
                    motion_preview_time -= motion_editor_asset.duration;
                while (motion_preview_time < 0.0f)
                    motion_preview_time += motion_editor_asset.duration;
            }
            else
            {
                motion_preview_time =
                    (std::min)((std::max)(motion_preview_time, 0.0f),
                        motion_editor_asset.duration);
            }
        }
        apply_motion_preview_time();
    }

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("Time", &motion_preview_time, 0.0f,
        (std::max)(0.001f, motion_editor_asset.duration)))
    {
        apply_motion_preview_time();
    }

    ImGui::Separator();
    ImGui::Text("Tracks: %d", static_cast<int>(motion_editor_asset.tracks.size()));
    ImGui::TextDisabled("PreviewはPropertyRegistry::Capture/Applyで復元します。");
    ImGui::End();
}
