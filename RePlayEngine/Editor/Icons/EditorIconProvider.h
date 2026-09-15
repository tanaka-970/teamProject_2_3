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
        // アトラスは複数枚。並び順が、名前が重なったときの優先順になる。
        bool SetAtlasGuids(const std::vector<std::string>& guids);
        void Reload();
        const std::string& AtlasError() const noexcept { return atlas_error_; }
        ImVec4 ResolveTint(const std::string& key, const std::string& category = {}) const;
        Icon Resolve(const std::string& key, const std::string& category = {});
        Icon ResolveComponent(const Core::ComponentTypeInfo& info);
        // どのアトラスの、どの領域か。名前が重なっても選び分けられるよう対で持つ。
        struct RegionRef final
        {
            std::string atlas_guid;
            std::string name;
        };
        // 選択 UI 用。指定した全アトラスの領域を並び順のまま返す。
        std::vector<RegionRef> RegionNames() const;
        // 画面へ状態を出すための素の値。読めているのかを設定から確認できるようにする。
        std::size_t LoadedAtlasCount() const noexcept { return atlases_.size(); }
        std::size_t RegionCount() const noexcept;
        // 名前を指定して 1 枚だけ引く。解決規則を通さないので選択肢の見本に使える。
        Icon IconForRegion(const std::string& region_name, const std::string& atlas_guid = {});
        // 選択が変わったら覚えた解決結果を捨てる。アトラスは読み直さない。
        void InvalidateResolved() { resolved_.clear(); }
        static bool DrawHeader(const char* id, const char* label, ImGuiTreeNodeFlags flags,
            const std::vector<Icon>& icons, bool hover_only = false, float brightness = 1.0f);

    private:
        // 読み込み済みのアトラス 1 枚ぶん。埋め込みがあれば image_path は空のまま。
        struct LoadedAtlas final
        {
            std::string guid;
            Assets::SpriteAtlasAsset atlas;
            std::filesystem::path image_path;
        };
        struct Source final
        {
            std::filesystem::path path;
            // 空でなければ、パスではなくアトラスの埋め込み DDS から引く。
            bool from_embedded = false;
            std::string atlas_guid;
            ImVec2 uv0{ 0.0f, 0.0f };
            ImVec2 uv1{ 1.0f, 1.0f };
        };
        const LoadedAtlas* FindAtlas(const std::string& guid) const;
        const Assets::SpriteAtlasRegion* FindRegion(const std::string& region_name,
            const std::string& atlas_guid, const LoadedAtlas*& owner) const;
        const Assets::AssetDatabase* database_ = nullptr;
        TextureResolver texture_resolver_;
        BytesTextureResolver bytes_texture_resolver_;
        const Project::ProjectSettings* settings_ = nullptr;
        std::vector<std::string> atlas_guids_;
        std::string atlas_error_;
        std::vector<LoadedAtlas> atlases_;
        std::unordered_map<std::string, std::filesystem::path> files_;
        std::unordered_map<std::string, Source> resolved_;
    };
}
