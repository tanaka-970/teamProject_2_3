#include "framework.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace
{
    std::string NormalizeCommand(std::string text)
    {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return {};
        const auto last = text.find_last_not_of(" \t\r\n");
        text = text.substr(first, last - first + 1);
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character)
        {
            return character < 128 ? static_cast<char>(std::tolower(character)) :
                static_cast<char>(character);
        });
        return text;
    }

    bool ApplySwitch(const std::string& value, bool& target)
    {
        if (value == "on") target = true;
        else if (value == "off") target = false;
        else if (value == "toggle" || value.empty()) target = !target;
        else return false;
        return true;
    }
}

void framework::execute_editor_command(const std::string& command)
{
    const std::string normalized = NormalizeCommand(command);
    std::istringstream stream(normalized);
    std::string name;
    std::string argument;
    stream >> name >> argument;

    if (name == "help" || name == "ヘルプ")
    {
        editor_command_result =
            "deferred [on/off/toggle] | stage [pbr/toon/unlit/default] | "
            "outline [on/off/toggle] | bloom [on/off/toggle] | save | undo | redo | "
            "copy | paste | duplicate | placements | "
            "workspace [general/placement/modeling/animation/rendering] | fullscreen";
        return;
    }
    if (name == "deferred")
    {
        enable_deferred = true;
        editor_command_result = argument == "off"
            ? "通常Forwardは廃止済みです。Deferred固定で動作します"
            : "Renderer: Deferred（固定）";
        return;
    }
    if (name == "stage")
    {
        if (argument == "pbr") shading_per_stage = SHADING_MODEL_PBR;
        else if (argument == "toon") shading_per_stage = SHADING_MODEL_TOON;
        else if (argument == "unlit") shading_per_stage = SHADING_MODEL_UNLIT;
        else if (argument == "default") shading_per_stage = SHADING_MODEL_FBX_DEFAULT;
        else
        {
            editor_command_result = "使い方: stage [pbr/toon/unlit/default]";
            return;
        }
        enable_stage_shader = true;
        editor_command_result = "ステージのSurfaceシェーダーを変更しました";
        return;
    }
    if (name == "outline")
    {
        if (ApplySwitch(argument, outline_per_stage))
        {
            if (outline_per_stage) enable_outline_shader = true;
            editor_command_result = std::string("ステージ輪郭線: ") + (outline_per_stage ? "ON" : "OFF");
        }
        else editor_command_result = "使い方: outline [on/off/toggle]";
        return;
    }
    if (name == "bloom")
    {
        if (ApplySwitch(argument, enable_bloom_shader))
            editor_command_result = std::string("Bloom: ") + (enable_bloom_shader ? "ON" : "OFF");
        else editor_command_result = "使い方: bloom [on/off/toggle]";
        return;
    }
    if (name == "save" || name == "保存")
    {
        save_scene_document(false);
        editor_command_result = scene_document_status;
        return;
    }
    if (name == "undo")
    {
        std::string label;
        editor_command_result = scene_undo_stack.Undo(editor_scene_document, label)
            ? "元に戻しました: " + label : "元に戻せる操作がありません";
        return;
    }
    if (name == "redo")
    {
        std::string label;
        editor_command_result = scene_undo_stack.Redo(editor_scene_document, label)
            ? "やり直しました: " + label : "やり直せる操作がありません";
        return;
    }
    if (name == "copy" || name == "コピー")
    {
        copy_selected_entities();
        editor_command_result = scene_document_status;
        return;
    }
    if (name == "paste" || name == "貼り付け")
    {
        paste_copied_entities();
        editor_command_result = scene_document_status;
        return;
    }
    if (name == "duplicate" || name == "複製")
    {
        duplicate_selected_entities();
        editor_command_result = scene_document_status;
        return;
    }
    if (name == "placements" || name == "配置一覧")
    {
        std::ostringstream result;
        result << "配置記録 " << editor_scene_document.Entities().size() << "件";
        for (const auto& entity : editor_scene_document.Entities())
            result << " | " << entity.name << " [" << entity.identifier << ']';
        editor_command_result = result.str();
        return;
    }
    if (name == "workspace")
    {
        if (argument == "general") set_editor_workspace(editor_workspace::general);
        else if (argument == "placement") set_editor_workspace(editor_workspace::placement);
        else if (argument == "modeling") set_editor_workspace(editor_workspace::modeling);
        else if (argument == "animation") set_editor_workspace(editor_workspace::animation);
        else if (argument == "rendering") set_editor_workspace(editor_workspace::rendering);
        else if (argument == "shader") set_editor_workspace(editor_workspace::shader_adjustment);
        else
        {
            editor_command_result = "使い方: workspace [general/placement/modeling/animation/rendering/shader]";
            return;
        }
        editor_command_result = "ワークスペースを切り替えました";
        return;
    }
    if (name == "fullscreen")
    {
        toggle_fullscreen();
        editor_command_result = "全画面表示を切り替えました";
        return;
    }
    editor_command_result = "不明なコマンドです。help を入力してください";
}
