#pragma once

#include "../../Assets/SpriteAtlasAsset.h"
#include "imgui/imgui.h"

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ReplayEngine::Assets { class AssetDatabase; }
namespace ReplayEngine::Core { struct ComponentTypeInfo; }
namespace ReplayEngine::Project { class ProjectSettings; }

namespace ReplayEngine::Editor
{
    enum class HierarchyIconDisplay { Always, Hovered, Hidden };

    class EditorIconProvider final
    {
    public:
        struct Icon final
        {
            ImTextureID texture = nullptr;
            ImVec2 uv0{ 0.0f, 0.0f };
            ImVec2 uv1{ 1.0f, 1.0f };
            ImVec4 tint{ 1.0f, 1.0f, 1.0f, 1.0f };
            bool available = false;
            explicit operator bool() const noexcept { return available; }
        };
        using TextureResolver = std::function<void*(const std::filesystem::path&)>;
        // 埋め込み DDS 用。key はアトラスの GUID を渡す。
        using BytesTextureResolver =
            std::function<void*(const std::string&, const std::vector<std::uint8_t>&)>;
        void Configure(const Assets::AssetDatabase* database, TextureResolver resolver,
            const Project::ProjectSettings* settings = nullptr,
            BytesTextureResolver bytes_resolver = {});
        bool SetAtlasGuid(const std::string& guid);
        void Reload();
        const std::string& AtlasError() const noexcept { return atlas_error_; }
        ImVec4 ResolveTint(const std::string& key, const std::string& category = {}) const;
        Icon Resolve(const std::string& key, const std::string& category = {});
        Icon ResolveComponent(const Core::ComponentTypeInfo& info);
        // 選択 UI 用。アトラスが持つ領域名を並び順のまま返す。
        std::vector<std::string> RegionNames() const;
        // 名前を指定して 1 枚だけ引く。解決規則を通さないので選択肢の見本に使える。
        Icon IconForRegion(const std::string& region_name);
        // 選択が変わったら覚えた解決結果を捨てる。アトラスは読み直さない。
        void InvalidateResolved() { resolved_.clear(); }
        static bool DrawHeader(const char* id, const char* label, ImGuiTreeNodeFlags flags,
            const std::vector<Icon>& icons, bool hover_only = false, float brightness = 1.0f);

    private:
        struct Source final
        {
            std::filesystem::path path;
            // 空でなければ、パスではなくアトラスの埋め込み DDS から引く。
            bool from_embedded = false;
            ImVec2 uv0{ 0.0f, 0.0f };
            ImVec2 uv1{ 1.0f, 1.0f };
        };
        const Assets::AssetDatabase* database_ = nullptr;
        TextureResolver texture_resolver_;
        BytesTextureResolver bytes_texture_resolver_;
        const Project::ProjectSettings* settings_ = nullptr;
        std::string atlas_guid_;
        std::string atlas_error_;
        Assets::SpriteAtlasAsset atlas_;
        std::filesystem::path atlas_image_path_;
        std::unordered_map<std::string, std::filesystem::path> files_;
        std::unordered_map<std::string, Source> resolved_;
    };
}
