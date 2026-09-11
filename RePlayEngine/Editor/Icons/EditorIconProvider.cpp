#include "EditorIconProvider.h"

#include "../../Assets/AssetDatabase.h"
#include "../../Object/Registry/ComponentTypeInfo.h"
#include "../../Project/ProjectSettings.h"
#include "../Style/EditorStyle.h"
#include "imgui/imgui_internal.h"

#include <algorithm>
#include <system_error>
#include <utility>

namespace ReplayEngine::Editor
{
    void EditorIconProvider::Configure(const Assets::AssetDatabase* database,
        TextureResolver resolver, const Project::ProjectSettings* settings,
        BytesTextureResolver bytes_resolver)
    {
        database_ = database;
        settings_ = settings;
        texture_resolver_ = std::move(resolver);
        bytes_texture_resolver_ = std::move(bytes_resolver);
    }

    bool EditorIconProvider::SetAtlasGuids(const std::vector<std::string>& guids)
    {
        if (atlas_guids_ == guids) return false;
        atlas_guids_ = guids;
        Reload();
        return true;
    }

    void EditorIconProvider::Reload()
    {
        atlases_.clear();
        atlas_error_.clear();
        files_.clear();
        resolved_.clear();
        for (const std::string& guid : atlas_guids_)
        {
            if (guid.empty()) continue;
            const auto* record = database_ != nullptr ? database_->FindByGuid(guid) : nullptr;
            if (record == nullptr || record->kind != Assets::AssetKind::SpriteAtlas)
            {
                if (atlas_error_.empty()) atlas_error_ = "指定したアトラスが見つかりません";
                continue;
            }
            const auto& path = record->cache_path.empty() ? record->source_path : record->cache_path;
            LoadedAtlas loaded;
            loaded.guid = guid;
            std::string error;
            if (!Assets::SpriteAtlasAsset::LoadFromFile(path, loaded.atlas, error))
            {
                if (atlas_error_.empty()) atlas_error_ = "アトラスを読み込めません: " + error;
                continue;
            }
            // v3 の埋め込みがあれば元画像も隣の DDS も要らない。
            if (loaded.atlas.embedded_texture_bytes.empty())
            {
                std::error_code image_error;
                if (!loaded.atlas.embedded_texture_path.empty())
                {
                    const auto embedded = path.parent_path() /
                        std::filesystem::u8path(loaded.atlas.embedded_texture_path).filename();
                    if (std::filesystem::is_regular_file(embedded, image_error))
                        loaded.image_path = embedded;
                }
                if (loaded.image_path.empty() && database_ != nullptr)
                {
                    const auto* image = database_->FindByGuid(loaded.atlas.image_guid);
                    if (image != nullptr)
                    {
                        const auto& image_path = image->cache_path.empty()
                            ? image->source_path : image->cache_path;
                        image_error.clear();
                        if (std::filesystem::is_regular_file(image_path, image_error))
                            loaded.image_path = image_path;
                    }
                }
                if (loaded.image_path.empty())
                {
                    if (atlas_error_.empty()) atlas_error_ = "アトラスの画像が見つかりません";
                    continue;
                }
            }
            atlases_.push_back(std::move(loaded));
        }

        // 一覧を再読込時に確定し、描画中は未発見のキーもファイル探索しない。
        std::error_code ec;
        const auto directory = std::filesystem::path("resources") / "Icons";
        std::filesystem::directory_iterator it(directory, ec), end;
        for (; !ec && it != end; it.increment(ec))
        {
            std::error_code file_error;
            if (!it->is_regular_file(file_error)) continue;
            const auto extension = it->path().extension();
            if (extension == ".png" || extension == ".dds")
                files_.emplace(it->path().filename().string(), it->path());
        }
    }

    ImVec4 EditorIconProvider::ResolveTint(const std::string& key, const std::string& category) const
    {
        if (settings_ != nullptr)
        {
            const auto& colors = settings_->IconTints();
            auto found = colors.find(key);
            if (found == colors.end() && !category.empty()) found = colors.find(category);
            if (found != colors.end())
            {
                const auto& color = found->second;
                return ImVec4(color.x, color.y, color.z, color.w);
            }
        }
        return category.empty() ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
            : EditorStyle::ComponentCategoryColor(category);
    }

