#include "framework.h"
#include "GoldenImageState.h"


namespace Capture = ReplayEngine::Rendering::Capture;

void framework::request_automated_frame_capture(const std::string& name)
{
    // Startup Scene の読み込みは非同期なので、ここでは要求を覚えるだけにする。
    // 実際の撮影要求は、描画済みフレームを数える終了処理から十分待って積む。
    golden_state_->golden_name = name;
    automated_frame_capture_pending = true;
}

bool framework::golden_last_capture_ok() const noexcept
{
    return golden_state_->golden_last_ok;
}

const std::string& framework::golden_last_capture_summary() const noexcept
{
    return golden_state_->golden_last_summary;
}

void framework::request_golden(golden_request_kind kind)
{
    golden_state_->golden_request = kind;
    golden_state_->golden_countdown = golden_state_->golden_settle_frames;
    golden_state_->golden_self_check_has_first = false;
    golden_state_->golden_self_check_first = Capture::Image{};

    const char* label =
        kind == golden_request_kind::capture ? u8"基準画像を撮ります" :
        kind == golden_request_kind::compare ? u8"基準画像と比べます" :
        u8"2 回撮って一致するか調べます";

    golden_state_->golden_last_summary = std::string(label) + u8"（" +
        std::to_string(golden_state_->golden_settle_frames) + u8" フレーム止めています）";
    golden_state_->golden_last_ok = false;
}

bool framework::prepare_dx12_golden_capture() noexcept
{
    if (golden_state_->golden_request == golden_request_kind::none)
        return false;
    if (golden_state_->golden_countdown > 0)
    {
        --golden_state_->golden_countdown;
        return false;
    }
    return dx12_device_context.RequestBackBufferCapture();
}

