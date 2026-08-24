#include "framework.h"
#include "../../RePlayEngine/Assets/SpriteAtlasAsset.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

namespace
{
    bool AtlasRectOverlaps(const DirectX::XMFLOAT4& a, const DirectX::XMFLOAT4& b)
    {
        return a.x < b.x + b.z && a.x + a.z > b.x &&
            a.y < b.y + b.w && a.y + a.w > b.y;
    }

    float SnapAtlasUV(float value, float pixels, bool enabled)
    {
        if (!enabled || pixels <= 1.0f) return value;
        return std::round(value * pixels) / pixels;
    }

    bool AtlasRegionIsFreeform(const ReplayEngine::Assets::SpriteAtlasRegion& region)
    {
        return region.path_points.size() >= 3;
    }

    void SetAtlasRegionPathToRect(ReplayEngine::Assets::SpriteAtlasRegion& region)
    {
        const auto& r = region.uv_rect;
        region.path_points = {
            { r.x, r.y },
            { r.x + r.z, r.y },
            { r.x + r.z, r.y + r.w },
            { r.x, r.y + r.w } };
    }

    void UpdateAtlasRegionBoundsFromPath(
        ReplayEngine::Assets::SpriteAtlasRegion& region)
    {
        if (!AtlasRegionIsFreeform(region)) return;
        float min_x = region.path_points.front().x;
        float min_y = region.path_points.front().y;
        float max_x = min_x;
        float max_y = min_y;
        for (const DirectX::XMFLOAT2& point : region.path_points)
        {
            min_x = (std::min)(min_x, point.x);
            min_y = (std::min)(min_y, point.y);
            max_x = (std::max)(max_x, point.x);
            max_y = (std::max)(max_y, point.y);
        }
        region.uv_rect = { min_x, min_y,
            (std::max)(0.000001f, max_x - min_x),
            (std::max)(0.000001f, max_y - min_y) };
    }

    float PointSegmentDistanceSquared(const ImVec2& point,
        const ImVec2& a, const ImVec2& b, float& out_t)
    {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float length_squared = dx * dx + dy * dy;
        out_t = length_squared > 0.000001f
            ? ((point.x - a.x) * dx + (point.y - a.y) * dy) / length_squared
            : 0.0f;
        out_t = (std::max)(0.0f, (std::min)(1.0f, out_t));
        const float cx = a.x + dx * out_t;
        const float cy = a.y + dy * out_t;
        const float ex = point.x - cx;
        const float ey = point.y - cy;
        return ex * ex + ey * ey;
    }

    bool AtlasPointInPolygon(const ReplayEngine::Assets::SpriteAtlasRegion& region,
        float u, float v)
    {
        if (!AtlasRegionIsFreeform(region)) return false;
        bool inside = false;
        for (std::size_t index = 0; index < region.path_points.size(); ++index)
        {
            const auto& a = region.path_points[index];
            const auto& b = region.path_points[(index + 1) % region.path_points.size()];
            const bool crosses = ((a.y > v) != (b.y > v)) &&
                (u < (b.x - a.x) * (v - a.y) /
                    (b.y - a.y) + a.x);
            if (crosses) inside = !inside;
        }
        return inside;
    }
}


void framework::begin_sprite_atlas_edit(const std::string& label)
{
    if (!sprite_atlas_editor_loaded || sprite_atlas_history_transaction) return;
    sprite_atlas_history_before = sprite_atlas_editor_asset;
    sprite_atlas_history_label = label;
    sprite_atlas_history_transaction = true;
}

void framework::commit_sprite_atlas_edit()
{
    if (!sprite_atlas_history_transaction) return;
    if (sprite_atlas_history_cursor < sprite_atlas_history.size())
        sprite_atlas_history.erase(sprite_atlas_history.begin() +
            static_cast<std::ptrdiff_t>(sprite_atlas_history_cursor), sprite_atlas_history.end());
    SpriteAtlasHistoryEntry entry;
    entry.before = std::move(sprite_atlas_history_before);
    entry.after = sprite_atlas_editor_asset;
    entry.label = sprite_atlas_history_label;
    sprite_atlas_history.push_back(std::move(entry));
    if (sprite_atlas_history.size() > 128)
        sprite_atlas_history.erase(sprite_atlas_history.begin());
    sprite_atlas_history_cursor = sprite_atlas_history.size();
    sprite_atlas_history_transaction = false;
    sprite_atlas_history_label.clear();
    sprite_atlas_editor_dirty = true;
}

void framework::cancel_sprite_atlas_edit()
{
    if (!sprite_atlas_history_transaction) return;
    sprite_atlas_history_before = {};
    sprite_atlas_history_label.clear();
    sprite_atlas_history_transaction = false;
}

bool framework::undo_sprite_atlas_edit()
{
    if (sprite_atlas_history_transaction) cancel_sprite_atlas_edit();
    if (sprite_atlas_history_cursor == 0 || sprite_atlas_history.empty()) return false;
    --sprite_atlas_history_cursor;
    const auto& entry = sprite_atlas_history[sprite_atlas_history_cursor];
    sprite_atlas_editor_asset = entry.before;
    sprite_atlas_selected_region = (std::min)(sprite_atlas_selected_region,
        static_cast<int>(sprite_atlas_editor_asset.regions.size()) - 1);
    sprite_atlas_editor_dirty = true;
    sprite_atlas_editor_status = "Undo: " + entry.label;
    return true;
}

