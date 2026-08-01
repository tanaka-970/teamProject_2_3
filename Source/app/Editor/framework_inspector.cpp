#include "framework.h"
#include "skinned_mesh.h"
#include "../../RePlayEngine/Components/Gameplay/CharacterMotorComponent.h"
#include "../../RePlayEngine/Components/Gameplay/PlayerControllerComponent.h"
#include "../../RePlayEngine/Components/Gameplay/PlayerInputComponent.h"

#include <string>

void framework::draw_shader_adjustment_workspace()
{
    // 「プレイヤー」という対象は無くなった。
    // キャラクターの描画方式・輪郭線は SkinnedMeshRendererComponent /
    // MeshRendererComponent のプロパティが持ち、GameObject インスペクターで編集する。
    // 同じ値をここと Component の両方から変えられる状態は作らない。
    const char* targets[] = { "ステージ", "静的メッシュ" };
    int target = selected_editor_object == editor_selection::rendering ? 1 : 0;

    ImGui::TextUnformatted("シェーダー調整");
    ImGui::TextDisabled("材質の表現、追加パスの順序、合成、プリセットをまとめて編集します。");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##ShaderTarget", &target, targets, IM_ARRAYSIZE(targets)))
    {
        selected_editor_object = target == 0
            ? editor_selection::stage : editor_selection::rendering;
    }
    ImGui::TextDisabled("キャラクターの材質は GameObject の Renderer で編集します");
    ImGui::Separator();

    if (ImGui::BeginTabBar("ShaderAdjustmentTabs"))
    {
        if (ImGui::BeginTabItem("材質・表現"))
        {
            if (target == 0)
            {
                ImGui::Checkbox("ステージシェーダーを使う", &enable_stage_shader);
                draw_shader_stack("shader_workspace_stage", shading_per_stage,
                    outline_per_stage, stage_shader_layers);
                draw_character_material_controls("shader_workspace_stage_material", shading_per_stage,
                    outline_per_stage, stage_shader_layers, stage_character_profile);
                if (stage_asset_placed && active_stage_placement_id != 0) sync_selected_entity_to_stage();
            }
            else
            {
                ImGui::Checkbox("静的メッシュを表示", &enable_static_meshes);
                draw_shader_stack("shader_workspace_static", shading_per_static[0],
                    outline_per_static[0], shader_layers_static[0]);
                draw_character_material_controls("shader_workspace_static_material",
                    shading_per_static[0], outline_per_static[0], shader_layers_static[0],
                    character_profiles_static[0]);
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("ステージ表面"))
        {
            ImGui::TextUnformatted("マットな地形向けPBR設定");
            ImGui::Checkbox("テクスチャを繰り返す", &stage_texture_wrap);
            ImGui::SliderFloat("テクスチャの濃さ", &stage_texture_contrast, 0.50f, 2.50f, "%.2f");
            bool geometric = pbr.stage_material.options.x > 0.5f;
            bool normal_map = pbr.stage_material.options.y > 0.5f;
            bool diffuse_ibl = pbr.stage_material.options.z > 0.5f;
            bool shadow = pbr.stage_material.options.w > 0.5f;
            if (ImGui::Checkbox("面法線を使う", &geometric))
                pbr.stage_material.options.x = geometric ? 1.0f : 0.0f;
            if (ImGui::Checkbox("法線マップ", &normal_map))
                pbr.stage_material.options.y = normal_map ? 1.0f : 0.0f;
            if (ImGui::Checkbox("拡散IBL", &diffuse_ibl))
                pbr.stage_material.options.z = diffuse_ibl ? 1.0f : 0.0f;
            if (ImGui::Checkbox("影を受ける", &shadow))
                pbr.stage_material.options.w = shadow ? 1.0f : 0.0f;
            ImGui::ColorEdit3("ステージ基本色", &pbr.stage_material.base_tint.x);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("画面効果"))
        {
            draw_screen_effect_stack();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("描画確認"))
        {
            ImGui::TextUnformatted("レンダラー: Deferred（固定）");
            ImGui::TextDisabled("輪郭線と半透明表現はDeferred照明後に追加パスで合成します。");
            ImGui::Checkbox("PBR", &use_pbr_skin);
            ImGui::Checkbox("トゥーン", &enable_toon_shader);
            ImGui::Checkbox("アンリット", &enable_unlit_shader);
            ImGui::Checkbox("輪郭線パス", &enable_outline_shader);
            ImGui::Checkbox("PBR影パス", &enable_pbr_shadow_shader);
            int output = render_graph.OutputIndex();
            if (ImGui::Combo("描画出力 (F2)", &output, ReplayEngine::Rendering::RenderGraph::Names()))
                render_graph.SetOutput(output);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void framework::draw_inspector()
{
    ImGui::Begin("インスペクター");

    const char* tables[] = {
        "基本", "配置", "モデリング", "アニメーション", "レンダリング", "シェーダー調整"
    };
    int table = static_cast<int>(active_editor_workspace);
    ImGui::TextDisabled("編集テーブル");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##RightUpperWorkspace", &table, tables, IM_ARRAYSIZE(tables)))
        set_editor_workspace(static_cast<editor_workspace>(table));
    ImGui::Separator();
    if (active_editor_workspace == editor_workspace::shader_adjustment)
    {
        draw_shader_adjustment_workspace();
        ImGui::End();
        return;
    }

    switch (selected_editor_object)
    {
    case editor_selection::world:
        ImGui::TextUnformatted("ワールド");
        ImGui::Separator();
        ImGui::ColorEdit4("背景色", &background_color.x);
        ImGui::Checkbox("背景画像", &draw_background_image);
        ImGui::Checkbox("ゲームシーン", &enable_scene_game);
        ImGui::Checkbox("パーティクル", &enable_particles);
        ImGui::Checkbox("軌跡", &enable_trail);
        ImGui::Separator();
        draw_project_settings_panel();
        ImGui::Separator();
        draw_controlled_character_diagnostics();
        break;

    case editor_selection::camera:
        ImGui::TextUnformatted("メインカメラ");
        ImGui::Separator();
        if (enable_scene_game && game_scene)
        {
            const auto& camera = game_scene->Gameplay().GetCamera();
            const auto& eye = camera.GetEye();
            const auto& focus = camera.GetFocus();
            ImGui::Text("位置     %.3f  %.3f  %.3f", eye.x, eye.y, eye.z);
            ImGui::Text("注視点   %.3f  %.3f  %.3f", focus.x, focus.y, focus.z);
            game_scene->Gameplay().DrawCameraGUI();
        }
        else
        {
            ImGui::DragFloat3("位置", &camera_position.x, 0.05f, -100.0f, 100.0f, "%.3f");
        }
        break;

    case editor_selection::stage:
        ImGui::TextUnformatted(stage_asset_placed ? "ステージ（配置済み）" : "ステージ素材（未配置）");
        ImGui::Separator();
        if (ImGui::Button("ファイルからモデルを選択...")) browse_stage_asset();
        if (!selected_stage_asset_path.empty())
        {
            ImGui::TextWrapped("選択中: %s", selected_stage_asset_path.c_str());
            if (!selected_stage_cache_path.empty())
                ImGui::TextWrapped("キャッシュ: %s", selected_stage_cache_path.c_str());
        }
        ImGui::TextWrapped("状態: %s", stage_asset_status.c_str());
        draw_stage_placement_controls();
        ImGui::Checkbox("シェーダーを使う", &enable_stage_shader);
        if (ImGui::Button("シェーダー調整テーブルを開く"))
            set_editor_workspace(editor_workspace::shader_adjustment);
        if (stage_asset_placed && active_stage_placement_id != 0) sync_selected_entity_to_stage();
        if (shading_per_stage == SHADING_MODEL_PBR)
        {
            ImGui::TextDisabled("マットなステージ材質（鏡面反射なし）");
            ImGui::Checkbox("テクスチャを繰り返す", &stage_texture_wrap);
            ImGui::SliderFloat("Deferredテクスチャの濃さ", &stage_texture_contrast,
                0.50f, 2.50f, "%.2f");
            bool geometric = pbr.stage_material.options.x > 0.5f;
            bool normal_map = pbr.stage_material.options.y > 0.5f;
            bool diffuse_ibl = pbr.stage_material.options.z > 0.5f;
            bool shadow = pbr.stage_material.options.w > 0.5f;
            if (ImGui::Checkbox("面法線を使う", &geometric)) pbr.stage_material.options.x = geometric ? 1.0f : 0.0f;
            if (ImGui::Checkbox("法線マップ", &normal_map)) pbr.stage_material.options.y = normal_map ? 1.0f : 0.0f;
            if (ImGui::Checkbox("拡散IBL", &diffuse_ibl)) pbr.stage_material.options.z = diffuse_ibl ? 1.0f : 0.0f;
            if (ImGui::Checkbox("影を受ける", &shadow)) pbr.stage_material.options.w = shadow ? 1.0f : 0.0f;
            ImGui::ColorEdit3("基本色", &pbr.stage_material.base_tint.x);
        }
        break;

    case editor_selection::scene_entity:
        draw_scene_entity_inspector();
        break;

    case editor_selection::game_object:
        // GameObject / Component の編集は専用パネルへ委譲する。
        // ここに Component 型ごとの分岐は書かない。
        // 表示内容は ComponentRegistry と PropertyRegistry から自動生成される。
        object_inspector_panel.DrawContents(object_editor_context);
        break;

    case editor_selection::directional_light:
        ImGui::TextUnformatted("平行光源 / PBR");
        ImGui::Separator();
        ImGui::DragFloat3("方向", &light_direction.x, 0.01f, -1.0f, 1.0f);
        ImGui::ColorEdit3("色", &pbr.light.directional_color.x);
        ImGui::SliderFloat("強さ", &pbr.light.directional_color.w, 0, 10);
        ImGui::SliderFloat("IBL Diffuse", &pbr.light.ibl_params.x, 0, 4);
        ImGui::SliderFloat("IBL Specular", &pbr.light.ibl_params.y, 0, 4);
        ImGui::SliderFloat("AO Strength", &pbr.light.ibl_params.z, 0, 1);
        ImGui::SliderFloat("PBR Exposure", &pbr.light.ibl_params.w, 0, 4);
        ImGui::SliderFloat("Shadow Strength", &pbr.light.shadow_params.x, 0, 1);
        ImGui::SliderFloat("Shadow Bias", &pbr.light.shadow_params.y, 0, 0.01f, "%.5f");
        ImGui::SliderFloat("Shadow Filter", &pbr.light.shadow_params.z, 0, 4);
        {
            bool enabled = pbr.light.shadow_params.w > 0.5f;
            if (ImGui::Checkbox("PBR Shadow", &enabled)) pbr.light.shadow_params.w = enabled ? 1.0f : 0.0f;
        }
        if (ImGui::CollapsingHeader("Cascaded Shadow Map"))
        {
            ImGui::DragFloat4("Splits", &csm.constants.split_distances.x, 0.5f, 1, 500);
            ImGui::DragFloat("CSM Bias", &csm.constants.params.x, 0.0005f, 0, 0.05f, "%.5f");
            ImGui::DragFloat("Normal Bias", &csm.constants.params.y, 0.005f, 0, 1);
            ImGui::DragFloat("Filter", &csm.constants.params.z, 0.05f, 0, 8);
            bool enabled = csm.constants.params.w > 0.5f;
            if (ImGui::Checkbox("Enable CSM", &enabled)) csm.constants.params.w = enabled ? 1.0f : 0.0f;
        }
        break;

    case editor_selection::point_lights:
    {
        ImGui::TextUnformatted("点光源");
        ImGui::Separator();
        int count = lights.data.light_counts.x;
        const int old_count = count;
        if (ImGui::DragInt("個数", &count, 0.1f, 0, lights_manager::POINT_LIGHT_MAX))
        {
            lights.data.light_counts.x = count;
            for (int i = old_count; i < count; ++i)
            {
                lights.data.point_lights[i].position = { -4.0f + i * 2.5f, 3, -24, 14 };
                lights.data.point_lights[i].color = { 0.55f, 0.75f, 1, 1.8f };
            }
        }
        for (int i = 0; i < count; ++i)
        {
            ImGui::PushID(i);
            if (ImGui::CollapsingHeader(("Point Light " + std::to_string(i)).c_str()))
            {
                ImGui::DragFloat4("Position / Radius", &lights.data.point_lights[i].position.x, 0.05f);
                ImGui::ColorEdit4("Color / Intensity", &lights.data.point_lights[i].color.x);
            }
            ImGui::PopID();
        }
        break;
    }

    case editor_selection::rendering:
        ImGui::TextUnformatted("描画設定");
        ImGui::Separator();
        {
            bool fullscreen = is_fullscreen();
            if (ImGui::Checkbox("全画面表示 (F11 / Alt+Enter)", &fullscreen)) toggle_fullscreen();
        }
        ImGui::TextUnformatted("レンダラー: Deferred（固定）");
        ImGui::TextDisabled("輪郭線・半透明はDeferred照明後の追加パスとして合成します");
        ImGui::TextDisabled("Forward+は将来、別レンダラーとして追加できます");
        ImGui::Checkbox("静的メッシュ", &enable_static_meshes);
        if (ImGui::Button("シェーダー調整テーブルを開く"))
            set_editor_workspace(editor_workspace::shader_adjustment);
        if (ImGui::CollapsingHeader("シェーダー機能の有効化"))
        {
            ImGui::Checkbox("PBR", &use_pbr_skin);
            ImGui::Checkbox("トゥーン", &enable_toon_shader);
            ImGui::Checkbox("アンリット", &enable_unlit_shader);
            ImGui::Checkbox("輪郭線パス", &enable_outline_shader);
            ImGui::Checkbox("PBR影パス", &enable_pbr_shadow_shader);
        }
        {
            int output = render_graph.OutputIndex();
            if (ImGui::Combo("Render Output (F2)", &output, ReplayEngine::Rendering::RenderGraph::Names()))
            {
                render_graph.SetOutput(output);
                if (render_graph.RequiresDeferred()) enable_deferred = true;
            }
        }
        break;

    case editor_selection::post_process:
        ImGui::TextUnformatted("ポスト処理");
        ImGui::Separator();
        draw_screen_effect_stack();
        break;
    }
    ImGui::End();
}