void framework::tick_golden_capture()
{
    if (golden_state_->golden_request == golden_request_kind::none) return;

    // まだ落ち着いていない。
    if (golden_state_->golden_countdown > 0)
    {
        --golden_state_->golden_countdown;
        return;
    }

    const golden_request_kind kind = golden_state_->golden_request;
    const std::string name = golden_state_->golden_name;

    // 何があってもここで要求を下ろす。
    // 下ろさないと、失敗したときに毎フレーム撮り続けることになる。
    golden_state_->golden_request = golden_request_kind::none;

    Capture::Image current;
    std::string error;
    bool capture_ok = false;
    if (dx12_framework_active)
    {
        if (dx12_device_context.ConsumeBackBufferCapture(
            current.rgba, current.width, current.height))
        {
            capture_ok = current.Valid();
        }
        if (!capture_ok) error = u8"DX12 Readback から画面を取得できません";
    }
    else
    {
        capture_ok = Capture::GoldenImage::CaptureBackBuffer(device.Get(),
            immediate_context.Get(), swap_chain.Get(), current, error);
    }
    if (!capture_ok)
    {
        golden_state_->golden_last_ok = false;
        golden_state_->golden_last_summary = u8"撮影に失敗しました: " + error;
        push_editor_log("Error", golden_state_->golden_last_summary);
        return;
    }

    if (kind == golden_request_kind::capture)
    {
        const std::filesystem::path path =
            Capture::GoldenImage::GoldenPath(name);
        if (!Capture::GoldenImage::SavePng(path, current, error))
        {
            golden_state_->golden_last_ok = false;
            golden_state_->golden_last_summary = u8"保存に失敗しました: " + error;
            push_editor_log("Error", golden_state_->golden_last_summary, path);
            return;
        }
        golden_state_->golden_last_ok = true;
        golden_state_->golden_last_summary = u8"基準画像を保存しました: " +
            path.generic_u8string();
        push_editor_log("Info", golden_state_->golden_last_summary, path);
        // 影の内訳は Editor のパネルからしか見えないので、撮影時は stderr へも出す。
        std::fprintf(stderr,
            "shadow stats: directional=%d preview=%d rendered=%d "
            "casters(prim=%d static=%d skinned=%d landscape=%d) "
            "skipped=%d culled=%d unresolved=%d draws=%d spot=%d point=%d "
            "coverage=%d coverage_unsupported=%d "
            "missing_bounds(prim=%d static=%d landscape=%d)\n",
            shadow_stats.directional_light_present ? 1 : 0,
            shadow_stats.directional_preview_light ? 1 : 0,
            shadow_stats.directional_shadow_rendered ? 1 : 0,
            shadow_stats.primitive_casters, shadow_stats.static_casters,
            shadow_stats.skinned_casters, shadow_stats.landscape_casters,
            shadow_stats.skipped_cast_shadow, shadow_stats.culled_casters,
            shadow_stats.skinned_unresolved, shadow_stats.shadow_draw_calls,
            shadow_stats.spot_shadow_lights, shadow_stats.point_shadow_lights,
            shadow_stats.coverage_casters, shadow_stats.coverage_unsupported,
            shadow_stats.missing_bounds_primitive, shadow_stats.missing_bounds_static,
            shadow_stats.missing_bounds_landscape);
        return;
    }

    if (kind == golden_request_kind::self_check)
    {
        if (!golden_state_->golden_self_check_has_first)
        {
            // 1 枚目。もう一度同じ手順で撮る。
            golden_state_->golden_self_check_first = current;
            golden_state_->golden_self_check_has_first = true;
            golden_state_->golden_request = golden_request_kind::self_check;
            golden_state_->golden_countdown = golden_state_->golden_settle_frames;
            golden_state_->golden_last_summary = u8"1 枚目を撮りました。2 枚目を撮ります";
            return;
        }

        Capture::CompareResult result;
        Capture::GoldenImage::Compare(golden_state_->golden_self_check_first, current,
            golden_state_->golden_tolerance, result, nullptr);

        golden_state_->golden_self_check_has_first = false;
        golden_state_->golden_self_check_first = Capture::Image{};

        golden_state_->golden_last_ok = result.Identical();
        if (golden_state_->golden_last_ok)
        {
            golden_state_->golden_last_summary =
                std::string(u8"自己診断 OK: 2 回撮って一致しました。") +
                u8"この設定で比較を信用してよい / " + result.Summary();
            push_editor_log("Info", golden_state_->golden_last_summary);
        }
        else
        {
            // ここが肝心。
            // 一致しないなら「まだ使えない」とはっきり言う。
            golden_state_->golden_last_summary =
                std::string(u8"自己診断 NG: 同じ画面を 2 回撮ったのに一致しません。") +
                u8"決定論が足りていないので、比較結果はまだ信用できません / " +
                result.Summary();
            push_editor_log("Warning", golden_state_->golden_last_summary);
        }
        return;
    }

    // ---- compare -------------------------------------------------------
    const std::filesystem::path golden_path =
        Capture::GoldenImage::GoldenPath(name);

    Capture::Image golden;
    if (!Capture::GoldenImage::LoadPng(golden_path, golden, error))
    {
        // 「基準が無い」を「差分 0」と混同させない。
        golden_state_->golden_last_ok = false;
        golden_state_->golden_last_summary = u8"基準画像がありません。先に「基準を撮る」を押してください: " +
            error;
        push_editor_log("Warning", golden_state_->golden_last_summary, golden_path);
        return;
    }

    Capture::Image diff;
    Capture::CompareResult result;
    Capture::GoldenImage::Compare(golden, current, golden_state_->golden_tolerance, result, &diff);

    // 今回撮ったものは必ず残す。
    // 差分が出たとき、実際に何が写っていたかを見られないと直せない。
    const std::filesystem::path latest_path =
        Capture::GoldenImage::LatestPath(name);
    std::string save_error;
    if (!Capture::GoldenImage::SavePng(latest_path, current, save_error))
    {
        push_editor_log("Warning",
            u8"今回の画像を保存できませんでした: " + save_error, latest_path);
    }

    if (result.size_mismatch)
    {
        golden_state_->golden_last_ok = false;
        golden_state_->golden_last_summary = u8"大きさが違います。ウィンドウサイズを基準と合わせてください / " +
            result.Summary();
        push_editor_log("Warning", golden_state_->golden_last_summary, golden_path);
        return;
    }

    if (!result.compared)
    {
        golden_state_->golden_last_ok = false;
        golden_state_->golden_last_summary = u8"比較できませんでした（画像が壊れている可能性があります）";
        push_editor_log("Error", golden_state_->golden_last_summary, golden_path);
        return;
    }

    golden_state_->golden_last_ok = result.Identical();
    golden_state_->golden_last_summary = result.Summary();

    if (golden_state_->golden_last_ok)
    {
        push_editor_log("Info", u8"回帰なし: " + golden_state_->golden_last_summary, golden_path);
        return;
    }

    const std::filesystem::path diff_path = Capture::GoldenImage::DiffPath(name);
    if (Capture::GoldenImage::SavePng(diff_path, diff, save_error))
    {
        golden_state_->golden_last_summary += u8" / 差分画像: " + diff_path.generic_u8string();
    }
    push_editor_log("Warning", u8"回帰あり: " + golden_state_->golden_last_summary, diff_path);
}

