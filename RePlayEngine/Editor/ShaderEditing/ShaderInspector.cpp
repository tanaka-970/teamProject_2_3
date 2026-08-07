#include "ShaderInspector.h"

#include "CharacterMaterialEditor.h"
#include "ShaderPresetEditor.h"
#include "ShaderStackEditor.h"

#include "imgui/imgui.h"

namespace ReplayEngine::Editor
{
    namespace
    {
        // 「キャラクター材質」欄を出すべき絵柄か。
        //
        // 0=FBX標準 / 1=PBR / 2=トゥーン / 3=アンリット / 4=ピクセル化
        // トゥーン系だけがキャラ材質のパラメータを実際に読む。
        // 常に出すと、効かない欄をいじって「変わらない」と悩むことになる。
        bool UsesCharacterMaterial(int base_shader) noexcept
        {
            return base_shader == 2;
        }
    }

    ShaderInspectorResult ShaderInspector::Draw(const char* id, HWND owner,
        const ShaderInspectorTarget& target)
    {
        ShaderInspectorResult result{};

        // 編集対象が無い状態で欄だけ出さない。
        // 「何を編集しているのか分からない」状態を作らないため。
        if (target.base_shader == nullptr)
        {
            ImGui::TextDisabled(u8"編集対象がありません。");
            ImGui::TextDisabled(u8"Hierarchy で GameObject を選んでください。");
            return result;
        }

        ImGui::PushID(id);

        // ---- 何を編集しているか ------------------------------------------
        //
        // これが無いと、選択を切り替えたときに
        // 前のオブジェクトを編集し続けているのか分からない。
        if (!target.label.empty())
        {
            ImGui::TextDisabled(u8"編集対象");
            ImGui::SameLine();
            ImGui::TextUnformatted(target.label.c_str());
            ImGui::Separator();
        }

        // ---- プリセット ---------------------------------------------------
        //
        // 一番上に置く。「まず土台を選んでから細部を詰める」順序にする。
        if (target.preset_status != nullptr && target.layers != nullptr &&
            target.character != nullptr && target.outline_pass != nullptr &&
            target.pixel_grid != nullptr && target.pixelate_strength != nullptr)
        {
            ShaderPresetEditor::Draw(owner, *target.base_shader,
                *target.outline_pass, *target.layers, *target.character,
                *target.pixel_grid, *target.pixelate_strength,
                *target.preset_status);
            ImGui::Separator();
        }

        // ---- 絵柄とレイヤ -------------------------------------------------
        if (target.layers != nullptr && target.outline_pass != nullptr &&
            target.advanced_mode != nullptr && target.outline_color != nullptr &&
            target.outline_parameters != nullptr && target.pixel_grid != nullptr &&
            target.pixelate_strength != nullptr)
        {
            const ShaderStackEditorResult stack = ShaderStackEditor::Draw(id,
                *target.base_shader, *target.outline_pass, *target.layers,
                *target.advanced_mode, *target.outline_color,
                *target.outline_parameters, *target.pixel_grid,
                *target.pixelate_strength);

            result.changed = result.changed || stack.changed;
            result.requires_pbr = stack.requires_pbr;
            result.requires_toon = stack.requires_toon;
            result.requires_unlit = stack.requires_unlit;
            result.requires_outline = stack.requires_outline;
        }

        // ---- キャラクター材質 ---------------------------------------------
        //
        // 効く絵柄のときだけ出す。
        // 別ウィンドウにも別タブにもしない。同じ 1 枚の続きとして並べる。
        if (target.character != nullptr)
        {
            ImGui::Separator();
            if (UsesCharacterMaterial(*target.base_shader))
            {
                CharacterMaterialEditor::Draw(*target.character);
            }
            else
            {
                ImGui::TextDisabled(u8"キャラクター材質");
                ImGui::TextDisabled(
                    u8"  トゥーンを選ぶと、この下に肌・影・リムの設定が出ます。");
            }
        }

        // 何か操作中かどうか。
        //
        // この imgui (1.80 WIP) には IsAnyItemEdited が無い。
        // 厳密な「値が変わったか」は各エディタが返していないので、
        // ここでは「触っている最中か」までしか分からない。
        // 呼び出し側はこれを保存の要否ではなく、
        // プレビュー更新の判断にだけ使うこと。
        result.changed = result.changed || ImGui::IsAnyItemActive();

        ImGui::PopID();
        return result;
    }
}
