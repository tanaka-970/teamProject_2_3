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
            "deferred | bloom [on/off/toggle] | save | undo | redo | duplicate | objects | "
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
    if (name == "bloom")
    {
        if (ApplySwitch(argument, enable_bloom_shader))
            editor_command_result = std::string("Bloom: ") + (enable_bloom_shader ? "ON" : "OFF");
        else editor_command_result = "使い方: bloom [on/off/toggle]";
        return;
    }
    if (name == "save" || name == "保存")
    {
        save_object_scene(false);
        editor_command_result = object_editor_context.Status();
        return;
    }
    if (name == "undo")
    {
        editor_command_result = object_editor_context.Undo()
            ? "元に戻しました" : "元に戻せる操作がありません";
        return;
    }
    if (name == "redo")
    {
        editor_command_result = object_editor_context.Redo()
            ? "やり直しました" : "やり直せる操作がありません";
        return;
    }
    if (name == "duplicate" || name == "複製")
    {
        object_hierarchy_panel.DuplicateSelection(object_editor_context);
        editor_command_result = object_editor_context.Status();
        return;
    }
    if (name == "objects" || name == "一覧")
    {
        std::ostringstream result;
        const auto& scene = active_object_scene();
        result << "GameObjects " << scene.GameObjectCount() << "件";
        for (std::size_t index = 0; index < scene.GameObjectCount(); ++index)
            if (const auto* object = scene.GameObjectAt(index)) result << " | " << object->Name();
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
