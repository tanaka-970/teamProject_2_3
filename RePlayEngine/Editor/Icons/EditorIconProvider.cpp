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

    bool EditorIconProvider::SetAtlasGuid(const std::string& guid)
    {
        if (atlas_guid_ == guid) return false;
        atlas_guid_ = guid;
        Reload();
        return true;
    }

    void EditorIconProvider::Reload()
    {
        atlas_ = {};
        atlas_image_path_.clear();
        atlas_error_.clear();
        files_.clear();
        resolved_.clear();
        if (!atlas_guid_.empty())
        {
            const auto* record = database_ != nullptr ? database_->FindByGuid(atlas_guid_) : nullptr;
            if (record == nullptr || record->kind != Assets::AssetKind::SpriteAtlas)
                atlas_error_ = "指定したアトラスが見つかりません";
            else
            {
                const auto& path = record->cache_path.empty() ? record->source_path : record->cache_path;
                std::string error;
                if (!Assets::SpriteAtlasAsset::LoadFromFile(path, atlas_, error))
                    atlas_error_ = "アトラスを読み込めません: " + error;
                else if (!atlas_.embedded_texture_bytes.empty())
                {
                    // v3 の埋め込み。元画像も隣の DDS も要らない。
                }
                else
                {
                    std::error_code image_error;
                    if (!atlas_.embedded_texture_path.empty())
                    {
                        const auto embedded = path.parent_path() /
                            std::filesystem::u8path(atlas_.embedded_texture_path).filename();
                        if (std::filesystem::is_regular_file(embedded, image_error))
                            atlas_image_path_ = embedded;
                    }
                    if (atlas_image_path_.empty())
                    {
                        const auto* image = database_->FindByGuid(atlas_.image_guid);
                        if (image != nullptr)
                        {
                            const auto& image_path = image->cache_path.empty()
                                ? image->source_path : image->cache_path;
                            image_error.clear();
                            if (std::filesystem::is_regular_file(image_path, image_error))
                                atlas_image_path_ = image_path;
                        }
                    }
                    if (atlas_image_path_.empty())
                        atlas_error_ = "アトラスの画像が見つかりません";
                }
            }
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
                    if (picked->second.empty()) suppressed = true;
                    else region = atlas_.FindRegion(picked->second);
                }
            }
            // 名前での自動一致は既定で行わない。設定で有効にしたときだけ引く。
            if (!chosen && settings_ != nullptr && settings_->IconAutoMatchByName())
                region = atlas_.FindRegion(key);
            const bool embedded = !atlas_.embedded_texture_bytes.empty();
            if (suppressed)
            {
                // 何も入れない。以降の名前一致も PNG 探索も行わない。
            }
            else if (region != nullptr && (embedded || !atlas_image_path_.empty()))
            {
                source.from_embedded = embedded;
                if (!embedded) source.path = atlas_image_path_;
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
            if (bytes_texture_resolver_)
                icon.texture = bytes_texture_resolver_(atlas_guid_, atlas_.embedded_texture_bytes);
        }
        else if (texture_resolver_) icon.texture = texture_resolver_(source.path);
        return icon;
    }

    std::vector<std::string> EditorIconProvider::RegionNames() const
    {
        std::vector<std::string> names;
        names.reserve(atlas_.regions.size());
        for (const auto& region : atlas_.regions) names.push_back(region.name);
        return names;
    }

    EditorIconProvider::Icon EditorIconProvider::IconForRegion(const std::string& region_name)
    {
        const auto* region = atlas_.FindRegion(region_name);
        if (region == nullptr) return {};
        const bool embedded = !atlas_.embedded_texture_bytes.empty();
        if (!embedded && atlas_image_path_.empty()) return {};
        Icon icon;
        icon.available = true;
        icon.uv0 = { region->uv_rect.x, region->uv_rect.y };
        icon.uv1 = { region->uv_rect.x + region->uv_rect.z,
            region->uv_rect.y + region->uv_rect.w };
        if (embedded)
        {
            if (bytes_texture_resolver_)
                icon.texture = bytes_texture_resolver_(atlas_guid_, atlas_.embedded_texture_bytes);
        }
        else if (texture_resolver_) icon.texture = texture_resolver_(atlas_image_path_);
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
