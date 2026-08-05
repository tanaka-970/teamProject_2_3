#include "framework.h"

// スクリーンショット回帰（フェーズ 18）。
//
// 【なぜこれを作ったか】
//   シェーダ基盤のフェーズ 4・5・6・11・15・16 の完了条件はすべて
//   「スクリーンショットが 1 ピクセルも変わらないこと」。
//   ところが、それを確かめる手段が無いまま進めていた。
//   つまり完了条件は誰も検証していなかった。
//
//   目視では気付けない。色が 1 段ずれても人間には分からないし、
//   画面の隅で起きた変化はまず見落とす。数で止めるしかない。
//
// 【決定論をどう作るか】
//   撮る前に数フレーム「止めた状態」で回す。
//     ・update へ渡す elapsed_time を 0 にする（アニメ・物理・粒子が進まない）
//     ・frame_index を進めない（時間ノイズが同じ値を返す）
//   これで TAA の履歴が同じ絵へ収束する。
//
//   止めずに撮ると毎回違う絵になり、差分が出続けて誰も結果を見なくなる。
//   それは「検査が無い」より悪い。あると思って見ていない状態になるから。
//
// 【自己診断がある理由】
//   決定論が足りているかどうかは、こちらの都合では決まらない。
//   「2 回撮って一致するか」を実際に試すのが唯一の確かめ方。
//   一致しないなら、この仕組みはまだ使えないと分かる。
//   分かる形にしておくのが要点で、黙って通すのが一番悪い。

namespace Capture = ReplayEngine::Rendering::Capture;

void framework::request_golden(golden_request_kind kind)
{
    golden_request = kind;
    golden_countdown = golden_settle_frames;
    golden_self_check_has_first = false;
    golden_self_check_first = Capture::Image{};

    const char* label =
        kind == golden_request_kind::capture ? u8"基準画像を撮ります" :
        kind == golden_request_kind::compare ? u8"基準画像と比べます" :
        u8"2 回撮って一致するか調べます";

    golden_last_summary = std::string(label) + u8"（" +
        std::to_string(golden_settle_frames) + u8" フレーム止めています）";
    golden_last_ok = false;
}

void framework::tick_golden_capture()
{
    if (golden_request == golden_request_kind::none) return;

    // まだ落ち着いていない。
    if (golden_countdown > 0)
    {
        --golden_countdown;
        return;
    }

    const golden_request_kind kind = golden_request;
    const std::string name = golden_name;

    // 何があってもここで要求を下ろす。
    // 下ろさないと、失敗したときに毎フレーム撮り続けることになる。
    golden_request = golden_request_kind::none;

    Capture::Image current;
    std::string error;
    if (!Capture::GoldenImage::CaptureBackBuffer(device.Get(),
        immediate_context.Get(), swap_chain.Get(), current, error))
    {
        golden_last_ok = false;
        golden_last_summary = u8"撮影に失敗しました: " + error;
        push_editor_log("Error", golden_last_summary);
        return;
    }

    if (kind == golden_request_kind::capture)
    {
        const std::filesystem::path path =
            Capture::GoldenImage::GoldenPath(name);
        if (!Capture::GoldenImage::SavePng(path, current, error))
        {
            golden_last_ok = false;
            golden_last_summary = u8"保存に失敗しました: " + error;
            push_editor_log("Error", golden_last_summary, path);
            return;
        }
        golden_last_ok = true;
        golden_last_summary = u8"基準画像を保存しました: " +
            path.generic_u8string();
        push_editor_log("Info", golden_last_summary, path);
        return;
    }

    if (kind == golden_request_kind::self_check)
    {
        if (!golden_self_check_has_first)
        {
            // 1 枚目。もう一度同じ手順で撮る。
            golden_self_check_first = current;
            golden_self_check_has_first = true;
            golden_request = golden_request_kind::self_check;
            golden_countdown = golden_settle_frames;
            golden_last_summary = u8"1 枚目を撮りました。2 枚目を撮ります";
            return;
        }

        Capture::CompareResult result;
        Capture::GoldenImage::Compare(golden_self_check_first, current,
            golden_tolerance, result, nullptr);

        golden_self_check_has_first = false;
        golden_self_check_first = Capture::Image{};

        golden_last_ok = result.Identical();
        if (golden_last_ok)
        {
            golden_last_summary =
                std::string(u8"自己診断 OK: 2 回撮って一致しました。") +
                u8"この設定で比較を信用してよい / " + result.Summary();
            push_editor_log("Info", golden_last_summary);
        }
        else
        {
            // ここが肝心。
            // 一致しないなら「まだ使えない」とはっきり言う。
            golden_last_summary =
                std::string(u8"自己診断 NG: 同じ画面を 2 回撮ったのに一致しません。") +
                u8"決定論が足りていないので、比較結果はまだ信用できません / " +
                result.Summary();
            push_editor_log("Warning", golden_last_summary);
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
        golden_last_ok = false;
        golden_last_summary = u8"基準画像がありません。先に「基準を撮る」を押してください: " +
            error;
        push_editor_log("Warning", golden_last_summary, golden_path);
        return;
    }

    Capture::Image diff;
    Capture::CompareResult result;
    Capture::GoldenImage::Compare(golden, current, golden_tolerance, result, &diff);

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
        golden_last_ok = false;
        golden_last_summary = u8"大きさが違います。ウィンドウサイズを基準と合わせてください / " +
            result.Summary();
        push_editor_log("Warning", golden_last_summary, golden_path);
        return;
    }

    if (!result.compared)
    {
        golden_last_ok = false;
        golden_last_summary = u8"比較できませんでした（画像が壊れている可能性があります）";
        push_editor_log("Error", golden_last_summary, golden_path);
        return;
    }

    golden_last_ok = result.Identical();
    golden_last_summary = result.Summary();

    if (golden_last_ok)
    {
        push_editor_log("Info", u8"回帰なし: " + golden_last_summary, golden_path);
        return;
    }

    const std::filesystem::path diff_path = Capture::GoldenImage::DiffPath(name);
    if (Capture::GoldenImage::SavePng(diff_path, diff, save_error))
    {
        golden_last_summary += u8" / 差分画像: " + diff_path.generic_u8string();
    }
    push_editor_log("Warning", u8"回帰あり: " + golden_last_summary, diff_path);
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
        golden_name = golden_name_buffer;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Saved/Golden/%s.png", golden_name.c_str());

    ImGui::SetNextItemWidth(220.0f);
    ImGui::SliderInt(u8"止めるフレーム数", &golden_settle_frames, 1, 60);
    ImGui::SameLine();
    ImGui::TextDisabled(u8"TAA が収束するまで待つ");

    ImGui::SetNextItemWidth(220.0f);
    ImGui::SliderInt(u8"許容差", &golden_tolerance, 0, 8);
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
        ImGui::TextDisabled(u8"撮影中... 残り %d フレーム", golden_countdown);
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

    if (!golden_last_summary.empty())
    {
        const ImVec4 color = golden_last_ok
            ? ImVec4(0.45f, 0.85f, 0.50f, 1.0f)
            : ImVec4(1.0f, 0.78f, 0.35f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("%s", golden_last_summary.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::End();
}
