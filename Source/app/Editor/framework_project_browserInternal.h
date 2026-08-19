#pragma once

#include "framework.h"
#include "texture.h"
#include "../../RePlayEngine/Assets/AssetCache.h"
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
            "DirectXTK-main", "imgui", "cereal-master", "tinygltf-release",
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
            lower_extension == ".gif";
    }

    inline std::string SafeProjectFileName(std::string name)
    {
        for (char& character : name)
        {
            if (character == '<' || character == '>' || character == ':' ||
                character == '"' || character == '/' || character == '\\' ||
                character == '|' || character == '?' || character == '*')
            {
                character = '_';
            }
        }
        while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
        return name;
    }

    // 新規作成時だけ Unity/Explorer 風の "Name (1)" 連番を付ける。
    // 既存ファイルや手動 rename は勝手に変えない。
    inline std::filesystem::path UniqueProjectPath(const std::filesystem::path& folder,
        const std::string& stem, const std::string& extension = {})
    {
        std::filesystem::path candidate = folder / (stem + extension);
        std::error_code error;
        if (!std::filesystem::exists(candidate, error) || error) return candidate;
        for (int suffix = 1; suffix < 10000; ++suffix)
        {
            candidate = folder / (stem + " (" + std::to_string(suffix) + ")" + extension);
            error.clear();
            if (!std::filesystem::exists(candidate, error) || error) return candidate;
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
        default:                  return ImVec4(0.72f, 0.72f, 0.72f, 1.0f);
        }
    }
}