bool framework::golden_capture_pending() const noexcept
{
    return golden_state_->golden_request != golden_request_kind::none;
}

void framework::draw_golden_panel()
{
    if (!show_golden_panel) return;

    ImGui::SetNextWindowSize(ImVec2(560.0f, 320.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(u8"スクリーンショット回帰", &show_golden_panel))
    {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped(u8"「見た目が変わっていないこと」を機械で確かめます。"
        u8"シェーダや描画を触る前に基準を撮り、触ったあとに比べてください。");
    ImGui::Separator();

    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::InputText(u8"名前", golden_name_buffer,
        IM_ARRAYSIZE(golden_name_buffer)))
    {
        golden_state_->golden_name = golden_name_buffer;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Saved/Golden/%s.png", golden_state_->golden_name.c_str());

    ImGui::SetNextItemWidth(220.0f);
    ImGui::SliderInt(u8"止めるフレーム数", &golden_state_->golden_settle_frames, 1, 60);
    ImGui::SameLine();
    ImGui::TextDisabled(u8"TAA が収束するまで待つ");

    ImGui::SetNextItemWidth(220.0f);
    ImGui::SliderInt(u8"許容差", &golden_state_->golden_tolerance, 0, 8);
    ImGui::SameLine();
    ImGui::TextDisabled(u8"0 なら完全一致");

    ImGui::Separator();

    // 撮影中はボタンを出さない。
    //
    // ImGui 1.80 には BeginDisabled が無いので、押せる状態のまま
    // 無視するのではなく、そもそも出さないことで二重要求を防ぐ。
    // 押せるのに効かないボタンは「壊れている」と受け取られる。
    if (golden_capture_pending())
    {
        ImGui::TextDisabled(u8"撮影中... 残り %d フレーム", golden_state_->golden_countdown);
    }
    else
    {
        if (ImGui::Button(u8"基準を撮る", ImVec2(150.0f, 0.0f)))
        {
            request_golden(golden_request_kind::capture);
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"基準と比べる", ImVec2(150.0f, 0.0f)))
        {
            request_golden(golden_request_kind::compare);
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"自己診断", ImVec2(150.0f, 0.0f)))
        {
            request_golden(golden_request_kind::self_check);
        }
    }

    ImGui::TextDisabled(u8"自己診断 = 同じ画面を 2 回撮って一致するか。"
        u8"ここが通らないうちは比較結果を信用しないこと。");

    ImGui::Separator();

    if (!golden_state_->golden_last_summary.empty())
    {
        const ImVec4 color = golden_state_->golden_last_ok
            ? ImVec4(0.45f, 0.85f, 0.50f, 1.0f)
            : ImVec4(1.0f, 0.78f, 0.35f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("%s", golden_state_->golden_last_summary.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::End();
}
