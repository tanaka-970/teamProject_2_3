#pragma once

#include "../../Rendering/ShaderComposer/ShaderComposerAsset.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace ReplayEngine::Assets { class AssetDatabase; }
namespace ReplayEngine::Rendering { class ShaderLibrary; }

namespace ReplayEngine::Editor
{
    // First-generation visual Shader Composer.
    // Graph is the source of truth; generated .hlsl is an ordinary ShaderAsset consumed by ShaderLibrary.
    class ShaderComposerEditor final
    {
    public:
        bool Open(const std::filesystem::path& path, std::string& error);
        void Close() noexcept { visible_ = false; }
        bool IsVisible() const noexcept { return visible_; }
        bool HasAsset() const noexcept { return !path_.empty(); }
        const std::filesystem::path& Path() const noexcept { return path_; }
        void Show() noexcept { if (HasAsset()) visible_ = true; }
        void NotifyAssetRenamed(const std::filesystem::path& old_path,
            const std::filesystem::path& new_path) noexcept
        {
            if (path_ == old_path) path_ = new_path;
        }

        // Graph data is the source of truth. This autosaves only the graph asset;
        // HLSL generation/compile remains an explicit Ctrl+S operation so an
        // invalid in-progress graph never destroys the last successful shader.
        bool AutoSaveGraph(std::string& error);

        void Draw(const std::filesystem::path& project_root,
            Rendering::ShaderLibrary& shader_library,
            Assets::AssetDatabase& asset_database);

    private:
        bool SaveAndGenerate(const std::filesystem::path& project_root,
            Rendering::ShaderLibrary& shader_library,
            Assets::AssetDatabase& asset_database);
        void DrawCanvas();
        void DrawInspector();
        void AddNode(Rendering::ShaderComposerNodeKind kind, float x, float y);

        std::filesystem::path path_;
        Rendering::ShaderComposerAsset asset_;
        bool visible_ = false;
        bool dirty_ = false;
        std::uint64_t selected_node_ = 0;
        std::uint64_t pending_output_node_ = 0;
        float pan_x_ = 20.0f;
        float pan_y_ = 20.0f;
        std::string status_;
    };
}
