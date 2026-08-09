#include "ShaderComposerEditor.h"

#include "../../Assets/AssetDatabase.h"
#include "../../Rendering/ShaderComposer/ShaderComposerGenerator.h"
#include "../../Rendering/Shaders/ShaderLibrary.h"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace ReplayEngine::Editor
{
    namespace
    {
        const char* NodeTitle(Rendering::ShaderComposerNodeKind kind)
        {
            using K = Rendering::ShaderComposerNodeKind;
            switch (kind)
            {
            case K::UV: return "UV";
            case K::Time: return "Time";
            case K::Normal: return "Normal";
            case K::ViewDirection: return "View Direction";
            case K::Float: return "Float";
            case K::Color: return "Color";
            case K::FloatProperty: return "Float Property";
            case K::ColorProperty: return "Color Property";
            case K::TextureProperty: return "Texture Property";
            case K::Add: return "Add";
            case K::Subtract: return "Subtract";
            case K::Multiply: return "Multiply";
            case K::Divide: return "Divide";
            case K::Lerp: return "Lerp";
            case K::Saturate: return "Saturate";
            case K::Power: return "Power";
            case K::Fresnel: return "Fresnel";
            case K::UVScroll: return "UV Scroll";
            case K::Noise: return "Noise";
            case K::Dissolve: return "Dissolve";
            case K::SurfaceOutput: return "Surface Output";
            case K::LayerOutput: return "Layer Output";
            default: return "Node";
            }
        }

        std::uint32_t InputCount(Rendering::ShaderComposerNodeKind kind)
        {
            using K = Rendering::ShaderComposerNodeKind;
            switch (kind)
            {
            case K::TextureProperty: return 1;
            case K::Add: case K::Subtract: case K::Multiply: case K::Divide: return 2;
            case K::Lerp: return 3;
            case K::Saturate: return 1;
            case K::Power: return 2;
            case K::Fresnel: return 3;
            case K::UVScroll: return 3;
            case K::Noise: return 2;
            case K::Dissolve: return 3;
            case K::SurfaceOutput: return 3;
            case K::LayerOutput: return 1;
            default: return 0;
            }
        }

        const char* InputName(Rendering::ShaderComposerNodeKind kind, std::uint32_t pin)
        {
            using K = Rendering::ShaderComposerNodeKind;
            switch (kind)
            {
            case K::TextureProperty: return "UV";
            case K::Add: case K::Subtract: case K::Multiply: case K::Divide:
                return pin == 0 ? "A" : "B";
            case K::Lerp: return pin == 0 ? "A" : pin == 1 ? "B" : "T";
            case K::Saturate: return "In";
            case K::Power: return pin == 0 ? "Base" : "Power";
            case K::Fresnel: return pin == 0 ? "Normal" : pin == 1 ? "View" : "Power";
            case K::UVScroll: return pin == 0 ? "UV" : pin == 1 ? "Speed" : "Time";
            case K::Noise: return pin == 0 ? "UV" : "Scale";
            case K::Dissolve: return pin == 0 ? "Value" : pin == 1 ? "Threshold" : "Edge";
            case K::SurfaceOutput: return pin == 0 ? "Base Color" : pin == 1 ? "Emission" : "Opacity";
            case K::LayerOutput: return "Color";
            default: return "In";
            }
        }

        bool HasOutput(Rendering::ShaderComposerNodeKind kind)
        {
            return kind != Rendering::ShaderComposerNodeKind::SurfaceOutput &&
                kind != Rendering::ShaderComposerNodeKind::LayerOutput;
        }

        bool EditString(const char* label, std::string& value, std::size_t capacity = 256)
        {
            std::vector<char> buffer(capacity, 0);
            const std::size_t copy = (std::min)(value.size(), capacity - 1);
            std::memcpy(buffer.data(), value.data(), copy);
            if (!ImGui::InputText(label, buffer.data(), buffer.size())) return false;
            value = buffer.data();
            return true;
        }

        ImVec2 NodeSize(const Rendering::ShaderComposerNode& node)
        {
            const float height = 52.0f + 24.0f * static_cast<float>((std::max)(1u, InputCount(node.kind)));
            return ImVec2(180.0f, height);
        }

        ImVec2 InputPosition(const Rendering::ShaderComposerNode& node, std::uint32_t pin,
            const ImVec2& origin)
        {
            return ImVec2(origin.x + node.x, origin.y + node.y + 51.0f + 24.0f * pin);
        }

        ImVec2 OutputPosition(const Rendering::ShaderComposerNode& node, const ImVec2& origin)
        {
            const ImVec2 size = NodeSize(node);
            return ImVec2(origin.x + node.x + size.x, origin.y + node.y + 51.0f);
        }
    }

    bool ShaderComposerEditor::Open(const std::filesystem::path& path, std::string& error)
    {
        if (dirty_ && !path_.empty() && path_ != path)
        {
            std::string save_error;
            if (!AutoSaveGraph(save_error))
            {
                error = "現在の Graph を退避できないため別 Graph を開けません: " + save_error;
                return false;
            }
        }
        Rendering::ShaderComposerAsset loaded;
        if (!Rendering::ShaderComposerAsset::Load(path, loaded, error)) return false;
        path_ = path;
        asset_ = std::move(loaded);
        visible_ = true;
        dirty_ = false;
        selected_node_ = 0;
        pending_output_node_ = 0;
        status_ = "Loaded " + path.filename().u8string();
        return true;
    }

    bool ShaderComposerEditor::AutoSaveGraph(std::string& error)
    {
        error.clear();
        if (!dirty_ || path_.empty()) return true;
        if (!Rendering::ShaderComposerAsset::Save(asset_, path_, error)) return false;
        dirty_ = false;
        status_ = "Graph autosaved (generated HLSL unchanged until Ctrl+S)";
        return true;
    }

    bool ShaderComposerEditor::SaveAndGenerate(const std::filesystem::path& project_root,
        Rendering::ShaderLibrary& shader_library, Assets::AssetDatabase& asset_database)
    {
        std::string error;
        if (!Rendering::ShaderComposerAsset::Save(asset_, path_, error))
        {
            status_ = "Save failed: " + error;
            return false;
        }
        if (!Rendering::ShaderComposerGenerator::GenerateToFile(asset_, project_root, error))
        {
            status_ = "Generate failed: " + error;
            return false;
        }

        const std::filesystem::path hlsl = asset_.generated_hlsl.is_absolute()
            ? asset_.generated_hlsl : project_root / asset_.generated_hlsl;
        asset_database.Register(path_, Assets::AssetKind::Shader);
        asset_database.Register(hlsl, Assets::AssetKind::Shader);
        if (!asset_database.Save(error))
        {
            status_ = "Generated, but AssetDatabase save failed: " + error;
            return false;
        }

        const Rendering::ShaderLibrary::ScanReport report = shader_library.ScanAll(project_root);
        dirty_ = false;
        status_ = report.compile_failed == 0
            ? "Saved / generated / compiled"
            : "Saved / generated. Compile errors: " + std::to_string(report.compile_failed);
        return report.compile_failed == 0;
    }

    void ShaderComposerEditor::AddNode(Rendering::ShaderComposerNodeKind kind, float x, float y)
    {
        Rendering::ShaderComposerNode& node = asset_.AddNode(kind, x, y);
        if (kind == Rendering::ShaderComposerNodeKind::FloatProperty)
        {
            node.name = "Value" + std::to_string(node.id);
            node.display_name = "Value";
        }
        else if (kind == Rendering::ShaderComposerNodeKind::ColorProperty)
        {
            node.name = "Color" + std::to_string(node.id);
            node.display_name = "Color";
        }
        else if (kind == Rendering::ShaderComposerNodeKind::TextureProperty)
        {
            node.name = "Texture" + std::to_string(node.id);
            node.display_name = "Texture";
        }
        selected_node_ = node.id;
        dirty_ = true;
    }

    void ShaderComposerEditor::DrawCanvas()
    {
        ImGui::BeginChild("##ComposerCanvas", ImVec2(0, 0), true,
            ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 canvas_min = ImGui::GetWindowPos();
        const ImVec2 canvas_size = ImGui::GetWindowSize();
        const ImVec2 origin(canvas_min.x + pan_x_, canvas_min.y + pan_y_);

        // grid
        const float grid = 32.0f;
        for (float x = std::fmod(pan_x_, grid); x < canvas_size.x; x += grid)
            draw->AddLine(ImVec2(canvas_min.x + x, canvas_min.y),
                ImVec2(canvas_min.x + x, canvas_min.y + canvas_size.y), IM_COL32(50, 55, 64, 110));
        for (float y = std::fmod(pan_y_, grid); y < canvas_size.y; y += grid)
            draw->AddLine(ImVec2(canvas_min.x, canvas_min.y + y),
                ImVec2(canvas_min.x + canvas_size.x, canvas_min.y + y), IM_COL32(50, 55, 64, 110));

        // pan on middle mouse / empty right-click opens add menu.
        ImGui::SetCursorScreenPos(canvas_min);
        ImGui::InvisibleButton("##CanvasHit", canvas_size,
            ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
        {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            pan_x_ += delta.x; pan_y_ += delta.y;
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopup("##ComposerAddNode");

        if (ImGui::BeginPopup("##ComposerAddNode"))
        {
            const ImVec2 mouse = ImGui::GetMousePos();
            const float x = mouse.x - origin.x;
            const float y = mouse.y - origin.y;
            using K = Rendering::ShaderComposerNodeKind;
            if (ImGui::BeginMenu("Input"))
            {
                if (ImGui::MenuItem("UV")) AddNode(K::UV, x, y);
                if (ImGui::MenuItem("Time")) AddNode(K::Time, x, y);
                if (ImGui::MenuItem("Normal")) AddNode(K::Normal, x, y);
                if (ImGui::MenuItem("View Direction")) AddNode(K::ViewDirection, x, y);
                if (ImGui::MenuItem("Float")) AddNode(K::Float, x, y);
                if (ImGui::MenuItem("Color")) AddNode(K::Color, x, y);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Material Property"))
            {
                if (ImGui::MenuItem("Float / Slider")) AddNode(K::FloatProperty, x, y);
                if (ImGui::MenuItem("Color")) AddNode(K::ColorProperty, x, y);
                if (ImGui::MenuItem("Texture")) AddNode(K::TextureProperty, x, y);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Math"))
            {
                if (ImGui::MenuItem("Add")) AddNode(K::Add, x, y);
                if (ImGui::MenuItem("Subtract")) AddNode(K::Subtract, x, y);
                if (ImGui::MenuItem("Multiply")) AddNode(K::Multiply, x, y);
                if (ImGui::MenuItem("Divide")) AddNode(K::Divide, x, y);
                if (ImGui::MenuItem("Lerp")) AddNode(K::Lerp, x, y);
                if (ImGui::MenuItem("Saturate")) AddNode(K::Saturate, x, y);
                if (ImGui::MenuItem("Power")) AddNode(K::Power, x, y);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Effect"))
            {
                if (ImGui::MenuItem("Fresnel")) AddNode(K::Fresnel, x, y);
                if (ImGui::MenuItem("UV Scroll")) AddNode(K::UVScroll, x, y);
                if (ImGui::MenuItem("Noise")) AddNode(K::Noise, x, y);
                if (ImGui::MenuItem("Dissolve")) AddNode(K::Dissolve, x, y);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Output"))
            {
                if (asset_.domain == Rendering::ShaderDomain::Layer)
                {
                    if (ImGui::MenuItem("Layer Output")) AddNode(K::LayerOutput, x, y);
                }
                else
                {
                    if (ImGui::MenuItem("Surface Output")) AddNode(K::SurfaceOutput, x, y);
                }
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        // connections first, nodes on top.
        for (const Rendering::ShaderComposerConnection& edge : asset_.connections)
        {
            const Rendering::ShaderComposerNode* from = asset_.FindNode(edge.from_node);
            const Rendering::ShaderComposerNode* to = asset_.FindNode(edge.to_node);
            if (!from || !to) continue;
            const ImVec2 a = OutputPosition(*from, origin);
            const ImVec2 b = InputPosition(*to, edge.to_pin, origin);
            const float tangent = (std::max)(60.0f, std::abs(b.x - a.x) * 0.45f);
            draw->AddBezierCurve(a, ImVec2(a.x + tangent, a.y),
                ImVec2(b.x - tangent, b.y), b, IM_COL32(96, 190, 255, 230), 3.0f);
        }

        for (Rendering::ShaderComposerNode& node : asset_.nodes)
        {
            ImGui::PushID(static_cast<int>(node.id));
            const ImVec2 pos(origin.x + node.x, origin.y + node.y);
            const ImVec2 size = NodeSize(node);
            const bool selected = selected_node_ == node.id;
            draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                selected ? IM_COL32(48, 58, 76, 245) : IM_COL32(35, 39, 48, 245), 7.0f);
            draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                selected ? IM_COL32(255, 190, 75, 255) : IM_COL32(83, 91, 106, 255), 7.0f, 0, selected ? 2.0f : 1.0f);
            draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + 34.0f),
                IM_COL32(55, 69, 92, 255), 7.0f);
            draw->AddText(ImVec2(pos.x + 10, pos.y + 9), IM_COL32(240, 244, 250, 255), NodeTitle(node.kind));

            ImGui::SetCursorScreenPos(pos);
            ImGui::InvisibleButton("##NodeHeader", ImVec2(size.x, 34.0f));
            if (ImGui::IsItemClicked()) selected_node_ = node.id;
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                const ImVec2 delta = ImGui::GetIO().MouseDelta;
                node.x += delta.x; node.y += delta.y; dirty_ = true;
            }

            const std::uint32_t input_count = InputCount(node.kind);
            for (std::uint32_t pin = 0; pin < input_count; ++pin)
            {
                const ImVec2 p = InputPosition(node, pin, origin);
                const bool connected = asset_.FindInput(node.id, pin) != nullptr;
                draw->AddCircleFilled(p, 6.0f,
                    connected ? IM_COL32(94, 210, 145, 255) : IM_COL32(145, 153, 170, 255));
                draw->AddText(ImVec2(p.x + 11.0f, p.y - 7.0f), IM_COL32(210, 216, 228, 255), InputName(node.kind, pin));
                ImGui::SetCursorScreenPos(ImVec2(p.x - 9, p.y - 9));
                ImGui::InvisibleButton(("##In" + std::to_string(pin)).c_str(), ImVec2(18, 18),
                    ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && pending_output_node_ != 0)
                {
                    if (asset_.Connect(pending_output_node_, node.id, pin)) dirty_ = true;
                    pending_output_node_ = 0;
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                {
                    if (asset_.DisconnectInput(node.id, pin)) dirty_ = true;
                }
            }

            if (HasOutput(node.kind))
            {
                const ImVec2 p = OutputPosition(node, origin);
                draw->AddCircleFilled(p, 6.0f,
                    pending_output_node_ == node.id ? IM_COL32(255, 190, 75, 255) : IM_COL32(96, 190, 255, 255));
                draw->AddText(ImVec2(p.x - 34.0f, p.y - 7.0f), IM_COL32(210, 216, 228, 255), "Out");
                ImGui::SetCursorScreenPos(ImVec2(p.x - 9, p.y - 9));
                ImGui::InvisibleButton("##Out", ImVec2(18, 18));
                if (ImGui::IsItemClicked()) pending_output_node_ = node.id;
            }
            ImGui::PopID();
        }

        if (pending_output_node_ != 0)
        {
            if (const Rendering::ShaderComposerNode* from = asset_.FindNode(pending_output_node_))
            {
                const ImVec2 a = OutputPosition(*from, origin);
                const ImVec2 b = ImGui::GetMousePos();
                draw->AddBezierCurve(a, ImVec2(a.x + 70, a.y), ImVec2(b.x - 70, b.y), b,
                    IM_COL32(255, 190, 75, 230), 2.5f);
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) pending_output_node_ = 0;
        }

        if (selected_node_ != 0 && ImGui::IsWindowFocused() && ImGui::IsKeyPressed(VK_DELETE))
        {
            const Rendering::ShaderComposerNode* selected = asset_.FindNode(selected_node_);
            if (selected != nullptr &&
                (selected->kind == Rendering::ShaderComposerNodeKind::SurfaceOutput ||
                 selected->kind == Rendering::ShaderComposerNodeKind::LayerOutput))
            {
                status_ = "Output node is protected. Add a replacement first if the graph is being repaired.";
            }
            else
            {
                if (asset_.RemoveNode(selected_node_)) dirty_ = true;
                selected_node_ = 0; pending_output_node_ = 0;
            }
        }

        ImGui::EndChild();
    }

    void ShaderComposerEditor::DrawInspector()
    {
        Rendering::ShaderComposerNode* node = asset_.FindNode(selected_node_);
        if (node == nullptr)
        {
            ImGui::TextDisabled("Node を選択してください");
            ImGui::Separator();
            ImGui::TextWrapped("右クリック: Node追加\n青い Out → 入力Pin: 接続\n入力Pinを右クリック: 切断\nDelete: Node削除\n中ボタンドラッグ: Pan");
            return;
        }

        ImGui::Text("%s  #%llu", NodeTitle(node->kind),
            static_cast<unsigned long long>(node->id));
        ImGui::Separator();
        bool changed = false;
        using K = Rendering::ShaderComposerNodeKind;
        if (node->kind == K::Float)
            changed |= ImGui::DragFloat("Value", &node->value, 0.01f);
        else if (node->kind == K::Color)
            changed |= ImGui::ColorEdit4("Color", &node->color.x);
        else if (node->kind == K::FloatProperty || node->kind == K::ColorProperty || node->kind == K::TextureProperty)
        {
            changed |= EditString("HLSL Name", node->name, 128);
            changed |= EditString("Display Name", node->display_name, 192);
            changed |= EditString("Category", node->category, 192);
            changed |= EditString("Tooltip", node->tooltip, 384);
            if (node->kind == K::FloatProperty)
            {
                changed |= ImGui::DragFloat("Default", &node->value, 0.01f);
                changed |= ImGui::DragFloat("Minimum", &node->minimum, 0.01f);
                changed |= ImGui::DragFloat("Maximum", &node->maximum, 0.01f);
                if (node->minimum > node->maximum) std::swap(node->minimum, node->maximum);
            }
            else if (node->kind == K::ColorProperty)
                changed |= ImGui::ColorEdit4("Default", &node->color.x);
            else
            {
                const char* defaults[] = { "white", "black", "gray", "bump" };
                int selected = 0;
                for (int i = 0; i < 4; ++i) if (node->default_texture == defaults[i]) selected = i;
                if (ImGui::Combo("Default Texture", &selected, defaults, 4))
                { node->default_texture = defaults[selected]; changed = true; }
            }
        }
        else if (node->kind == K::Fresnel)
            changed |= ImGui::DragFloat("Fallback Power", &node->value, 0.02f, 0.01f, 32.0f);
        else if (node->kind == K::UVScroll)
            changed |= ImGui::DragFloat2("Fallback Speed", &node->vector2.x, 0.01f);
        else if (node->kind == K::Noise)
            changed |= ImGui::DragFloat("Fallback Scale", &node->value, 0.05f, 0.01f, 256.0f);
        else if (node->kind == K::Dissolve)
        {
            changed |= ImGui::SliderFloat("Fallback Threshold", &node->value, 0.0f, 1.0f);
            changed |= ImGui::SliderFloat("Fallback Edge", &node->minimum, 0.001f, 0.5f);
        }
        if (changed) dirty_ = true;
    }

    void ShaderComposerEditor::Draw(const std::filesystem::path& project_root,
        Rendering::ShaderLibrary& shader_library, Assets::AssetDatabase& asset_database)
    {
        if (!visible_ || path_.empty())
        {
            keyboard_focus_ = false;
            return;
        }
        ImGui::SetNextWindowSize(ImVec2(1180, 720), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Shader Composer", &visible_, ImGuiWindowFlags_MenuBar))
        {
            keyboard_focus_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            ImGui::End();
            return;
        }
        keyboard_focus_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::MenuItem("Save + Generate + Compile", "Ctrl+S"))
                SaveAndGenerate(project_root, shader_library, asset_database);
            if (ImGui::MenuItem("Generate Preview"))
            {
                const Rendering::ShaderComposerGenerateResult result =
                    Rendering::ShaderComposerGenerator::Generate(asset_);
                status_ = result.succeeded ? "HLSL generation OK" :
                    (result.diagnostics.empty() ? "Generation failed" : result.diagnostics.front().message);
            }
            ImGui::EndMenuBar();
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed('S'))
            SaveAndGenerate(project_root, shader_library, asset_database);

        ImGui::Text("%s%s", path_.filename().u8string().c_str(), dirty_ ? " *" : "");
        ImGui::SameLine();
        ImGui::TextDisabled("-> %s", asset_.generated_hlsl.generic_u8string().c_str());
        if (!status_.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", status_.c_str());
        }

        ImGui::SetNextItemWidth(220);
        if (EditString("Display", asset_.display_name, 192)) dirty_ = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220);
        if (EditString("Category", asset_.category, 192)) dirty_ = true;
        ImGui::SameLine();
        ImGui::TextDisabled("Lighting: Unlit (Composer v1)");
        asset_.lighting_model = Rendering::ShaderLightingModel::Unlit;

        const float inspector_width = 315.0f;
        if (ImGui::BeginChild("##ComposerLeft", ImVec2(-inspector_width - 8, 0), false))
            DrawCanvas();
        ImGui::EndChild();
        ImGui::SameLine();
        if (ImGui::BeginChild("##ComposerInspector", ImVec2(0, 0), true))
            DrawInspector();
        ImGui::EndChild();

        ImGui::End();
        if (!visible_ && dirty_)
        {
            std::string save_error;
            if (!AutoSaveGraph(save_error))
            {
                status_ = "Autosave failed: " + save_error;
                visible_ = true;
            }
        }
    }
}
