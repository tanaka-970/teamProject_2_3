#include "../framework_class.h"

#include "../../../RePlayEngine/Components/UI/CanvasComponent.h"
#include "../../../RePlayEngine/Components/UI/RectTransformComponent.h"
#include "../../../RePlayEngine/Components/UI/UIImageComponent.h"
#include "../../../RePlayEngine/Components/UI/UIEffectStackComponent.h"
#include "../../../RePlayEngine/Components/UI/UIInputFieldComponent.h"
#include "../../../RePlayEngine/Components/UI/UIMaskComponent.h"
#include "../../../RePlayEngine/Components/UI/UIScrollViewComponent.h"
#include "../../../RePlayEngine/Components/UI/UISelectableComponent.h"
#include "../../../RePlayEngine/Components/UI/UIShapeComponent.h"
#include "../../../RePlayEngine/Components/UI/UIShapeImageComponent.h"
#include "../../../RePlayEngine/Components/UI/UITextComponent.h"
#include "../../../RePlayEngine/Components/UI/UIPuppetDeformComponent.h"
#include "../../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../../RePlayEngine/Scene/Runtime/Scene.h"
#include "../../../RePlayEngine/UI/FontAtlas.h"
#include "../../../RePlayEngine/UI/Effects/UIEffect.h"
#include "../../../RePlayEngine/UI/UILayout.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace
{
    using ReplayEngine::Assets::AssetDatabase;
    using ReplayEngine::Assets::AssetKind;
    using ReplayEngine::Components::CanvasComponent;
    using ReplayEngine::Components::RectTransformComponent;
    using ReplayEngine::Components::UIImageComponent;
    using ReplayEngine::Components::UIInputFieldComponent;
    using ReplayEngine::Components::UIMaskComponent;
    using ReplayEngine::Components::UIScrollViewComponent;
    using ReplayEngine::Components::UISelectableComponent;
    using ReplayEngine::Components::UIShapeComponent;
    using ReplayEngine::Components::UIShapeImageComponent;
    using ReplayEngine::Components::UITextComponent;
    using ReplayEngine::Components::UIPuppetDeformComponent;
    using ReplayEngine::Core::GameObject;
    using ReplayEngine::Rendering::DX12::D3D12UIBatch;
    using ReplayEngine::Rendering::DX12::D3D12UIBlendMode;
    using ReplayEngine::Rendering::DX12::D3D12UIFrame;
    using ReplayEngine::Rendering::DX12::D3D12UIVertex;

    constexpr int kMaxUiDepth = 64;

    float Clamp01(float value) noexcept
    {
        return (std::max)(0.0f, (std::min)(1.0f, value));
    }

    DirectX::XMFLOAT2 TransformPoint(const DirectX::XMFLOAT4X4& matrix,
        float x, float y) noexcept
    {
        const DirectX::XMVECTOR point = DirectX::XMVector3TransformCoord(
            DirectX::XMVectorSet(x, y, 0.0f, 1.0f),
            DirectX::XMLoadFloat4x4(&matrix));
        DirectX::XMFLOAT2 result{};
        DirectX::XMStoreFloat2(&result, point);
        return result;
    }

    template <typename Viewport>
    DirectX::XMFLOAT2 ToUiPixel(const DirectX::XMFLOAT2& logical,
        float canvas_scale, float logical_height,
        const Viewport& viewport) noexcept
    {
        const float x = logical.x * canvas_scale;
        const float y = logical_height - logical.y * canvas_scale;
        const float scale_x = viewport.logical_width > 0.0001f
            ? viewport.width / viewport.logical_width : 1.0f;
        const float scale_y = viewport.logical_height > 0.0001f
            ? viewport.height / viewport.logical_height : 1.0f;
        return { viewport.left + x * scale_x, viewport.top + y * scale_y };
    }

    DirectX::XMFLOAT4 MultiplyColor(const DirectX::XMFLOAT4& color,
        float opacity) noexcept
    {
        return { color.x, color.y, color.z, color.w * Clamp01(opacity) };
    }

    D3D12_RECT ClampRect(
        const D3D12_RECT& input,
        std::uint32_t width, std::uint32_t height) noexcept
    {
        D3D12_RECT result = input;
        result.left = (std::max)(0L, (std::min)(result.left, static_cast<LONG>(width)));
        result.top = (std::max)(0L, (std::min)(result.top, static_cast<LONG>(height)));
        result.right = (std::max)(result.left,
            (std::min)(result.right, static_cast<LONG>(width)));
        result.bottom = (std::max)(result.top,
            (std::min)(result.bottom, static_cast<LONG>(height)));
        return result;
    }

    D3D12_RECT IntersectRect(
        const D3D12_RECT& lhs,
        const D3D12_RECT& rhs) noexcept
    {
        D3D12_RECT result{};
        result.left = (std::max)(lhs.left, rhs.left);
        result.top = (std::max)(lhs.top, rhs.top);
        result.right = (std::min)(lhs.right, rhs.right);
        result.bottom = (std::min)(lhs.bottom, rhs.bottom);
        if (result.right < result.left) result.right = result.left;
        if (result.bottom < result.top) result.bottom = result.top;
        return result;
    }

    bool RectIsEmpty(const D3D12_RECT& rect) noexcept
    {
        return rect.right <= rect.left || rect.bottom <= rect.top;
    }
}

// DX12 UI のCPU提出層。ここでは既存の Canvas/RectTransform/FontAtlas の正本だけを読み、
// GPU APIやD3D11 Viewを触らない。GPU所有権は D3D12DeviceContext::DrawRuntimeUI に限定する。
bool framework::build_dx12_ui(
    ReplayEngine::Rendering::DX12::D3D12UIFrame& frame)
{
    const object_ui_viewport viewport = object_ui_viewport_target();
    return build_dx12_ui_for_scene(frame, active_object_scene(),
        dx12_device_context.Width(), dx12_device_context.Height(),
        viewport.logical_width, viewport.logical_height);
}

