#include "framework.h"

// SSAO / SSR / TAA / CSM の調整UI。
// 効果が見えているかを確認できるように、各機能のON/OFFと
// 「Render Output」で中間バッファを直接覗ける導線をここへまとめる。
void framework::draw_screen_space_settings()
{
#ifdef USE_IMGUI
    if (ImGui::CollapsingHeader("スクリーン空間パス (SSAO / SSR / TAA)"))
    {
        ImGui::TextDisabled("効果が見えないときは Render Output を SSAO / SSR に切り替えて");
        ImGui::TextDisabled("バッファそのものを確認してください。");

        // ---------------- SSAO ----------------
        if (ImGui::TreeNodeEx("SSAO (GTAO)", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::Checkbox("有効##ssao", &enable_ssao))
            {
                project_settings.SetSsaoEnabled(enable_ssao);
                save_project_settings();
            }
            if (!ssao_pass.Initialized())
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "初期化に失敗しています");

            ImGui::SliderFloat("探索半径 (m)", &ssao_pass.radius, 0.05f, 4.0f, "%.2f");
            ImGui::SliderFloat("強さ", &ssao_pass.intensity, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("コントラスト", &ssao_pass.power, 0.5f, 4.0f, "%.2f");
            ImGui::SliderInt("方向スライス数", &ssao_pass.slice_count, 1, 8);
            ImGui::SliderInt("探索ステップ数", &ssao_pass.step_count, 2, 24);
            ImGui::SliderFloat("法線オフセット", &ssao_pass.normal_bias, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("薄物補正", &ssao_pass.thin_occluder, 0.0f, 1.0f, "%.2f");
            ImGui::Checkbox("バイラテラルブラー", &ssao_pass.blur_enabled);
            ImGui::SliderFloat("ブラーの輪郭保持", &ssao_pass.blur_sharpness, 0.1f, 4.0f, "%.2f");
            ImGui::SliderFloat("フェード開始 (m)", &ssao_pass.fade_start, 1.0f, 400.0f, "%.0f");
            ImGui::SliderFloat("フェード終了 (m)", &ssao_pass.fade_end, 2.0f, 800.0f, "%.0f");
            ImGui::TreePop();
        }

        // ---------------- SSR ----------------
        if (ImGui::TreeNodeEx("SSR (スクリーンスペース反射)", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::Checkbox("有効##ssr", &enable_ssr))
            {
                project_settings.SetSsrEnabled(enable_ssr);
                save_project_settings();
            }
            if (!ssr_pass.Initialized())
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "初期化に失敗しています");
            ImGui::TextDisabled("反射源は前フレームの照明結果です。金属/低ラフネス面で効きます。");

            ImGui::SliderFloat("レイ長 (m)", &ssr_pass.max_distance, 1.0f, 200.0f, "%.0f");
            ImGui::SliderFloat("交差の厚み", &ssr_pass.thickness, 0.01f, 2.0f, "%.2f");
            ImGui::SliderFloat("ステップ幅 (px)", &ssr_pass.stride, 1.0f, 16.0f, "%.1f");
            ImGui::SliderInt("最大マーチ数", &ssr_pass.max_step, 8, 128);
            ImGui::SliderInt("二分探索回数", &ssr_pass.refine_step, 0, 8);
            ImGui::SliderFloat("適用する最大ラフネス", &ssr_pass.max_roughness, 0.05f, 1.0f, "%.2f");
            ImGui::SliderFloat("強さ##ssr", &ssr_pass.intensity, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("画面端フェード", &ssr_pass.edge_fade, 0.01f, 0.4f, "%.3f");
            ImGui::SliderFloat("レイ押し出し", &ssr_pass.ray_bias, 0.0f, 4.0f, "%.2f");
            ImGui::SliderFloat("resolve半径 (px)", &ssr_pass.resolve_radius, 0.0f, 40.0f, "%.1f");
            ImGui::SliderInt("resolveタップ数", &ssr_pass.resolve_tap_count, 1, 16);
            ImGui::TreePop();
        }

        // ---------------- TAA ----------------
        if (ImGui::TreeNodeEx("TAA (テンポラルAA)", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::Checkbox("有効##taa", &enable_taa))
            {
                // 切り替え直後の残像を出さないため履歴を破棄する。
                taa_pass.InvalidateHistory();
                project_settings.SetTaaEnabled(enable_taa);
                save_project_settings();
            }
            if (!taa_pass.Initialized())
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "初期化に失敗しています");
            ImGui::TextDisabled("履歴比率を上げるとエッジは滑らかに、下げると残像が減ります。");

            ImGui::SliderFloat("履歴比率", &taa_pass.blend, 0.0f, 0.98f, "%.2f");
            ImGui::SliderFloat("クリップ幅", &taa_pass.variance_gamma, 0.25f, 3.0f, "%.2f");
            ImGui::SliderFloat("シャープ化", &taa_pass.sharpness, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("速度リジェクト (px)", &taa_pass.max_velocity, 4.0f, 200.0f, "%.0f");
            ImGui::TextDisabled("履歴: %s", taa_pass.HistoryValid() ? "有効" : "無効");
            ImGui::TreePop();
        }

        // ---------------- CSM ----------------
        if (ImGui::TreeNodeEx("CSM (カスケードシャドウ)"))
        {
            bool csm_enabled = csm.constants.params.w >= 0.5f;
            if (ImGui::Checkbox("有効##csm", &csm_enabled))
                csm.constants.params.w = csm_enabled ? 1.0f : 0.0f;

            bool pcss_enabled = csm.constants.params2.w >= 0.5f;
            if (ImGui::Checkbox("PCSS (可変半影)", &pcss_enabled))
                csm.constants.params2.w = pcss_enabled ? 1.0f : 0.0f;

            ImGui::SliderFloat("深度バイアス", &csm.constants.params.x, 0.0f, 0.01f, "%.5f");
            ImGui::SliderFloat("法線オフセット (テクセル)", &csm.constants.params.y, 0.0f, 6.0f, "%.2f");
            ImGui::SliderFloat("フィルタ半径 (テクセル)", &csm.constants.params.z, 0.5f, 8.0f, "%.2f");
            ImGui::SliderFloat("カスケード混合幅 (m)", &csm.constants.params2.y, 0.0f, 30.0f, "%.1f");
            ImGui::SliderFloat("光源サイズ (UV)", &csm.constants.params2.z, 0.0005f, 0.02f, "%.4f");
            ImGui::SliderFloat("傾斜バイアス倍率", &csm.constants.params3.x, 0.0f, 8.0f, "%.2f");
            ImGui::SliderFloat("影の濃さ", &csm.constants.params3.z, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("分割の偏り", &csm.split_lambda, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("影の最遠距離 (m)", &csm.shadow_distance, 20.0f, 600.0f, "%.0f");
            ImGui::SliderFloat("キャスター延長 (m)", &csm.caster_extrusion, 0.0f, 200.0f, "%.0f");
            ImGui::Text("分割: %.1f / %.1f / %.1f / %.1f",
                csm.constants.split_distances.x, csm.constants.split_distances.y,
                csm.constants.split_distances.z, csm.constants.split_distances.w);
            ImGui::TreePop();
        }

        // ---------------- タイルドDeferred ----------------
        if (ImGui::TreeNodeEx("タイルドDeferred (Compute)", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("有効##tiled", &tiled_deferred.enabled);
            if (!tiled_deferred.Initialized())
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "初期化に失敗しています");
            ImGui::TextDisabled("16x16タイルごとにライトを絞ってCSでシェーディングします。");
            ImGui::TextDisabled("ライト数が多いときにPS版より伸びが緩くなります。");

            ImGui::Checkbox("ライト数ヒートマップ", &tiled_deferred.debug_heatmap);
            ImGui::SliderFloat("赤になるライト数", &tiled_deferred.heatmap_scale, 1.0f, 128.0f, "%.0f");
            ImGui::Text("タイル数: %u x %u", tiled_deferred.TileCountX(), tiled_deferred.TileCountY());
            ImGui::Text("投入ライト数: %zu", tiled_deferred.LightCount());
            ImGui::TextDisabled("※デバッグ表示(Render Output)中はPS版が使われます");
            ImGui::TreePop();
        }

        if (ImGui::Button("スクリーン空間パスを既定値へ戻す"))
        {
            // GPUリソースは触らず、調整値だけ初期状態へ戻す。
            enable_ssao = enable_ssr = enable_taa = true;
            project_settings.SetSsaoEnabled(true);
            project_settings.SetSsrEnabled(true);
            project_settings.SetTaaEnabled(true);
            save_project_settings();

            ssao_pass.radius = 0.75f;
            ssao_pass.intensity = 1.0f;
            ssao_pass.power = 1.6f;
            ssao_pass.thin_occluder = 1.0f;
            ssao_pass.slice_count = 4;
            ssao_pass.step_count = 8;
            ssao_pass.normal_bias = 0.35f;
            ssao_pass.blur_sharpness = 1.0f;
            ssao_pass.blur_enabled = true;
            ssao_pass.fade_start = 60.0f;
            ssao_pass.fade_end = 140.0f;

            ssr_pass.max_distance = 40.0f;
            ssr_pass.thickness = 0.4f;
            ssr_pass.stride = 3.0f;
            ssr_pass.max_step = 48;
            ssr_pass.refine_step = 5;
            ssr_pass.max_roughness = 0.65f;
            ssr_pass.intensity = 1.0f;
            ssr_pass.edge_fade = 0.12f;
            ssr_pass.ray_bias = 1.0f;
            ssr_pass.resolve_radius = 12.0f;
            ssr_pass.resolve_tap_count = 8;
            ssr_pass.InvalidateHistory();

            taa_pass.blend = 0.88f;
            taa_pass.variance_gamma = 1.0f;
            taa_pass.sharpness = 0.35f;
            taa_pass.max_velocity = 48.0f;
            taa_pass.InvalidateHistory();
        }
    }
#endif
}
