#pragma once

#include "framework.h"
#include "../../RePlayEngine/Assets/AssetCache.h"
#include "../../RePlayEngine/Assets/AssetDatabase.h"
#include "../../RePlayEngine/Localization/LocalizationTable.h"
#include "../../RePlayEngine/Rendering/Effects/EffectPresetAsset.h"
#include "../../RePlayEngine/Editor/Style/EditorStyle.h"
#include "../../RePlayEngine/Motion/CompositionAsset.h"
#include "../../RePlayEngine/Motion/MotionAsset.h"
#include "../../RePlayEngine/Rendering/Materials/MaterialAsset.h"
#include "../../RePlayEngine/Rendering/Shaders/ShaderAssetFactory.h"
#include "../../RePlayEngine/Rendering/ShaderComposer/ShaderComposerAsset.h"
#include "../../RePlayEngine/Rendering/ShaderComposer/ShaderComposerGenerator.h"
#include "../../RePlayEngine/Runtime/Scene/SceneFlowAsset.h"
#include "../../RePlayEngine/Scripting/CSharp/CSharpProject.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace framework_project_browser::Detail
{
    // Project ブラウザに出さないフォルダ。
    // ビルド生成物・外部ライブラリ・VCS 内部データはノイズにしかならない。
    inline bool IsHiddenProjectFolder(const std::string& name)
    {
        static const char* const hidden[] =
        {
            ".git", ".vs", ".vscode", ".github",
            "obj", "bin", "x64", "x86", "ipch",
            "imgui", "cereal-master", "tinygltf-release",
            "Saved", "Tools", "node_modules",
        };
        for (const char* entry : hidden)
        {
            if (_stricmp(name.c_str(), entry) == 0) return true;
        }
        return false;
    }

    inline std::string ToLowerCopy(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    inline bool IsImageExtension(const std::string& lower_extension)
    {
        return lower_extension == ".png" || lower_extension == ".jpg" ||
            lower_extension == ".jpeg" || lower_extension == ".bmp" ||
            lower_extension == ".dds" || lower_extension == ".tga" ||
            lower_extension == ".tif" || lower_extension == ".tiff" ||
            lower_extension == ".gif" || lower_extension == ".hdr";
    }

    inline std::filesystem::path NormalizeProjectPath(const std::filesystem::path& value)
    {
        std::error_code error;
        std::filesystem::path absolute = std::filesystem::absolute(value, error);
        if (error) absolute = value;
        return absolute.lexically_normal();
    }

    inline bool IsProjectPathInsideOrEqual(const std::filesystem::path& value,
        const std::filesystem::path& parent)
    {
        const std::filesystem::path absolute_value = NormalizeProjectPath(value);
        const std::filesystem::path absolute_parent = NormalizeProjectPath(parent);
        std::error_code error;
        const std::filesystem::path relative =
            std::filesystem::relative(absolute_value, absolute_parent, error);
        if (error) return absolute_value == absolute_parent;
        if (relative.empty() || relative == ".") return true;
        const auto first = relative.begin();
        return first != relative.end() && first->generic_u8string() != "..";
    }

    inline bool ValidateProjectEntryName(const std::string& name, std::string& error)
    {
        error.clear();
        if (name.empty() || name == "." || name == "..")
        {
            error = "名前が空か、使用できない名前です";
            return false;
        }
        if (name.back() == ' ' || name.back() == '.')
        {
            error = "名前の末尾に空白または . は使えません";
            return false;
        }
        for (unsigned char character : name)
        {
            if (character < 32 || character == '<' || character == '>' ||
                character == ':' || character == '"' || character == '/' ||
                character == '\\' || character == '|' || character == '?' || character == '*')
            {
                error = "Windowsで使用できない文字が含まれています";
                return false;
            }
        }
        std::string base = name;
        const std::size_t dot = base.find('.');
        if (dot != std::string::npos) base.resize(dot);
        base = ToLowerCopy(base);
        static const char* const reserved[] =
        {
            "con", "prn", "aux", "nul",
            "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
            "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9",
        };
        for (const char* value : reserved)
        {
            if (base == value)
            {
                error = "Windowsの予約名は使えません: " + name;
                return false;
            }
        }
        return true;
    }

    inline std::string SafeProjectFileName(std::string name)
    {
        std::string error;
        return ValidateProjectEntryName(name, error) ? name : std::string();
    }

    // 新規作成時だけ Unity/Explorer 風の "Name (1)" 連番を付ける。
    // 既存ファイルや手動 rename は勝手に変えない。
    inline std::filesystem::path UniqueProjectPath(const std::filesystem::path& folder,
        const std::string& stem, const std::string& extension = {},
        const ReplayEngine::Assets::AssetDatabase* database = nullptr)
    {
        const auto available = [&](const std::filesystem::path& candidate)
        {
            std::error_code error;
            if (std::filesystem::exists(candidate, error) && !error) return false;
            // Project Trash の Undo 待ち Asset は filesystem 上では消えていても
            // AssetDatabase に元 path/GUID を保持している。ここを再利用すると
            // 削除 Asset と新規 Asset が同一 GUID になり得るので予約済みとして扱う。
            return database == nullptr || database->FindByPath(candidate) == nullptr;
        };

        std::filesystem::path candidate = folder / (stem + extension);
        if (available(candidate)) return candidate;
        for (int suffix = 1; suffix < 10000; ++suffix)
        {
            candidate = folder / (stem + " (" + std::to_string(suffix) + ")" + extension);
            if (available(candidate)) return candidate;
        }
        return candidate;
    }

    struct ProjectEntry final
    {
        std::filesystem::path path;
        std::string name;
        bool is_directory = false;
    };

    // フォルダ 1 段ぶんを読む。フォルダが先、その中で名前順。
    inline std::vector<ProjectEntry> ListProjectFolder(const std::filesystem::path& folder,
        bool directories_only)
    {
        std::vector<ProjectEntry> entries;
        std::error_code error;
        if (!std::filesystem::is_directory(folder, error) || error) return entries;

        std::filesystem::directory_iterator iterator(folder,
            std::filesystem::directory_options::skip_permission_denied, error);
        if (error) return entries;

        for (const std::filesystem::directory_entry& item : iterator)
        {
            std::error_code entry_error;
            const bool is_directory = item.is_directory(entry_error);
            if (entry_error) continue;
            if (!is_directory && directories_only) continue;

            const std::string name = item.path().filename().u8string();
            if (name.empty() || name.front() == '.') continue;
            if (is_directory && IsHiddenProjectFolder(name)) continue;

            ProjectEntry entry;
            entry.path = item.path();
            entry.name = name;
            entry.is_directory = is_directory;
            entries.push_back(std::move(entry));
        }

        std::sort(entries.begin(), entries.end(),
            [](const ProjectEntry& a, const ProjectEntry& b)
            {
                if (a.is_directory != b.is_directory) return a.is_directory;
                return ToLowerCopy(a.name) < ToLowerCopy(b.name);
            });
        return entries;
    }

    // Search 入力中は current folder だけでなく Project 全体を検索する。
    // .git/.vs/build 系の隠しフォルダは通常ツリーと同じ規則で再帰しない。
    inline std::vector<ProjectEntry> SearchProjectFiles(const std::filesystem::path& root,
        const std::string& lowered_query)
    {
        std::vector<ProjectEntry> entries;
        if (lowered_query.empty()) return entries;
        std::error_code error;
        std::filesystem::recursive_directory_iterator iterator(root,
            std::filesystem::directory_options::skip_permission_denied, error), end;
        for (; iterator != end && !error; iterator.increment(error))
        {
            if (error) break;
            const auto& item = *iterator;
            std::error_code entry_error;
            const bool directory = item.is_directory(entry_error);
            if (entry_error) continue;
            const std::string filename = item.path().filename().u8string();
            if (directory)
            {
                if (filename.empty() || filename.front() == '.' || IsHiddenProjectFolder(filename))
                    iterator.disable_recursion_pending();
                continue;
            }
            const std::filesystem::path relative = std::filesystem::relative(item.path(), root, entry_error);
            if (entry_error) continue;
            const std::string label = relative.generic_u8string();
            if (ToLowerCopy(label).find(lowered_query) == std::string::npos) continue;
            ProjectEntry entry; entry.path = item.path(); entry.name = label; entry.is_directory = false;
            entries.push_back(std::move(entry));
        }
        std::sort(entries.begin(), entries.end(), [](const ProjectEntry& a, const ProjectEntry& b)
        { return ToLowerCopy(a.name) < ToLowerCopy(b.name); });
        return entries;
    }

    // アイコン画像が無い種別のための文字ラベル。
    // 画像アイコンを用意するまでの繋ぎで、サムネイルとは別物。
    inline const char* KindBadge(ReplayEngine::Assets::AssetKind kind, bool is_directory)
    {
        using ReplayEngine::Assets::AssetKind;
        if (is_directory) return "FOLDER";
        switch (kind)
        {
        case AssetKind::Model:    return "MESH";
        case AssetKind::Image:    return "IMAGE";
        case AssetKind::Material: return "MAT";
        case AssetKind::Scene:    return "SCENE";
        case AssetKind::Script:   return "C#";
        case AssetKind::Audio:    return "AUDIO";
        case AssetKind::Shader:   return "SHADER";
        case AssetKind::SceneFlow:return "FLOW";
        case AssetKind::Motion:   return "MOTION";
        case AssetKind::Font:     return "FONT";
        case AssetKind::Localization: return "LOC";
        case AssetKind::EffectPreset: return "FX";
        case AssetKind::SpriteAtlas:return "ATLAS";
        case AssetKind::Composition:return "COMP";
        case AssetKind::EasingCurve:return "EASE";
        default:                  return "FILE";
        }
    }

    inline ImVec4 KindColor(ReplayEngine::Assets::AssetKind kind, bool is_directory)
    {
        using ReplayEngine::Assets::AssetKind;
        if (is_directory) return ImVec4(0.98f, 0.80f, 0.36f, 1.0f);
        switch (kind)
        {
        case AssetKind::Model:    return ImVec4(0.47f, 0.76f, 0.98f, 1.0f);
        case AssetKind::Image:    return ImVec4(0.62f, 0.90f, 0.62f, 1.0f);
        case AssetKind::Material: return ImVec4(0.95f, 0.60f, 0.85f, 1.0f);
        case AssetKind::Scene:    return ImVec4(0.75f, 0.72f, 0.98f, 1.0f);
        case AssetKind::Script:   return ImVec4(0.45f, 0.88f, 0.80f, 1.0f);
        case AssetKind::Shader:   return ImVec4(0.98f, 0.67f, 0.28f, 1.0f);
        case AssetKind::SceneFlow:return ImVec4(0.55f, 0.86f, 1.00f, 1.0f);
        case AssetKind::Motion:   return ImVec4(0.95f, 0.78f, 0.36f, 1.0f);
        case AssetKind::Font:     return ImVec4(0.80f, 0.92f, 0.98f, 1.0f);
        case AssetKind::Localization:return ImVec4(0.62f, 0.92f, 0.82f, 1.0f);
        case AssetKind::EffectPreset:return ImVec4(0.96f, 0.66f, 0.92f, 1.0f);
        case AssetKind::InputAction:return ImVec4(0.55f, 0.90f, 1.0f, 1.0f);
        case AssetKind::SpriteAtlas:return ImVec4(0.90f, 0.72f, 1.00f, 1.0f);
        case AssetKind::Composition:return ImVec4(0.55f, 0.90f, 1.00f, 1.0f);
        case AssetKind::EasingCurve:return ImVec4(0.78f, 0.96f, 0.56f, 1.0f);
        default:                  return ImVec4(0.72f, 0.72f, 0.72f, 1.0f);
        }
    }
}
