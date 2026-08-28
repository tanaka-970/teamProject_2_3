#include "framework.h"

#include "../../../RePlayEngine/Rendering/Shaders/ShaderConstantPacker.h"
// 「Visual Studio で開く」に使う。C# の .cs を開く経路と同じものを流用する。
// シェーダ用に別の起動処理を作らないこと。
#include "../../../RePlayEngine/Scripting/CSharp/CSharpProject.h"

// シェーダ資産の走査と一覧表示。
// この一覧は「宣言がそのまま Inspector の項目になる」ことを
// 目で確かめるためのもの。property を 1 行足して保存し、
// 「再走査」を押せば欄が増える。C++ は 1 行も書かなくてよい。

void framework::scan_shader_library()
{
    if (standalone_game_mode) return;

    // ログをエディタの Console と Saved/Diagnostics へ流す。
    //
    // エンジン側から Editor を直接呼ばないよう、関数オブジェクトで渡す。
    // 走査・採番・書式エラーを全部ここへ出すのが要点。
    // 「書いたのに出てこない」ときに理由が分からない状態を作らない。
    shader_library.SetLogSink(
        [this](const std::string& severity, const std::string& message,
            const std::filesystem::path& file, int line)
        {
            push_editor_log(severity, message, file, line);
        });

    shader_library.ScanAll(content_root_path());
    // DX12 は ShaderID 単位で Production Surface PSO を Cache する。手動再走査では
    // 同じ GUID のまま Source/Schema が変わるため、この安全な Editor 境界で Backend
    // Cache を無効化し、古い Bytecode を描画しないようにする。
    if (dx12_framework_active && dx12_device_context.IsInitialized())
        (void)dx12_device_context.ClearStaticAssetCaches();
}

void framework::poll_shader_source_changes(float elapsed_time)
{
    if (standalone_game_mode) return;
    if (!shader_auto_recompile) return;

    // 毎フレーム全部の更新時刻を取りに行くと、
    // 枚数が増えたときにファイルシステムへの問い合わせが効いてくる。
    // C# の poll_csharp_script_changes と同じ間隔にそろえる。
    shader_poll_timer += elapsed_time;
    if (shader_poll_timer < 1.0f) return;
    shader_poll_timer = 0.0f;

    // 保存直後はエディタがまだ書き込み中のことがある。
    // ここで失敗しても直前のバイトコードは残るので、
    // 次の 1 秒でもう一度拾い直せばよい。握り潰さず理由はログへ出る。
    const std::size_t recompiled = shader_library.PollSourceChanges(false);
    // ShaderLibrary は Hot Reload 前後で ShaderID を意図的に維持する。実際に再構築が
    // 起きた場合だけ DX12 Static Cache を消す。これにより DXC Compile に失敗した Shader も
    // ユーザーが修正した直後に再試行できる。
    if (recompiled != 0 && dx12_framework_active && dx12_device_context.IsInitialized())
        (void)dx12_device_context.ClearStaticAssetCaches();
}

namespace
{
    // コンパイル状態を 1 目で分かる色と文字にする。
    struct ShaderStatusBadge final
    {
        ImVec4 color;
        const char* text;
    };

    ShaderStatusBadge shader_status_badge(
        const ReplayEngine::Rendering::ShaderCatalog::Entry& entry)
    {
        if (entry.AllCompiled())
        {
            return { ImVec4(0.45f, 0.85f, 0.50f, 1.0f), u8"OK" };
        }
        if (entry.EverCompiled())
        {
            // 直前に成功したものが残っている状態。
            // 絵は出続けるが、書いた内容は反映されていない。
            // 「直っていないのに直ったと思う」のが一番困るので分けて出す。
            return { ImVec4(1.0f, 0.72f, 0.30f, 1.0f), u8"失敗（前回のを使用中）" };
        }
        return { ImVec4(1.0f, 0.42f, 0.38f, 1.0f), u8"失敗" };
    }
}

