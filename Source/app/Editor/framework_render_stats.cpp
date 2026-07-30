#include "framework.h"
#include "gltf_model.h"

// 描画統計のデバッグ表示。F4で開閉する。
//
// 2種類の数字を並べているのは意味が違うため:
//   投入(CPU) = DrawIndexedへ渡した三角形数。LODやカリングを入れたときに
//               「送る量をどれだけ減らせたか」が直接見える。
//   実測(GPU) = D3D11のパイプライン統計。クリッピング後に実際に
//               ラスタライズされた三角形数なので、画面内の実数はこちら。
void framework::draw_render_stats_overlay()
{
#ifdef USE_IMGUI
    if (!show_render_stats) return;

    const auto& stats = ReplayEngine::Rendering::Stats();
    const auto& cpu = stats.Cpu();
    const auto& gpu = stats.Gpu();

    // 数字を3桁区切りにする。フレームごとに桁が変わるので視認性が要る。
    const auto separated = [](std::uint64_t value)
    {
        std::string text = std::to_string(value);
        for (int position = static_cast<int>(text.size()) - 3; position > 0; position -= 3)
            text.insert(static_cast<size_t>(position), ",");
        return text;
    };

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    // 左下に置く。右側はインスペクタが常駐しているため重ねると両方読めない。
    // 3Dビューの左下は情報密度が低く、邪魔になりにくい。
    //
    // 初期位置は imgui.ini に保存された古い座標(インスペクタと重なる位置)が
    // 残っていると FirstUseEver では上書きできない。そのため初回フレームだけ
    // Always で強制し、以降はユーザーの移動を尊重する。
    constexpr float kWindowWidth = 250.0f;
    constexpr float kMargin = 12.0f;
    constexpr float kHierarchyWidth = 300.0f;
    const ImGuiCond placement = stats_window_placed_ ? ImGuiCond_FirstUseEver : ImGuiCond_Always;
    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x + kHierarchyWidth + kMargin,
               viewport->Pos.y + viewport->Size.y - kMargin),
        placement, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(kWindowWidth, 0.0f), placement);
    ImGui::SetNextWindowBgAlpha(0.85f);
    stats_window_placed_ = true;

    if (ImGui::Begin(u8"描画統計 (F4)", &show_render_stats,
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize))
    {
        const float frame_time = ImGui::GetIO().DeltaTime * 1000.0f;
        ImGui::Text(u8"FPS %.1f  /  %.2f ms", ImGui::GetIO().Framerate, frame_time);
        // 要点だけ常時表示する。詳細は下の折りたたみへ。
        if (gpu.valid)
        {
            const double screen_pixels =
                static_cast<double>(SCREEN_WIDTH) * static_cast<double>(SCREEN_HEIGHT);
            ImGui::Text(u8"三角形 %s / DC %s",
                separated(gpu.rasterized_primitives).c_str(),
                separated(cpu.draw_calls).c_str());
            ImGui::Text(u8"オーバードロー %.2fx",
                static_cast<double>(gpu.pixel_shader_invocations) / (std::max)(screen_pixels, 1.0));
        }
        ImGui::Separator();

        if (ImGui::CollapsingHeader(u8"投入 / 実測"))
        {
        ImGui::TextUnformatted(u8"■ 投入 (CPU集計)");
        ImGui::Text(u8"  三角形    %s", separated(cpu.triangles).c_str());
        ImGui::Text(u8"  頂点      %s", separated(cpu.vertices).c_str());
        ImGui::Text(u8"  ドローコール %s", separated(cpu.draw_calls).c_str());

        ImGui::Separator();
        ImGui::TextUnformatted(u8"■ 実測 (GPUパイプライン統計)");
        if (!stats.Initialized())
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), u8"  クエリの初期化に失敗");
        }
        else if (!gpu.valid)
        {
            ImGui::TextDisabled(u8"  計測中...");
        }
        else
        {
            ImGui::Text(u8"  画面内の三角形 %s", separated(gpu.rasterized_primitives).c_str());
            ImGui::Text(u8"  投入プリミティブ %s", separated(gpu.input_primitives).c_str());
            // クリップで落ちた割合。視錐台外や裏面がどれだけあるかの目安。
            if (gpu.input_primitives > 0)
            {
                const double survived = static_cast<double>(gpu.rasterized_primitives) /
                    static_cast<double>(gpu.input_primitives);
                ImGui::Text(u8"  通過率 %.1f%% (棄却 %s)", survived * 100.0,
                    separated(gpu.input_primitives > gpu.rasterized_primitives
                        ? gpu.input_primitives - gpu.rasterized_primitives : 0).c_str());
            }
            ImGui::Text(u8"  VS実行 %s", separated(gpu.vertex_shader_invocations).c_str());
            ImGui::Text(u8"  PS実行 %s", separated(gpu.pixel_shader_invocations).c_str());
            if (gpu.compute_shader_invocations > 0)
                ImGui::Text(u8"  CS実行 %s", separated(gpu.compute_shader_invocations).c_str());
        }
        ImGui::TextDisabled(u8"影/ポストのパスも含む");
        } // 投入 / 実測

        // 最適化の効き具合と切り替え。数値だけ見たいときは畳めるようにする。
        if (ImGui::CollapsingHeader(u8"最適化 (カリング / LOD / プリパス)"))
        {
            auto& culling = ReplayEngine::Rendering::Culling();
            ImGui::TextUnformatted(u8"■ 視錐台カリング");
            if (ImGui::Checkbox(u8"  有効##culling", &culling.enabled)) {}
            if (culling.tested > 0)
            {
                const float ratio = 100.0f * static_cast<float>(culling.culled) /
                    static_cast<float>(culling.tested);
                ImGui::Text(u8"  除外 %u / %u (%.1f%%)",
                    culling.culled, culling.tested, ratio);
            }
            else
            {
                ImGui::TextDisabled(u8"  判定対象なし");
            }

            // --- 自動LOD ---
            ImGui::Separator();
            ImGui::TextUnformatted(u8"■ 自動LOD (QEM簡略化)");
            if (culling.lod_building)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                    u8"  生成中... (完了までLOD0で描画)");
            }
            else if (culling.lod_available == 0)
            {
                ImGui::TextDisabled(u8"  LODなし (三角形が少ないか未生成)");
            }
            else
            {
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
                    u8"  準備完了 (最大LOD%u)", culling.lod_available);
            }
            ImGui::Checkbox(u8"  有効##lod", &culling.lod_enabled);
            ImGui::SliderFloat(u8"  切替の画面高さ(px)",
                &culling.lod_pixel_threshold, 40.0f, 1200.0f, "%.0f");
            const char* const forced_names[]{
                u8"自動", u8"LOD0固定", u8"LOD1固定", u8"LOD2固定", u8"LOD3固定" };
            int forced = culling.forced_lod + 1;
            if (ImGui::Combo(u8"  強制LOD", &forced, forced_names, 5))
                culling.forced_lod = forced - 1;
            ImGui::Text(u8"  描画数 L0:%u L1:%u L2:%u L3:%u",
                culling.lod_draws[0], culling.lod_draws[1],
                culling.lod_draws[2], culling.lod_draws[3]);

            // --- 深度プリパス ---
            ImGui::Separator();
            ImGui::TextUnformatted(u8"■ 深度プリパス");
            ImGui::Checkbox(u8"  有効##prepass", &enable_depth_prepass);
            ImGui::TextDisabled(u8"  G-BufferのPS実行を最前面の1回に抑えます");
            ImGui::TextDisabled(u8"  頂点処理は2回になるのでLODと併用が前提");
        }


        // --- ロード時間の内訳 ---
        if (stage_gltf_model &&
            ImGui::CollapsingHeader(u8"ロード内訳"))
        {
            const auto& timings = stage_gltf_model->Timings();
            ImGui::TextUnformatted(u8"■ ステージのロード内訳");
            ImGui::Text(u8"  合計          %.0f ms", timings.total_ms);
            ImGui::Text(u8"  glTF解析      %.0f ms", timings.parse_ms);
            ImGui::Text(u8"  画像(%d枚)    %.0f ms",
                timings.image_count, timings.image_decode_ms);
            ImGui::Text(u8"  ジオメトリ    %.0f ms", timings.geometry_ms);
            ImGui::Text(u8"  LODキャッシュ %.0f ms %s", timings.lod_cache_ms,
                timings.lod_from_cache ? u8"(ヒット)" : u8"(ミス→生成)");
            ImGui::TextColored(
                timings.mesh_from_cache ? ImVec4(0.5f, 1.0f, 0.5f, 1.0f)
                                        : ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                timings.mesh_from_cache ? u8"  メッシュキャッシュ: ヒット"
                                        : u8"  メッシュキャッシュ: ミス→生成");
        }

        ImGui::TextDisabled(u8"影/ポストのパスも含む");
    }
    ImGui::End();
#endif
}