    EditorIconProvider::Icon EditorIconProvider::Resolve(const std::string& key,
        const std::string& category)
    {
        if (key.empty()) return {};
        auto found = resolved_.find(key);
        if (found == resolved_.end())
        {
            Source source;
            // 利用者が選んだ領域を最優先。空の指定は「出さない」の意味。
            const LoadedAtlas* owner = nullptr;
            const Assets::SpriteAtlasRegion* region = nullptr;
            bool suppressed = false;
            bool chosen = false;
            if (settings_ != nullptr)
            {
                const auto& overrides = settings_->IconRegions();
                const auto picked = overrides.find(key);
                if (picked != overrides.end())
                {
                    chosen = true;
                    if (picked->second.region.empty()) suppressed = true;
                    else region = FindRegion(picked->second.region,
                        picked->second.atlas_guid, owner);
                }
            }
            // 名前での自動一致は既定で行わない。設定で有効にしたときだけ引く。
            if (!chosen && settings_ != nullptr && settings_->IconAutoMatchByName())
                region = FindRegion(key, {}, owner);
            if (suppressed)
            {
                // 何も入れない。以降の名前一致も PNG 探索も行わない。
            }
            else if (region != nullptr && owner != nullptr)
            {
                source.from_embedded = !owner->atlas.embedded_texture_bytes.empty();
                source.atlas_guid = owner->guid;
                if (!source.from_embedded) source.path = owner->image_path;
                source.uv0 = { region->uv_rect.x, region->uv_rect.y };
                source.uv1 = { region->uv_rect.x + region->uv_rect.z,
                    region->uv_rect.y + region->uv_rect.w };
            }
            else
            {
                auto file = files_.find(key + ".png");
                if (file == files_.end()) file = files_.find(key + ".dds");
                if (file != files_.end()) source.path = file->second;
            }
            found = resolved_.emplace(key, std::move(source)).first;
        }
        const Source& source = found->second;
        if (!source.from_embedded && source.path.empty()) return {};
        Icon icon;
        icon.available = true;
        icon.tint = ResolveTint(key, category);
        icon.uv0 = source.uv0;
        icon.uv1 = source.uv1;
        if (source.from_embedded)
        {
            const LoadedAtlas* owner = FindAtlas(source.atlas_guid);
            if (owner != nullptr && bytes_texture_resolver_)
                icon.texture = bytes_texture_resolver_(owner->guid,
                    owner->atlas.embedded_texture_bytes);
        }
        else if (texture_resolver_) icon.texture = texture_resolver_(source.path);
        return icon;
    }

    const EditorIconProvider::LoadedAtlas* EditorIconProvider::FindAtlas(
        const std::string& guid) const
    {
        for (const auto& loaded : atlases_)
            if (loaded.guid == guid) return &loaded;
        return nullptr;
    }

    const Assets::SpriteAtlasRegion* EditorIconProvider::FindRegion(
        const std::string& region_name, const std::string& atlas_guid,
        const LoadedAtlas*& owner) const
    {
        owner = nullptr;
        if (region_name.empty()) return nullptr;
        // アトラスの指定があればその 1 枚だけ。無ければ並び順で最初に見つかったもの。
        for (const auto& loaded : atlases_)
        {
            if (!atlas_guid.empty() && loaded.guid != atlas_guid) continue;
            if (const auto* region = loaded.atlas.FindRegion(region_name))
            {
                owner = &loaded;
                return region;
            }
        }
        return nullptr;
    }

    std::size_t EditorIconProvider::RegionCount() const noexcept
    {
        std::size_t count = 0;
        for (const auto& loaded : atlases_) count += loaded.atlas.regions.size();
        return count;
    }

    std::vector<EditorIconProvider::RegionRef> EditorIconProvider::RegionNames() const
    {
        std::vector<RegionRef> names;
        for (const auto& loaded : atlases_)
            for (const auto& region : loaded.atlas.regions)
                names.push_back(RegionRef{ loaded.guid, region.name });
        return names;
    }