bool framework::redo_sprite_atlas_edit()
{
    if (sprite_atlas_history_transaction) cancel_sprite_atlas_edit();
    if (sprite_atlas_history_cursor >= sprite_atlas_history.size()) return false;
    const auto& entry = sprite_atlas_history[sprite_atlas_history_cursor++];
    sprite_atlas_editor_asset = entry.after;
    sprite_atlas_selected_region = (std::min)(sprite_atlas_selected_region,
        static_cast<int>(sprite_atlas_editor_asset.regions.size()) - 1);
    sprite_atlas_editor_dirty = true;
    sprite_atlas_editor_status = "Redo: " + entry.label;
    return true;
}

void framework::draw_sprite_atlas_editor()
{
    if (!show_sprite_atlas_editor_panel) return;
    if (!ImGui::Begin("Sprite Atlas Editor", &show_sprite_atlas_editor_panel))
    {
        ImGui::End();
        return;
    }
    sprite_atlas_editor_keyboard_focus =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (!sprite_atlas_editor_loaded)
    {
        ImGui::TextDisabled("Sprite Atlas を Project Browser から開いてください。");
        ImGui::End();
        return;
    }

    if (ImGui::Button("保存")) save_current_sprite_atlas();
    ImGui::SameLine();
    ImGui::Text("%s%s", sprite_atlas_editor_asset.name.c_str(),
        sprite_atlas_editor_dirty ? " *" : "");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("Zoom", &sprite_atlas_zoom, 0.1f, 4.0f, "%.2fx");
    ImGui::SameLine();
    if (ImGui::Button(sprite_atlas_draw_region_mode ? "矩形作成: ON" : "矩形作成: OFF"))
        sprite_atlas_draw_region_mode = !sprite_atlas_draw_region_mode;
    ImGui::SameLine();
    ImGui::Checkbox("Pixel Snap", &sprite_atlas_pixel_snap);

    if (const ReplayEngine::Assets::AssetRecord* selected =
        asset_database.FindByGuid(selected_asset_guid))
    {
        if (selected->kind == ReplayEngine::Assets::AssetKind::Image)
        {
            ImGui::SameLine();
            if (ImGui::Button("選択画像をAtlasに設定"))
            {
                begin_sprite_atlas_edit("Atlas画像を変更");
                sprite_atlas_editor_asset.image_guid = selected->guid;
                sprite_atlas_editor_asset.embedded_texture_path.clear();
                commit_sprite_atlas_edit();
            }
        }
    }

    // Project Browser から画像をそのままドロップできる。
    ImGui::Text("Image GUID: %s", sprite_atlas_editor_asset.image_guid.empty()
        ? "<未指定>" : sprite_atlas_editor_asset.image_guid.c_str());
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("REPLAY_ASSET_GUID"))
        {
            const char* guid = static_cast<const char*>(payload->Data);
            if (guid != nullptr)
            {
                if (const ReplayEngine::Assets::AssetRecord* record = asset_database.FindByGuid(guid))
                {
                    if (record->kind == ReplayEngine::Assets::AssetKind::Image)
                    {
                        begin_sprite_atlas_edit("Atlas画像を変更");
                        sprite_atlas_editor_asset.image_guid = record->guid;
                        sprite_atlas_editor_asset.embedded_texture_path.clear();
                        commit_sprite_atlas_edit();
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    const ReplayEngine::Assets::AssetRecord* image_record =
        sprite_atlas_editor_asset.image_guid.empty() ? nullptr :
        asset_database.FindByGuid(sprite_atlas_editor_asset.image_guid);
    std::filesystem::path atlas_texture_path;
    if (!sprite_atlas_editor_asset.embedded_texture_path.empty())
    {
        const std::filesystem::path candidate = sprite_atlas_editor_path.parent_path() /
            std::filesystem::u8path(sprite_atlas_editor_asset.embedded_texture_path);
        std::error_code texture_error;
        if (std::filesystem::exists(candidate, texture_error) && !texture_error)
            atlas_texture_path = candidate;
    }
    if (atlas_texture_path.empty() && image_record != nullptr)
        atlas_texture_path = image_record->source_path;
    ID3D11ShaderResourceView* texture = atlas_texture_path.empty()
        ? nullptr : project_thumbnail_for(atlas_texture_path);

    float image_width = 1.0f;
    float image_height = 1.0f;
    if (texture != nullptr)
    {
        ID3D11Resource* resource = nullptr;
        texture->GetResource(&resource);
        if (resource != nullptr)
        {
            ID3D11Texture2D* texture2d = nullptr;
            if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(&texture2d))) && texture2d != nullptr)
            {
                D3D11_TEXTURE2D_DESC desc{};
                texture2d->GetDesc(&desc);
                image_width = static_cast<float>((std::max)(1u, desc.Width));
                image_height = static_cast<float>((std::max)(1u, desc.Height));
                texture2d->Release();
            }
            resource->Release();
        }
    }

    ImGui::Separator();
    ImGui::BeginChild("##AtlasMain", ImVec2(0.0f, 0.0f), false);
    const float inspector_width = 300.0f;
    const float view_width = (std::max)(100.0f,
        ImGui::GetContentRegionAvail().x - inspector_width - 12.0f);
    ImGui::BeginChild("##AtlasImage", ImVec2(view_width, 0.0f), true,
        ImGuiWindowFlags_HorizontalScrollbar);

    if (texture == nullptr)
    {
        ImGui::TextDisabled("Atlas画像を設定してください。");
    }
    else
    {
        const float base_scale = (std::min)(1.0f,
            view_width / (std::max)(1.0f, image_width));
        const float scale = base_scale * sprite_atlas_zoom;
        const ImVec2 size(image_width * scale, image_height * scale);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImTextureID atlas_texture_id = dx12_framework_active
            ? reinterpret_cast<ImTextureID>(
                dx12_device_context.ImGuiTextureForPath(atlas_texture_path))
            : reinterpret_cast<ImTextureID>(texture);
        ImGui::Image(atlas_texture_id, size,
            ImVec2(0, 0), ImVec2(1, 1));
        ImDrawList* draw = ImGui::GetWindowDrawList();

        for (int index = 0; index < static_cast<int>(sprite_atlas_editor_asset.regions.size()); ++index)
        {
            const auto& region = sprite_atlas_editor_asset.regions[index];
            const ImVec2 a(origin.x + region.uv_rect.x * size.x,
                origin.y + region.uv_rect.y * size.y);
            const ImVec2 b(a.x + region.uv_rect.z * size.x,
                a.y + region.uv_rect.w * size.y);
            const bool freeform = AtlasRegionIsFreeform(region);
            bool overlaps = false;
            if (index == sprite_atlas_selected_region)
            {
                for (int other = 0; other < static_cast<int>(sprite_atlas_editor_asset.regions.size()); ++other)
                    if (other != index && AtlasRectOverlaps(region.uv_rect,
                        sprite_atlas_editor_asset.regions[other].uv_rect)) { overlaps = true; break; }
            }
            const ImU32 color = index == sprite_atlas_selected_region
                ? (overlaps ? IM_COL32(255, 90, 90, 255) : IM_COL32(255, 210, 70, 255))
                : IM_COL32(80, 220, 255, 210);
            if (freeform)
            {
                draw->AddRect(a, b, color & 0x00ffffffu | 0x50000000u,
                    0.0f, 0, 1.0f);
                for (std::size_t point_index = 0; point_index < region.path_points.size(); ++point_index)
                {
                    const auto& point = region.path_points[point_index];
                    const auto& next = region.path_points[(point_index + 1) % region.path_points.size()];
                    const ImVec2 p(origin.x + point.x * size.x, origin.y + point.y * size.y);
                    const ImVec2 q(origin.x + next.x * size.x, origin.y + next.y * size.y);
                    draw->AddLine(p, q, color, index == sprite_atlas_selected_region ? 2.5f : 1.0f);
                    if (index == sprite_atlas_selected_region)
                    {
                        const ImU32 point_color = static_cast<int>(point_index) ==
                            sprite_atlas_active_point
                            ? IM_COL32(255, 255, 255, 255)
                            : IM_COL32(255, 235, 90, 255);
                        draw->AddCircleFilled(p, 5.0f, point_color);
                    }
                }
            }
            else
            {
                draw->AddRect(a, b, color, 0.0f, 0, index == sprite_atlas_selected_region ? 2.5f : 1.0f);
                if (index == sprite_atlas_selected_region)
                {
                    const ImVec2 handles[8] = {
                        a, ImVec2((a.x+b.x)*0.5f,a.y), ImVec2(b.x,a.y),
                        ImVec2(b.x,(a.y+b.y)*0.5f), b, ImVec2((a.x+b.x)*0.5f,b.y),
                        ImVec2(a.x,b.y), ImVec2(a.x,(a.y+b.y)*0.5f) };
                    for (const ImVec2& h : handles)
                        draw->AddRectFilled(ImVec2(h.x-4.0f,h.y-4.0f), ImVec2(h.x+4.0f,h.y+4.0f),
                            IM_COL32(255,255,255,255));
                }
            }
            draw->AddText(ImVec2(a.x + 2.0f, a.y + 2.0f), color, region.name.c_str());
            const ImVec2 pivot(a.x + region.pivot.x * (b.x - a.x),
                a.y + region.pivot.y * (b.y - a.y));
            draw->AddLine(ImVec2(pivot.x - 5.0f, pivot.y), ImVec2(pivot.x + 5.0f, pivot.y), color);
            draw->AddLine(ImVec2(pivot.x, pivot.y - 5.0f), ImVec2(pivot.x, pivot.y + 5.0f), color);
        }

        const bool image_hovered = ImGui::IsItemHovered();
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const bool space_down = ImGui::GetIO().KeysDown[VK_SPACE];
        if (image_hovered && ImGui::GetIO().MouseWheel != 0.0f)
        {
            const float factor = ImGui::GetIO().MouseWheel > 0.0f ? 1.12f : (1.0f / 1.12f);
            sprite_atlas_zoom = (std::max)(0.1f, (std::min)(8.0f, sprite_atlas_zoom * factor));
        }
        if (image_hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
            (space_down && ImGui::IsMouseDragging(ImGuiMouseButton_Left))))
        {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            ImGui::SetScrollX(ImGui::GetScrollX() - delta.x);
            ImGui::SetScrollY(ImGui::GetScrollY() - delta.y);
        }

        const float u = (mouse.x - origin.x) / (std::max)(1.0f, size.x);
        const float v = (mouse.y - origin.y) / (std::max)(1.0f, size.y);
        if (image_hovered && ImGui::IsMouseClicked(0) && !space_down)
        {
            if (sprite_atlas_draw_region_mode)
            {
                begin_sprite_atlas_edit("Atlas Regionを作成");
                sprite_atlas_region_dragging = true;
                sprite_atlas_drag_start = mouse;
            }
            else
            {
                bool handled_freeform = false;
                if (sprite_atlas_selected_region >= 0 &&
                    sprite_atlas_selected_region < static_cast<int>(sprite_atlas_editor_asset.regions.size()) &&
                    AtlasRegionIsFreeform(sprite_atlas_editor_asset.regions[
                        sprite_atlas_selected_region]))
                {
                    auto& region = sprite_atlas_editor_asset.regions[sprite_atlas_selected_region];
                    int hit_point = -1;
                    for (int index = 0; index < static_cast<int>(region.path_points.size()); ++index)
                    {
                        const auto& point = region.path_points[static_cast<std::size_t>(index)];
                        const ImVec2 screen_point(origin.x + point.x * size.x,
                            origin.y + point.y * size.y);
                        if (std::fabs(mouse.x - screen_point.x) <= 8.0f &&
                            std::fabs(mouse.y - screen_point.y) <= 8.0f)
                        {
                            hit_point = index;
                            break;
                        }
                    }
                    if (hit_point >= 0)
                    {
                        begin_sprite_atlas_edit("Atlas 自由形状頂点を移動");
                        sprite_atlas_active_point = hit_point;
                        sprite_atlas_active_handle = -1;
                        sprite_atlas_region_transform_dragging = true;
                        sprite_atlas_transform_start_mouse = mouse;
                        handled_freeform = true;
                    }
                    else
                    {
                        int hit_segment = -1;
                        float hit_t = 0.0f;
                        float closest = 10.0f * 10.0f;
                        for (int index = 0; index < static_cast<int>(region.path_points.size()); ++index)
                        {
                            const int next_index = (index + 1) % static_cast<int>(region.path_points.size());
                            const auto& a = region.path_points[static_cast<std::size_t>(index)];
                            const auto& b = region.path_points[static_cast<std::size_t>(next_index)];
                            const ImVec2 screen_a(origin.x + a.x * size.x, origin.y + a.y * size.y);
                            const ImVec2 screen_b(origin.x + b.x * size.x, origin.y + b.y * size.y);
                            float segment_t = 0.0f;
                            const float distance = PointSegmentDistanceSquared(mouse,
                                screen_a, screen_b, segment_t);
                            if (distance < closest)
                            {
                                closest = distance;
                                hit_segment = index;
                                hit_t = segment_t;
                            }
                        }
                        if (hit_segment >= 0)
                        {
                            begin_sprite_atlas_edit("Atlas 自由形状頂点を追加");
                            const int next_index = (hit_segment + 1) %
                                static_cast<int>(region.path_points.size());
                            const auto& a = region.path_points[static_cast<std::size_t>(hit_segment)];
                            const auto& b = region.path_points[static_cast<std::size_t>(next_index)];
                            DirectX::XMFLOAT2 inserted{
                                SnapAtlasUV(a.x + (b.x - a.x) * hit_t, image_width, sprite_atlas_pixel_snap),
                                SnapAtlasUV(a.y + (b.y - a.y) * hit_t, image_height, sprite_atlas_pixel_snap) };
                            inserted.x = (std::max)(0.0f, (std::min)(1.0f, inserted.x));
                            inserted.y = (std::max)(0.0f, (std::min)(1.0f, inserted.y));
                            region.path_points.insert(region.path_points.begin() + next_index, inserted);
                            UpdateAtlasRegionBoundsFromPath(region);
                            sprite_atlas_active_point = next_index;
                            sprite_atlas_active_handle = -1;
                            sprite_atlas_region_transform_dragging = true;
                            sprite_atlas_transform_start_mouse = mouse;
                            handled_freeform = true;
                        }
                        else if (AtlasPointInPolygon(region, u, v))
                        {
                            begin_sprite_atlas_edit("Atlas 自由形状を移動");
                            sprite_atlas_active_point = -1;
                            sprite_atlas_active_handle = 8;
                            sprite_atlas_region_transform_dragging = true;
                            sprite_atlas_transform_start_uv = region.uv_rect;
                            sprite_atlas_transform_start_path_points = region.path_points;
                            sprite_atlas_transform_start_mouse = mouse;
                            handled_freeform = true;
                        }
                    }
                }
                if (!handled_freeform)
                {
                    int hit_handle = -1;
                    if (sprite_atlas_selected_region >= 0 &&
                        sprite_atlas_selected_region < static_cast<int>(sprite_atlas_editor_asset.regions.size()) &&
                        !AtlasRegionIsFreeform(sprite_atlas_editor_asset.regions[
                            sprite_atlas_selected_region]))
                    {
                        const auto& r = sprite_atlas_editor_asset.regions[sprite_atlas_selected_region].uv_rect;
                        const ImVec2 a(origin.x+r.x*size.x, origin.y+r.y*size.y);
                        const ImVec2 b(a.x+r.z*size.x, a.y+r.w*size.y);
                        const ImVec2 handles[8] = { a, ImVec2((a.x+b.x)*0.5f,a.y), ImVec2(b.x,a.y),
                            ImVec2(b.x,(a.y+b.y)*0.5f), b, ImVec2((a.x+b.x)*0.5f,b.y),
                            ImVec2(a.x,b.y), ImVec2(a.x,(a.y+b.y)*0.5f) };
                        for (int h=0; h<8; ++h)
                            if (std::fabs(mouse.x-handles[h].x)<=7.0f && std::fabs(mouse.y-handles[h].y)<=7.0f)
                            { hit_handle=h; break; }
                    }
                    if (hit_handle >= 0)
                    {
                        begin_sprite_atlas_edit("Atlas Regionの大きさを変更");
                        sprite_atlas_active_point = -1;
                        sprite_atlas_active_handle = hit_handle;
                        sprite_atlas_region_transform_dragging = true;
                        sprite_atlas_transform_start_uv =
                            sprite_atlas_editor_asset.regions[sprite_atlas_selected_region].uv_rect;
                        sprite_atlas_transform_start_mouse = mouse;
                    }
                    else
                    {
                        int hit = -1;
                        for (int index = static_cast<int>(sprite_atlas_editor_asset.regions.size()) - 1; index >= 0; --index)
                        {
                            const auto& r = sprite_atlas_editor_asset.regions[index].uv_rect;
                            const bool inside = AtlasRegionIsFreeform(sprite_atlas_editor_asset.regions[index])
                                ? AtlasPointInPolygon(sprite_atlas_editor_asset.regions[index], u, v)
                                : (u >= r.x && u <= r.x+r.z && v >= r.y && v <= r.y+r.w);
                            if (inside) { hit=index; break; }
                        }
                        if (sprite_atlas_selected_region != hit)
                            sprite_atlas_active_point = -1;
                        sprite_atlas_selected_region = hit;
                        if (ImGui::GetIO().KeyCtrl && hit >= 0)
                        {
                            begin_sprite_atlas_edit("Atlas Pivotを変更");
                            auto& region = sprite_atlas_editor_asset.regions[hit];
                            region.pivot.x = (std::max)(0.0f, (std::min)(1.0f,
                                (u - region.uv_rect.x) /
                                (std::max)(0.0001f, region.uv_rect.z)));
                            region.pivot.y = (std::max)(0.0f, (std::min)(1.0f,
                                (v - region.uv_rect.y) /
                                (std::max)(0.0001f, region.uv_rect.w)));
                            commit_sprite_atlas_edit();
                        }
                        else if (hit >= 0)
                        {
                            begin_sprite_atlas_edit("Atlas Regionを移動");
                            sprite_atlas_active_handle = 8;
                            sprite_atlas_active_point = -1;
                            sprite_atlas_region_transform_dragging = true;
                            sprite_atlas_transform_start_uv = sprite_atlas_editor_asset.regions[hit].uv_rect;
                            sprite_atlas_transform_start_path_points =
                                sprite_atlas_editor_asset.regions[hit].path_points;
                            sprite_atlas_transform_start_mouse = mouse;
                        }
                    }
                }
            }
        }
        if (sprite_atlas_region_transform_dragging && sprite_atlas_selected_region >= 0 &&
            ImGui::IsMouseDown(0))
        {
            auto& region = sprite_atlas_editor_asset.regions[sprite_atlas_selected_region];
            auto& r = region.uv_rect;
            const float du = (mouse.x-sprite_atlas_transform_start_mouse.x)/(std::max)(1.0f,size.x);
            const float dv = (mouse.y-sprite_atlas_transform_start_mouse.y)/(std::max)(1.0f,size.y);
            if (sprite_atlas_active_point >= 0 &&
                sprite_atlas_active_point < static_cast<int>(region.path_points.size()))
            {
                DirectX::XMFLOAT2& point = region.path_points[
                    static_cast<std::size_t>(sprite_atlas_active_point)];
                point.x = SnapAtlasUV((mouse.x - origin.x) /
                    (std::max)(1.0f, size.x), image_width, sprite_atlas_pixel_snap);
                point.y = SnapAtlasUV((mouse.y - origin.y) /
                    (std::max)(1.0f, size.y), image_height, sprite_atlas_pixel_snap);
                point.x = (std::max)(0.0f, (std::min)(1.0f, point.x));
                point.y = (std::max)(0.0f, (std::min)(1.0f, point.y));
                UpdateAtlasRegionBoundsFromPath(region);
                region.original_size = { r.z * image_width, r.w * image_height };
                sprite_atlas_editor_dirty = true;
            }
            else if (AtlasRegionIsFreeform(region) && sprite_atlas_active_handle == 8 &&
                sprite_atlas_transform_start_path_points.size() == region.path_points.size())
            {
                const float width = sprite_atlas_transform_start_uv.z;
                const float height = sprite_atlas_transform_start_uv.w;
                const float moved_left = (std::max)(0.0f,
                    (std::min)(1.0f - width, sprite_atlas_transform_start_uv.x + du));
                const float moved_top = (std::max)(0.0f,
                    (std::min)(1.0f - height, sprite_atlas_transform_start_uv.y + dv));
                const float applied_du = moved_left - sprite_atlas_transform_start_uv.x;
                const float applied_dv = moved_top - sprite_atlas_transform_start_uv.y;
                for (std::size_t index = 0; index < region.path_points.size(); ++index)
                {
                    region.path_points[index] = {
                        sprite_atlas_transform_start_path_points[index].x + applied_du,
                        sprite_atlas_transform_start_path_points[index].y + applied_dv };
                }
                UpdateAtlasRegionBoundsFromPath(region);
                region.original_size = { r.z * image_width, r.w * image_height };
                sprite_atlas_editor_dirty = true;
            }
            else
            {
                float l=sprite_atlas_transform_start_uv.x, t=sprite_atlas_transform_start_uv.y;
                float rr=l+sprite_atlas_transform_start_uv.z, bb=t+sprite_atlas_transform_start_uv.w;
                if (sprite_atlas_active_handle == 8) { l += du; rr += du; t += dv; bb += dv; }
                else
                {
                    if (sprite_atlas_active_handle==0 || sprite_atlas_active_handle==6 || sprite_atlas_active_handle==7) l += du;
                    if (sprite_atlas_active_handle==2 || sprite_atlas_active_handle==3 || sprite_atlas_active_handle==4) rr += du;
                    if (sprite_atlas_active_handle==0 || sprite_atlas_active_handle==1 || sprite_atlas_active_handle==2) t += dv;
                    if (sprite_atlas_active_handle==4 || sprite_atlas_active_handle==5 || sprite_atlas_active_handle==6) bb += dv;
                    if (ImGui::GetIO().KeyShift)
                    {
                        const float aspect = sprite_atlas_transform_start_uv.z /
                            (std::max)(0.0001f, sprite_atlas_transform_start_uv.w);
                        const float w = rr-l, h = bb-t;
                        if (std::fabs(w) > std::fabs(h*aspect)) bb = t + w/aspect;
                        else rr = l + h*aspect;
                    }
                }
                l=SnapAtlasUV(l,image_width,sprite_atlas_pixel_snap); rr=SnapAtlasUV(rr,image_width,sprite_atlas_pixel_snap);
                t=SnapAtlasUV(t,image_height,sprite_atlas_pixel_snap); bb=SnapAtlasUV(bb,image_height,sprite_atlas_pixel_snap);
                if (sprite_atlas_active_handle==8)
                {
                    const float w=sprite_atlas_transform_start_uv.z, h=sprite_atlas_transform_start_uv.w;
                    l=(std::max)(0.0f,(std::min)(1.0f-w,l)); t=(std::max)(0.0f,(std::min)(1.0f-h,t));
                    rr=l+w; bb=t+h;
                }
                l=(std::max)(0.0f,(std::min)(1.0f,l)); rr=(std::max)(0.0f,(std::min)(1.0f,rr));
                t=(std::max)(0.0f,(std::min)(1.0f,t)); bb=(std::max)(0.0f,(std::min)(1.0f,bb));
                if (rr<l) std::swap(rr,l); if (bb<t) std::swap(bb,t);
                r={l,t,(std::max)(1.0f/image_width,rr-l),(std::max)(1.0f/image_height,bb-t)};
                region.original_size = {r.z*image_width,r.w*image_height};
                sprite_atlas_editor_dirty=true;
            }
        }
        if (sprite_atlas_region_transform_dragging && ImGui::IsMouseReleased(0))
        {
            sprite_atlas_region_transform_dragging=false;
            sprite_atlas_active_handle=-1;
            sprite_atlas_transform_start_path_points.clear();
            commit_sprite_atlas_edit();
        }
        if (sprite_atlas_region_dragging && ImGui::IsMouseDown(0))
            draw->AddRect(sprite_atlas_drag_start, mouse, IM_COL32(255,210,70,255),0.0f,0,2.0f);
        if (sprite_atlas_region_dragging && ImGui::IsMouseReleased(0))
        {
            sprite_atlas_region_dragging=false;
            const float x0=(std::min)(sprite_atlas_drag_start.x,mouse.x), y0=(std::min)(sprite_atlas_drag_start.y,mouse.y);
            const float x1=(std::max)(sprite_atlas_drag_start.x,mouse.x), y1=(std::max)(sprite_atlas_drag_start.y,mouse.y);
            if (x1-x0>=2.0f && y1-y0>=2.0f)
            {
                ReplayEngine::Assets::SpriteAtlasRegion region;
                region.name="Region"+std::to_string(sprite_atlas_editor_asset.regions.size());
                float l=SnapAtlasUV((x0-origin.x)/size.x,image_width,sprite_atlas_pixel_snap);
                float t=SnapAtlasUV((y0-origin.y)/size.y,image_height,sprite_atlas_pixel_snap);
                float rr=SnapAtlasUV((x1-origin.x)/size.x,image_width,sprite_atlas_pixel_snap);
                float bb=SnapAtlasUV((y1-origin.y)/size.y,image_height,sprite_atlas_pixel_snap);
                l=(std::max)(0.0f,(std::min)(1.0f,l)); t=(std::max)(0.0f,(std::min)(1.0f,t));
                rr=(std::max)(l,(std::min)(1.0f,rr)); bb=(std::max)(t,(std::min)(1.0f,bb));
                region.uv_rect={l,t,rr-l,bb-t};
                region.original_size={region.uv_rect.z*image_width,region.uv_rect.w*image_height};
                sprite_atlas_editor_asset.regions.push_back(std::move(region));
                sprite_atlas_selected_region=static_cast<int>(sprite_atlas_editor_asset.regions.size())-1;
                sprite_atlas_active_point = -1;
                commit_sprite_atlas_edit();
            }
            else cancel_sprite_atlas_edit();
        }
        if (image_hovered)
        {
            const int px = static_cast<int>(
                (std::max)(0.0f, (std::min)(1.0f, u)) * image_width);
            const int py = static_cast<int>(
                (std::max)(0.0f, (std::min)(1.0f, v)) * image_height);
            ImGui::SetTooltip("Pixel: %d, %d / UV: %.4f, %.4f\nWheel: Zoom / MMB or Space+Drag: Pan / Ctrl+Click: Pivot", px,py,u,v);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##AtlasInspector", ImVec2(inspector_width, 0.0f), true);
    ImGui::Text("Regions: %d", static_cast<int>(sprite_atlas_editor_asset.regions.size()));
    if (ImGui::Button("+ Region"))
    {
        begin_sprite_atlas_edit("Atlas Regionを追加");
        ReplayEngine::Assets::SpriteAtlasRegion region;
        region.name = "Region" + std::to_string(sprite_atlas_editor_asset.regions.size());
        region.uv_rect = { 0.1f, 0.1f, 0.25f, 0.25f };
        region.original_size = { image_width * 0.25f, image_height * 0.25f };
        sprite_atlas_editor_asset.regions.push_back(std::move(region));
        sprite_atlas_selected_region = static_cast<int>(sprite_atlas_editor_asset.regions.size()) - 1;
        sprite_atlas_active_point = -1;
        commit_sprite_atlas_edit();
    }
    ImGui::SameLine();
    if (ImGui::Button("削除") && sprite_atlas_selected_region >= 0 &&
        sprite_atlas_selected_region < static_cast<int>(sprite_atlas_editor_asset.regions.size()))
    {
        begin_sprite_atlas_edit("Atlas Regionを削除");
        sprite_atlas_editor_asset.regions.erase(sprite_atlas_editor_asset.regions.begin() +
            sprite_atlas_selected_region);
        sprite_atlas_selected_region = (std::min)(sprite_atlas_selected_region,
            static_cast<int>(sprite_atlas_editor_asset.regions.size()) - 1);
        sprite_atlas_active_point = -1;
        commit_sprite_atlas_edit();
    }

    for (int index = 0; index < static_cast<int>(sprite_atlas_editor_asset.regions.size()); ++index)
    {
        if (ImGui::Selectable(sprite_atlas_editor_asset.regions[index].name.c_str(),
            index == sprite_atlas_selected_region))
        {
            if (sprite_atlas_selected_region != index) sprite_atlas_active_point = -1;
            sprite_atlas_selected_region = index;
        }
    }

    if (sprite_atlas_editor_keyboard_focus && !ImGui::GetIO().WantTextInput &&
        sprite_atlas_selected_region >= 0 &&
        (ImGui::IsKeyPressed(VK_BACK) || ImGui::IsKeyPressed(VK_DELETE)))
    {
        auto& region = sprite_atlas_editor_asset.regions[
            static_cast<std::size_t>(sprite_atlas_selected_region)];
        if (AtlasRegionIsFreeform(region) && sprite_atlas_active_point >= 0 &&
            sprite_atlas_active_point < static_cast<int>(region.path_points.size()))
        {
            if (region.path_points.size() > 3)
            {
                begin_sprite_atlas_edit("Atlas 自由形状頂点を削除");
                region.path_points.erase(region.path_points.begin() +
                    sprite_atlas_active_point);
                sprite_atlas_active_point = (std::min)(sprite_atlas_active_point,
                    static_cast<int>(region.path_points.size()) - 1);
                UpdateAtlasRegionBoundsFromPath(region);
                region.original_size = { region.uv_rect.z * image_width,
                    region.uv_rect.w * image_height };
                commit_sprite_atlas_edit();
            }
            else
            {
                sprite_atlas_editor_status = "自由形状は3頂点未満にできません";
            }
        }
        else
        {
            begin_sprite_atlas_edit("Atlas Regionを削除");
            sprite_atlas_editor_asset.regions.erase(sprite_atlas_editor_asset.regions.begin() +
                sprite_atlas_selected_region);
            sprite_atlas_selected_region = (std::min)(sprite_atlas_selected_region,
                static_cast<int>(sprite_atlas_editor_asset.regions.size()) - 1);
            sprite_atlas_active_point = -1;
            commit_sprite_atlas_edit();
        }
    }

    if (sprite_atlas_selected_region >= 0 &&
        sprite_atlas_selected_region < static_cast<int>(sprite_atlas_editor_asset.regions.size()))
    {
        auto& region = sprite_atlas_editor_asset.regions[sprite_atlas_selected_region];
        ImGui::Separator();
        char name[128]{};
        strncpy_s(name, region.name.c_str(), _TRUNCATE);
        const auto name_before = sprite_atlas_editor_asset;
        if (ImGui::InputText("Name", name, IM_ARRAYSIZE(name)))
        {
            region.name = name;
            if (!sprite_atlas_history_transaction)
            {
                sprite_atlas_history_before = name_before; sprite_atlas_history_label = "Atlas Region名を変更";
                sprite_atlas_history_transaction = true; commit_sprite_atlas_edit();
            }
        }
        bool freeform = AtlasRegionIsFreeform(region);
        const auto shape_before = sprite_atlas_editor_asset;
        if (ImGui::Checkbox("自由形状", &freeform))
        {
            begin_sprite_atlas_edit(freeform
                ? "Atlas Regionを自由形状化" : "Atlas Regionを矩形へ戻す");
            if (freeform)
                SetAtlasRegionPathToRect(region);
            else
                region.path_points.clear();
            UpdateAtlasRegionBoundsFromPath(region);
            region.original_size = { region.uv_rect.z * image_width,
                region.uv_rect.w * image_height };
            if (!sprite_atlas_history_transaction)
            {
                sprite_atlas_history_before = shape_before;
                sprite_atlas_history_label = freeform
                    ? "Atlas Regionを自由形状化" : "Atlas Regionを矩形へ戻す";
                sprite_atlas_history_transaction = true;
            }
            commit_sprite_atlas_edit();
        }
        float uv[4]{ region.uv_rect.x, region.uv_rect.y, region.uv_rect.z, region.uv_rect.w };
        const auto uv_before = sprite_atlas_editor_asset;
        if (!freeform && ImGui::DragFloat4("UV Rect", uv, 0.001f, 0.0f, 1.0f))
        {
            region.uv_rect = {uv[0],uv[1],uv[2],uv[3]};
            if (!sprite_atlas_history_transaction)
            {
                sprite_atlas_history_before=uv_before; sprite_atlas_history_label="Atlas UV Rectを変更";
                sprite_atlas_history_transaction=true; commit_sprite_atlas_edit();
            }
        }
        if (freeform)
        {
            ImGui::TextDisabled("UV Rect は自由形状の外接矩形");
            ImGui::Text("頂点: %d", static_cast<int>(region.path_points.size()));
        }
        float pivot[2]{ region.pivot.x, region.pivot.y };
        const auto pivot_before=sprite_atlas_editor_asset;
        if (ImGui::DragFloat2("Pivot", pivot, 0.001f, 0.0f, 1.0f))
        {
            region.pivot={pivot[0],pivot[1]};
            if (!sprite_atlas_history_transaction)
            {
                sprite_atlas_history_before=pivot_before; sprite_atlas_history_label="Atlas Pivotを変更";
                sprite_atlas_history_transaction=true; commit_sprite_atlas_edit();
            }
        }
        float original[2]{ region.original_size.x, region.original_size.y };
        const auto original_before=sprite_atlas_editor_asset;
        if (ImGui::DragFloat2("Original Size",original,1.0f,0.0f,16384.0f))
        {
            region.original_size={original[0],original[1]};
            if (!sprite_atlas_history_transaction)
            {
                sprite_atlas_history_before=original_before; sprite_atlas_history_label="Atlas Original Sizeを変更";
                sprite_atlas_history_transaction=true; commit_sprite_atlas_edit();
            }
        }
        float trim[2]{ region.trim_offset.x, region.trim_offset.y };
        const auto trim_before=sprite_atlas_editor_asset;
        if (ImGui::DragFloat2("Trim Offset",trim,1.0f,-16384.0f,16384.0f))
        {
            region.trim_offset={trim[0],trim[1]};
            if (!sprite_atlas_history_transaction)
            {
                sprite_atlas_history_before=trim_before; sprite_atlas_history_label="Atlas Trim Offsetを変更";
                sprite_atlas_history_transaction=true; commit_sprite_atlas_edit();
            }
        }
        const auto rotated_before=sprite_atlas_editor_asset;
        if (ImGui::Checkbox("Rotated", &region.rotated) && !sprite_atlas_history_transaction)
        {
            sprite_atlas_history_before=rotated_before; sprite_atlas_history_label="Atlas Rotatedを変更";
            sprite_atlas_history_transaction=true; commit_sprite_atlas_edit();
        }
        if (freeform)
            ImGui::TextDisabled("頂点ドラッグ / 線上クリック: 頂点追加 / 内側ドラッグ: 移動");
        else
            ImGui::TextDisabled("Drag: 移動 / 8ハンドル: Resize / Shift: 縦横比固定");
        ImGui::TextDisabled("Ctrl+Click: Pivot / Wheel: Zoom / MMB or Space+Drag: Pan");
    }
    ImGui::Separator();
    ImGui::TextWrapped("%s", sprite_atlas_editor_status.c_str());
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::End();
}
