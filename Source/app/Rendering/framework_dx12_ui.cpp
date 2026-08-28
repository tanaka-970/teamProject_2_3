#include "../framework_class.h"

#include "../../../RePlayEngine/Components/UI/CanvasComponent.h"
#include "../../../RePlayEngine/Components/UI/RectTransformComponent.h"
#include "../../../RePlayEngine/Components/UI/UIImageComponent.h"
#include "../../../RePlayEngine/Components/UI/UIEffectStackComponent.h"
#include "../../../RePlayEngine/Components/Rendering/ModelEffectStackComponent.h"
#include "../../../RePlayEngine/Components/Rendering/ScreenEffectStackComponent.h"
#include "../../../RePlayEngine/Components/UI/UIInputFieldComponent.h"
#include "../../../RePlayEngine/Components/UI/UIMaskComponent.h"
#include "../../../RePlayEngine/Components/UI/UIScrollViewComponent.h"
#include "../../../RePlayEngine/Components/UI/UISelectableComponent.h"
#include "../../../RePlayEngine/Components/UI/UIShapeComponent.h"
#include "../../../RePlayEngine/Components/UI/UIShapeImageComponent.h"
#include "../../../RePlayEngine/Components/UI/UITextComponent.h"
#include "../../../RePlayEngine/Components/UI/UITextAnimatorComponent.h"
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
    using ReplayEngine::Components::UITextAnimatorComponent;
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

    float Lerp(float a, float b, float t) noexcept
    {
        return a + (b - a) * t;
    }

    DirectX::XMFLOAT4 LerpColor(const DirectX::XMFLOAT4& a,
        const DirectX::XMFLOAT4& b, float t) noexcept
    {
        return {
            Lerp(a.x, b.x, t), Lerp(a.y, b.y, t),
            Lerp(a.z, b.z, t), Lerp(a.w, b.w, t) };
    }

    float SmoothStep(float value) noexcept
    {
        value = Clamp01(value);
        return value * value * (3.0f - 2.0f * value);
    }

    float TextAnimatorInfluence(const UITextAnimatorComponent& animator,
        float position) noexcept
    {
        const float start = animator.range_start + animator.range_offset;
        const float end = animator.range_end + animator.range_offset;
        const float low = (std::min)(start, end);
        const float high = (std::max)(start, end);
        const float width = (std::max)(0.0001f, high - low);
        if (position < low || position > high) return 0.0f;

        const float t = Clamp01((position - low) / width);
        float influence = 1.0f;
        switch (animator.range_shape)
        {
        case UITextAnimatorComponent::RampUp:
            influence = t;
            break;
        case UITextAnimatorComponent::RampDown:
            influence = 1.0f - t;
            break;
        case UITextAnimatorComponent::Triangle:
            influence = 1.0f - std::fabs(t * 2.0f - 1.0f);
            break;
        case UITextAnimatorComponent::Round:
        {
            const float centered = t * 2.0f - 1.0f;
            influence = std::sqrt(Clamp01(1.0f - centered * centered));
            break;
        }
        case UITextAnimatorComponent::Smooth:
            influence = SmoothStep(t);
            break;
        default:
            break;
        }

        const float smoothness = Clamp01(animator.range_smoothness);
        if (smoothness > 0.0f)
        {
            const float edge = (std::min)(0.5f, smoothness * 0.5f);
            const float in_edge = edge > 0.0f ? SmoothStep(t / edge) : 1.0f;
            const float out_edge = edge > 0.0f
                ? SmoothStep((1.0f - t) / edge) : 1.0f;
            influence *= (std::min)(in_edge, out_edge);
        }
        return Clamp01(influence);
    }

    std::uint32_t HashTextGlyph(int seed, int character_index,
        std::uint32_t salt) noexcept
    {
        std::uint32_t value = static_cast<std::uint32_t>(seed);
        value ^= static_cast<std::uint32_t>(character_index) + 0x9e3779b9u +
            (value << 6) + (value >> 2);
        value ^= salt;
        value ^= value >> 16;
        value *= 0x7feb352du;
        value ^= value >> 15;
        value *= 0x846ca68bu;
        value ^= value >> 16;
        return value;
    }

    float RandomTextGlyphSigned(int seed, int character_index,
        std::uint32_t salt) noexcept
    {
        const std::uint32_t value = HashTextGlyph(seed, character_index, salt);
        return static_cast<float>(value & 0x00ffffffu) / 8388607.5f - 1.0f;
    }

    DirectX::XMFLOAT2 TextAnimatorAnchor(int anchor) noexcept
    {
        switch (anchor)
        {
        case UITextAnimatorComponent::BaselineLeft:
            return { 0.0f, 0.5f };
        case UITextAnimatorComponent::BaselineCenter:
            return { 0.5f, 0.5f };
        case UITextAnimatorComponent::TopLeft:
            return { 0.0f, 0.0f };
        case UITextAnimatorComponent::BottomCenter:
            return { 0.5f, 1.0f };
        default:
            return { 0.5f, 0.5f };
        }
    }

    void GatherTextAnimators(const GameObject& object,
        std::vector<const UITextAnimatorComponent*>& animators)
    {
        animators.clear();
        for (std::size_t index = 0; index < object.ComponentCount(); ++index)
        {
            const auto* animator = dynamic_cast<const UITextAnimatorComponent*>(
                object.ComponentAt(index));
            if (animator != nullptr && animator->ActiveInHierarchy())
                animators.push_back(animator);
        }
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
        dx12_device_context.Width(), dx12_device_context.Height(), viewport);
}

