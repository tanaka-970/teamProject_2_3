#include "ShaderStackEditor.h"
#include "ShaderPropertyInspector.h"
#include "../ReorderableList.h"
#include "../../Rendering/Materials/MaterialSchema.h"
#include "../../Rendering/Shaders/ShaderCatalog.h"
#include "../../Assets/AssetDatabase.h"

#include "imgui/imgui.h"
// PushItemFlag / ImGuiItemFlags_Disabled は internal 側にある。
// 並べ替えボタンの端で無効表示にするために使う。
#include "imgui/imgui_internal.h"

#include <cstdint>
#include <string>
#include <algorithm>
#include <vector>

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

    }

    ShaderStackEditorResult ShaderStackEditor::Draw(const char* id, int& base_shader,
        bool& outline_pass, Rendering::ShaderLayerStack& layers,
        bool& advanced_mode, DirectX::XMFLOAT4& outline_color,
        DirectX::XMFLOAT4& outline_parameters, float& pixel_grid,
        float& pixelate_strength, bool show_surface_controls,
        const Rendering::ShaderCatalog* catalog,
        const Assets::AssetDatabase* assets,
        ShaderStackEditor::BeforeReorderCallback before_reorder,
        void* before_reorder_owner)
    {
        using namespace Rendering;
        const char* shading_names[] = { "FBX標準", "PBR", "トゥーン", "アンリット", "ピクセレーション" };
        const char* blend_names[] = { "アルファ", "加算", "乗算" };

        bool changed = false;
        ImGui::PushID(id);
        ImGui::TextUnformatted("シェーダースタック");
        ImGui::TextDisabled("Surfaceの後へ上から順に合成。長押しドラッグで順番を変更できます");
        ImGui::Checkbox("詳細表示", &advanced_mode);
        ImGui::SameLine();
        ImGui::TextDisabled(advanced_mode
            ? "GUID / Shader-owned Pass を表示" : "必要な設定だけ表示します");
        if (outline_pass && !layers.Contains(ShaderLayerType::Outline))
        {
            layers.Add(ShaderLayerType::Outline);
            changed = true;
        }

        if (show_surface_controls)
        {
            if (ImGui::Button("PBR + 輪郭"))
            {
                base_shader = 1;
                outline_pass = true;
                if (!layers.Contains(ShaderLayerType::Outline)) layers.Add(ShaderLayerType::Outline);
                changed = true;
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
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Surfaceのみ"))
            {
                outline_pass = false;
                layers.Clear();
                changed = true;
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Pass 1  Surface");
            if (ImGui::Combo("基本シェーダー", &base_shader, shading_names, IM_ARRAYSIZE(shading_names)))
                changed = true;
            if (base_shader == 4)
            {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.35f, 0.72f, 1.0f, 1.0f), "四角ピクセルの調整");
                ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f), "適用中: モデル色の低解像度化");
                ImGui::TextDisabled("サイズを上げるほど四角いブロックが大きくなります");
                if (ImGui::SmallButton("細かい  3px")) { pixel_grid = 3.0f; changed = true; }
                ImGui::SameLine();
                if (ImGui::SmallButton("標準  6px")) { pixel_grid = 6.0f; changed = true; }
                ImGui::SameLine();
                if (ImGui::SmallButton("粗い  12px")) { pixel_grid = 12.0f; changed = true; }
                ImGui::TextUnformatted("四角ピクセルサイズ (px)");
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##BasePixelSize", &pixel_grid, 1.0f, 24.0f, "%.1f px"))
                    changed = true;
                ImGui::TextUnformatted("効果の強さ");
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##BasePixelStrength", &pixelate_strength,
                    0.0f, 1.0f, "%.2f"))
                    changed = true;
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
            if (catalog != nullptr)
            {
                std::vector<const ShaderCatalog::Entry*> entries;
                for (const ShaderCatalog::Entry& entry : catalog->All())
                    if (entry.info.domain == ShaderDomain::Layer) entries.push_back(&entry);
                std::sort(entries.begin(), entries.end(),
                    [](const ShaderCatalog::Entry* a, const ShaderCatalog::Entry* b)
                    { return a->info.MenuPath() < b->info.MenuPath(); });
                for (const ShaderCatalog::Entry* entry : entries)
                {
                    const std::string label = entry->info.MenuPath();
                    if (ImGui::MenuItem(label.c_str()))
                    {
                        ShaderLayer& added = layers.Add(entry->info.id);
                        changed = true;
                        if (entry->schema)
                            MaterialSchema::EnsurePropertyBag(added.properties, *entry->schema);
                        added.SyncPropertiesToLegacyFields();
                    }
                }
                if (entries.empty()) ImGui::TextDisabled("Layer Shader Asset がありません");
            }
            else
            {
                // 旧 debug inspector の fallback。Material Editor は Catalog 経路を使う。
                const auto add_layer = [&layers, &changed](const char* label, ShaderLayerType type)
                { if (ImGui::MenuItem(label)) { layers.Add(type); changed = true; } };
                add_layer("PBR補助", ShaderLayerType::Pbr);
                add_layer("Toon補助", ShaderLayerType::Toon);
                add_layer("Unlit発光", ShaderLayerType::Unlit);
                add_layer("ピクセレーション", ShaderLayerType::Pixelate);
                add_layer("ワイヤーフレーム", ShaderLayerType::Wireframe);
                add_layer("キャラクター材質", ShaderLayerType::StylizedCharacter);
                add_layer("輪郭線", ShaderLayerType::Outline);
            }
            ImGui::EndPopup();
        }

        std::size_t remove_index = static_cast<std::size_t>(-1);
        ReorderRequest move_request{};
        if (const char* dragging = ActiveReorderLabel(&layers))
        {
            ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_DragDropTarget],
                "移動中: %s", dragging);
        }
        for (std::size_t index = 0; index < layers.Layers().size(); ++index)
        {
            ShaderLayer& layer = layers.Layers()[index];
            const ShaderID layer_shader = layer.EffectiveShader();
            const ShaderCatalog::Entry* layer_entry =
                catalog != nullptr && layer_shader.IsValid() ? catalog->Find(layer_shader) : nullptr;
            const std::string layer_name = layer_entry != nullptr
                ? layer_entry->info.DisplayName()
                : (layer.type != ShaderLayerType::Custom ? std::string(LayerName(layer.type))
                    : std::string(u8"レイヤーシェーダーが見つかりません"));
            const bool unsupported_builtin = BuiltInShaderLayers::IsBuiltIn(layer_shader);
            const std::string title = "Layer " + std::to_string(index + 1) + "  " +
                layer_name + (unsupported_builtin
                    ? u8" [保存されますが、DX12 描画には反映されません]" : "");
            const std::string layer_id = "layer_" + std::to_string(layer.id);
            ImGui::PushID(layer_id.c_str());
            if (ImGui::Checkbox("有効", &layer.enabled)) changed = true;
            ImGui::SameLine();
            const ReorderableItemResult item = DrawReorderableItem(
                &layers, layer_id.c_str(), index, layers.Layers().size(),
                title.c_str(), false, false, true,
                [&]()
                {
                    if (ImGui::MenuItem("Layer を削除"))
                    {
                        remove_index = index;
                        changed = true;
                    }
                });
            if (item.request.Valid() && !move_request.Valid())
            {
                move_request = item.request;
                changed = true;
            }

            if (item.opened && layer.enabled)
            {
                ImGui::Indent();
                int blend = static_cast<int>(layer.blend);
                if (ImGui::Combo("合成方式", &blend, blend_names, IM_ARRAYSIZE(blend_names)))
                {
                    layer.blend = static_cast<ShaderLayerBlend>(blend);
                    changed = true;
                }

                if (layer_entry != nullptr && layer_entry->schema && assets != nullptr)
                {
                    MaterialSchema::EnsurePropertyBag(layer.properties, *layer_entry->schema);
                    if (ShaderPropertyInspector::Draw("LayerProperties", layer.properties,
                        *layer_entry->schema, *assets))
                    {
                        layer.SyncPropertiesToLegacyFields();
                        changed = true;
                    }
                    if (!layer_entry->AllCompiled())
                        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.30f, 1.0f),
                            layer_entry->EverCompiled()
                                ? "コンパイル失敗: 前回成功した bytecode を使用中"
                                : "未対応: 成功した bytecode がなく、この Layer は描画されません");

                    if (advanced_mode)
                    {
                        ImGui::TextDisabled("Shader GUID  %s",
                            layer_entry->info.id.ToString().c_str());
                        if (!layer_entry->passes.empty())
                        {
                            ImGui::TextDisabled("Shader-owned Passes (order is fixed)");
                            for (std::size_t pass_index = 0;
                                pass_index < layer_entry->passes.size(); ++pass_index)
                            {
                                const auto& pass = layer_entry->passes[pass_index];
                                ImGui::BulletText("%zu. %s  [%s]", pass_index + 1,
                                    pass.info.name.c_str(), ToString(pass.info.blend));
                            }
                        }
                    }
                }
                else if (catalog != nullptr)
                {
                    // Missing Shader でも保存済み PropertyBag は捨てない。
                    ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.38f, 1.0f),
                        "未対応: この Layer Shader は現行 DX12 では描画されません。設定値は保存されます");
                    if (layer_shader.IsValid())
                        ImGui::TextDisabled("GUID  %s", layer_shader.ToString().c_str());
                }
                else if (layer.type == ShaderLayerType::Outline)
                {
                    if (ImGui::ColorEdit4("輪郭色", &outline_color.x)) changed = true;
                    if (ImGui::SliderFloat("輪郭幅", &outline_parameters.x, 0.0f, 0.10f, "%.3f")) changed = true;
                    if (ImGui::SliderFloat("距離補正", &outline_parameters.y, 0.0f, 0.10f, "%.3f")) changed = true;
                }
                else if (layer.type == ShaderLayerType::Pixelate)
                {
                    const bool pixel_size_changed = ImGui::SliderFloat("四角ピクセルサイズ", &layer.parameter, 1.0f, 24.0f, "%.1f px");
                    const bool pixel_strength_changed = ImGui::SliderFloat("ピクセル化強度", &layer.strength, 0.0f, 1.0f, "%.2f");
                    if (pixel_size_changed || pixel_strength_changed)
                    {
                        layer.SyncLegacyFieldsToProperties();
                        changed = true;
                    }
                }
                else
                {
                    const bool opacity_changed = ImGui::SliderFloat("不透明度", &layer.opacity, 0.0f, 1.0f, "%.2f");
                    const bool tint_changed = ImGui::ColorEdit3("色", &layer.tint.x);
                    if (opacity_changed || tint_changed)
                    {
                        layer.SyncLegacyFieldsToProperties();
                        changed = true;
                    }
                }

                // 旧特殊パスへも同じ値を渡す。新 custom layer はここを読まない。
                if (layer.Is(BuiltInShaderLayers::Outline))
                {
                    outline_color = layer.tint;
                    outline_parameters.x = layer.parameter;
                }
                if (layer.Is(BuiltInShaderLayers::Pixelate))
                {
                    pixel_grid = layer.parameter;
                    pixelate_strength = layer.strength;
                }
                ImGui::Unindent();
            }
            else if (item.opened)
            {
                ImGui::Indent();
                ImGui::TextDisabled("この Layer は無効です");
                ImGui::Unindent();
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        bool reordered = false;
        if (move_request.Valid() && remove_index == static_cast<std::size_t>(-1))
        {
            if (before_reorder != nullptr) before_reorder(before_reorder_owner);
            layers.Move(move_request.source, move_request.destination);
            reordered = true;
        }
        if (remove_index != static_cast<std::size_t>(-1)) layers.Remove(remove_index);
        outline_pass = layers.Contains(BuiltInShaderLayers::Outline);
        ImGui::PopID();

        ShaderStackEditorResult result{};
        result.changed = changed;
        result.reordered = reordered;
        result.requires_pbr = base_shader == 1 || layers.Contains(ShaderLayerType::Pbr);
        result.requires_toon = base_shader == 2 || layers.Contains(ShaderLayerType::Toon);
        result.requires_unlit = base_shader == 3 || base_shader == 4 ||
            layers.Contains(ShaderLayerType::Unlit);
        result.requires_outline = outline_pass;
        return result;
    }
}
