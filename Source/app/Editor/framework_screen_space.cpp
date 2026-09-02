#include "framework.h"

// SSAO / SSR / TAA / CSM の調整UI。
// 効果が見えているかを確認できるように、各機能のON/OFFと
// 「Render Output」で中間バッファを直接覗ける導線をここへまとめる。
void framework::draw_screen_space_settings()
{
#ifdef USE_IMGUI
    project_settings_file_undo_enabled = object_editor_context.CanEdit();
    if (ImGui::CollapsingHeader("スクリーン空間パス (SSAO / SSR / TAA)"))
    {
        bool screen_space_settings_changed = false;
        ImGui::TextDisabled(u8"効果が見えないときは描画出力を SSAO / SSR に切り替えて");
        ImGui::TextDisabled("バッファそのものを確認してください。");

        // ---------------- SSAO ----------------
        if (ImGui::TreeNodeEx("SSAO (GTAO)", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::Checkbox("有効##ssao", &enable_ssao))
            {
                ssao_pass.enabled = enable_ssao;
                project_settings.SetSsaoEnabled(enable_ssao);
                save_project_settings();
            }
            ReplayEngine::Editor::EditorHelp::Item("screen_space.ssao.enabled",
                u8"SSAO を有効にすると、接地部や隙間に環境遮蔽を加えます。");
            if (!ssao_pass.Initialized())
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "初期化に失敗しています");

            screen_space_settings_changed |= ImGui::SliderFloat("探索半径 (m)", &ssao_pass.radius, 0.05f, 4.0f, "%.2f");
            ReplayEngine::Editor::EditorHelp::Item("screen_space.ssao.radius",
                u8"探索半径 (m)。0.05〜4.00 m の範囲で、周囲の遮蔽を探す距離を指定します。");
            screen_space_settings_changed |= ImGui::SliderFloat("強さ", &ssao_pass.intensity, 0.0f, 1.0f, "%.2f");
            ReplayEngine::Editor::EditorHelp::Item("screen_space.ssao.intensity",
                u8"強さ (0〜1)。接地部や隙間へ加える暗さの量を指定します。");
            screen_space_settings_changed |= ImGui::SliderFloat("コントラスト", &ssao_pass.power, 0.5f, 4.0f, "%.2f");
            ReplayEngine::Editor::EditorHelp::Item("screen_space.ssao.power",
                u8"コントラスト (0.5〜4.0)。遮蔽の暗部をどれだけ強調するかを指定します。");
            screen_space_settings_changed |= ImGui::SliderInt("方向スライス数", &ssao_pass.slice_count, 1, 8);
            ReplayEngine::Editor::EditorHelp::Item("screen_space.ssao.slice_count",
                u8"方向スライス数 (1〜8)。周囲を調べる方向の本数で、多いほど輪郭を細かく調べます。");
            screen_space_settings_changed |= ImGui::SliderInt("探索ステップ数", &ssao_pass.step_count, 2, 12);
            ReplayEngine::Editor::EditorHelp::Item("screen_space.ssao.step_count",
                u8"探索ステップ数 (2〜12)。各方向で深度を調べる回数で、多いほど探索が細かくなります。");
            screen_space_settings_changed |= ImGui::SliderFloat("法線オフセット", &ssao_pass.normal_bias, 0.0f, 2.0f, "%.2f");
            ReplayEngine::Editor::EditorHelp::Item("screen_space.ssao.normal_bias",
                u8"法線オフセット (0〜2)。表面自身を遮蔽物として拾う量をずらします。");
            screen_space_settings_changed |= ImGui::SliderFloat("薄物補正", &ssao_pass.thin_occluder, 0.0f, 1.0f, "%.2f");
            ReplayEngine::Editor::EditorHelp::Item("screen_space.ssao.thin_occluder",
                u8"薄物補正 (0〜1)。薄い遮蔽物を遮蔽として反映する量を指定します。");
            screen_space_settings_changed |= ImGui::SliderFloat("フェード開始 (m)", &ssao_pass.fade_start, 1.0f, 400.0f, "%.0f");
            ReplayEngine::Editor::EditorHelp::Item("screen_space.ssao.fade_start",
                u8"フェード開始 (m)。この距離から SSAO を遠景で弱め始めます。");
            screen_space_settings_changed |= ImGui::SliderFloat("フェード終了 (m)", &ssao_pass.fade_end, 2.0f, 800.0f, "%.0f");
            ReplayEngine::Editor::EditorHelp::Item("screen_space.ssao.fade_end",
                u8"フェード終了 (m)。この距離で SSAO を遠景から無くします。");
            ImGui::TreePop();
        }

        // ---------------- SSR ----------------
        if (ImGui::TreeNodeEx("SSR (スクリーンスペース反射)", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::Checkbox("有効##ssr", &enable_ssr))
            {
                ssr_pass.enabled = enable_ssr;
                project_settings.SetSsrEnabled(enable_ssr);
                save_project_settings();
            }
            ReplayEngine::Editor::EditorHelp::Item("screen_space.ssr.enabled",
                u8"SSR を有効にすると、画面内に映っている前フレームの照明結果を反射として使います。");
            if (!ssr_pass.Initialized())
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "初期化に失敗しています");
            ImGui::TextDisabled("反射源は前フレームの照明結果です。金属/低ラフネス面で効きます。");

            screen_space_settings_changed |= ImGui::SliderFloat("レイ長 (m)", &ssr_pass.max_distance, 1.0f, 200.0f, "%.0f");
            ReplayEngine::Editor::EditorHelp::Item("screen_space.ssr.max_distance",
                u8"レイ長 (m)。1〜200 m の範囲で、反射を探す最大距離を指定します。");
            screen_space_settings_changed |= ImGui::SliderFloat("交差の厚み", &ssr_pass.thickness, 0.01f, 2.0f, "%.2f");
            ReplayEngine::Editor::EditorHelp::Item("screen_space.ssr.thickness",
                u8"交差の厚み (m)。反射レイが深度へ交差したとみなす奥行き幅を指定します。");
            screen_space_settings_changed |= ImGui::SliderFloat("ステップ幅 (px)", &ssr_pass.stride, 1.0f, 16.0f, "%.1f");
            ReplayEngine::Editor::EditorHelp::Item("screen_space.ssr.stride",
                u8"ステップ幅 (px)。画面上で反射レイを進める画素間隔で、小さいほど交差を細かく探します。");
            screen_space_settings_changed |= ImGui::SliderInt("最大マーチ数", &ssr_pass.max_step, 4, 64);
            ReplayEngine::Editor::EditorHelp::Item("screen_space.ssr.max_step",
                u8"最大マーチ数 (4〜64)。反射レイを進める回数の上限で、多いほど遠くまで探せます。");
            screen_space_settings_changed |= ImGui::SliderInt("二分探索回数", &ssr_pass.refine_step, 0, 8);
            ReplayEngine::Editor::EditorHelp::Item("screen_space.ssr.refine_step",
                u8"二分探索回数 (0〜8)。交差位置を絞り込む回数で、多いほど境界を細かく求めます。");
            screen_space_settings_changed |= ImGui::SliderFloat("適用する最大ラフネス", &ssr_pass.max_roughness, 0.05f, 1.0f, "%.2f");
            ReplayEngine::Editor::EditorHelp::Item("screen_space.ssr.max_roughness",
                u8"適用する最大ラフネス (0.05〜1.0)。この値以下の粗さの面だけへ SSR を適用します。");
            screen_space_settings_changed |= ImGui::SliderFloat("強さ##ssr", &ssr_pass.intensity, 0.0f, 2.0f, "%.2f");
            ReplayEngine::Editor::EditorHelp::Item("screen_space.ssr.intensity",
                u8"強さ (0〜2)。画面へ混ぜる反射の量を指定します。");
            screen_space_settings_changed |= ImGui::SliderFloat("画面端フェード", &ssr_pass.edge_fade, 0.01f, 0.4f, "%.3f");
            ReplayEngine::Editor::EditorHelp::Item("screen_space.ssr.edge_fade",
                u8"画面端フェード (0.01〜0.4)。画面の端で反射を弱める幅を正規化座標で指定します。");
            screen_space_settings_changed |= ImGui::SliderFloat("レイ押し出し", &ssr_pass.ray_bias, 0.0f, 4.0f, "%.2f");
            ReplayEngine::Editor::EditorHelp::Item("screen_space.ssr.ray_bias",
                u8"レイ押し出し (0〜4)。反射レイの開始位置を表面からずらして自己交差を抑えます。");
            screen_space_settings_changed |= ImGui::SliderFloat("resolve半径 (px)", &ssr_pass.resolve_radius, 0.0f, 40.0f, "%.1f");
            ReplayEngine::Editor::EditorHelp::Item("screen_space.ssr.resolve_radius",
                u8"resolve 半径 (px)。反射を周囲から集める画面上の半径を指定します。");
            screen_space_settings_changed |= ImGui::SliderInt("resolveタップ数", &ssr_pass.resolve_tap_count, 1, 16);
            ReplayEngine::Editor::EditorHelp::Item("screen_space.ssr.resolve_tap_count",
                u8"resolve タップ数 (1〜16)。反射を集めるサンプル数で、多いほど滑らかになります。");
            ImGui::TreePop();
        }

        // ---------------- TAA ----------------
        if (ImGui::TreeNodeEx("TAA (テンポラルAA)", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::Checkbox("有効##taa", &enable_taa))
            {
                // 切り替え直後の残像を出さないため履歴を破棄する。
                taa_pass.InvalidateHistory();
                taa_pass.enabled = enable_taa;
                project_settings.SetTaaEnabled(enable_taa);
                save_project_settings();
            }
            ReplayEngine::Editor::EditorHelp::Item("screen_space.taa.enabled",
                u8"TAA を有効にすると、複数フレームの画像を使ってちらつきとギザギザを抑えます。");
            if (!taa_pass.Initialized())
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "初期化に失敗しています");
            ImGui::TextDisabled("履歴比率を上げるとエッジは滑らかに、下げると残像が減ります。");

            screen_space_settings_changed |= ImGui::SliderFloat("履歴比率", &taa_pass.blend, 0.0f, 0.98f, "%.2f");
            ReplayEngine::Editor::EditorHelp::Item("screen_space.taa.blend",
                u8"履歴比率 (0〜0.98)。過去フレームを混ぜる量で、大きいほど滑らかですが残像が増えます。");
            screen_space_settings_changed |= ImGui::SliderFloat("クリップ幅", &taa_pass.variance_gamma, 0.25f, 3.0f, "%.2f");
            ReplayEngine::Editor::EditorHelp::Item("screen_space.taa.variance_gamma",
                u8"クリップ幅 (0.25〜3.0)。履歴色を現在フレームへ合わせる許容幅を指定します。");
            screen_space_settings_changed |= ImGui::SliderFloat("シャープ化", &taa_pass.sharpness, 0.0f, 1.0f, "%.2f");
            ReplayEngine::Editor::EditorHelp::Item("screen_space.taa.sharpness",
                u8"シャープ化 (0〜1)。TAA 後の画像へ加える輪郭強調の量を指定します。");
            screen_space_settings_changed |= ImGui::SliderFloat("速度リジェクト (px)", &taa_pass.max_velocity, 4.0f, 200.0f, "%.0f");
            ReplayEngine::Editor::EditorHelp::Item("screen_space.taa.max_velocity",
                u8"速度リジェクト (px)。この画面速度を超える画素では履歴を捨てて残像を抑えます。");
            ImGui::TextDisabled("履歴: %s", taa_pass.HistoryValid() ? "有効" : "無効");
            ImGui::TreePop();
        }

        // ---------------- 影 (全体設定。個々の値は Light Component が正本) ----------------
        if (ImGui::TreeNodeEx("影 (Shadow)", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("動的影を使う", &enable_dynamic_shadows);
            ImGui::TextDisabled("切ると Directional / Point / Spot すべての影を止めます。");

            ImGui::Checkbox("ライトが無いときも Scene View で影を出す",
                &editor_preview_light_casts_shadows);
            ImGui::TextDisabled("Scene に保存されないプレビュー光の影です。");
            ImGui::TextDisabled("Play / Standalone では Scene の照明設定だけが使われます。");

            ImGui::Separator();
            ImGui::Checkbox("Point / Spot の影##local", &local_shadows.enabled);
            int local_resolution = static_cast<int>(local_shadows.resolution_setting);
            if (ImGui::SliderInt("影マップ解像度##local", &local_resolution, 256, 2048))
            {
                // 解像度を変えると次のフレームで作り直される。
                local_shadows.resolution_setting =
                    static_cast<std::uint32_t>((std::max)(256, local_resolution));
            }
            ImGui::TextDisabled("Spot は 1 灯 1 枚、Point は 1 灯 6 枚使います。");
            ImGui::Text("影付きライト枠: Spot %u / Point %u",
                ReplayEngine::Rendering::LocalShadowAtlas::kMaxSpotShadows,
                ReplayEngine::Rendering::LocalShadowAtlas::kMaxPointShadows);
            ImGui::Text("影マップ: %s",
                local_shadows.AtlasReady() ? "確保済み" : "未確保 (影付きライトなし)");

            ImGui::Separator();
            ImGui::TextUnformatted("このフレームの影");
            // 「影が出ない」ときにどこで止まっているかを切り分けるための表示。
            if (!shadow_stats.directional_light_present)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                    shadow_stats.directional_preview_light
                        ? "Directional Light なし (プレビュー光で代用中)"
                        : "Directional Light なし");
            }
            ImGui::Text("Directional 影: %s",
                shadow_stats.directional_shadow_rendered ? "生成した" : "生成していない");
            ImGui::Text("キャスター: Primitive %d / 静的 %d / Skinned %d / Landscape %d",
                shadow_stats.primitive_casters, shadow_stats.static_casters,
                shadow_stats.skinned_casters, shadow_stats.landscape_casters);
            ImGui::Text("Cast Shadow=false で除外: %d", shadow_stats.skipped_cast_shadow);
            ImGui::Text("影ボリューム外で除外: %d", shadow_stats.culled_casters);
            if (shadow_stats.skinned_unresolved > 0)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f),
                    "Mesh Asset を解決できない Skinned Mesh: %d",
                    shadow_stats.skinned_unresolved);
                ImGui::TextDisabled("影ではなく Asset の問題です (通常描画にも出ていません)");
            }
            ImGui::Text("影の描画コール: %d", shadow_stats.shadow_draw_calls);
            ImGui::Text("影付き Spot %d / Point %d",
                shadow_stats.spot_shadow_lights, shadow_stats.point_shadow_lights);
            ImGui::Text("Effect Stack の面消しを反映: %d", shadow_stats.coverage_casters);
            if (shadow_stats.coverage_unsupported > 0)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                    "影で再現できない面消し Effect: %d", shadow_stats.coverage_unsupported);
                ImGui::TextDisabled("投げ縄/画像マスクの範囲、5個目以降、別々のマスク画像が対象です");
            }
            const int missing_bounds = shadow_stats.missing_bounds_primitive +
                shadow_stats.missing_bounds_static + shadow_stats.missing_bounds_landscape;
            ImGui::Text("bounding_box 未設定: Primitive %d / 静的 %d / Landscape %d",
                shadow_stats.missing_bounds_primitive,
                shadow_stats.missing_bounds_static,
                shadow_stats.missing_bounds_landscape);
            if (missing_bounds > 0)
                ImGui::TextDisabled("影ボリュームの選別を掛けずに必ず描きます (影は正しく出ます)");
            if (shadow_stats.TotalCasters() == 0 && shadow_stats.directional_shadow_rendered)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                    "キャスターが 0 件です (Cast Shadow か影の最遠距離を確認)");
            }

            // source_path がフォルダのままの Asset は影以前に通常描画へも出ない。
            const std::vector<const ReplayEngine::Assets::AssetRecord*> folder_assets =
                asset_database.FindFolderSourcePaths();
            if (!folder_assets.empty())
            {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f),
                    "source が ファイルではない Asset: %d 件",
                    static_cast<int>(folder_assets.size()));
                ImGui::TextDisabled("拡張子が無いのでファイル名を復元できません。登録し直してください");
                for (const ReplayEngine::Assets::AssetRecord* record : folder_assets)
                {
                    ImGui::BulletText("%s  (%s)", record->display_name.c_str(),
                        record->source_path.generic_u8string().c_str());
                }
            }
            ImGui::TreePop();
        }

        // ---------------- CSM ----------------
        if (ImGui::TreeNodeEx("CSM (カスケードシャドウ)"))
        {
            // params.w は毎フレーム作り直されるので UI はユーザー設定側だけを触ること。
            ImGui::Checkbox("有効##csm", &csm_enabled_setting);
            ImGui::SameLine();
            ImGui::TextDisabled(csm.constants.params.w >= 0.5f ? "(稼働中)" : "(停止中)");

            bool pcss_enabled = csm.constants.params2.w >= 0.5f;
            if (ImGui::Checkbox("PCSS (可変半影)", &pcss_enabled))
                csm.constants.params2.w = pcss_enabled ? 1.0f : 0.0f;

            // バイアス・濃さ・最遠距離は Directional Light Component が正本。
            if (shadow_stats.directional_light_present)
            {
                ImGui::TextDisabled("深度バイアス: %.3f m (Light Component)",
                    csm.constants.params.x);
                ImGui::TextDisabled("法線オフセット: %.2f (Light Component)",
                    csm.constants.params.y);
                ImGui::TextDisabled("影の濃さ: %.2f (Light Component)",
                    csm.constants.params3.z);
                ImGui::TextDisabled("影の最遠距離: %.0f m (Light Component)",
                    csm.shadow_distance);
                ImGui::TextDisabled("↑ これらは Directional Light の Inspector で編集します");
            }
            else
            {
                ImGui::SliderFloat("深度バイアス (m)", &csm.constants.params.x, 0.0f, 0.5f, "%.3f");
                ImGui::SliderFloat("法線オフセット (テクセル)", &csm.constants.params.y, 0.0f, 6.0f, "%.2f");
                ImGui::SliderFloat("影の濃さ", &csm.constants.params3.z, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("影の最遠距離 (m)", &csm.shadow_distance, 20.0f, 600.0f, "%.0f");
            }
            ImGui::SliderFloat("フィルタ半径 (テクセル)", &csm.constants.params.z, 0.5f, 8.0f, "%.2f");
            ImGui::SliderFloat("カスケード混合幅 (m)", &csm.constants.params2.y, 0.0f, 30.0f, "%.1f");
            ImGui::SliderFloat("光源サイズ (UV)", &csm.constants.params2.z, 0.0005f, 0.02f, "%.4f");
            ImGui::SliderFloat("傾斜バイアス倍率", &csm.constants.params3.x, 0.0f, 8.0f, "%.2f");
            ImGui::SliderFloat("分割の偏り", &csm.split_lambda, 0.0f, 1.0f, "%.2f");
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
            ImGui::TextDisabled(u8"※描画出力のデバッグ表示中はPS版が使われます");
            ImGui::TreePop();
        }

        if (ImGui::Button("スクリーン空間パスを既定値へ戻す"))
        {
            // GPUリソースは触らず、調整値だけ初期状態へ戻す。
            enable_ssao = enable_ssr = enable_taa = true;
            project_settings.SetSsaoEnabled(true);
            project_settings.SetSsrEnabled(true);
            project_settings.SetTaaEnabled(true);

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
            ssao_pass.enabled = true;
            ssr_pass.enabled = true;
            taa_pass.enabled = true;
            ssr_pass.InvalidateHistory();

            taa_pass.blend = 0.88f;
            taa_pass.variance_gamma = 1.0f;
            taa_pass.sharpness = 0.35f;
            taa_pass.max_velocity = 48.0f;
            taa_pass.InvalidateHistory();
            save_project_settings();
        }
        ReplayEngine::Editor::EditorHelp::Item("button.screen_space.reset_defaults");
        if (screen_space_settings_changed) save_project_settings();
        if (external_file_history.InTransaction() && !ImGui::IsAnyItemActive())
        {
            std::string undo_error;
            external_file_history.Commit(undo_error);
            if (!undo_error.empty()) project_settings_status = undo_error;
        }
    }
#endif
}