bool framework::build_dx12_ui_for_scene(
    ReplayEngine::Rendering::DX12::D3D12UIFrame& frame,
    ReplayEngine::Scene::Scene& scene,
    std::uint32_t target_width, std::uint32_t target_height,
    const object_ui_viewport& requested_viewport)
{
    using namespace ReplayEngine;
    using namespace ReplayEngine::Rendering::DX12;

    frame = {};
    frame.target_width = target_width;
    frame.target_height = target_height;
    // 移行前のCanvas Previewは透明クリアしたRTを灰色のパネル上へ重ねていた。
    // ここを不透明色で消去するとパネル背景を覆い、以前より真っ暗に見える。
    frame.clear_color = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (frame.target_width == 0 || frame.target_height == 0) return true;

    object_ui_viewport viewport = requested_viewport;
    viewport.width = (std::max)(1.0f, viewport.width);
    viewport.height = (std::max)(1.0f, viewport.height);
    viewport.logical_width = (std::max)(1.0f, viewport.logical_width);
    viewport.logical_height = (std::max)(1.0f, viewport.logical_height);
    ReplayEngine::UI::UILayout::Resolve(scene,
        viewport.logical_width, viewport.logical_height);

    std::unordered_set<std::string> texture_keys;
    std::unordered_map<std::string, std::size_t> font_indices;
    std::unordered_map<std::string, std::uint64_t> font_revisions;

    const auto add_image_texture = [&](const UIImageComponent& image,
        DirectX::XMFLOAT4& uv, bool& rotated,
        std::vector<DirectX::XMFLOAT2>* atlas_path_points) -> std::string
    {
        if (atlas_path_points != nullptr) atlas_path_points->clear();
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
                        if (atlas_path_points != nullptr &&
                            region->path_points.size() >= 3 &&
                            region->uv_rect.z > 0.000001f &&
                            region->uv_rect.w > 0.000001f)
                        {
                            atlas_path_points->reserve(region->path_points.size());
                            for (const DirectX::XMFLOAT2& point : region->path_points)
                            {
                                const float relative_x =
                                    (point.x - region->uv_rect.x) / region->uv_rect.z;
                                const float relative_y =
                                    (point.y - region->uv_rect.y) / region->uv_rect.w;
                                atlas_path_points->push_back(region->rotated
                                    ? DirectX::XMFLOAT2{ relative_y, relative_x }
                                    : DirectX::XMFLOAT2{ relative_x, 1.0f - relative_y });
                            }
                        }
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
        std::uint64_t revision = 0;
        if (!ui_font_atlas.ActiveAtlasRevision(key, revision)) return false;
        const auto old = font_revisions.find(key);
        if (old != font_revisions.end() && old->second == revision) return true;
        // 同じ版が既に GPU にあるなら Atlas 本体を複製しない。
        if (dx12_device_context.HasUIFontTexture(key, revision))
        {
            font_revisions[key] = revision;
            return true;
        }
        std::vector<std::uint8_t> rgba;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint64_t copied_revision = 0;
        if (!ui_font_atlas.CopyActiveAtlas(key, rgba, width, height, copied_revision))
            return false;
        revision = copied_revision;
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
        using ReplayEngine::UI::UIEffectRegionData;
        using ReplayEngine::UI::UIEffectRegionScope;
        using ReplayEngine::UI::UIEffectRegionShape;
        const auto register_effect_texture = [&](const Assets::AssetRecord* record)
            -> std::string
        {
            if (record == nullptr || record->kind != AssetKind::Image) return {};
            const std::filesystem::path path = content_path(record->cache_path.empty()
                ? record->source_path : record->cache_path).lexically_normal();
            const std::string key = path.generic_string();
            if (!key.empty() && texture_keys.insert(key).second)
            {
                D3D12StaticTextureSource source;
                source.key = key;
                source.source_path = path;
                frame.texture_sources.push_back(std::move(source));
            }
            return key;
        };
        const auto texture_for_guid = [&](const std::string& guid)
        {
            return guid.empty() || guid == "__runtime_ui_matte"
                ? std::string{} : register_effect_texture(asset_database.FindByGuid(guid));
        };
        const std::uint64_t owner_id = stack.Owner() != nullptr
            ? stack.Owner()->ID().Value() : 0ull;
        for (const ReplayEngine::UI::UIEffect& effect : stack.EffectiveEffects(&asset_database))
        {
            if (!effect.enabled || output.size() >= 32) continue;
            D3D12UIEffectCommand command{};
            const int kind = effect.kind;
            if (kind < static_cast<int>(UIEffectKind::Blur) ||
                kind > static_cast<int>(UIEffectKind::FrostCrack))
                continue;
            command.kind = static_cast<std::uint32_t>(kind);
            command.radius = (std::max)(0.0f, effect.radius);
            command.intensity = (std::max)(0.0f, effect.intensity);
            command.threshold = effect.threshold;
            command.amount = effect.amount;
            command.angle = effect.angle;
            command.progress = effect.progress;
            command.softness = effect.softness;
            command.speed = effect.speed;
            command.seed = effect.seed;
            command.time = shader_composer_time;
            command.waveform = effect.waveform;
            command.direction = effect.direction;
            command.color = effect.color;
            command.color_2 = effect.color_2;
            command.color_3 = effect.color_3;
            command.color_4 = effect.color_4;
            command.color_stops = { effect.color_stop_2,
                effect.color_stop_3, effect.color_stop_4, 0.0f };
            std::string mask_guid = effect.mask;
            const UIEffectKind effect_kind = static_cast<UIEffectKind>(kind);
            if (effect_kind == UIEffectKind::BrushStroke && effect.brush_atlas_enabled)
            {
                if (mask_guid.empty())
                {
                    const Assets::AssetRecord* atlas = asset_database.FindByPath(
                        std::filesystem::path("resources") / "BrushMasks" /
                        "brush_masks_atlas.png");
                    if (atlas != nullptr) mask_guid = atlas->guid;
                }
                command.brush_atlas = true;
                command.brush_pattern_settings = {
                    static_cast<float>((std::max)(0, (std::min)(15,
                        effect.brush_pattern_index))),
                    static_cast<float>((std::max)(0, (std::min)(1,
                        effect.brush_pattern_mode))), 0.0f, 0.0f };
                for (std::size_t group = 0;
                    group < command.brush_pattern_weights.size(); ++group)
                {
                    const std::size_t first = group * 4;
                    command.brush_pattern_weights[group] = {
                        (std::max)(0.0f, effect.brush_pattern_weights[first]),
                        (std::max)(0.0f, effect.brush_pattern_weights[first + 1]),
                        (std::max)(0.0f, effect.brush_pattern_weights[first + 2]),
                        (std::max)(0.0f, effect.brush_pattern_weights[first + 3]) };
                }
            }
            command.auxiliary_texture_key = texture_for_guid(mask_guid);

            command.temporal = effect_kind == UIEffectKind::MotionBlur ||
                effect_kind == UIEffectKind::Echo ||
                effect_kind == UIEffectKind::FeedbackZoom;
            if (command.temporal && owner_id != 0)
            {
                // 移行前と同じくStackの最終合成結果を全Temporal Effectで共有する。
                command.history_key = owner_id;
            }

            const auto& region = stack.effect_region;
            command.region_enabled = region.enabled &&
                (region.scope == static_cast<int>(UIEffectRegionScope::AllEffects) ||
                    effect.region_enabled);
            if (command.region_enabled)
            {
                const auto fill_region = [](const UIEffectRegionData& source,
                    DirectX::XMFLOAT4& params, DirectX::XMFLOAT4& settings)
                {
                    const int shape = (std::max)(0, (std::min)(3, source.shape));
                    params = { source.center.x, source.center.y,
                        (std::max)(source.size.x, 0.0001f),
                        (std::max)(source.size.y, 0.0001f) };
                    settings = { source.rotation, (std::max)(0.0f, source.feather),
                        Clamp01(source.strength),
                        static_cast<float>(shape) + (source.invert ? 4.0f : 0.0f) };
                };
                const auto fill_path = [&](const UIEffectRegionData& source,
                    std::size_t slot)
                {
                    if (source.shape != static_cast<int>(UIEffectRegionShape::Freeform) ||
                        slot >= command.effect_region_path_points.size()) return;
                    const std::size_t count = (std::min)(source.path_points.size(),
                        command.effect_region_path_points[slot].size());
                    command.effect_region_path_counts[slot].x =
                        static_cast<float>(count);
                    for (std::size_t point = 0; point < count; ++point)
                    {
                        command.effect_region_path_points[slot][point] = {
                            source.path_points[point].x, source.path_points[point].y,
                            0.0f, 0.0f };
                    }
                };
                fill_region(region, command.effect_region_params,
                    command.effect_region_settings);
                fill_path(region, 0);
                std::size_t region_count = 1;
                for (const UIEffectRegionData& additional : region.additional)
                {
                    if (region_count > command.effect_region_extra_params.size() ||
                        !additional.enabled ||
                        (additional.scope == static_cast<int>(
                            UIEffectRegionScope::SelectedEffects) && !effect.region_enabled))
                        continue;
                    const std::size_t extra_slot = region_count - 1;
                    fill_region(additional,
                        command.effect_region_extra_params[extra_slot],
                        command.effect_region_extra_settings[extra_slot]);
                    fill_path(additional, region_count);
                    ++region_count;
                }
                command.effect_region_count.x = static_cast<float>(region_count);
                if (region.shape == static_cast<int>(UIEffectRegionShape::TextureMask))
                    command.region_mask_texture_key = texture_for_guid(region.mask);
            }
            output.push_back(command);
        }
        return !output.empty();
    };

    bool world_space_canvas = false;
    DirectX::XMFLOAT4X4 world_canvas_matrix{};
    DirectX::XMStoreFloat4x4(&world_canvas_matrix, DirectX::XMMatrixIdentity());
    DirectX::XMFLOAT4X4 world_view_projection = frame_constants.view_projection;
    DirectX::XMFLOAT4 world_canvas_parameters{ 0, 0, 0, 0 };
    const DirectX::XMFLOAT4 world_viewport{
        viewport.left, viewport.top, viewport.width, viewport.height };

    const auto project_world_canvas_pixel = [&](const DirectX::XMFLOAT2& input)
    {
        if (!world_space_canvas) return input;
        const float normalized_x = (input.x - viewport.left) /
            (std::max)(viewport.width, 1.0f);
        const float normalized_y = (input.y - viewport.top) /
            (std::max)(viewport.height, 1.0f);
        const DirectX::XMVECTOR canvas_position = DirectX::XMVectorSet(
            (normalized_x - 0.5f) * world_canvas_parameters.y,
            (0.5f - normalized_y) * world_canvas_parameters.z, 0.0f, 1.0f);
        const DirectX::XMMATRIX world = DirectX::XMLoadFloat4x4(
            &world_canvas_matrix);
        const DirectX::XMMATRIX view_projection = DirectX::XMLoadFloat4x4(
            &world_view_projection);
        const DirectX::XMVECTOR projected = DirectX::XMVector4Transform(
            DirectX::XMVector4Transform(canvas_position, world), view_projection);
        const float w = DirectX::XMVectorGetW(projected);
        if (std::fabs(w) <= 0.000001f) return input;
        const float ndc_x = DirectX::XMVectorGetX(projected) / w;
        const float ndc_y = DirectX::XMVectorGetY(projected) / w;
        return DirectX::XMFLOAT2{
            viewport.left + (ndc_x * 0.5f + 0.5f) * viewport.width,
            viewport.top + (0.5f - ndc_y * 0.5f) * viewport.height };
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
    const auto finalize_effect_group = [&](int group_index,
        const ReplayEngine::Components::UIEffectStackComponent& stack)
    {
        if (group_index < 0 || static_cast<std::size_t>(group_index) >=
            frame.effect_groups.size()) return;
        D3D12UIEffectGroup& group = frame.effect_groups[
            static_cast<std::size_t>(group_index)];
        group.batch_count = static_cast<std::uint32_t>(frame.batches.size()) -
            group.first_batch;
        bool has_bounds = false;
        float min_x = 0.0f;
        float min_y = 0.0f;
        float max_x = 0.0f;
        float max_y = 0.0f;
        const std::size_t first = (std::min)(
            static_cast<std::size_t>(group.first_batch), frame.batches.size());
        const std::size_t end = (std::min)(first + group.batch_count,
            frame.batches.size());
        for (std::size_t batch_index = first; batch_index < end; ++batch_index)
        {
            for (const D3D12UIVertex& vertex : frame.batches[batch_index].vertices)
            {
                const DirectX::XMFLOAT2 bounds_position =
                    project_world_canvas_pixel(vertex.position);
                if (!has_bounds)
                {
                    min_x = max_x = bounds_position.x;
                    min_y = max_y = bounds_position.y;
                    has_bounds = true;
                }
                else
                {
                    min_x = (std::min)(min_x, bounds_position.x);
                    min_y = (std::min)(min_y, bounds_position.y);
                    max_x = (std::max)(max_x, bounds_position.x);
                    max_y = (std::max)(max_y, bounds_position.y);
                }
            }
        }
        if (!has_bounds) return;
        const DirectX::XMFLOAT4 expansion = stack.ExpandBounds(
            static_cast<float>(frame.target_width),
            static_cast<float>(frame.target_height), &asset_database);
        group.composite_scissor = ClampRect({
            static_cast<LONG>(std::floor(min_x - expansion.x)),
            static_cast<LONG>(std::floor(min_y - expansion.y)),
            static_cast<LONG>(std::ceil(max_x + expansion.z)),
            static_cast<LONG>(std::ceil(max_y + expansion.w)) },
            frame.target_width, frame.target_height);
        group.composite_scissor_enabled = !RectIsEmpty(group.composite_scissor);
    };

    const auto make_scissor = [&](const RectTransformComponent& rect,
        float canvas_scale) noexcept
    {
        const DirectX::XMFLOAT4 r = rect.ResolvedRect();
        const DirectX::XMFLOAT4X4& matrix = rect.ResolvedMatrix();
        const DirectX::XMFLOAT2 points[] =
        {
            project_world_canvas_pixel(ToUiPixel(TransformPoint(matrix, r.x, r.y),
                canvas_scale, viewport.logical_height, viewport)),
            project_world_canvas_pixel(ToUiPixel(TransformPoint(matrix, r.x + r.z, r.y),
                canvas_scale, viewport.logical_height, viewport)),
            project_world_canvas_pixel(ToUiPixel(TransformPoint(matrix,
                r.x + r.z, r.y + r.w), canvas_scale,
                viewport.logical_height, viewport)),
            project_world_canvas_pixel(ToUiPixel(TransformPoint(matrix, r.x, r.y + r.w),
                canvas_scale, viewport.logical_height, viewport)),
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

    const auto configure_mask_transform = [&](D3D12UIMask& mask,
        const RectTransformComponent& rect, float canvas_scale) noexcept
    {
        const DirectX::XMFLOAT4 bounds = rect.ResolvedRect();
        const DirectX::XMFLOAT4X4& matrix = rect.ResolvedMatrix();
        // Matteの左上を原点にし、画面座標から画像内0..1座標へ戻す。
        // Scene Viewのoffset/scaleも頂点と同じToUiPixelを通すため枠と一致する。
        const DirectX::XMFLOAT2 top_left = project_world_canvas_pixel(
            ToUiPixel(TransformPoint(matrix,
            bounds.x, bounds.y + bounds.w), canvas_scale,
            viewport.logical_height, viewport));
        const DirectX::XMFLOAT2 top_right = project_world_canvas_pixel(
            ToUiPixel(TransformPoint(matrix,
            bounds.x + bounds.z, bounds.y + bounds.w), canvas_scale,
            viewport.logical_height, viewport));
        const DirectX::XMFLOAT2 bottom_left = project_world_canvas_pixel(
            ToUiPixel(TransformPoint(matrix,
            bounds.x, bounds.y), canvas_scale,
            viewport.logical_height, viewport));
        const float axis_x_x = top_right.x - top_left.x;
        const float axis_x_y = top_right.y - top_left.y;
        const float axis_y_x = bottom_left.x - top_left.x;
        const float axis_y_y = bottom_left.y - top_left.y;
        const float determinant = axis_x_x * axis_y_y - axis_y_x * axis_x_y;
        mask.screen_origin = top_left;
        if (std::fabs(determinant) <= 0.000001f)
        {
            mask.screen_inverse = { 0, 0, 0, 0 };
            return;
        }
        mask.screen_inverse = {
            axis_y_y / determinant, -axis_y_x / determinant,
            -axis_x_y / determinant, axis_x_x / determinant };
    };

    const auto append_quad = [&](D3D12UIBatch& batch,
        const DirectX::XMFLOAT4& rect, const DirectX::XMFLOAT4X4& matrix,
        const DirectX::XMFLOAT4& uv, const DirectX::XMFLOAT4& color,
        float canvas_scale, bool rotated)
    {
        // 左上・左下・右下と、左上・右下・右上の2三角形で矩形を作る。
        // 後半の始点を右上にすると同じ頂点が重なり、半分だけの三角形になる。
        const float nx[] = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f };
        const float ny[] = { 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f };
        for (int index = 0; index < 6; ++index)
        {
            const DirectX::XMFLOAT2 logical = TransformPoint(matrix,
                rect.x + rect.z * nx[index], rect.y + rect.w * ny[index]);
            const DirectX::XMFLOAT2 position = ToUiPixel(logical, canvas_scale,
                viewport.logical_height, viewport);
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

    const auto append_quad_local_with_bounds = [&](D3D12UIBatch& batch,
        const DirectX::XMFLOAT4& rect, const DirectX::XMFLOAT4X4& matrix,
        const DirectX::XMFLOAT4& uv, const DirectX::XMFLOAT4& color,
        float canvas_scale, const DirectX::XMFLOAT2& local_scale,
        float rotation_degrees, const DirectX::XMFLOAT2& anchor,
        float shear_x, const DirectX::XMFLOAT4& uv_bounds)
    {
        const float pivot_x = rect.x + rect.z * anchor.x;
        const float pivot_y = rect.y + rect.w * anchor.y;
        const float radians = DirectX::XMConvertToRadians(rotation_degrees);
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);
        const auto transformed_position = [&](float nx, float ny)
        {
            const float local_x = rect.x + rect.z * nx;
            const float local_y = rect.y + rect.w * ny;
            float dx = (local_x - pivot_x) * local_scale.x;
            const float dy = (local_y - pivot_y) * local_scale.y;
            dx += dy * shear_x;
            const float rotated_x = dx * cosine - dy * sine + pivot_x;
            const float rotated_y = dx * sine + dy * cosine + pivot_y;
            return ToUiPixel(TransformPoint(matrix, rotated_x, rotated_y),
                canvas_scale, viewport.logical_height, viewport);
        };

        // 通常矩形と同じ頂点順を使い、変形文字でも2枚目の三角形を退化させない。
        const float nx[] = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f };
        const float ny[] = { 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f };
        for (int index = 0; index < 6; ++index)
        {
            D3D12UIVertex vertex;
            vertex.position = transformed_position(nx[index], ny[index]);
            vertex.uv = {
                uv.x + nx[index] * uv.z,
                uv.y + (1.0f - ny[index]) * uv.w };
            vertex.color = color;
            vertex.uv_bounds = uv_bounds;
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
                viewport.logical_height, viewport);
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
        const D3D12_RECT* scissor, const std::vector<D3D12UIClip>* clips = nullptr,
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
        batch.constants.world_canvas_matrix = world_canvas_matrix;
        batch.constants.world_view_projection = world_view_projection;
        batch.constants.world_canvas_parameters = world_canvas_parameters;
        batch.constants.world_canvas_parameters.x = world_space_canvas ? 1.0f : 0.0f;
        batch.constants.world_viewport = world_viewport;
        if (scissor != nullptr)
        {
            batch.scissor = *scissor;
            batch.scissor_enabled = true;
        }
        if (clips != nullptr && !clips->empty())
        {
            batch.clip_count = static_cast<std::uint32_t>((std::min)(
                clips->size(), batch.clips.size()));
            for (std::size_t index = 0; index < batch.clip_count; ++index)
                batch.clips[index] = (*clips)[index];
            batch.clip = batch.clips[0];
            batch.clip_enabled = true;
            batch.constants.clip_parameters = batch.clips[0].parameters;
            batch.constants.clip_bounds = batch.clips[0].bounds;
            batch.constants.clip_shape = batch.clips[0].shape;
            batch.constants.clip_state.x = static_cast<float>(batch.clip_count);
            for (std::size_t index = 1; index < batch.clip_count; ++index)
            {
                batch.constants.clip_parameters_extra[index - 1] =
                    batch.clips[index].parameters;
                batch.constants.clip_bounds_extra[index - 1] =
                    batch.clips[index].bounds;
                batch.constants.clip_shapes_extra[index - 1] =
                    batch.clips[index].shape;
            }
        }
        if (masks != nullptr && !masks->empty())
        {
            const std::size_t count = (std::min)(masks->size(), batch.masks.size());
            for (std::size_t index = 0; index < count; ++index)
            {
                if ((*masks)[index].texture_key.empty()) continue;
                batch.masks[index] = (*masks)[index];
                batch.constants.mask_uvs[index] = (*masks)[index].uv;
                batch.constants.mask_origins[index] = {
                    (*masks)[index].screen_origin.x,
                    (*masks)[index].screen_origin.y, 0, 0 };
                batch.constants.mask_inverses[index] =
                    (*masks)[index].screen_inverse;
                const float operation = static_cast<float>((*masks)[index].operation);
                const float luma = (*masks)[index].luma ? 1.0f : 0.0f;
                const float invert = (*masks)[index].invert ? 1.0f : 0.0f;
                const float rotated = (*masks)[index].rotated ? 1.0f : 0.0f;
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
                set_component(batch.constants.mask_inverts, index, invert);
                set_component(batch.constants.mask_rotated, index, rotated);
                if (index == 0) batch.constants.mask_uv = (*masks)[index].uv;
                batch.mask_count = static_cast<std::uint32_t>(index + 1);
            }
            if (batch.mask_count > 0)
            {
                batch.mask_enabled = true;
                batch.constants.mask_parameters = {
                    static_cast<float>(batch.mask_count), 0.0f, 0.0f, 0.0f };
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

    const auto append_scrollbars = [&](GameObject& object,
        const RectTransformComponent& rect, float canvas_scale, float opacity,
        const D3D12_RECT* scissor, const std::vector<D3D12UIClip>* clips,
        const std::vector<D3D12UIMask>* masks)
    {
        const UIScrollViewComponent* scroll =
            object.GetComponent<UIScrollViewComponent>();
        if (scroll == nullptr || !scroll->ActiveInHierarchy() ||
            !scroll->show_scrollbars ||
            (!scroll->vertical_overflow && !scroll->horizontal_overflow)) return;
        const DirectX::XMFLOAT4 r = rect.ResolvedRect();
        const float width = (std::max)(2.0f,
            scroll->scrollbar_width / (std::max)(0.0001f, canvas_scale));
        D3D12UIBatch& bars = make_batch("__dx12_white",
            { 1, 1, 1, 1 }, D3D12UIBlendMode::Alpha,
            scissor, clips, masks);
        if (scroll->vertical_overflow)
        {
            const float thumb_h = (std::max)(width,
                r.w * Clamp01(scroll->vertical_visible_ratio));
            const float travel = (std::max)(0.0f, r.w - thumb_h);
            const float thumb_y = r.y + travel *
                (1.0f - Clamp01(scroll->vertical_normalized));
            append_quad(bars, { r.x + r.z - width, r.y, width, r.w },
                rect.ResolvedMatrix(), { 0, 0, 1, 1 },
                MultiplyColor(scroll->scrollbar_track_color, opacity),
                canvas_scale, false);
            append_quad(bars, { r.x + r.z - width, thumb_y, width, thumb_h },
                rect.ResolvedMatrix(), { 0, 0, 1, 1 },
                MultiplyColor(scroll->scrollbar_thumb_color, opacity),
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
                rect.ResolvedMatrix(), { 0, 0, 1, 1 },
                MultiplyColor(scroll->scrollbar_track_color, opacity),
                canvas_scale, false);
            append_quad(bars, { thumb_x, r.y, thumb_w, width },
                rect.ResolvedMatrix(), { 0, 0, 1, 1 },
                MultiplyColor(scroll->scrollbar_thumb_color, opacity),
                canvas_scale, false);
        }
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
                viewport.logical_height, viewport);
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

    const auto append_shape = [&](const UIShapeComponent& shape,
        const DirectX::XMFLOAT4& rect, const DirectX::XMFLOAT4X4& matrix,
        float canvas_scale, float opacity, const D3D12_RECT* scissor,
        const std::vector<D3D12UIClip>* clips,
        const std::vector<D3D12UIMask>* masks)
    {
        std::vector<DirectX::XMFLOAT2> path;
        bool closed = true;
        const auto append_arc = [&](float center_x, float center_y,
            float radius_x, float radius_y, float start, float end, int steps)
        {
            for (int step = 0; step <= steps; ++step)
            {
                const float t = static_cast<float>(step) /
                    static_cast<float>((std::max)(1, steps));
                const float angle = start + (end - start) * t;
                path.push_back({ center_x + std::cos(angle) * radius_x,
                    center_y + std::sin(angle) * radius_y });
            }
        };
        const auto append_cubic = [&](const DirectX::XMFLOAT2& p0,
            const DirectX::XMFLOAT2& p1, const DirectX::XMFLOAT2& p2,
            const DirectX::XMFLOAT2& p3, int subdivisions, bool include_first)
        {
            for (int step = include_first ? 0 : 1; step <= subdivisions; ++step)
            {
                const float t = static_cast<float>(step) /
                    static_cast<float>(subdivisions);
                const float u = 1.0f - t;
                const float uu = u * u;
                const float tt = t * t;
                path.push_back({
                    p0.x * uu * u + 3.0f * p1.x * uu * t +
                        3.0f * p2.x * u * tt + p3.x * tt * t,
                    p0.y * uu * u + 3.0f * p1.y * uu * t +
                        3.0f * p2.y * u * tt + p3.y * tt * t });
            }
        };
        switch (shape.shape)
        {
        case UIShapeComponent::Circle:
        {
            const float curvature = Clamp01(shape.arc_curvature);
            if (curvature <= 0.0001f)
            {
                closed = false;
                path.push_back({ rect.x, rect.y + rect.w * 0.5f });
                path.push_back({ rect.x + rect.z, rect.y + rect.w * 0.5f });
                break;
            }
            const float radius = 0.5f / curvature;
            const float chord_height = (std::sqrt)((std::max)(0.0f,
                radius * radius - 0.25f));
            const float top_center_y = 0.5f + chord_height;
            const float bottom_center_y = 0.5f - chord_height;
            float top_start = std::atan2(-chord_height, -0.5f);
            if (top_start < 0.0f) top_start += DirectX::XM_2PI;
            float top_end = std::atan2(-chord_height, 0.5f);
            if (top_end < 0.0f) top_end += DirectX::XM_2PI;
            if (top_end <= top_start) top_end += DirectX::XM_2PI;
            const float bottom_start = std::atan2(chord_height, 0.5f);
            const float bottom_end = std::atan2(chord_height, -0.5f);
            const float arc_angle = top_end - top_start;
            const float pixel_radius = (std::max)(std::fabs(rect.z),
                std::fabs(rect.w)) * (std::max)(std::fabs(canvas_scale),
                    0.0001f) * radius;
            const float max_angle = pixel_radius > 0.5f
                ? 2.0f * (std::acos)((std::max)(-1.0f, (std::min)(1.0f,
                    1.0f - 0.5f / pixel_radius))) : arc_angle;
            const int subdivisions = (std::min)(256, (std::max)(1,
                static_cast<int>(std::ceil(arc_angle /
                    (std::max)(0.0001f, max_angle)))));
            const auto to_rect = [&](float x, float y)
            {
                return DirectX::XMFLOAT2{ rect.x + rect.z * x,
                    rect.y + rect.w * y };
            };
            for (int step = 0; step <= subdivisions; ++step)
            {
                const float t = static_cast<float>(step) /
                    static_cast<float>(subdivisions);
                const float angle = top_start + arc_angle * t;
                path.push_back(to_rect(0.5f + std::cos(angle) * radius,
                    top_center_y + std::sin(angle) * radius));
            }
            for (int step = 1; step <= subdivisions; ++step)
            {
                const float t = static_cast<float>(step) /
                    static_cast<float>(subdivisions);
                const float angle = bottom_start + (bottom_end - bottom_start) * t;
                path.push_back(to_rect(0.5f + std::cos(angle) * radius,
                    bottom_center_y + std::sin(angle) * radius));
            }
            break;
        }
        case UIShapeComponent::Line:
            closed = false;
            path.push_back({ rect.x, rect.y + rect.w * 0.5f });
            path.push_back({ rect.x + rect.z, rect.y + rect.w * 0.5f });
            break;
        case UIShapeComponent::Polygon:
        {
            const int sides = (std::max)(3, (std::min)(64, shape.sides));
            for (int side = 0; side < sides; ++side)
            {
                const float angle = -DirectX::XM_PIDIV2 + DirectX::XM_2PI *
                    static_cast<float>(side) / static_cast<float>(sides);
                path.push_back({ rect.x + rect.z *
                    (0.5f + std::cos(angle) * 0.5f), rect.y + rect.w *
                    (0.5f + std::sin(angle) * 0.5f) });
            }
            break;
        }
        case UIShapeComponent::BezierPath:
        {
            closed = false;
            append_cubic({ rect.x, rect.y + rect.w * 0.5f },
                { rect.x + rect.z * 0.35f, rect.y },
                { rect.x + rect.z * 0.65f, rect.y + rect.w },
                { rect.x + rect.z, rect.y + rect.w * 0.5f }, 48, true);
            break;
        }
        case UIShapeComponent::CustomBezierPath:
        {
            const std::size_t count = shape.path_points.size();
            if (count < 2)
            {
                closed = false;
                break;
            }
            closed = shape.path_closed && count >= 3;
            const std::size_t segment_count = closed ? count : count - 1;
            const auto point_at = [&](const DirectX::XMFLOAT2& point)
            {
                return DirectX::XMFLOAT2{ rect.x + point.x * rect.z,
                    rect.y + point.y * rect.w };
            };
            const auto handle_at = [](const std::vector<DirectX::XMFLOAT2>& values,
                std::size_t index)
            {
                return index < values.size() ? values[index] : DirectX::XMFLOAT2{};
            };
            for (std::size_t segment = 0; segment < segment_count; ++segment)
            {
                const std::size_t next = (segment + 1) % count;
                const DirectX::XMFLOAT2 a = shape.path_points[segment];
                const DirectX::XMFLOAT2 b = shape.path_points[next];
                const DirectX::XMFLOAT2 out = handle_at(
                    shape.path_out_handles, segment);
                const DirectX::XMFLOAT2 in = handle_at(
                    shape.path_in_handles, next);
                append_cubic(point_at(a), point_at({ a.x + out.x, a.y + out.y }),
                    point_at({ b.x + in.x, b.y + in.y }), point_at(b), 16,
                    segment == 0);
            }
            break;
        }
        case UIShapeComponent::Superellipse:
        {
            constexpr int subdivisions = 128;
            const float exponent = (std::max)(0.25f,
                (std::min)(16.0f, shape.superellipse_exponent));
            const float power = 2.0f / exponent;
            for (int segment = 0; segment < subdivisions; ++segment)
            {
                const float angle = DirectX::XM_2PI *
                    static_cast<float>(segment) / static_cast<float>(subdivisions);
                const float cosine = std::cos(angle);
                const float sine = std::sin(angle);
                const float x = std::copysign(
                    (std::pow)(std::fabs(cosine), power), cosine);
                const float y = std::copysign(
                    (std::pow)(std::fabs(sine), power), sine);
                path.push_back({ rect.x + rect.z * (0.5f + x * 0.5f),
                    rect.y + rect.w * (0.5f + y * 0.5f) });
            }
            break;
        }
        case UIShapeComponent::PolarFormula:
        {
            constexpr int subdivisions = 160;
            const float base_radius = (std::max)(0.05f,
                (std::min)(1.5f, shape.polar_base_radius));
            const float amplitude = (std::max)(-1.0f,
                (std::min)(1.0f, shape.polar_amplitude));
            const float lobes = (std::max)(1.0f,
                (std::min)(32.0f, shape.polar_lobes));
            const float rotation = DirectX::XMConvertToRadians(shape.polar_rotation);
            for (int segment = 0; segment < subdivisions; ++segment)
            {
                const float theta = DirectX::XM_2PI *
                    static_cast<float>(segment) / static_cast<float>(subdivisions);
                const float radial = base_radius + amplitude * std::cos(lobes * theta);
                const float angle = theta + rotation;
                path.push_back({ rect.x + rect.z *
                    (0.5f + std::cos(angle) * radial * 0.5f),
                    rect.y + rect.w *
                    (0.5f + std::sin(angle) * radial * 0.5f) });
            }
            break;
        }
        default:
        {
            const float radius = (std::min)((std::max)(0.0f, shape.corner_radius),
                (std::min)(std::fabs(rect.z), std::fabs(rect.w)) * 0.5f);
            if (radius <= 0.001f)
            {
                path = { { rect.x, rect.y }, { rect.x + rect.z, rect.y },
                    { rect.x + rect.z, rect.y + rect.w },
                    { rect.x, rect.y + rect.w } };
            }
            else
            {
                append_arc(rect.x + rect.z - radius, rect.y + radius,
                    radius, radius, -DirectX::XM_PIDIV2, 0.0f, 8);
                append_arc(rect.x + rect.z - radius, rect.y + rect.w - radius,
                    radius, radius, 0.0f, DirectX::XM_PIDIV2, 8);
                append_arc(rect.x + radius, rect.y + rect.w - radius,
                    radius, radius, DirectX::XM_PIDIV2, DirectX::XM_PI, 8);
                append_arc(rect.x + radius, rect.y + radius,
                    radius, radius, DirectX::XM_PI,
                    DirectX::XM_PI + DirectX::XM_PIDIV2, 8);
            }
            break;
        }
        }
        if (closed && path.size() > 1)
        {
            const float dx = path.front().x - path.back().x;
            const float dy = path.front().y - path.back().y;
            if (dx * dx + dy * dy < 0.00000001f) path.pop_back();
        }
        if (path.empty()) return;

        const auto make_fill_vertex = [&](const DirectX::XMFLOAT2& point,
            const DirectX::XMFLOAT4& color)
        {
            D3D12UIVertex vertex{};
            vertex.position = ToUiPixel(TransformPoint(matrix, point.x, point.y),
                canvas_scale, viewport.logical_height, viewport);
            vertex.uv = { (point.x - rect.x) /
                    (std::max)(0.0001f, std::fabs(rect.z)),
                (point.y - rect.y) /
                    (std::max)(0.0001f, std::fabs(rect.w)) };
            vertex.color = color;
            vertex.uv_bounds = { 0, 0, 1, 1 };
            return vertex;
        };
        const bool has_fill = closed && shape.shape != UIShapeComponent::Line &&
            path.size() >= 3 && shape.fill_color.w * opacity > 0.0f;
        if (has_fill)
        {
            const DirectX::XMFLOAT4 fill = MultiplyColor(shape.fill_color, opacity);
            D3D12UIBatch& batch = make_batch("__dx12_white", fill,
                D3D12UIBlendMode::Alpha, scissor, clips, masks);
            batch.constants.fill_color_2 = shape.fill_color_2;
            batch.constants.fill_color_3 = shape.fill_color_3;
            batch.constants.fill_color_4 = shape.fill_color_4;
            batch.constants.fill_stops = { shape.fill_stop_2, shape.fill_stop_3,
                shape.fill_stop_4, 0.0f };
            batch.constants.fill_parameters = {
                DirectX::XMConvertToRadians(shape.fill_angle),
                shape.fill_center.x, shape.fill_center.y,
                static_cast<float>(shape.fill_mode) };

            std::vector<std::size_t> indices(path.size());
            for (std::size_t index = 0; index < indices.size(); ++index)
                indices[index] = index;
            float signed_area = 0.0f;
            for (std::size_t index = 0; index < path.size(); ++index)
            {
                const DirectX::XMFLOAT2& a = path[index];
                const DirectX::XMFLOAT2& b = path[(index + 1) % path.size()];
                signed_area += a.x * b.y - b.x * a.y;
            }
            if (signed_area < 0.0f) std::reverse(indices.begin(), indices.end());
            const auto cross = [](const DirectX::XMFLOAT2& a,
                const DirectX::XMFLOAT2& b, const DirectX::XMFLOAT2& c)
            {
                return (b.x - a.x) * (c.y - a.y) -
                    (b.y - a.y) * (c.x - a.x);
            };
            const auto inside_triangle = [&cross](const DirectX::XMFLOAT2& point,
                const DirectX::XMFLOAT2& a, const DirectX::XMFLOAT2& b,
                const DirectX::XMFLOAT2& c)
            {
                constexpr float epsilon = -0.00001f;
                return cross(a, b, point) >= epsilon &&
                    cross(b, c, point) >= epsilon &&
                    cross(c, a, point) >= epsilon;
            };
            std::size_t guard = path.size() * path.size();
            while (indices.size() > 2 && guard-- > 0)
            {
                bool clipped = false;
                for (std::size_t index = 0; index < indices.size(); ++index)
                {
                    const std::size_t previous = indices[
                        (index + indices.size() - 1) % indices.size()];
                    const std::size_t current = indices[index];
                    const std::size_t next = indices[(index + 1) % indices.size()];
                    if (cross(path[previous], path[current], path[next]) <= 0.00001f)
                        continue;
                    bool contains = false;
                    for (const std::size_t candidate : indices)
                    {
                        if (candidate == previous || candidate == current ||
                            candidate == next) continue;
                        if (inside_triangle(path[candidate], path[previous],
                            path[current], path[next]))
                        {
                            contains = true;
                            break;
                        }
                    }
                    if (contains) continue;
                    batch.vertices.push_back(make_fill_vertex(path[previous], fill));
                    batch.vertices.push_back(make_fill_vertex(path[current], fill));
                    batch.vertices.push_back(make_fill_vertex(path[next], fill));
                    indices.erase(indices.begin() +
                        static_cast<std::ptrdiff_t>(index));
                    clipped = true;
                    break;
                }
                if (!clipped) break;
            }
        }

        float stroke_width = shape.stroke_width;
        DirectX::XMFLOAT4 stroke_color = shape.stroke_color;
        if (shape.shape == UIShapeComponent::Line && stroke_width <= 0.0f)
        {
            stroke_width = 1.0f;
            stroke_color = shape.fill_color;
        }
        if (stroke_width <= 0.0f || stroke_color.w * opacity <= 0.0f ||
            path.size() < 2) return;
        D3D12UIBatch& stroke = make_batch("__dx12_white",
            MultiplyColor(stroke_color, opacity), D3D12UIBlendMode::Alpha,
            scissor, clips, masks);
        stroke.constants.stroke_color_2 = shape.stroke_color_2;
        stroke.constants.stroke_parameters.x = static_cast<float>(shape.stroke_mode);
        stroke.constants.fill_stops = { 1.0f, -1.0f, -1.0f, 0.0f };

        const std::size_t segment_count = closed ? path.size() : path.size() - 1;
        std::vector<float> lengths(segment_count + 1, 0.0f);
        for (std::size_t segment = 0; segment < segment_count; ++segment)
        {
            const DirectX::XMFLOAT2& a = path[segment];
            const DirectX::XMFLOAT2& b = path[(segment + 1) % path.size()];
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            lengths[segment + 1] = lengths[segment] + std::sqrt(dx * dx + dy * dy);
        }
        const float total_length = lengths.back();
        const float trim_span = (std::max)(0.0f,
            Clamp01(shape.trim_end) - Clamp01(shape.trim_start));
        if (total_length <= 0.001f || trim_span <= 0.0f) return;
        float interval_start[2]{};
        float interval_end[2]{};
        int interval_count = 0;
        if (trim_span >= 0.9999f)
        {
            interval_end[interval_count++] = total_length;
        }
        else
        {
            float start = std::fmod(Clamp01(shape.trim_start) + shape.trim_offset, 1.0f);
            if (start < 0.0f) start += 1.0f;
            const float end = start + trim_span;
            interval_start[interval_count] = start * total_length;
            interval_end[interval_count++] = (std::min)(end, 1.0f) * total_length;
            if (end > 1.0f)
            {
                interval_start[interval_count] = 0.0f;
                interval_end[interval_count++] = (end - 1.0f) * total_length;
            }
        }
        const float dash_length = (std::max)(0.0f, shape.dash_length);
        const float dash_gap = (std::max)(0.0f, shape.dash_gap);
        const float dash_pattern = dash_length + dash_gap;
        const bool dashed = dash_length > 0.0f && dash_gap > 0.0f;
        const auto emit_line = [&](const DirectX::XMFLOAT2& logical_a,
            const DirectX::XMFLOAT2& logical_b, float gradient_a, float gradient_b)
        {
            const DirectX::XMFLOAT2 a = ToUiPixel(TransformPoint(matrix,
                logical_a.x, logical_a.y), canvas_scale,
                viewport.logical_height, viewport);
            const DirectX::XMFLOAT2 b = ToUiPixel(TransformPoint(matrix,
                logical_b.x, logical_b.y), canvas_scale,
                viewport.logical_height, viewport);
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length <= 0.001f) return;
            const float half_width = stroke_width * canvas_scale * 0.5f;
            const float nx = -dy / length * half_width;
            const float ny = dx / length * half_width;
            const DirectX::XMFLOAT4 color = MultiplyColor(stroke_color, opacity);
            const auto vertex = [&](float x, float y, float u, float v)
            {
                D3D12UIVertex result{};
                result.position = { x, y };
                result.uv = { u, v };
                result.color = color;
                result.uv_bounds = { 0, 0, 1, 1 };
                return result;
            };
            const D3D12UIVertex q0 = vertex(a.x - nx, a.y - ny, gradient_a, 0.0f);
            const D3D12UIVertex q1 = vertex(a.x + nx, a.y + ny, gradient_a, 1.0f);
            const D3D12UIVertex q2 = vertex(b.x + nx, b.y + ny, gradient_b, 1.0f);
            const D3D12UIVertex q3 = vertex(b.x - nx, b.y - ny, gradient_b, 0.0f);
            stroke.vertices.insert(stroke.vertices.end(), { q0, q1, q2, q0, q2, q3 });
        };
        const auto lerp_point = [](const DirectX::XMFLOAT2& a,
            const DirectX::XMFLOAT2& b, float t)
        {
            return DirectX::XMFLOAT2{ a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t };
        };
        for (std::size_t segment = 0; segment < segment_count; ++segment)
        {
            const float segment_start = lengths[segment];
            const float segment_end = lengths[segment + 1];
            if (segment_end <= segment_start) continue;
            const DirectX::XMFLOAT2& a = path[segment];
            const DirectX::XMFLOAT2& b = path[(segment + 1) % path.size()];
            for (int interval = 0; interval < interval_count; ++interval)
            {
                float cursor = (std::max)(segment_start, interval_start[interval]);
                const float clipped_end = (std::min)(segment_end, interval_end[interval]);
                int dash_guard = 0;
                while (cursor < clipped_end && dash_guard++ < 256)
                {
                    float draw_end = clipped_end;
                    if (dashed)
                    {
                        float phase = std::fmod(cursor + shape.dash_offset, dash_pattern);
                        if (phase < 0.0f) phase += dash_pattern;
                        if (phase >= dash_length)
                        {
                            cursor += (std::min)(clipped_end - cursor,
                                dash_pattern - phase);
                            continue;
                        }
                        draw_end = cursor + (std::min)(clipped_end - cursor,
                            dash_length - phase);
                    }
                    const float ta = (cursor - segment_start) /
                        (segment_end - segment_start);
                    const float tb = (draw_end - segment_start) /
                        (segment_end - segment_start);
                    emit_line(lerp_point(a, b, ta), lerp_point(a, b, tb),
                        cursor / total_length, draw_end / total_length);
                    cursor = draw_end;
                    if (!dashed) break;
                }
            }
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
                viewport.logical_height, viewport);
            const float u = rotated ? point.y : point.x;
            const float v = rotated ? point.x : (1.0f - point.y);
            vertex.uv = { uv.x + u * uv.z, uv.y + v * uv.w };
            vertex.color = color;
            vertex.uv_bounds = { uv.x, uv.y, uv.x + uv.z, uv.y + uv.w };
            return vertex;
        };
        std::vector<std::size_t> indices(points.size());
        for (std::size_t index = 0; index < indices.size(); ++index)
            indices[index] = index;
        const auto cross = [](const DirectX::XMFLOAT2& a,
            const DirectX::XMFLOAT2& b, const DirectX::XMFLOAT2& c)
        {
            return (b.x - a.x) * (c.y - a.y) -
                (b.y - a.y) * (c.x - a.x);
        };
        float signed_area = 0.0f;
        for (std::size_t index = 0; index < points.size(); ++index)
        {
            const DirectX::XMFLOAT2& a = points[index];
            const DirectX::XMFLOAT2& b = points[(index + 1) % points.size()];
            signed_area += a.x * b.y - b.x * a.y;
        }
        if (signed_area < 0.0f) std::reverse(indices.begin(), indices.end());
        const auto inside_triangle = [&cross](const DirectX::XMFLOAT2& point,
            const DirectX::XMFLOAT2& a, const DirectX::XMFLOAT2& b,
            const DirectX::XMFLOAT2& c)
        {
            constexpr float epsilon = -0.00001f;
            return cross(a, b, point) >= epsilon &&
                cross(b, c, point) >= epsilon &&
                cross(c, a, point) >= epsilon;
        };
        std::size_t guard = points.size() * points.size();
        while (indices.size() > 2 && guard-- > 0)
        {
            bool clipped = false;
            for (std::size_t index = 0; index < indices.size(); ++index)
            {
                const std::size_t previous = indices[
                    (index + indices.size() - 1) % indices.size()];
                const std::size_t current = indices[index];
                const std::size_t next = indices[(index + 1) % indices.size()];
                if (cross(points[previous], points[current], points[next]) <= 0.00001f)
                    continue;
                bool contains = false;
                for (const std::size_t candidate : indices)
                {
                    if (candidate == previous || candidate == current ||
                        candidate == next) continue;
                    if (inside_triangle(points[candidate], points[previous],
                        points[current], points[next]))
                    {
                        contains = true;
                        break;
                    }
                }
                if (contains) continue;
                batch.vertices.push_back(make_vertex(points[previous]));
                batch.vertices.push_back(make_vertex(points[current]));
                batch.vertices.push_back(make_vertex(points[next]));
                indices.erase(indices.begin() +
                    static_cast<std::ptrdiff_t>(index));
                clipped = true;
                break;
            }
            if (!clipped) break;
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
        const D3D12_RECT*, const std::vector<D3D12UIClip>*,
        const std::vector<D3D12UIMask>*, int)> render_object;
    render_object = [&](GameObject& object, float canvas_scale, float opacity,
        const D3D12_RECT* inherited_scissor,
        const std::vector<D3D12UIClip>* inherited_clips,
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
        const std::vector<D3D12UIClip>* active_clips = inherited_clips;
        std::vector<D3D12UIClip> local_clips;
        const std::vector<D3D12UIMask>* active_masks = inherited_masks;
        std::vector<D3D12UIMask> local_masks;
        bool render_self = true;
        if (rect != nullptr)
        {
            const UIMaskComponent* mask = object.GetComponent<UIMaskComponent>();
            if (mask != nullptr && mask->ActiveInHierarchy() && mask->enabled_mask)
                render_self = mask->show_mask_graphic;
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
                if (inherited_clips != nullptr)
                {
                    const std::size_t inherited_count = (std::min)(
                        inherited_clips->size(), std::size_t{ 4 });
                    local_clips.assign(inherited_clips->begin(),
                        inherited_clips->begin() + inherited_count);
                }
                D3D12UIClip local_clip{};
                const DirectX::XMFLOAT4 mask_rect = rect->ResolvedRect();
                const DirectX::XMFLOAT4X4& mask_matrix = rect->ResolvedMatrix();
                const DirectX::XMFLOAT2 center = project_world_canvas_pixel(
                    ToUiPixel(TransformPoint(mask_matrix,
                    mask_rect.x + mask_rect.z * 0.5f,
                    mask_rect.y + mask_rect.w * 0.5f), canvas_scale,
                    viewport.logical_height, viewport));
                const DirectX::XMFLOAT2 right = project_world_canvas_pixel(
                    ToUiPixel(TransformPoint(mask_matrix,
                    mask_rect.x + mask_rect.z,
                    mask_rect.y + mask_rect.w * 0.5f), canvas_scale,
                    viewport.logical_height, viewport));
                const DirectX::XMFLOAT2 top = project_world_canvas_pixel(
                    ToUiPixel(TransformPoint(mask_matrix,
                    mask_rect.x + mask_rect.z * 0.5f,
                    mask_rect.y + mask_rect.w), canvas_scale,
                    viewport.logical_height, viewport));
                const float right_x = right.x - center.x;
                const float right_y = right.y - center.y;
                const float top_x = top.x - center.x;
                const float top_y = top.y - center.y;
                const float half_width = std::sqrt(
                    right_x * right_x + right_y * right_y) *
                    std::fabs(mask->group_scale.x);
                const float half_height = std::sqrt(
                    top_x * top_x + top_y * top_y) *
                    std::fabs(mask->group_scale.y);
                const float rotation = std::atan2(right_y, right_x) *
                    (180.0f / DirectX::XM_PI) + mask->shape_rotation;
                local_clip.bounds = { center.x, center.y,
                    (std::max)(half_width, 0.0001f),
                    (std::max)(half_height, 0.0001f) };
                local_clip.parameters.x = static_cast<float>((std::max)(0,
                    (std::min)(4, mask->shape_kind)) + 1);
                local_clip.parameters.y = mask->invert ? 1.0f : 0.0f;
                local_clip.parameters.z = Clamp01(mask->softness);
                local_clip.parameters.w = Clamp01(mask->shape_corner_radius);
                local_clip.shape = {
                    static_cast<float>((std::max)(3, mask->shape_sides)),
                    Clamp01(mask->shape_inner_radius), rotation, 0.0f };
                if (local_clips.size() < 4)
                    local_clips.push_back(local_clip);
                active_clips = &local_clips;
                ++frame.mask_depth;
            }
            else if (mask != nullptr && mask->ActiveInHierarchy() && mask->enabled_mask &&
                (mask->mask_mode == UIMaskComponent::Image ||
                    mask->mask_mode == UIMaskComponent::ObjectAlpha ||
                    mask->mask_mode == UIMaskComponent::ObjectLuma))
            {
                if (inherited_masks != nullptr)
                {
                    const std::size_t inherited_count = (std::min)(
                        inherited_masks->size(), std::size_t{ 4 });
                    local_masks.assign(inherited_masks->begin(),
                        inherited_masks->begin() + inherited_count);
                }
                if (mask->mask_mode == UIMaskComponent::Image)
                {
                    D3D12UIMask image_mask = add_mask_texture(*mask);
                    image_mask.invert = mask->invert;
                    configure_mask_transform(image_mask, *rect, canvas_scale);
                    if (!image_mask.texture_key.empty() && local_masks.size() < 4)
                        local_masks.push_back(std::move(image_mask));
                }
                else if (mask->mask_object.IsAssigned())
                {
                    ReplayEngine::Scene::Scene* scene = object.GetScene();
                    GameObject* matte_object = scene != nullptr
                        ? scene->FindGameObjectByID(mask->mask_object.object) : nullptr;
                    UIImageComponent* matte_image = matte_object != nullptr
                        ? matte_object->GetComponent<UIImageComponent>() : nullptr;
                    RectTransformComponent* matte_rect = matte_object != nullptr
                        ? matte_object->GetComponent<RectTransformComponent>() : nullptr;
                    if (matte_image != nullptr && matte_rect != nullptr)
                    {
                        DirectX::XMFLOAT4 matte_uv{};
                        bool matte_rotated = false;
                        D3D12UIMask object_mask{};
                        object_mask.texture_key = add_image_texture(
                            *matte_image, matte_uv, matte_rotated, nullptr);
                        object_mask.uv = matte_uv;
                        object_mask.luma = mask->mask_mode == UIMaskComponent::ObjectLuma;
                        object_mask.invert = mask->invert;
                        object_mask.rotated = matte_rotated;
                        configure_mask_transform(object_mask, *matte_rect, canvas_scale);
                        if (!object_mask.texture_key.empty() && local_masks.size() < 4)
                            local_masks.push_back(std::move(object_mask));
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
                    RectTransformComponent* matte_rect = matte_object != nullptr
                        ? matte_object->GetComponent<RectTransformComponent>() : nullptr;
                    if (matte_image == nullptr || matte_rect == nullptr) continue;
                    DirectX::XMFLOAT4 matte_uv{};
                    bool matte_rotated = false;
                    D3D12UIMask extra_mask{};
                    extra_mask.texture_key = add_image_texture(
                        *matte_image, matte_uv, matte_rotated, nullptr);
                    extra_mask.uv = matte_uv;
                    extra_mask.luma = mask->mask_mode == UIMaskComponent::ObjectLuma;
                    extra_mask.invert = mask->invert;
                    extra_mask.rotated = matte_rotated;
                    configure_mask_transform(extra_mask, *matte_rect, canvas_scale);
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
        if (rect != nullptr && render_self && !RectIsEmpty(active_scissor != nullptr
            ? *active_scissor : D3D12_RECT{ 0, 0,
                static_cast<LONG>(frame.target_width), static_cast<LONG>(frame.target_height) }))
        {
            if (UIShapeComponent* shape = object.GetComponent<UIShapeComponent>())
            {
                if (shape->ActiveInHierarchy())
                    append_shape(*shape, rect->ResolvedRect(), rect->ResolvedMatrix(),
                        canvas_scale, local_opacity, active_scissor,
                        active_clips, active_masks);
            }

            if (UIImageComponent* image = object.GetComponent<UIImageComponent>())
            {
                if (image->ActiveInHierarchy() && image->opacity > 0.0f &&
                    image->fill_amount > 0.0f)
                {
                DirectX::XMFLOAT4 uv{};
                bool rotated = false;
                std::vector<DirectX::XMFLOAT2> atlas_path_points;
                const std::string texture_key = add_image_texture(*image, uv, rotated,
                    &atlas_path_points);
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
                        MultiplyColor(image->color,
                            local_opacity * Clamp01(image->opacity)), blend,
                        active_scissor, active_clips, active_masks);
                    batch.constants.fill_color_2 =
                        MultiplyColor(image->fill_color_2, local_opacity);
                    batch.constants.fill_stops = { 1.0f, -1.0f, -1.0f, 0.0f };
                    batch.constants.stroke_color_2 = image->stroke_color_2;
                    batch.constants.stroke_parameters.x =
                        static_cast<float>(image->stroke_mode);
                    batch.constants.mode.x = 0.0f;
                    batch.constants.fill_parameters = {
                        DirectX::XMConvertToRadians(image->fill_angle),
                        image->fill_center.x, image->fill_center.y,
                        static_cast<float>(image->fill_mode) };
                    const DirectX::XMFLOAT4 image_color =
                        MultiplyColor(image->color,
                            local_opacity * Clamp01(image->opacity));
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
                        else if (atlas_path_points.size() >= 3)
                        {
                            UIShapeImageComponent atlas_shape;
                            atlas_shape.path_closed = true;
                            atlas_shape.path_points = std::move(atlas_path_points);
                            append_shape_image(batch, draw_rect, rect->ResolvedMatrix(), uv,
                                image_color, canvas_scale, rotated, atlas_shape);
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
            }

            if (UITextComponent* text = object.GetComponent<UITextComponent>())
            {
                if (text->ActiveInHierarchy() && text->opacity > 0.0f)
                {
                    text->UpdateNumberDisplay(scene);
                    text->font_size = (std::max)(1.0f, text->font_size);
                    ui_font_atlas.BuildGlyphs(*text, rect->ResolvedRect().z,
                        rect->ResolvedRect().w, &asset_database);
                    UIInputFieldComponent* input =
                        object.GetComponent<UIInputFieldComponent>();
                    const UISelectableComponent* selectable =
                        object.GetComponent<UISelectableComponent>();
                    const bool focused_input = input != nullptr &&
                        selectable != nullptr && selectable->focused &&
                        selectable->ActiveInHierarchy();
                    if (focused_input && input->HasSelection() && !input->password)
                    {
                        D3D12UIBatch& selection = make_batch("__dx12_white",
                            MultiplyColor(input->selection_color, local_opacity),
                            D3D12UIBlendMode::Alpha, active_scissor,
                            active_clips, active_masks);
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
                                    (std::max)(glyph.advance, glyph.size.x),
                                    glyph.size.y },
                                rect->ResolvedMatrix(), { 0, 0, 1, 1 },
                                MultiplyColor(input->selection_color, local_opacity),
                                canvas_scale, false);
                        }
                    }

                    std::string atlas_key;
                    if (add_font_source(atlas_key))
                    {
                        std::vector<const UITextAnimatorComponent*> animators;
                        GatherTextAnimators(object, animators);
                        const float character_count = (std::max)(1.0f,
                            static_cast<float>(text->DisplayCharacterCount()));
                        const float outline_extent = (std::min)(
                            (std::max)(0.0f, text->outline_width),
                            static_cast<float>(
                                ReplayEngine::UI::FontAtlas::AtlasPaddingPixels()));
                        const float shadow_extent_x = (std::max)(0.0f,
                            std::fabs(text->shadow_offset.x));
                        const float shadow_extent_y = (std::max)(0.0f,
                            std::fabs(text->shadow_offset.y));
                        const bool has_text_effect = outline_extent > 0.0f ||
                            text->shadow_color.w > 0.0f;
                        const DirectX::XMFLOAT4 base_color = MultiplyColor(
                            text->color, local_opacity * Clamp01(text->opacity));

                        for (const UITextComponent::GlyphQuad& glyph : text->Glyphs())
                        {
                            D3D12UIBatch& batch = make_batch(atlas_key, base_color,
                                D3D12UIBlendMode::Alpha, active_scissor,
                                active_clips, active_masks);
                            batch.constants.mode = { 0, 1, outline_extent, 0 };
                            batch.constants.outline_color = text->outline_color;
                            batch.constants.shadow_offset = {
                                text->shadow_offset.x, text->shadow_offset.y, 0, 0 };
                            batch.constants.shadow_color = text->shadow_color;

                            DirectX::XMFLOAT4 glyph_rect{
                                rect->ResolvedRect().x + glyph.position.x,
                                rect->ResolvedRect().y + glyph.position.y,
                                glyph.size.x, glyph.size.y };
                            DirectX::XMFLOAT4 color{
                                base_color.x * glyph.rich_color.x,
                                base_color.y * glyph.rich_color.y,
                                base_color.z * glyph.rich_color.z,
                                base_color.w * glyph.rich_color.w };
                            DirectX::XMFLOAT2 glyph_scale{
                                glyph.rich_bold ? 1.035f : 1.0f,
                                glyph.rich_bold ? 1.035f : 1.0f };
                            DirectX::XMFLOAT2 anchor{ 0.5f, 0.5f };
                            float rotation = 0.0f;
                            const float italic_shear =
                                glyph.rich_italic ? -0.18f : 0.0f;

                            for (const UITextAnimatorComponent* animator : animators)
                            {
                                const float position =
                                    (static_cast<float>(glyph.character_index) + 0.5f) /
                                    character_count;
                                const float influence = TextAnimatorInfluence(
                                    *animator, position);
                                if (influence <= 0.0f) continue;
                                glyph_rect.x += animator->position_offset.x * influence;
                                glyph_rect.y += animator->position_offset.y * influence;
                                glyph_rect.x += animator->character_spacing *
                                    static_cast<float>(glyph.character_index) * influence;
                                glyph_rect.x += animator->random_position.x *
                                    RandomTextGlyphSigned(animator->random_seed,
                                        glyph.character_index, 11u) * influence;
                                glyph_rect.y += animator->random_position.y *
                                    RandomTextGlyphSigned(animator->random_seed,
                                        glyph.character_index, 23u) * influence;
                                rotation += animator->rotation * influence;
                                rotation += animator->random_rotation *
                                    RandomTextGlyphSigned(animator->random_seed,
                                        glyph.character_index, 37u) * influence;
                                glyph_scale.x *= Lerp(
                                    1.0f, animator->scale.x, influence);
                                glyph_scale.y *= Lerp(
                                    1.0f, animator->scale.y, influence);
                                color = LerpColor(color,
                                    { base_color.x * glyph.rich_color.x * animator->color.x,
                                      base_color.y * glyph.rich_color.y * animator->color.y,
                                      base_color.z * glyph.rich_color.z * animator->color.z,
                                      base_color.w * glyph.rich_color.w * animator->color.w },
                                    influence);
                                color.w *= Lerp(1.0f, animator->opacity, influence);
                                anchor = TextAnimatorAnchor(animator->anchor);
                            }

                            const DirectX::XMFLOAT4 glyph_uv_bounds{
                                glyph.uv.x, glyph.uv.y,
                                glyph.uv.x + glyph.uv.z,
                                glyph.uv.y + glyph.uv.w };
                            DirectX::XMFLOAT4 draw_rect = glyph_rect;
                            DirectX::XMFLOAT4 draw_uv = glyph.uv;
                            if (has_text_effect)
                            {
                                // SDFの外枠と影をクアッド内へ収め、字形境界は元UVで判定する。
                                const float safe_canvas_scale = (std::max)(
                                    std::fabs(canvas_scale), 0.0001f);
                                const float safe_scale_x = (std::max)(
                                    std::fabs(glyph_scale.x), 0.0001f);
                                const float safe_scale_y = (std::max)(
                                    std::fabs(glyph_scale.y), 0.0001f);
                                const float expand_x =
                                    (outline_extent + shadow_extent_x) /
                                    (safe_canvas_scale * safe_scale_x);
                                const float expand_y =
                                    (outline_extent + shadow_extent_y) /
                                    (safe_canvas_scale * safe_scale_y);
                                draw_rect.x -= expand_x;
                                draw_rect.y -= expand_y;
                                draw_rect.z += expand_x * 2.0f;
                                draw_rect.w += expand_y * 2.0f;
                                const float width = (std::max)(
                                    std::fabs(glyph_rect.z), 0.0001f);
                                const float height = (std::max)(
                                    std::fabs(glyph_rect.w), 0.0001f);
                                const float uv_expand_x = expand_x / width * glyph.uv.z;
                                const float uv_expand_y = expand_y / height * glyph.uv.w;
                                draw_uv.x -= uv_expand_x;
                                draw_uv.y -= uv_expand_y;
                                draw_uv.z += uv_expand_x * 2.0f;
                                draw_uv.w += uv_expand_y * 2.0f;
                            }
                            append_quad_local_with_bounds(batch, draw_rect,
                                rect->ResolvedMatrix(), draw_uv, color, canvas_scale,
                                glyph_scale, rotation, anchor, italic_shear,
                                glyph_uv_bounds);
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
                            float caret_y = text_rect.y + (std::max)(0.0f,
                                (text_rect.w - text->font_size) * 0.5f);
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
                                D3D12UIBlendMode::Alpha, active_scissor,
                                active_clips, active_masks);
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
                        D3D12UIBlendMode::Alpha, active_scissor,
                        active_clips, active_masks);
                    append_border(outline, rect->ResolvedRect(), rect->ResolvedMatrix(),
                        width / (std::max)(0.0001f, canvas_scale), color, canvas_scale);
                }
            }
            // 移行前と同じ順序でSelfの一部として描き、子要素のEffectへ混入させない。
            append_scrollbars(object, *rect, canvas_scale, local_opacity,
                active_scissor, active_clips, active_masks);
        }

        // Selfは子へ入る前に閉じ、Subtreeだけ同じGroupを子孫の末尾まで維持する。
        if (owned_effect_group >= 0 && effect_stack != nullptr &&
            effect_stack->target_scope == ReplayEngine::Components::UIEffectStackComponent::Self)
        {
            finalize_effect_group(owned_effect_group, *effect_stack);
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
                active_scissor, active_clips, active_masks, depth + 1);
        }

        if (owned_effect_group >= 0 && !effect_group_restored)
        {
            finalize_effect_group(owned_effect_group, *effect_stack);
            active_effect_group = inherited_effect_group;
        }
    };

    for (GameObject* canvas_object : canvases)
    {
        if (canvas_object == nullptr) continue;
        CanvasComponent* canvas = canvas_object->GetComponent<CanvasComponent>();
        if (canvas == nullptr || !canvas->ActiveInHierarchy()) continue;
        const float scale = (std::max)(0.0001f,
            ReplayEngine::UI::UILayout::CanvasScale(*canvas,
                viewport.logical_width, viewport.logical_height));
        world_space_canvas = canvas->render_mode == CanvasComponent::WorldSpace;
        if (world_space_canvas)
        {
            const float reference_width = canvas->reference_resolution.x > 0.0f
                ? canvas->reference_resolution.x : 1920.0f;
            const float reference_height = canvas->reference_resolution.y > 0.0f
                ? canvas->reference_resolution.y : 1080.0f;
            world_canvas_parameters = {
                1.0f, reference_width / reference_height, 1.0f, 0.0f };
            DirectX::XMStoreFloat4x4(&world_canvas_matrix,
                canvas_object->GetTransform().WorldMatrix());
        }
        else
        {
            world_canvas_parameters = { 0, 0, 0, 0 };
            DirectX::XMStoreFloat4x4(&world_canvas_matrix,
                DirectX::XMMatrixIdentity());
        }
        render_object(*canvas_object, scale, Clamp01(canvas->opacity),
            nullptr, nullptr, nullptr, 0);
    }
    world_space_canvas = false;
    frame.draw_commands = static_cast<std::uint32_t>(frame.batches.size());
    for (const D3D12UIBatch& batch : frame.batches)
        frame.vertex_count += static_cast<std::uint32_t>(batch.vertices.size());
    frame.texture_count = static_cast<std::uint32_t>(texture_keys.size() + frame.font_atlases.size());
    return true;
}

bool framework::build_dx12_scene_effects(
    ReplayEngine::Rendering::DX12::D3D12SceneEffectSubmission& submission)
{
    using namespace ReplayEngine;
    using namespace ReplayEngine::Rendering::DX12;
    using ReplayEngine::UI::UIEffectKind;
    using ReplayEngine::UI::UIEffectRegionData;
    using ReplayEngine::UI::UIEffectRegionScope;
    using ReplayEngine::UI::UIEffectRegionShape;

    submission.Clear();
    std::unordered_set<std::string> texture_keys;
    const auto register_effect_texture = [&](const Assets::AssetRecord* record)
        -> std::string
    {
        if (record == nullptr || record->kind != AssetKind::Image) return {};
        const std::filesystem::path path = content_path(record->cache_path.empty()
            ? record->source_path : record->cache_path).lexically_normal();
        const std::string key = path.generic_string();
        if (!key.empty() && texture_keys.insert(key).second)
        {
            D3D12StaticTextureSource source;
            source.key = key;
            source.source_path = path;
            submission.texture_sources.push_back(std::move(source));
        }
        return key;
    };
    const auto texture_for_guid = [&](const std::string& guid)
    {
        return guid.empty() || guid == "__runtime_ui_matte"
            ? std::string{} : register_effect_texture(asset_database.FindByGuid(guid));
    };
    const auto fill_effects = [&](const std::vector<UI::UIEffect>& effects,
        const UI::UIEffectRegion& region, std::uint64_t owner_id,
        std::vector<D3D12UIEffectCommand>& output)
    {
        output.clear();
        output.reserve((std::min)(effects.size(), static_cast<std::size_t>(32)));
        for (const UI::UIEffect& effect : effects)
        {
            if (!effect.enabled || output.size() >= 32) continue;
            const int kind = effect.kind;
            if (kind < static_cast<int>(UIEffectKind::Blur) ||
                kind >= static_cast<int>(UIEffectKind::Count))
                continue;
            D3D12UIEffectCommand command{};
            command.kind = static_cast<std::uint32_t>(kind);
            command.radius = (std::max)(0.0f, effect.radius);
            command.intensity = (std::max)(0.0f, effect.intensity);
            command.threshold = effect.threshold;
            command.amount = effect.amount;
            command.angle = effect.angle;
            command.progress = effect.progress;
            command.softness = effect.softness;
            command.speed = effect.speed;
            command.seed = effect.seed;
            command.time = shader_composer_time;
            command.waveform = effect.waveform;
            command.direction = effect.direction;
            command.color = effect.color;
            command.color_2 = effect.color_2;
            command.color_3 = effect.color_3;
            command.color_4 = effect.color_4;
            command.color_stops = { effect.color_stop_2,
                effect.color_stop_3, effect.color_stop_4, 0.0f };
            std::string mask_guid = effect.mask;
            const UIEffectKind effect_kind = static_cast<UIEffectKind>(kind);
            if (effect_kind == UIEffectKind::BrushStroke && effect.brush_atlas_enabled)
            {
                if (mask_guid.empty())
                {
                    const Assets::AssetRecord* atlas = asset_database.FindByPath(
                        std::filesystem::path("resources") / "BrushMasks" /
                        "brush_masks_atlas.png");
                    if (atlas != nullptr) mask_guid = atlas->guid;
                }
                command.brush_atlas = true;
                command.brush_pattern_settings = {
                    static_cast<float>((std::max)(0, (std::min)(15,
                        effect.brush_pattern_index))),
                    static_cast<float>((std::max)(0, (std::min)(1,
                        effect.brush_pattern_mode))), 0.0f, 0.0f };
                for (std::size_t group = 0;
                    group < command.brush_pattern_weights.size(); ++group)
                {
                    const std::size_t first = group * 4;
                    command.brush_pattern_weights[group] = {
                        (std::max)(0.0f, effect.brush_pattern_weights[first]),
                        (std::max)(0.0f, effect.brush_pattern_weights[first + 1]),
                        (std::max)(0.0f, effect.brush_pattern_weights[first + 2]),
                        (std::max)(0.0f, effect.brush_pattern_weights[first + 3]) };
                }
            }
            command.auxiliary_texture_key = texture_for_guid(mask_guid);
            command.temporal = effect_kind == UIEffectKind::MotionBlur ||
                effect_kind == UIEffectKind::Echo ||
                effect_kind == UIEffectKind::FeedbackZoom;
            if (command.temporal && owner_id != 0) command.history_key = owner_id;
            command.region_enabled = region.enabled &&
                (region.scope == static_cast<int>(UIEffectRegionScope::AllEffects) ||
                    effect.region_enabled);
            if (command.region_enabled)
            {
                const auto fill_region = [](const UIEffectRegionData& source,
                    DirectX::XMFLOAT4& params, DirectX::XMFLOAT4& settings)
                {
                    const int shape = (std::max)(0, (std::min)(3, source.shape));
                    params = { source.center.x, source.center.y,
                        (std::max)(source.size.x, 0.0001f),
                        (std::max)(source.size.y, 0.0001f) };
                    settings = { source.rotation, (std::max)(0.0f, source.feather),
                        Clamp01(source.strength),
                        static_cast<float>(shape) + (source.invert ? 4.0f : 0.0f) };
                };
                const auto fill_path = [&](const UIEffectRegionData& source,
                    std::size_t slot)
                {
                    if (source.shape != static_cast<int>(UIEffectRegionShape::Freeform) ||
                        slot >= command.effect_region_path_points.size()) return;
                    const std::size_t count = (std::min)(source.path_points.size(),
                        command.effect_region_path_points[slot].size());
                    command.effect_region_path_counts[slot].x =
                        static_cast<float>(count);
                    for (std::size_t point = 0; point < count; ++point)
                    {
                        command.effect_region_path_points[slot][point] = {
                            source.path_points[point].x, source.path_points[point].y,
                            0.0f, 0.0f };
                    }
                };
                fill_region(region, command.effect_region_params,
                    command.effect_region_settings);
                fill_path(region, 0);
                std::size_t region_count = 1;
                for (const UIEffectRegionData& additional : region.additional)
                {
                    if (region_count > command.effect_region_extra_params.size() ||
                        !additional.enabled ||
                        (additional.scope == static_cast<int>(
                            UIEffectRegionScope::SelectedEffects) && !effect.region_enabled))
                        continue;
                    const std::size_t extra_slot = region_count - 1;
                    fill_region(additional,
                        command.effect_region_extra_params[extra_slot],
                        command.effect_region_extra_settings[extra_slot]);
                    fill_path(additional, region_count);
                    ++region_count;
                }
                command.effect_region_count.x = static_cast<float>(region_count);
                if (region.shape == static_cast<int>(UIEffectRegionShape::TextureMask))
                    command.region_mask_texture_key = texture_for_guid(region.mask);
            }
            output.push_back(std::move(command));
        }
        return !output.empty();
    };

    Scene::Scene& scene = active_object_scene();
    for (std::size_t object_index = 0; object_index < scene.GameObjectCount(); ++object_index)
    {
        Core::GameObject* object = scene.GameObjectAt(object_index);
        if (object == nullptr || object->PendingDestroy() || !object->ActiveInHierarchy())
            continue;
        if (auto* model = object->GetComponent<Components::ModelEffectStackComponent>())
        {
            if (model->ActiveInHierarchy() && model->enabled &&
                model->HasActiveEffects(&asset_database))
            {
                D3D12ModelEffectStackSubmission stack{};
                stack.owner_id = object->ID().Value();
                stack.depth_mode = model->depth_mode;
                stack.scissor = { 0, 0, static_cast<LONG>(dx12_device_context.Width()),
                    static_cast<LONG>(dx12_device_context.Height()) };
                stack.scissor_enabled = true;
                if (fill_effects(model->EffectiveEffects(&asset_database),
                    model->effect_region, stack.owner_id, stack.effects))
                    submission.model_effects.push_back(std::move(stack));
            }
        }
        if (auto* screen = object->GetComponent<Components::ScreenEffectStackComponent>())
        {
            if (screen->ActiveInHierarchy() && screen->enabled &&
                screen->HasActiveEffects(&asset_database))
            {
                D3D12ScreenEffectStackSubmission stack{};
                stack.owner_id = object->ID().Value();
                stack.apply_stage = screen->apply_stage;
                stack.target_mode = screen->target_mode;
                const int layer = (std::max)(0, (std::min)(31,
                    screen->target_rendering_layer));
                stack.target_rendering_layer_mask = 1u << static_cast<std::uint32_t>(layer);
                if (fill_effects(screen->EffectiveEffects(&asset_database),
                    screen->effect_region, stack.owner_id, stack.effects))
                    submission.screen_effects.push_back(std::move(stack));
            }
        }
    }
    return true;
}