void framework::draw_shader_catalog_panel()
{
    namespace Rendering = ReplayEngine::Rendering;

    if (!show_shader_catalog_panel) return;

    ImGui::SetNextWindowSize(ImVec2(720.0f, 480.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(u8"シェーダ一覧", &show_shader_catalog_panel))
    {
        ImGui::End();
        return;
    }

    const Rendering::ShaderLibrary::ScanReport& report = shader_library.LastReport();

    if (ImGui::Button(u8"再走査", ImVec2(120.0f, 0.0f)))
    {
        scan_shader_library();
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"全部コンパイル", ImVec2(140.0f, 0.0f)))
    {
        // 走査し直さずコンパイルだけやる。
        // #pragma を触っていないときはこちらの方が速い。
        const Rendering::ShaderLibrary::ScanReport compiled =
            shader_library.CompileAll(false);
        push_editor_log("Info",
            u8"コンパイル成功 " + std::to_string(compiled.compiled) +
            u8" 枚 / 失敗 " + std::to_string(compiled.compile_failed) + u8" 枚");
    }
    ImGui::SameLine();
    ImGui::Checkbox(u8"保存で自動コンパイル", &shader_auto_recompile);

    ImGui::TextDisabled("%s", report.Summary().c_str());

    // 問題があれば目立たせる。
    if (report.compile_failed != 0)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.38f, 1.0f),
            u8"コンパイルに失敗したシェーダが %zu 枚あります。"
            u8"下の赤い行をクリックすると該当行が開きます。",
            report.compile_failed);
    }
    if (report.duplicate_ids != 0)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
            u8"GUID が重複しています。コピーしたシェーダの replay_guid を消してください。");
    }
    if (report.parse_issues != 0)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.35f, 1.0f),
            u8"#pragma の書式エラーが %zu 件あります。コンソールを見てください。",
            report.parse_issues);
    }
    if (report.scanned == 0)
    {
        ImGui::Separator();
        ImGui::TextDisabled(u8"シェーダが 1 枚も見つかりません。");
        ImGui::TextDisabled(u8"次のフォルダへ .hlsl を置いてください。");
        for (const auto& pair : Rendering::ShaderLibrary::ScanFolders())
        {
            ImGui::BulletText("%s  (%s)",
                pair.first.generic_u8string().c_str(),
                Rendering::ToString(pair.second));
        }
        ImGui::End();
        return;
    }

    ImGui::Separator();

    const Rendering::ShaderCatalog& catalog = shader_library.Catalog();
    for (const Rendering::ShaderCatalog::Entry& entry : catalog.All())
    {
        ImGui::PushID(entry.info.id.ToString().c_str());

        const ShaderStatusBadge badge = shader_status_badge(entry);

        const std::string header = entry.info.MenuPath() + "   [" +
            Rendering::ToString(entry.info.domain) + "]";

        // 失敗しているものは畳んだままでも分かるようにする。
        // 開かないと気付けない作りにすると、気付かないまま作業が進む。
        const bool opened = ImGui::CollapsingHeader(header.c_str());
        ImGui::SameLine();
        ImGui::TextColored(badge.color, "%s", badge.text);

        if (opened)
        {
            ImGui::TextDisabled("%s",
                entry.info.source_path.generic_u8string().c_str());
            ImGui::TextDisabled("GUID  %s", entry.info.id.ToString().c_str());

            if (ImGui::SmallButton(u8"Visual Studio で開く"))
            {
                std::string error;
                if (!ReplayEngine::Scripting::CSharp::CSharpProject::OpenVisualStudio(
                    entry.info.source_path, 1, error))
                {
                    push_editor_log("Warning", error, entry.info.source_path);
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(u8"このシェーダを再コンパイル"))
            {
                shader_library.CompileOne(entry.info.id, false);
            }

            // 変種ごとの状態と診断。
            //
            // 【変種を並べて出す理由】
            //   Static は通って Skinned だけ落ちることがある。
            //   1 つにまとめると「PBR は動いている」ように見えてしまい、
            //   キャラだけ描けない状態に気付けない。必ず分けて出す。
            //
            // ここが「エンジンは理由を知っているのに画面に出さない」を
            // 潰す場所。エラーが出ているのに一覧が空、という状態を作らない。
            ImGui::Separator();
            int diagnostic_index = 0;
            for (int variant_index = 0;
                variant_index < Rendering::shader_variant_count; ++variant_index)
            {
                const auto variant =
                    static_cast<Rendering::ShaderVariant>(variant_index);
                if (!entry.UsesVariant(variant)) continue;

                const Rendering::ShaderCatalog::VariantResult& result =
                    entry.At(variant);

                const ImVec4 status_color = result.compiled
                    ? ImVec4(0.45f, 0.85f, 0.50f, 1.0f)
                    : ImVec4(1.0f, 0.42f, 0.38f, 1.0f);

                ImGui::TextDisabled("%s", Rendering::ToString(variant));
                ImGui::SameLine();
                ImGui::TextColored(status_color, "%s",
                    result.compiled ? u8"OK" : u8"失敗");

                for (const Rendering::ShaderDiagnostic& item : result.diagnostics)
                {
                    // 同じ文言の診断が並ぶことがある。
                    // ID を分けないと 1 行目だけが反応する。
                    ImGui::PushID(diagnostic_index++);

                    const bool is_error =
                        item.severity == Rendering::ShaderDiagnostic::Severity::Error;
                    const ImVec4 color = is_error
                        ? ImVec4(1.0f, 0.42f, 0.38f, 1.0f)
                        : ImVec4(1.0f, 0.78f, 0.35f, 1.0f);

                    std::string label = "    ";
                    if (item.line > 0)
                    {
                        label += "(" + std::to_string(item.line) + ") ";
                    }
                    if (!item.code.empty()) label += item.code + ": ";
                    label += item.message;

                    ImGui::PushStyleColor(ImGuiCol_Text, color);
                    ImGui::Selectable(label.c_str(), false);
                    ImGui::PopStyleColor();

                    if (ImGui::IsItemClicked())
                    {
                        // 診断が指すファイルを開く。
                        // 差し込んだ cbuffer のぶんは #line で戻してあるので、
                        // 表示されている行番号は元の .hlsl の行番号。
                        const std::filesystem::path target =
                            item.file.empty() ? entry.info.source_path : item.file;
                        std::string error;
                        if (!ReplayEngine::Scripting::CSharp::CSharpProject::
                            OpenVisualStudio(target, item.line > 0 ? item.line : 1, error))
                        {
                            push_editor_log("Warning", error, target);
                        }
                    }

                    ImGui::PopID();
                }
            }
            ImGui::Separator();

            if (entry.schema)
            {
                ImGui::TextDisabled(u8"定数バッファ %u バイト / テクスチャ %u 枚",
                    entry.schema->ConstantBufferSize(),
                    entry.schema->TextureCount());
            }

            ImGui::Separator();

            if (entry.info.properties.empty())
            {
                ImGui::TextDisabled(u8"#pragma property がありません。");
                ImGui::TextDisabled(u8"1 行足して保存し、再走査してください。");
            }

            // ここが要点。
            //
            // 並んでいる項目は全部 .hlsl の #pragma property から来ている。
            // C++ 側にこの項目名は 1 つも書かれていない。
            // これがフェーズ 7 でそのまま Material の編集欄になる。
            for (const Rendering::ShaderProperty& property : entry.info.properties)
            {
                ImGui::BulletText("%s", property.DisplayName().c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("(%s  %s)",
                    property.name.c_str(),
                    Rendering::ToString(property.kind));

                ImGui::Indent();
                if (property.kind == Rendering::ShaderPropertyKind::Texture)
                {
                    ImGui::TextDisabled(u8"t%u / 既定 %s",
                        property.texture_slot,
                        property.default_texture.c_str());
                }
                else if (property.kind == Rendering::ShaderPropertyKind::Range)
                {
                    ImGui::TextDisabled(u8"offset %u  size %u  範囲 %.2f 〜 %.2f  既定 %.3f",
                        property.constant_offset, property.constant_size,
                        property.minimum, property.maximum,
                        property.default_value.x);
                }
                else if (property.kind == Rendering::ShaderPropertyKind::Enum)
                {
                    std::string names;
                    for (const std::string& name : property.enum_names)
                    {
                        if (!names.empty()) names += " / ";
                        names += name;
                    }
                    ImGui::TextDisabled(u8"offset %u  { %s }  既定 %d",
                        property.constant_offset, names.c_str(),
                        static_cast<int>(property.default_value.x));
                }
                else
                {
                    ImGui::TextDisabled(u8"offset %u  size %u  既定 (%.3f, %.3f, %.3f, %.3f)",
                        property.constant_offset, property.constant_size,
                        property.default_value.x, property.default_value.y,
                        property.default_value.z, property.default_value.w);
                }
                ImGui::Unindent();
            }

            // 自動生成される HLSL。
            //
            // 人が cbuffer を書かないことを目で確かめられるようにする。
            // 手で書かせるとパッキング規則を踏み外して値が 1 つずれ、
            // エラーも出ないまま絵だけおかしくなる。
            if (entry.schema && ImGui::TreeNode(u8"自動生成される cbuffer"))
            {
                const std::string hlsl =
                    Rendering::ShaderConstantPacker::GenerateHlslDeclaration(
                        *entry.schema);
                ImGui::TextUnformatted(hlsl.c_str());
                if (ImGui::SmallButton(u8"コピー"))
                {
                    ImGui::SetClipboardText(hlsl.c_str());
                }
                ImGui::TreePop();
            }
        }
        ImGui::PopID();
    }

    ImGui::End();
}
