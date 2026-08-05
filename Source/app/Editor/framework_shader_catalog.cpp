#include "framework.h"

#include "../../../RePlayEngine/Rendering/Shaders/ShaderConstantPacker.h"

// シェーダ資産の走査と一覧表示。
//
// 【今の段階でできること】
//   ・Shader/Materials, Shader/Layers, Shader/PostProcess の .hlsl を走査する
//   ・#pragma から名前・分類・プロパティを読む
//   ・GUID が無ければ採番してファイルへ書き戻す
//   ・定数バッファの配置を決める
//
// 【まだできないこと】
//   ・描画に使うこと（フェーズ 4 以降）
//   ・Material から選ぶこと（フェーズ 5 以降）
//
// この一覧は「宣言がそのまま Inspector の項目になる」ことを
// 目で確かめるためのもの。property を 1 行足して保存し、
// 「再走査」を押せば欄が増える。C++ は 1 行も書かなくてよい。

void framework::scan_shader_library()
{
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

    shader_library.ScanAll(std::filesystem::current_path());
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
    ImGui::TextDisabled("%s", report.Summary().c_str());

    // 問題があれば目立たせる。
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

        const std::string header = entry.info.MenuPath() + "   [" +
            Rendering::ToString(entry.info.domain) + "]";

        if (ImGui::CollapsingHeader(header.c_str()))
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