    EditorIconProvider::Icon EditorIconProvider::IconForRegion(const std::string& region_name,
        const std::string& atlas_guid)
    {
        const LoadedAtlas* owner = nullptr;
        const auto* region = FindRegion(region_name, atlas_guid, owner);
        if (region == nullptr || owner == nullptr) return {};
        const bool embedded = !owner->atlas.embedded_texture_bytes.empty();
        if (!embedded && owner->image_path.empty()) return {};
        Icon icon;
        icon.available = true;
        icon.uv0 = { region->uv_rect.x, region->uv_rect.y };
        icon.uv1 = { region->uv_rect.x + region->uv_rect.z,
            region->uv_rect.y + region->uv_rect.w };
        if (embedded)
        {
            if (bytes_texture_resolver_)
                icon.texture = bytes_texture_resolver_(owner->guid,
                    owner->atlas.embedded_texture_bytes);
        }
        else if (texture_resolver_) icon.texture = texture_resolver_(owner->image_path);
        return icon;
    }

    EditorIconProvider::Icon EditorIconProvider::ResolveComponent(const Core::ComponentTypeInfo& info)
    {
        Icon icon = Resolve(info.type_name, info.category);
        if (!icon) icon = Resolve(info.category, info.category);
        icon.tint = ResolveTint(info.type_name, info.category);
        return icon;
    }

    bool EditorIconProvider::DrawHeader(const char* id, const char* label,
        ImGuiTreeNodeFlags flags, const std::vector<Icon>& icons, bool hover_only, float brightness)
    {
        if (icons.empty())
            return id != nullptr ? ImGui::TreeNodeEx(id, flags, "%s", label)
                : ImGui::CollapsingHeader(label, flags);
        if (id == nullptr) flags |= ImGuiTreeNodeFlags_CollapsingHeader;
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return false;
        const auto& style = ImGui::GetStyle();
        const bool framed = (flags & ImGuiTreeNodeFlags_Framed) != 0;
        const float size = ImGui::GetTextLineHeight();
        const float padding_y = (framed || (flags & ImGuiTreeNodeFlags_FramePadding))
            ? style.FramePadding.y : (std::min)(window->DC.CurrLineTextBaseOffset, style.FramePadding.y);
        ImVec2 position = ImGui::GetCursorScreenPos();
        position.x += size + style.FramePadding.x * (framed ? 3.0f : 2.0f);
        position.y += (std::max)(padding_y, window->DC.CurrLineTextBaseOffset);
        const float right = window->WorkRect.Max.x;
        const float available = (std::max)(0.0f, right - position.x);
        const float gap = style.ItemInnerSpacing.x;
        const float step = size + gap;
        const float name_width = (std::min)(ImGui::CalcTextSize(label, nullptr, true).x, size * 4.0f);
        const float budget = (std::max)(0.0f, available - name_width - gap);
        std::size_t shown = icons.size();
        std::string remainder;
        while (id != nullptr && shown > 0)
        {
            const float suffix = shown < icons.size()
                ? ImGui::CalcTextSize(("+" + std::to_string(icons.size() - shown)).c_str()).x + gap : 0.0f;
            if (shown * step + suffix <= budget) break;
            --shown;
        }
        if (shown < icons.size()) remainder = "+" + std::to_string(icons.size() - shown);
        const float reserved = shown * step + (remainder.empty() ? 0.0f
            : ImGui::CalcTextSize(remainder.c_str()).x + gap);

        // 空の見出しへ重ね描きし、ツリーの操作対象と行高をそのまま保つ。
        const bool open = ImGui::TreeNodeEx(id != nullptr ? id : label, flags, "%s", "");
        const bool visible = !hover_only || ImGui::IsItemHovered();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->PushClipRect(ImVec2(position.x, ImGui::GetItemRectMin().y),
            ImVec2((std::max)(position.x, right), ImGui::GetItemRectMax().y), true);
        if (visible)
        {
            for (std::size_t index = 0; index < shown; ++index)
            {
                const ImVec2 start(position.x + index * step, position.y);
                ImVec4 tint = icons[index].tint;
                tint.x *= brightness;
                tint.y *= brightness;
                tint.z *= brightness;
                if (icons[index].texture != nullptr)
                    draw_list->AddImage(icons[index].texture, start,
                        ImVec2(start.x + size, start.y + size), icons[index].uv0, icons[index].uv1,
                        ImGui::GetColorU32(tint));
            }
            if (!remainder.empty())
                draw_list->AddText(ImVec2(position.x + shown * step, position.y),
                    ImGui::GetColorU32(ImGuiCol_Text), remainder.c_str());
        }
        draw_list->AddText(ImVec2(position.x + reserved, position.y),
            ImGui::GetColorU32(ImGuiCol_Text), label, ImGui::FindRenderedTextEnd(label));
        draw_list->PopClipRect();
        return open;
    }
}