bool framework::build_dx12_ui_for_scene(
    ReplayEngine::Rendering::DX12::D3D12UIFrame& frame,
    ReplayEngine::Scene::Scene& scene,
    std::uint32_t target_width, std::uint32_t target_height,
    float logical_width, float logical_height)
{
    using namespace ReplayEngine;
    using namespace ReplayEngine::Rendering::DX12;

    frame = {};
    frame.target_width = target_width;
    frame.target_height = target_height;
    if (frame.target_width == 0 || frame.target_height == 0) return true;

    object_ui_viewport viewport{};
    viewport.width = static_cast<float>(frame.target_width);
    viewport.height = static_cast<float>(frame.target_height);
    viewport.logical_width = (std::max)(1.0f, logical_width);
    viewport.logical_height = (std::max)(1.0f, logical_height);
    ReplayEngine::UI::UILayout::Resolve(scene,
        viewport.logical_width, viewport.logical_height);

    std::unordered_set<std::string> texture_keys;
    std::unordered_map<std::string, std::size_t> font_indices;
    std::unordered_map<std::string, std::uint64_t> font_revisions;

    const auto add_image_texture = [&](const UIImageComponent& image,
        DirectX::XMFLOAT4& uv, bool& rotated) -> std::string
    {
        uv = { image.uv_offset.x, image.uv_offset.y,
            image.uv_scale.x, image.uv_scale.y };
        rotated = false;
        std::string image_guid = image.sprite.guid;
        std::filesystem::path embedded_path;
        if (!image.atlas.guid.empty() && !image.atlas_region.empty())
        {
            const Assets::AssetRecord* atlas_record =
                asset_database.FindByGuid(image.atlas.guid);
            if (atlas_record != nullptr && atlas_record->kind == AssetKind::SpriteAtlas)
            {
                const std::filesystem::path atlas_path = content_path(
                    atlas_record->cache_path.empty()
                        ? atlas_record->source_path : atlas_record->cache_path);
                Assets::SpriteAtlasAsset atlas;
                std::string error;
                if (Assets::SpriteAtlasAsset::LoadFromFile(atlas_path, atlas, error))
                {
                    const Assets::SpriteAtlasRegion* region =
                        atlas.FindRegion(image.atlas_region);
                    if (region != nullptr)
                    {
                        image_guid = atlas.image_guid;
                        uv = {
                            region->uv_rect.x + region->uv_rect.z * image.uv_offset.x,
                            region->uv_rect.y + region->uv_rect.w * image.uv_offset.y,
                            region->uv_rect.z * image.uv_scale.x,
                            region->uv_rect.w * image.uv_scale.y };
                        rotated = region->rotated;
                        if (!atlas.embedded_texture_path.empty())
                            embedded_path = atlas_path.parent_path() /
                                std::filesystem::u8path(atlas.embedded_texture_path);
                    }
                }
            }
        }
        std::filesystem::path source_path;
        if (!embedded_path.empty()) source_path = embedded_path;
        else if (!image_guid.empty())
        {
            const Assets::AssetRecord* record = asset_database.FindByGuid(image_guid);
            if (record != nullptr && record->kind == AssetKind::Image)
                source_path = content_path(record->cache_path.empty()
                    ? record->source_path : record->cache_path);
        }
        if (source_path.empty()) return "__dx12_white";
        source_path = source_path.lexically_normal();
        const std::string key = source_path.generic_string();
        if (texture_keys.insert(key).second)
        {
            D3D12StaticTextureSource source;
            source.key = key;
            source.source_path = source_path;
            frame.texture_sources.push_back(std::move(source));
        }
        return key;
    };

    const auto add_font_source = [&](std::string& key) -> bool
    {
        std::vector<std::uint8_t> rgba;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint64_t revision = 0;
        if (!ui_font_atlas.CopyActiveAtlas(key, rgba, width, height, revision)) return false;
        const auto old = font_revisions.find(key);
        if (old != font_revisions.end() && old->second == revision) return true;
        D3D12UIFontAtlasSource source;
        source.key = key;
        source.rgba = std::move(rgba);
        source.width = width;
        source.height = height;
        source.revision = revision;
        const auto existing = font_indices.find(key);
        if (existing == font_indices.end())
        {
            font_indices.emplace(key, frame.font_atlases.size());
            frame.font_atlases.push_back(std::move(source));
        }
        else
        {
            frame.font_atlases[existing->second] = std::move(source);
        }
        font_revisions[key] = revision;
        return true;
    };

    const auto add_mask_texture = [&](const UIMaskComponent& mask) -> D3D12UIMask
    {
        D3D12UIMask result{};
        if (!mask.mask_image.IsAssigned()) return result;
        const Assets::AssetRecord* record =
            asset_database.FindByGuid(mask.mask_image.guid);
        if (record == nullptr || record->kind != AssetKind::Image) return result;
        const std::filesystem::path path = content_path(record->cache_path.empty()
            ? record->source_path : record->cache_path).lexically_normal();
        if (path.empty()) return result;
        result.texture_key = path.generic_string();
        if (texture_keys.insert(result.texture_key).second)
        {
            D3D12StaticTextureSource source;
            source.key = result.texture_key;
            source.source_path = path;
            frame.texture_sources.push_back(std::move(source));
        }
        return result;
    };

    const auto make_effect_commands = [&](const ReplayEngine::Components::UIEffectStackComponent& stack,
        std::vector<D3D12UIEffectCommand>& output) -> bool
    {
        output.clear();
        using ReplayEngine::UI::UIEffectKind;
        for (const ReplayEngine::UI::UIEffect& effect : stack.EffectiveEffects(&asset_database))
        {
            if (!effect.enabled || output.size() >= 32) continue;
            D3D12UIEffectCommand command{};
            const UIEffectKind kind = static_cast<UIEffectKind>(effect.kind);
            switch (kind)
            {
            case UIEffectKind::Blur:
            case UIEffectKind::DirectionalBlur:
            case UIEffectKind::RadialBlur:
                command.kind = 1; break;
            case UIEffectKind::Glow:
                command.kind = 2; break;
            case UIEffectKind::Outline:
                command.kind = 3; break;
            case UIEffectKind::DropShadow:
            case UIEffectKind::InnerShadow:
            case UIEffectKind::LongShadow:
                command.kind = 4; break;
            case UIEffectKind::ColorAdjust:
            case UIEffectKind::Temperature:
            case UIEffectKind::Levels:
                command.kind = 5; break;
            default:
                continue;
            }
            command.radius = (std::max)(0.0f, effect.radius);
            command.intensity = (std::max)(0.0f, effect.intensity);
            command.amount = effect.amount;
            command.direction = effect.direction;
            command.color = effect.color;
            command.color_2 = effect.color_2;
            output.push_back(command);
        }
        return !output.empty();
    };

    int active_effect_group = -1;
    const auto begin_effect_group = [&](const ReplayEngine::Components::UIEffectStackComponent& stack)
        -> int
    {
        std::vector<D3D12UIEffectCommand> effects;
        if (!make_effect_commands(stack, effects)) return -1;
        D3D12UIEffectGroup group{};
        group.first_batch = static_cast<std::uint32_t>(frame.batches.size());
        group.effects = std::move(effects);
        group.capture_backdrop = stack.capture_backdrop;
        group.target_scope = stack.target_scope;
        frame.effect_groups.push_back(std::move(group));
        frame.requires_offscreen = true;
        frame.capture_backdrop = frame.capture_backdrop || stack.capture_backdrop;
        return static_cast<int>(frame.effect_groups.size() - 1);
    };

    const auto make_scissor = [&](const RectTransformComponent& rect,
        float canvas_scale) noexcept
    {
        const DirectX::XMFLOAT4 r = rect.ResolvedRect();
        const DirectX::XMFLOAT4X4& matrix = rect.ResolvedMatrix();
        const DirectX::XMFLOAT2 points[] =
        {
            ToUiPixel(TransformPoint(matrix, r.x, r.y), canvas_scale,
                logical_height, viewport),
            ToUiPixel(TransformPoint(matrix, r.x + r.z, r.y), canvas_scale,
                logical_height, viewport),
            ToUiPixel(TransformPoint(matrix, r.x + r.z, r.y + r.w), canvas_scale,
                logical_height, viewport),
            ToUiPixel(TransformPoint(matrix, r.x, r.y + r.w), canvas_scale,
                logical_height, viewport),
        };
        float min_x = points[0].x;
        float max_x = points[0].x;
        float min_y = points[0].y;
        float max_y = points[0].y;
        for (const auto& point : points)
        {
            min_x = (std::min)(min_x, point.x);
            max_x = (std::max)(max_x, point.x);
            min_y = (std::min)(min_y, point.y);
            max_y = (std::max)(max_y, point.y);
        }
        return ClampRect({ static_cast<LONG>(std::floor(min_x)),
            static_cast<LONG>(std::floor(min_y)), static_cast<LONG>(std::ceil(max_x)),
            static_cast<LONG>(std::ceil(max_y)) }, frame.target_width, frame.target_height);
    };

    const auto append_quad = [&](D3D12UIBatch& batch,
        const DirectX::XMFLOAT4& rect, const DirectX::XMFLOAT4X4& matrix,
        const DirectX::XMFLOAT4& uv, const DirectX::XMFLOAT4& color,
        float canvas_scale, bool rotated)
    {
        const float nx[] = { 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };
        const float ny[] = { 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f };
        for (int index = 0; index < 6; ++index)
        {
            const DirectX::XMFLOAT2 logical = TransformPoint(matrix,
                rect.x + rect.z * nx[index], rect.y + rect.w * ny[index]);
            const DirectX::XMFLOAT2 position = ToUiPixel(logical, canvas_scale,
                logical_height, viewport);
            const float u = rotated ? ny[index] : nx[index];
            const float v = rotated ? nx[index] : (1.0f - ny[index]);
            D3D12UIVertex vertex;
            vertex.position = position;
            vertex.uv = { uv.x + u * uv.z, uv.y + v * uv.w };
            vertex.color = color;
            vertex.uv_bounds = { uv.x, uv.y, uv.x + uv.z, uv.y + uv.w };
            batch.vertices.push_back(vertex);
        }
    };

    const auto append_puppet_quad = [&](D3D12UIBatch& batch,
        const DirectX::XMFLOAT4& rect, const DirectX::XMFLOAT4X4& matrix,
        const DirectX::XMFLOAT4& uv, const DirectX::XMFLOAT4& color,
        float canvas_scale, bool rotated, const UIPuppetDeformComponent& puppet)
    {
        const int columns = (std::max)(1, (std::min)(32, puppet.grid_columns));
        const int rows = (std::max)(1, (std::min)(32, puppet.grid_rows));
        const auto make_vertex = [&](float nx, float ny)
        {
            DirectX::XMFLOAT2 deformed{ nx, ny };
            if (puppet.enabled_deform)
                deformed = puppet.DeformNormalizedPoint(deformed);
            const DirectX::XMFLOAT2 logical = TransformPoint(matrix,
                rect.x + rect.z * deformed.x, rect.y + rect.w * deformed.y);
            D3D12UIVertex vertex;
            vertex.position = ToUiPixel(logical, canvas_scale,
                logical_height, viewport);
            const float u = rotated ? ny : nx;
            const float v = rotated ? nx : (1.0f - ny);
            vertex.uv = { uv.x + u * uv.z, uv.y + v * uv.w };
            vertex.color = color;
            vertex.uv_bounds = { uv.x, uv.y, uv.x + uv.z, uv.y + uv.w };
            return vertex;
        };
        for (int row = 0; row < rows; ++row)
        {
            const float y0 = static_cast<float>(row) / static_cast<float>(rows);
            const float y1 = static_cast<float>(row + 1) / static_cast<float>(rows);
            for (int column = 0; column < columns; ++column)
            {
                const float x0 = static_cast<float>(column) / static_cast<float>(columns);
                const float x1 = static_cast<float>(column + 1) / static_cast<float>(columns);
                const D3D12UIVertex p0 = make_vertex(x0, y0);
                const D3D12UIVertex p1 = make_vertex(x1, y0);
                const D3D12UIVertex p2 = make_vertex(x1, y1);
                const D3D12UIVertex p3 = make_vertex(x0, y1);
                batch.vertices.push_back(p0);
                batch.vertices.push_back(p3);
                batch.vertices.push_back(p2);
                batch.vertices.push_back(p0);
                batch.vertices.push_back(p2);
                batch.vertices.push_back(p1);
            }
        }
    };

    const auto make_batch = [&](const std::string& texture_key,
        const DirectX::XMFLOAT4& color, const D3D12UIBlendMode blend,
        const D3D12_RECT* scissor, const D3D12UIClip* clip = nullptr,
        const std::vector<D3D12UIMask>* masks = nullptr)
        -> D3D12UIBatch&
    {
        frame.batches.emplace_back();
        D3D12UIBatch& batch = frame.batches.back();
        batch.texture_key = texture_key;
        batch.blend = blend;
        batch.effect_group = active_effect_group;
        batch.constants.screen_size = {
            static_cast<float>(frame.target_width),
            static_cast<float>(frame.target_height), 0, 0 };
        batch.constants.fill_color_2 = color;
        if (scissor != nullptr)
        {
            batch.scissor = *scissor;
            batch.scissor_enabled = true;
        }
        if (clip != nullptr)
        {
            batch.clip = *clip;
            batch.clip_enabled = true;
            batch.constants.clip_parameters = clip->parameters;
            batch.constants.clip_bounds = clip->bounds;
        }
        if (masks != nullptr && !masks->empty())
        {
            const std::size_t count = (std::min)(masks->size(), batch.masks.size());
            for (std::size_t index = 0; index < count; ++index)
            {
                if ((*masks)[index].texture_key.empty()) continue;
                batch.masks[index] = (*masks)[index];
                batch.constants.mask_uvs[index] = (*masks)[index].uv;
                const float operation = static_cast<float>((*masks)[index].operation);
                const float luma = (*masks)[index].luma ? 1.0f : 0.0f;
                auto set_component = [](DirectX::XMFLOAT4& value,
                    std::size_t component, float component_value)
                {
                    switch (component)
                    {
                    case 0: value.x = component_value; break;
                    case 1: value.y = component_value; break;
                    case 2: value.z = component_value; break;
                    default: value.w = component_value; break;
                    }
                };
                set_component(batch.constants.mask_operations, index, operation);
                set_component(batch.constants.mask_luma, index, luma);
                if (index == 0) batch.constants.mask_uv = (*masks)[index].uv;
                batch.mask_count = static_cast<std::uint32_t>(index + 1);
            }
            if (batch.mask_count > 0)
            {
                batch.mask_enabled = true;
                batch.constants.mask_parameters = {
                    static_cast<float>(batch.mask_count),
                    batch.masks[0].invert ? 1.0f : 0.0f, 0.0f, 0.0f };
            }
        }
        return batch;
    };

    const auto append_border = [&](D3D12UIBatch& batch,
        const DirectX::XMFLOAT4& rect, const DirectX::XMFLOAT4X4& matrix,
        float width, const DirectX::XMFLOAT4& color, float canvas_scale)
    {
        const float safe_width = (std::min)(
            (std::max)(0.5f, width), (std::min)(std::fabs(rect.z), std::fabs(rect.w)) * 0.5f);
        if (safe_width <= 0.0f) return;
        append_quad(batch, { rect.x, rect.y, rect.z, safe_width }, matrix,
            { 0, 0, 1, 1 }, color, canvas_scale, false);
        append_quad(batch, { rect.x, rect.y + rect.w - safe_width,
            rect.z, safe_width }, matrix, { 0, 0, 1, 1 }, color, canvas_scale, false);
        append_quad(batch, { rect.x, rect.y + safe_width,
            safe_width, rect.w - safe_width * 2.0f }, matrix,
            { 0, 0, 1, 1 }, color, canvas_scale, false);
        append_quad(batch, { rect.x + rect.z - safe_width, rect.y + safe_width,
            safe_width, rect.w - safe_width * 2.0f }, matrix,
            { 0, 0, 1, 1 }, color, canvas_scale, false);
    };

    const auto append_nine_slice = [&](D3D12UIBatch& batch,
        const DirectX::XMFLOAT4& rect, const DirectX::XMFLOAT4X4& matrix,
        const DirectX::XMFLOAT4& uv, const DirectX::XMFLOAT4& slice,
        const DirectX::XMFLOAT4& color, float canvas_scale, bool rotated)
    {
        const float width = (std::max)(0.0f, rect.z);
        const float height = (std::max)(0.0f, rect.w);
        const float left = (std::min)((std::max)(0.0f, slice.x), width * 0.5f);
        const float top = (std::min)((std::max)(0.0f, slice.y), height * 0.5f);
        const float right = (std::min)((std::max)(0.0f, slice.z), width * 0.5f);
        const float bottom = (std::min)((std::max)(0.0f, slice.w), height * 0.5f);
        const float xs[4] = { rect.x, rect.x + left, rect.x + width - right,
            rect.x + width };
        const float ys[4] = { rect.y, rect.y + top, rect.y + height - bottom,
            rect.y + height };
        const float us[4] = { uv.x, uv.x + uv.z * (width > 0.0f ? left / width : 0.0f),
            uv.x + uv.z * (width > 0.0f ? 1.0f - right / width : 1.0f),
            uv.x + uv.z };
        const float vs[4] = { uv.y, uv.y + uv.w * (height > 0.0f ? top / height : 0.0f),
            uv.y + uv.w * (height > 0.0f ? 1.0f - bottom / height : 1.0f),
            uv.y + uv.w };
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 3; ++column)
            {
                const DirectX::XMFLOAT4 cell{
                    xs[column], ys[row], xs[column + 1] - xs[column],
                    ys[row + 1] - ys[row] };
                if (cell.z <= 0.0f || cell.w <= 0.0f) continue;
                append_quad(batch, cell, matrix,
                    { us[column], vs[row], us[column + 1] - us[column],
                        vs[row + 1] - vs[row] }, color, canvas_scale, rotated);
            }
        }
    };

    const auto append_radial_fill = [&](D3D12UIBatch& batch,
        const DirectX::XMFLOAT4& rect, const DirectX::XMFLOAT4X4& matrix,
        const DirectX::XMFLOAT4& uv, const DirectX::XMFLOAT4& color,
        float canvas_scale, bool rotated, float amount, bool reverse)
    {
        const float fill = Clamp01(amount);
        if (fill <= 0.0f) return;
        const float center_x = 0.5f;
        const float center_y = 0.5f;
        const int segment_count = (std::max)(1, static_cast<int>(std::ceil(fill * 64.0f)));
        const float pi = DirectX::XM_PI;
        const float direction = reverse ? -1.0f : 1.0f;
        const float start_angle = -pi * 0.5f;
        const auto make_vertex = [&](float nx, float ny)
        {
            const DirectX::XMFLOAT2 logical = TransformPoint(matrix,
                rect.x + rect.z * nx, rect.y + rect.w * ny);
            D3D12UIVertex vertex;
            vertex.position = ToUiPixel(logical, canvas_scale,
                logical_height, viewport);
            const float u = rotated ? ny : nx;
            const float v = rotated ? nx : (1.0f - ny);
            vertex.uv = { uv.x + u * uv.z, uv.y + v * uv.w };
            vertex.color = color;
            vertex.uv_bounds = { uv.x, uv.y, uv.x + uv.z, uv.y + uv.w };
            return vertex;
        };
        const D3D12UIVertex center = make_vertex(center_x, center_y);
        for (int segment = 0; segment < segment_count; ++segment)
        {
            const float t0 = static_cast<float>(segment) /
                static_cast<float>(segment_count) * fill;
            const float t1 = static_cast<float>(segment + 1) /
                static_cast<float>(segment_count) * fill;
            const auto boundary = [&](float t)
            {
                const float angle = start_angle + direction * DirectX::XM_2PI * t;
                const float dx = std::cos(angle);
                const float dy = std::sin(angle);
                const float tx = dx > 0.0f ? (1.0f - center_x) / dx
                    : (dx < 0.0f ? (0.0f - center_x) / dx : 100000.0f);
                const float ty = dy > 0.0f ? (1.0f - center_y) / dy
                    : (dy < 0.0f ? (0.0f - center_y) / dy : 100000.0f);
                const float radius = (std::min)(tx, ty);
                return DirectX::XMFLOAT2{ center_x + dx * radius,
                    center_y + dy * radius };
            };
            const DirectX::XMFLOAT2 p0 = boundary(t0);
            const DirectX::XMFLOAT2 p1 = boundary(t1);
            batch.vertices.push_back(center);
            batch.vertices.push_back(make_vertex(p0.x, p0.y));
            batch.vertices.push_back(make_vertex(p1.x, p1.y));
        }
    };

    const auto append_polygon_shape = [&](D3D12UIBatch& batch,
        const DirectX::XMFLOAT4& rect, const DirectX::XMFLOAT4X4& matrix,
        const UIShapeComponent& shape, const DirectX::XMFLOAT4& color,
        float canvas_scale)
    {
        std::vector<DirectX::XMFLOAT2> points;
        if (shape.path_points.size() >= 3)
        {
            points = shape.path_points;
        }
        else
        {
            const int side_count = (std::max)(3, (std::min)(64, shape.sides));
            points.reserve(static_cast<std::size_t>(side_count));
            for (int index = 0; index < side_count; ++index)
            {
                const float angle = -DirectX::XM_PIDIV2 + DirectX::XM_2PI *
                    static_cast<float>(index) / static_cast<float>(side_count);
                points.push_back({ 0.5f + std::cos(angle) * 0.5f,
                    0.5f + std::sin(angle) * 0.5f });
            }
        }
        const auto make_vertex = [&](const DirectX::XMFLOAT2& point)
        {
            const DirectX::XMFLOAT2 logical = TransformPoint(matrix,
                rect.x + rect.z * point.x, rect.y + rect.w * point.y);
            D3D12UIVertex vertex;
            vertex.position = ToUiPixel(logical, canvas_scale,
                logical_height, viewport);
            vertex.uv = point;
            vertex.color = color;
            vertex.uv_bounds = { 0, 0, 1, 1 };
            return vertex;
        };
        const D3D12UIVertex center = make_vertex({ 0.5f, 0.5f });
        for (std::size_t index = 0; index < points.size(); ++index)
        {
            batch.vertices.push_back(center);
            batch.vertices.push_back(make_vertex(points[index]));
            batch.vertices.push_back(make_vertex(points[(index + 1) % points.size()]));
        }
    };

    const auto append_shape_image = [&](D3D12UIBatch& batch,
        const DirectX::XMFLOAT4& rect, const DirectX::XMFLOAT4X4& matrix,
        const DirectX::XMFLOAT4& uv, const DirectX::XMFLOAT4& color,
        float canvas_scale, bool rotated, const UIShapeImageComponent& shape_image)
    {
        if (shape_image.path_points.size() < 3) return;
        std::vector<DirectX::XMFLOAT2> points;
        const std::size_t count = shape_image.path_points.size();
        points.reserve(count * 8);
        const auto handle_at = [](const std::vector<DirectX::XMFLOAT2>& values,
            std::size_t index) noexcept
        {
            return index < values.size() ? values[index] : DirectX::XMFLOAT2{};
        };
        const auto lerp = [](const DirectX::XMFLOAT2& a,
            const DirectX::XMFLOAT2& b, float t) noexcept
        {
            return DirectX::XMFLOAT2{ a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t };
        };
        for (std::size_t index = 0; index < count; ++index)
        {
            const std::size_t next = (index + 1) % count;
            const DirectX::XMFLOAT2 a = shape_image.path_points[index];
            const DirectX::XMFLOAT2 b = shape_image.path_points[next];
            const DirectX::XMFLOAT2 out = handle_at(shape_image.path_out_handles, index);
            const DirectX::XMFLOAT2 in = handle_at(shape_image.path_in_handles, next);
            const DirectX::XMFLOAT2 p1{ a.x + out.x, a.y + out.y };
            const DirectX::XMFLOAT2 p2{ b.x + in.x, b.y + in.y };
            const int subdivisions = 8;
            for (int step = 0; step < subdivisions; ++step)
            {
                const float t = static_cast<float>(step) /
                    static_cast<float>(subdivisions);
                const float one_minus = 1.0f - t;
                const DirectX::XMFLOAT2 q0 = lerp(a, p1, t);
                const DirectX::XMFLOAT2 q1 = lerp(p1, p2, t);
                const DirectX::XMFLOAT2 q2 = lerp(p2, b, t);
                points.push_back({
                    one_minus * one_minus * q0.x + 2.0f * one_minus * t * q1.x + t * t * q2.x,
                    one_minus * one_minus * q0.y + 2.0f * one_minus * t * q1.y + t * t * q2.y });
            }
        }
        const auto make_vertex = [&](const DirectX::XMFLOAT2& point)
        {
            const DirectX::XMFLOAT2 logical = TransformPoint(matrix,
                rect.x + rect.z * point.x, rect.y + rect.w * point.y);
            D3D12UIVertex vertex;
            vertex.position = ToUiPixel(logical, canvas_scale,
                logical_height, viewport);
            const float u = rotated ? point.y : point.x;
            const float v = rotated ? point.x : (1.0f - point.y);
            vertex.uv = { uv.x + u * uv.z, uv.y + v * uv.w };
            vertex.color = color;
            vertex.uv_bounds = { uv.x, uv.y, uv.x + uv.z, uv.y + uv.w };
            return vertex;
        };
        const D3D12UIVertex center = make_vertex({ 0.5f, 0.5f });
        for (std::size_t index = 0; index < points.size(); ++index)
        {
            batch.vertices.push_back(center);
            batch.vertices.push_back(make_vertex(points[index]));
            batch.vertices.push_back(make_vertex(points[(index + 1) % points.size()]));
        }
    };

    std::vector<GameObject*> canvases;
    for (std::size_t index = 0; index < scene.GameObjectCount(); ++index)
    {
        GameObject* object = scene.GameObjectAt(index);
        if (object != nullptr && !object->PendingDestroy() && object->ActiveInHierarchy() &&
            object->GetComponent<CanvasComponent>() != nullptr)
            canvases.push_back(object);
    }
    std::stable_sort(canvases.begin(), canvases.end(),
        [](const GameObject* lhs, const GameObject* rhs)
        {
            const auto* a = lhs != nullptr ? lhs->GetComponent<CanvasComponent>() : nullptr;
            const auto* b = rhs != nullptr ? rhs->GetComponent<CanvasComponent>() : nullptr;
            return (a != nullptr ? a->sort_order : 0) < (b != nullptr ? b->sort_order : 0);
        });

    std::function<void(GameObject&, float, float,
        const D3D12_RECT*, const D3D12UIClip*,
        const std::vector<D3D12UIMask>*, int)> render_object;
    render_object = [&](GameObject& object, float canvas_scale, float opacity,
        const D3D12_RECT* inherited_scissor, const D3D12UIClip* inherited_clip,
        const std::vector<D3D12UIMask>* inherited_masks, int depth)
    {
        if (depth > kMaxUiDepth || object.PendingDestroy() || !object.ActiveInHierarchy()) return;
        const auto* effect_stack =
            object.GetComponent<ReplayEngine::Components::UIEffectStackComponent>();
        const int inherited_effect_group = active_effect_group;
        int owned_effect_group = -1;
        bool effect_group_restored = false;
        if (inherited_effect_group < 0 && effect_stack != nullptr && effect_stack->enabled &&
            effect_stack->HasActiveEffects(&asset_database))
        {
            owned_effect_group = begin_effect_group(*effect_stack);
            if (owned_effect_group >= 0) active_effect_group = owned_effect_group;
        }
        RectTransformComponent* rect = object.GetComponent<RectTransformComponent>();
        const D3D12_RECT* active_scissor = inherited_scissor;
        D3D12_RECT local_scissor{};
        const D3D12UIClip* active_clip = inherited_clip;
        D3D12UIClip local_clip{};
        const std::vector<D3D12UIMask>* active_masks = inherited_masks;
        std::vector<D3D12UIMask> local_masks;
        if (rect != nullptr)
        {
            const UIMaskComponent* mask = object.GetComponent<UIMaskComponent>();
            if (mask != nullptr && mask->ActiveInHierarchy() && mask->enabled_mask &&
                mask->mask_mode == UIMaskComponent::Rectangle)
            {
                local_scissor = make_scissor(*rect, canvas_scale);
                if (active_scissor != nullptr) local_scissor = IntersectRect(
                    *active_scissor, local_scissor);
                active_scissor = &local_scissor;
                ++frame.mask_depth;
            }
            else if (mask != nullptr && mask->ActiveInHierarchy() && mask->enabled_mask &&
                mask->mask_mode == UIMaskComponent::Shape)
            {
                const D3D12_RECT bounds = make_scissor(*rect, canvas_scale);
                local_clip.bounds = {
                    static_cast<float>(bounds.left), static_cast<float>(bounds.top),
                    static_cast<float>(bounds.right), static_cast<float>(bounds.bottom) };
                local_clip.parameters.x = mask->shape_kind == UIMaskComponent::ShapeCircle
                    ? 1.0f : (mask->shape_kind == UIMaskComponent::ShapeRoundedRectangle
                        ? 2.0f : 1.0f);
                local_clip.parameters.y = mask->invert ? 1.0f : 0.0f;
                local_clip.parameters.z = Clamp01(mask->softness) *
                    (std::max)(1.0f, (std::min)(
                        local_clip.bounds.z - local_clip.bounds.x,
                        local_clip.bounds.w - local_clip.bounds.y));
                local_clip.parameters.w = Clamp01(mask->shape_corner_radius);
                active_clip = &local_clip;
                ++frame.mask_depth;
            }
            else if (mask != nullptr && mask->ActiveInHierarchy() && mask->enabled_mask &&
                (mask->mask_mode == UIMaskComponent::Image ||
                    mask->mask_mode == UIMaskComponent::ObjectAlpha ||
                    mask->mask_mode == UIMaskComponent::ObjectLuma))
            {
                if (mask->mask_mode == UIMaskComponent::Image)
                {
                    D3D12UIMask image_mask = add_mask_texture(*mask);
                    image_mask.invert = mask->invert;
                    if (!image_mask.texture_key.empty()) local_masks.push_back(
                        std::move(image_mask));
                }
                else if (mask->mask_object.IsAssigned())
                {
                    ReplayEngine::Scene::Scene* scene = object.GetScene();
                    GameObject* matte_object = scene != nullptr
                        ? scene->FindGameObjectByID(mask->mask_object.object) : nullptr;
                    UIImageComponent* matte_image = matte_object != nullptr
                        ? matte_object->GetComponent<UIImageComponent>() : nullptr;
                    if (matte_image != nullptr)
                    {
                        DirectX::XMFLOAT4 matte_uv{};
                        bool matte_rotated = false;
                        D3D12UIMask object_mask{};
                        object_mask.texture_key = add_image_texture(
                            *matte_image, matte_uv, matte_rotated);
                        object_mask.uv = matte_uv;
                        object_mask.luma = mask->mask_mode == UIMaskComponent::ObjectLuma;
                        object_mask.invert = mask->invert;
                        if (!object_mask.texture_key.empty()) local_masks.push_back(
                            std::move(object_mask));
                    }
                }
                for (std::size_t matte_index = 0;
                    matte_index < mask->matte_objects.size() && local_masks.size() < 4;
                    ++matte_index)
                {
                    if (!mask->matte_objects[matte_index].IsAssigned()) continue;
                    ReplayEngine::Scene::Scene* scene = object.GetScene();
                    GameObject* matte_object = scene != nullptr
                        ? scene->FindGameObjectByID(
                            mask->matte_objects[matte_index].object) : nullptr;
                    UIImageComponent* matte_image = matte_object != nullptr
                        ? matte_object->GetComponent<UIImageComponent>() : nullptr;
                    if (matte_image == nullptr) continue;
                    DirectX::XMFLOAT4 matte_uv{};
                    bool matte_rotated = false;
                    D3D12UIMask extra_mask{};
                    extra_mask.texture_key = add_image_texture(
                        *matte_image, matte_uv, matte_rotated);
                    extra_mask.uv = matte_uv;
                    extra_mask.luma = mask->mask_mode == UIMaskComponent::ObjectLuma;
                    extra_mask.invert = mask->invert;
                    if (matte_index < mask->matte_operations.size())
                        extra_mask.operation = (std::max)(0, (std::min)(2,
                            mask->matte_operations[matte_index]));
                    if (!extra_mask.texture_key.empty()) local_masks.push_back(
                        std::move(extra_mask));
                }
                if (!local_masks.empty())
                {
                    active_masks = &local_masks;
                    ++frame.mask_depth;
                }
            }
        }

        const float local_opacity = opacity *
            (object.GetComponent<CanvasComponent>() != nullptr
                ? Clamp01(object.GetComponent<CanvasComponent>()->opacity) : 1.0f);
        if (rect != nullptr && !RectIsEmpty(active_scissor != nullptr
            ? *active_scissor : D3D12_RECT{ 0, 0,
                static_cast<LONG>(frame.target_width), static_cast<LONG>(frame.target_height) }))
        {
            if (UIShapeComponent* shape = object.GetComponent<UIShapeComponent>())
            {
                D3D12UIBatch& batch = make_batch("__dx12_white",
                    MultiplyColor(shape->fill_color, local_opacity),
                    D3D12UIBlendMode::Alpha, active_scissor, active_clip, active_masks);
                batch.constants.mode = {
                    shape->shape == UIShapeComponent::Circle ? 1.0f : 0.0f,
                    0.0f, 0.0f, 0.0f };
                const DirectX::XMFLOAT4 shape_color =
                    MultiplyColor(shape->fill_color, local_opacity);
                batch.constants.fill_color_2 =
                    MultiplyColor(shape->fill_color_2, local_opacity);
                batch.constants.fill_parameters = {
                    DirectX::XMConvertToRadians(shape->fill_angle),
                    shape->fill_center.x, shape->fill_center.y,
                    static_cast<float>(shape->fill_mode) };
                if (shape->shape == UIShapeComponent::Polygon ||
                    shape->shape == UIShapeComponent::CustomBezierPath)
                {
                    append_polygon_shape(batch, rect->ResolvedRect(),
                        rect->ResolvedMatrix(), *shape, shape_color, canvas_scale);
                }
                else
                {
                    append_quad(batch, rect->ResolvedRect(), rect->ResolvedMatrix(),
                        { 0, 0, 1, 1 }, shape_color, canvas_scale, false);
                }
            }

            if (UIImageComponent* image = object.GetComponent<UIImageComponent>())
            {
                DirectX::XMFLOAT4 uv{};
                bool rotated = false;
                const std::string texture_key = add_image_texture(*image, uv, rotated);
                DirectX::XMFLOAT4 draw_rect = rect->ResolvedRect();
                const float fill = Clamp01(image->fill_amount);
                if (image->fill_method == UIImageComponent::Horizontal)
                {
                    if (image->fill_reverse)
                    {
                        draw_rect.x += draw_rect.z * (1.0f - fill);
                        uv.x += uv.z * (1.0f - fill);
                    }
                    draw_rect.z *= fill;
                    uv.z *= fill;
                }
                else if (image->fill_method == UIImageComponent::Vertical)
                {
                    if (image->fill_reverse)
                    {
                        draw_rect.y += draw_rect.w * (1.0f - fill);
                        uv.y += uv.w * (1.0f - fill);
                    }
                    draw_rect.w *= fill;
                    uv.w *= fill;
                }
                if (fill > 0.0f)
                {
                    D3D12UIBlendMode blend = D3D12UIBlendMode::Alpha;
                    if (image->blend_mode == UIImageComponent::Additive) blend = D3D12UIBlendMode::Additive;
                    else if (image->blend_mode == UIImageComponent::Multiply) blend = D3D12UIBlendMode::Multiply;
                    else if (image->blend_mode == UIImageComponent::Screen) blend = D3D12UIBlendMode::Screen;
                    D3D12UIBatch& batch = make_batch(texture_key,
                        MultiplyColor(image->color, local_opacity), blend,
                        active_scissor, active_clip, active_masks);
                    batch.constants.fill_color_2 =
                        MultiplyColor(image->fill_color_2, local_opacity);
                    batch.constants.mode.x = 0.0f;
                    batch.constants.fill_parameters = {
                        DirectX::XMConvertToRadians(image->fill_angle),
                        image->fill_center.x, image->fill_center.y,
                        static_cast<float>(image->fill_mode) };
                    const DirectX::XMFLOAT4 image_color =
                        MultiplyColor(image->color, local_opacity);
                    if (image->fill_method == UIImageComponent::Radial360)
                    {
                        append_radial_fill(batch, rect->ResolvedRect(),
                            rect->ResolvedMatrix(), uv, image_color, canvas_scale,
                            rotated, fill, image->fill_reverse);
                    }
                    else if (image->nine_slice.x > 0.0f || image->nine_slice.y > 0.0f ||
                        image->nine_slice.z > 0.0f || image->nine_slice.w > 0.0f)
                    {
                        append_nine_slice(batch, draw_rect, rect->ResolvedMatrix(),
                            uv, image->nine_slice, image_color, canvas_scale, rotated);
                    }
                    else
                    {
                        const UIPuppetDeformComponent* puppet =
                            object.GetComponent<UIPuppetDeformComponent>();
                        const UIShapeImageComponent* shape_image =
                            object.GetComponent<UIShapeImageComponent>();
                        if (shape_image != nullptr && shape_image->path_points.size() >= 3)
                        {
                            append_shape_image(batch, draw_rect, rect->ResolvedMatrix(), uv,
                                image_color, canvas_scale, rotated, *shape_image);
                        }
                        else if (puppet != nullptr && puppet->enabled_deform)
                        {
                            append_puppet_quad(batch, draw_rect, rect->ResolvedMatrix(), uv,
                                image_color, canvas_scale, rotated, *puppet);
                        }
                        else
                        {
                            append_quad(batch, draw_rect, rect->ResolvedMatrix(), uv,
                                image_color, canvas_scale, rotated);
                        }
                    }
                }
            }

            if (UITextComponent* text = object.GetComponent<UITextComponent>())
            {
                text->UpdateNumberDisplay(scene);
                text->font_size = (std::max)(1.0f, text->font_size);
                ui_font_atlas.BuildGlyphs(*text, rect->ResolvedRect().z,
                    rect->ResolvedRect().w, &asset_database);
                UIInputFieldComponent* input = object.GetComponent<UIInputFieldComponent>();
                const UISelectableComponent* selectable =
                    object.GetComponent<UISelectableComponent>();
                const bool focused_input = input != nullptr && selectable != nullptr &&
                    selectable->focused && selectable->ActiveInHierarchy();
                if (focused_input && input->HasSelection() && !input->password)
                {
                    D3D12UIBatch& selection = make_batch("__dx12_white",
                        MultiplyColor(input->selection_color, local_opacity),
                        D3D12UIBlendMode::Alpha, active_scissor, active_clip, active_masks);
                    const int selection_start = input->SelectionStart();
                    const int selection_end = input->SelectionEnd();
                    for (const UITextComponent::GlyphQuad& glyph : text->Glyphs())
                    {
                        if (glyph.character_index < selection_start ||
                            glyph.character_index >= selection_end)
                            continue;
                        append_quad(selection,
                            { rect->ResolvedRect().x + glyph.position.x,
                                rect->ResolvedRect().y + glyph.position.y,
                                (std::max)(glyph.advance, glyph.size.x), glyph.size.y },
                            rect->ResolvedMatrix(), { 0, 0, 1, 1 },
                            MultiplyColor(input->selection_color, local_opacity),
                            canvas_scale, false);
                    }
                }
                std::string atlas_key;
                if (add_font_source(atlas_key))
                {
                    for (const UITextComponent::GlyphQuad& glyph : text->Glyphs())
                    {
                        D3D12UIBatch& batch = make_batch(atlas_key,
                            MultiplyColor(text->color, local_opacity),
                            D3D12UIBlendMode::Alpha, active_scissor, active_clip, active_masks);
                        batch.constants.mode = { 0, 1, text->outline_width, 0 };
                        batch.constants.outline_color = text->outline_color;
                        batch.constants.shadow_offset = {
                            text->shadow_offset.x, text->shadow_offset.y, 0, 0 };
                        batch.constants.shadow_color = text->shadow_color;
                        DirectX::XMFLOAT4 glyph_rect{
                            rect->ResolvedRect().x + glyph.position.x,
                            rect->ResolvedRect().y + glyph.position.y,
                            glyph.size.x, glyph.size.y };
                        DirectX::XMFLOAT4 color{
                            text->color.x * glyph.rich_color.x,
                            text->color.y * glyph.rich_color.y,
                            text->color.z * glyph.rich_color.z,
                            text->color.w * glyph.rich_color.w * Clamp01(local_opacity) };
                        append_quad(batch, glyph_rect, rect->ResolvedMatrix(), glyph.uv,
                            color, canvas_scale, false);
                    }
                }
                if (focused_input)
                {
                    const float blink_period = (std::max)(0.05f,
                        input->caret_blink_seconds);
                    const bool caret_visible = std::fmod(
                        (std::max)(0.0f, shader_composer_time), blink_period) <
                        blink_period * 0.5f;
                    if (caret_visible)
                    {
                        const DirectX::XMFLOAT4 text_rect = rect->ResolvedRect();
                        float caret_x = text_rect.x;
                        float caret_y = text_rect.y +
                            (std::max)(0.0f, (text_rect.w - text->font_size) * 0.5f);
                        float caret_h = (std::max)(1.0f, text->font_size);
                        for (const UITextComponent::GlyphQuad& glyph : text->Glyphs())
                        {
                            if (glyph.character_index >= input->caret_index)
                            {
                                caret_x = text_rect.x + glyph.position.x;
                                caret_y = text_rect.y + glyph.position.y;
                                caret_h = (std::max)(1.0f, glyph.size.y);
                                break;
                            }
                            caret_x = text_rect.x + glyph.position.x + glyph.advance;
                            caret_y = text_rect.y + glyph.position.y;
                            caret_h = (std::max)(1.0f, glyph.size.y);
                        }
                        D3D12UIBatch& caret = make_batch("__dx12_white",
                            MultiplyColor(input->caret_color, local_opacity),
                            D3D12UIBlendMode::Alpha, active_scissor, active_clip, active_masks);
                        append_quad(caret,
                            { caret_x, caret_y,
                                (std::max)(0.5f, input->caret_width /
                                    (std::max)(0.0001f, canvas_scale)), caret_h },
                            rect->ResolvedMatrix(), { 0, 0, 1, 1 },
                            MultiplyColor(input->caret_color, local_opacity),
                            canvas_scale, false);
                    }
                }
            }

        if (const UISelectableComponent* selectable =
                object.GetComponent<UISelectableComponent>())
            {
                const bool outline_enabled = selectable->override_focus_style
                    ? selectable->focus_outline_enabled : project_settings.FocusOutlineEnabled();
                if (selectable->focused && selectable->ActiveInHierarchy() && outline_enabled)
                {
                    const DirectX::XMFLOAT4 color = MultiplyColor(
                        selectable->override_focus_style
                            ? selectable->focus_outline_color : project_settings.FocusOutlineColor(),
                        local_opacity);
                    const float width = selectable->override_focus_style
                        ? selectable->focus_outline_width : project_settings.FocusOutlineWidth();
                    D3D12UIBatch& outline = make_batch("__dx12_white", color,
                        D3D12UIBlendMode::Alpha, active_scissor, active_clip, active_masks);
                    append_border(outline, rect->ResolvedRect(), rect->ResolvedMatrix(),
                        width / (std::max)(0.0001f, canvas_scale), color, canvas_scale);
                }
            }
        }

        // Self ends before children; Subtree intentionally keeps the same group
        // active while the DFS visits every descendant.
        if (owned_effect_group >= 0 && effect_stack != nullptr &&
            effect_stack->target_scope == ReplayEngine::Components::UIEffectStackComponent::Self)
        {
            D3D12UIEffectGroup& group = frame.effect_groups[
                static_cast<std::size_t>(owned_effect_group)];
            group.batch_count = static_cast<std::uint32_t>(frame.batches.size()) -
                group.first_batch;
            active_effect_group = inherited_effect_group;
            effect_group_restored = true;
        }

        std::vector<GameObject*> children = object.Children();
        std::stable_sort(children.begin(), children.end(),
            [](const GameObject* lhs, const GameObject* rhs)
            {
                const auto* a = lhs != nullptr ? lhs->GetComponent<RectTransformComponent>() : nullptr;
                const auto* b = rhs != nullptr ? rhs->GetComponent<RectTransformComponent>() : nullptr;
                return (a != nullptr ? a->sort_order : 0) < (b != nullptr ? b->sort_order : 0);
            });
        for (GameObject* child : children)
        {
            if (child != nullptr) render_object(*child, canvas_scale, local_opacity,
                active_scissor, active_clip, active_masks, depth + 1);
        }

        if (rect != nullptr)
        {
            if (const UIScrollViewComponent* scroll =
                object.GetComponent<UIScrollViewComponent>())
            {
                if (scroll->ActiveInHierarchy() && scroll->show_scrollbars)
                {
                    const DirectX::XMFLOAT4 r = rect->ResolvedRect();
                    const float width = (std::max)(2.0f,
                        scroll->scrollbar_width / (std::max)(0.0001f, canvas_scale));
                    if (scroll->vertical_overflow || scroll->horizontal_overflow)
                    {
                        D3D12UIBatch& bars = make_batch("__dx12_white",
                            { 1, 1, 1, 1 }, D3D12UIBlendMode::Alpha,
                            active_scissor, active_clip, active_masks);
                        if (scroll->vertical_overflow)
                        {
                        const float thumb_h = (std::max)(width,
                            r.w * Clamp01(scroll->vertical_visible_ratio));
                        const float travel = (std::max)(0.0f, r.w - thumb_h);
                        const float thumb_y = r.y + travel *
                            (1.0f - Clamp01(scroll->vertical_normalized));
                        append_quad(bars, { r.x + r.z - width, r.y, width, r.w },
                            rect->ResolvedMatrix(), { 0, 0, 1, 1 },
                            MultiplyColor(scroll->scrollbar_track_color, local_opacity),
                            canvas_scale, false);
                        append_quad(bars, { r.x + r.z - width, thumb_y, width, thumb_h },
                            rect->ResolvedMatrix(), { 0, 0, 1, 1 },
                            MultiplyColor(scroll->scrollbar_thumb_color, local_opacity),
                            canvas_scale, false);
                        }
                        if (scroll->horizontal_overflow)
                        {
                        const float thumb_w = (std::max)(width,
                            r.z * Clamp01(scroll->horizontal_visible_ratio));
                        const float travel = (std::max)(0.0f, r.z - thumb_w);
                        const float thumb_x = r.x + travel *
                            Clamp01(scroll->horizontal_normalized);
                        append_quad(bars, { r.x, r.y, r.z, width },
                            rect->ResolvedMatrix(), { 0, 0, 1, 1 },
                            MultiplyColor(scroll->scrollbar_track_color, local_opacity),
                            canvas_scale, false);
                        append_quad(bars, { thumb_x, r.y, thumb_w, width },
                            rect->ResolvedMatrix(), { 0, 0, 1, 1 },
                            MultiplyColor(scroll->scrollbar_thumb_color, local_opacity),
                            canvas_scale, false);
                        }
                    }
                }
            }
        }

        if (owned_effect_group >= 0 && !effect_group_restored)
        {
            D3D12UIEffectGroup& group = frame.effect_groups[
                static_cast<std::size_t>(owned_effect_group)];
            group.batch_count = static_cast<std::uint32_t>(frame.batches.size()) -
                group.first_batch;
            active_effect_group = inherited_effect_group;
        }
    };

    for (GameObject* canvas_object : canvases)
    {
        if (canvas_object == nullptr) continue;
        CanvasComponent* canvas = canvas_object->GetComponent<CanvasComponent>();
        if (canvas == nullptr || !canvas->ActiveInHierarchy()) continue;
        const float scale = (std::max)(0.0001f,
            ReplayEngine::UI::UILayout::CanvasScale(*canvas, logical_width, logical_height));
        render_object(*canvas_object, scale, Clamp01(canvas->opacity),
            nullptr, nullptr, nullptr, 0);
    }
    frame.draw_commands = static_cast<std::uint32_t>(frame.batches.size());
    for (const D3D12UIBatch& batch : frame.batches)
        frame.vertex_count += static_cast<std::uint32_t>(batch.vertices.size());
    frame.texture_count = static_cast<std::uint32_t>(texture_keys.size() + frame.font_atlases.size());
    return true;
}
