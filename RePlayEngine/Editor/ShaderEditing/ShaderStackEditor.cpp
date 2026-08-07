#include "ShaderStackEditor.h"

#include "imgui/imgui.h"
// PushItemFlag / ImGuiItemFlags_Disabled は internal 側にある。
// 並べ替えボタンの端で無効表示にするために使う。
#include "imgui/imgui_internal.h"

#include <cstdint>
#include <string>

namespace ReplayEngine::Editor
{
    namespace
    {
        const char* LayerName(Rendering::ShaderLayerType type)
        {
            using Rendering::ShaderLayerType;
            switch (type)
            {
            case ShaderLayerType::Pbr:       return "PBR補助";
            case ShaderLayerType::Toon:      return "Toon補助";
            case ShaderLayerType::Unlit:     return "Unlit発光";
            case ShaderLayerType::Pixelate:  return "ピクセレーション";
            case ShaderLayerType::Wireframe: return "ワイヤーフレーム";
            case ShaderLayerType::Outline:   return "輪郭線";
            case ShaderLayerType::StylizedCharacter: return "キャラクター材質";
            default:                         return "不明";
            }
        }

        struct DraggedLayer
        {
            std::uintptr_t stack = 0;
            std::size_t index = 0;
        };
    }

    ShaderStackEditorResult ShaderStackEditor::Draw(const char* id, int& base_shader,
        bool& outline_pass, Rendering::ShaderLayerStack& layers,
        bool& advanced_mode, DirectX::XMFLOAT4& outline_color,
        DirectX::XMFLOAT4& outline_parameters, float& pixel_grid,
        float& pixelate_strength, bool show_surface_controls)
    {
        using namespace Rendering;
        const char* shading_names[] = { "FBX標準", "PBR", "トゥーン", "アンリット", "ピクセレーション" };
        const char* blend_names[] = { "アルファ", "加算", "乗算" };

        ImGui::PushID(id);
        ImGui::TextUnformatted("シェーダースタック");
        ImGui::TextDisabled("Surfaceの後へ上から順に合成。長押しドラッグで順番を変更できます");
        ImGui::Checkbox("詳細編集", &advanced_mode);
        ImGui::SameLine();
        ImGui::TextDisabled(advanced_mode
            ? "同じパスを複数追加できます" : "重複しやすいパスを自動制限します");
        if (outline_pass && !layers.Contains(ShaderLayerType::Outline))
            layers.Add(ShaderLayerType::Outline);

        if (show_surface_controls)
        {
            if (ImGui::Button("PBR + 輪郭"))
            {
                base_shader = 1;
                outline_pass = true;
                if (!layers.Contains(ShaderLayerType::Outline)) layers.Add(ShaderLayerType::Outline);
            }
            ImGui::SameLine();
            if (ImGui::Button("Toon + Pixel + 輪郭"))
            {
                base_shader = 2;
                outline_pass = true;
                layers.Clear();
                auto& pixelate = layers.Add(ShaderLayerType::Pixelate);
                pixelate.opacity = 0.35f;
                pixelate.parameter = 6.0f;
                layers.Add(ShaderLayerType::Outline);
            }
            ImGui::SameLine();
            if (ImGui::Button("Surfaceのみ"))
            {
                outline_pass = false;
                layers.Clear();
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Pass 1  Surface");
            ImGui::Combo("基本シェーダー", &base_shader, shading_names, IM_ARRAYSIZE(shading_names));
            if (base_shader == 4)
            {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.35f, 0.72f, 1.0f, 1.0f), "四角ピクセルの調整");
                ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f), "適用中: モデル色の低解像度化");
                ImGui::TextDisabled("サイズを上げるほど四角いブロックが大きくなります");
                if (ImGui::SmallButton("細かい  3px")) pixel_grid = 3.0f;
                ImGui::SameLine();
                if (ImGui::SmallButton("標準  6px")) pixel_grid = 6.0f;
                ImGui::SameLine();
                if (ImGui::SmallButton("粗い  12px")) pixel_grid = 12.0f;
                ImGui::TextUnformatted("四角ピクセルサイズ (px)");
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::SliderFloat("##BasePixelSize", &pixel_grid, 1.0f, 24.0f, "%.1f px");
                ImGui::TextUnformatted("効果の強さ");
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::SliderFloat("##BasePixelStrength", &pixelate_strength,
                    0.0f, 1.0f, "%.2f");
                ImGui::Separator();
            }
        }
        else
        {
            ImGui::TextDisabled("Base Shader は上の Shader Picker で選択します");
            ImGui::Separator();
        }

        if (layers.CanAdd())
        {
            if (ImGui::Button("追加パスを選ぶ...")) ImGui::OpenPopup("AddShaderLayer");
        }
        else
        {
            ImGui::TextDisabled("追加パスは上限です");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%zu / %zu", layers.Layers().size(), ShaderLayerStack::MaxLayers);
        if (ImGui::BeginPopup("AddShaderLayer"))
        {
            const auto add_layer = [&layers](const char* label, ShaderLayerType type)
            {
                if (ImGui::MenuItem(label)) layers.Add(type);
            };
            add_layer("PBR補助", ShaderLayerType::Pbr);
            add_layer("Toon補助", ShaderLayerType::Toon);
            add_layer("Unlit発光", ShaderLayerType::Unlit);
            add_layer("ピクセレーション", ShaderLayerType::Pixelate);
            add_layer("ワイヤーフレーム", ShaderLayerType::Wireframe);
            // 種別ごとの枚数制限は設けない。
            //
            // 以前は輪郭線とキャラクター材質を 1 枚までに制限していたが、
            // 「輪郭を色違いで 2 重に掛ける」のような使い方ができなかった。
            // 重ね掛けは表現の道具なので、枚数はユーザーに決めさせる。
            add_layer("キャラクター材質", ShaderLayerType::StylizedCharacter);
            add_layer("輪郭線", ShaderLayerType::Outline);
            ImGui::EndPopup();
        }

        std::size_t remove_index = static_cast<std::size_t>(-1);
        std::size_t move_source = static_cast<std::size_t>(-1);
        std::size_t move_destination = static_cast<std::size_t>(-1);
        for (std::size_t index = 0; index < layers.Layers().size(); ++index)
        {
            ShaderLayer& layer = layers.Layers()[index];
            ImGui::PushID(static_cast<int>(layer.id));
            const std::string title = "Pass " + std::to_string(index + 2) + "  " + LayerName(layer.type);
            ImGui::Selectable(title.c_str(), false, ImGuiSelectableFlags_AllowItemOverlap);
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                const DraggedLayer payload{ reinterpret_cast<std::uintptr_t>(&layers), index };
                ImGui::SetDragDropPayload("REPLAY_SHADER_LAYER", &payload, sizeof(payload));
                ImGui::Text("移動: %s", LayerName(layer.type));
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("REPLAY_SHADER_LAYER"))
                {
                    const auto dragged = *static_cast<const DraggedLayer*>(payload->Data);
                    if (dragged.stack == reinterpret_cast<std::uintptr_t>(&layers))
                    {
                        move_source = dragged.index;
                        move_destination = index;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            ImGui::Checkbox("有効", &layer.enabled);

            // 並べ替えのボタン。
            //
            // ドラッグ&ドロップだけだと、並べ替えられること自体に
            // 気付けない。上下ボタンを出して発見できるようにする。
            // ドラッグは残すので、慣れたらそちらの方が速い。
            ImGui::SameLine();
            if (index == 0) ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            if (ImGui::ArrowButton("##MoveUp", ImGuiDir_Up) && index > 0)
            {
                move_source = index;
                move_destination = index - 1;
            }
            if (index == 0) ImGui::PopItemFlag();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("1つ手前へ（先に描く）");

            ImGui::SameLine();
            const bool is_last = index + 1 >= layers.Layers().size();
            if (is_last) ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            if (ImGui::ArrowButton("##MoveDown", ImGuiDir_Down) && !is_last)
            {
                move_source = index;
                move_destination = index + 1;
            }
            if (is_last) ImGui::PopItemFlag();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("1つ後ろへ（後に描く）");

            ImGui::SameLine();
            if (ImGui::SmallButton("削除")) remove_index = index;

            if (layer.enabled && ImGui::TreeNodeEx("調整", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (layer.type == ShaderLayerType::Outline)
                {
                    ImGui::ColorEdit4("輪郭色", &outline_color.x);
                    ImGui::SliderFloat("輪郭幅", &outline_parameters.x, 0.0f, 0.10f, "%.3f");
                    ImGui::SliderFloat("距離補正", &outline_parameters.y, 0.0f, 0.10f, "%.3f");
                }
                else if (layer.type == ShaderLayerType::Pixelate)
                {
                    ImGui::TextDisabled("モデル色を四角いセル単位で低解像度化します");
                    ImGui::SliderFloat("四角ピクセルサイズ", &layer.parameter,
                        1.0f, 24.0f, "%.1f px");
                    ImGui::SliderFloat("ピクセル化強度", &layer.strength,
                        0.0f, 1.0f, "%.2f");
                }
                else
                {
                    int blend = static_cast<int>(layer.blend);
                    if (ImGui::Combo("合成方式", &blend, blend_names, IM_ARRAYSIZE(blend_names)))
                        layer.blend = static_cast<ShaderLayerBlend>(blend);
                    ImGui::SliderFloat("不透明度", &layer.opacity, 0.0f, 1.0f, "%.2f");
                    ImGui::ColorEdit3("色", &layer.tint.x);
                }
                ImGui::TreePop();
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        if (move_source != static_cast<std::size_t>(-1)) layers.Move(move_source, move_destination);
        if (remove_index != static_cast<std::size_t>(-1)) layers.Remove(remove_index);
        outline_pass = layers.Contains(ShaderLayerType::Outline);
        ImGui::PopID();

        ShaderStackEditorResult result{};
        result.requires_pbr = base_shader == 1 || layers.Contains(ShaderLayerType::Pbr);
        result.requires_toon = base_shader == 2 || layers.Contains(ShaderLayerType::Toon);
        result.requires_unlit = base_shader == 3 || base_shader == 4 ||
            layers.Contains(ShaderLayerType::Unlit);
        result.requires_outline = outline_pass;
        return result;
    }
}
