#include "framework.h"

#include "../../../RePlayEngine/Components/Rendering/ModelEffectStackComponent.h"
#include "../../mesh/gltf_model.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace
{
    struct ModelScreenRect
    {
        LONG left = 0;
        LONG top = 0;
        LONG right = 0;
        LONG bottom = 0;

        bool Valid() const noexcept { return right > left && bottom > top; }
        std::uint32_t Width() const noexcept
        {
            return Valid() ? static_cast<std::uint32_t>(right - left) : 0u;
        }
        std::uint32_t Height() const noexcept
        {
            return Valid() ? static_cast<std::uint32_t>(bottom - top) : 0u;
        }
    };

    bool finite_bounds(const DirectX::XMFLOAT3& minimum,
        const DirectX::XMFLOAT3& maximum) noexcept
    {
        return std::isfinite(minimum.x) && std::isfinite(minimum.y) &&
            std::isfinite(minimum.z) && std::isfinite(maximum.x) &&
            std::isfinite(maximum.y) && std::isfinite(maximum.z) &&
            minimum.x <= maximum.x && minimum.y <= maximum.y &&
            minimum.z <= maximum.z;
    }

    ModelScreenRect project_bounds(const DirectX::XMFLOAT3& minimum,
        const DirectX::XMFLOAT3& maximum,
        const DirectX::XMFLOAT4X4& world,
        const DirectX::XMMATRIX& view,
        const DirectX::XMMATRIX& projection,
        const D3D11_VIEWPORT& viewport,
        const DirectX::XMFLOAT4& expansion,
        float max_bleed_pixels)
    {
        ModelScreenRect result{};
        if (!finite_bounds(minimum, maximum) || viewport.Width <= 0.0f ||
            viewport.Height <= 0.0f)
        {
            return result;
        }

        const DirectX::XMMATRIX wvp =
            DirectX::XMLoadFloat4x4(&world) * view * projection;
        const std::array<DirectX::XMFLOAT3, 8> corners{
            DirectX::XMFLOAT3{ minimum.x, minimum.y, minimum.z },
            { maximum.x, minimum.y, minimum.z },
            { minimum.x, maximum.y, minimum.z },
            { maximum.x, maximum.y, minimum.z },
            { minimum.x, minimum.y, maximum.z },
            { maximum.x, minimum.y, maximum.z },
            { minimum.x, maximum.y, maximum.z },
            { maximum.x, maximum.y, maximum.z },
        };

        float min_x = (std::numeric_limits<float>::max)();
        float min_y = (std::numeric_limits<float>::max)();
        float max_x = -(std::numeric_limits<float>::max)();
        float max_y = -(std::numeric_limits<float>::max)();
        bool any_visible = false;
        bool crosses_near_plane = false;

        for (const DirectX::XMFLOAT3& corner : corners)
        {
            const DirectX::XMVECTOR point = DirectX::XMVectorSet(
                corner.x, corner.y, corner.z, 1.0f);
            const DirectX::XMVECTOR clip = DirectX::XMVector4Transform(point, wvp);
            const float w = DirectX::XMVectorGetW(clip);
            if (!std::isfinite(w) || w <= 1.0e-5f)
            {
                crosses_near_plane = true;
                continue;
            }

            const float ndc_x = DirectX::XMVectorGetX(clip) / w;
            const float ndc_y = DirectX::XMVectorGetY(clip) / w;
            if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y)) continue;

            const float x = viewport.TopLeftX +
                (ndc_x * 0.5f + 0.5f) * viewport.Width;
            const float y = viewport.TopLeftY +
                (-ndc_y * 0.5f + 0.5f) * viewport.Height;
            min_x = (std::min)(min_x, x);
            min_y = (std::min)(min_y, y);
            max_x = (std::max)(max_x, x);
            max_y = (std::max)(max_y, y);
            any_visible = true;
        }

        if (!any_visible) return result;

        // Near plane を横切る AABB は角だけの射影では過小評価しやすい。
        // このケースだけ Camera viewport 全体を保守的な矩形にし、欠けを優先して防ぐ。
        if (crosses_near_plane)
        {
            min_x = viewport.TopLeftX;
            min_y = viewport.TopLeftY;
            max_x = viewport.TopLeftX + viewport.Width;
            max_y = viewport.TopLeftY + viewport.Height;
        }

        const float bleed_limit = (std::max)(0.0f, max_bleed_pixels);
        const float bleed_left = (std::min)((std::max)(0.0f, expansion.x), bleed_limit);
        const float bleed_top = (std::min)((std::max)(0.0f, expansion.y), bleed_limit);
        const float bleed_right = (std::min)((std::max)(0.0f, expansion.z), bleed_limit);
        const float bleed_bottom = (std::min)((std::max)(0.0f, expansion.w), bleed_limit);

        const float viewport_right = viewport.TopLeftX + viewport.Width;
        const float viewport_bottom = viewport.TopLeftY + viewport.Height;
        min_x = (std::max)(viewport.TopLeftX, min_x - bleed_left);
        min_y = (std::max)(viewport.TopLeftY, min_y - bleed_top);
        max_x = (std::min)(viewport_right, max_x + bleed_right);
        max_y = (std::min)(viewport_bottom, max_y + bleed_bottom);

        result.left = static_cast<LONG>(std::floor(min_x));
        result.top = static_cast<LONG>(std::floor(min_y));
        result.right = static_cast<LONG>(std::ceil(max_x));
        result.bottom = static_cast<LONG>(std::ceil(max_y));
        if (!result.Valid()) return {};
        return result;
    }

    ModelScreenRect expand_rect(const ModelScreenRect& base,
        const D3D11_VIEWPORT& viewport, const DirectX::XMFLOAT4& expansion,
        float max_bleed_pixels) noexcept
    {
        if (!base.Valid()) return {};
        const float bleed_limit = (std::max)(0.0f, max_bleed_pixels);
        const float left_bleed = (std::min)((std::max)(0.0f, expansion.x), bleed_limit);
        const float top_bleed = (std::min)((std::max)(0.0f, expansion.y), bleed_limit);
        const float right_bleed = (std::min)((std::max)(0.0f, expansion.z), bleed_limit);
        const float bottom_bleed = (std::min)((std::max)(0.0f, expansion.w), bleed_limit);
        const float viewport_right = viewport.TopLeftX + viewport.Width;
        const float viewport_bottom = viewport.TopLeftY + viewport.Height;

        ModelScreenRect result{};
        result.left = static_cast<LONG>(std::floor((std::max)(viewport.TopLeftX,
            static_cast<float>(base.left) - left_bleed)));
        result.top = static_cast<LONG>(std::floor((std::max)(viewport.TopLeftY,
            static_cast<float>(base.top) - top_bleed)));
        result.right = static_cast<LONG>(std::ceil((std::min)(viewport_right,
            static_cast<float>(base.right) + right_bleed)));
        result.bottom = static_cast<LONG>(std::ceil((std::min)(viewport_bottom,
            static_cast<float>(base.bottom) + bottom_bleed)));
        return result.Valid() ? result : ModelScreenRect{};
    }
}

void framework::draw_model_effect_stacks(const D3D11_VIEWPORT& camera_viewport)
{
    using ReplayEngine::Components::ModelEffectStackComponent;
    using ReplayEngine::Core::GameObject;
    using ReplayEngine::Rendering::RenderItem;

    if (object_render_items.Empty() || framebuffers[0] == nullptr ||
        bit_block_transfer == nullptr || immediate_context == nullptr)
    {
        return;
    }

    const std::uint32_t full_width = static_cast<std::uint32_t>(
        (std::max)(1.0f, framebuffers[0]->viewport.Width));
    const std::uint32_t full_height = static_cast<std::uint32_t>(
        (std::max)(1.0f, framebuffers[0]->viewport.Height));
    std::unordered_set<ReplayEngine::Core::ObjectID> rendered_owners;

    for (const RenderItem& item : object_render_items.Items())
    {
        if (!item.owner.Valid() || !rendered_owners.insert(item.owner).second) continue;

        GameObject* object = active_object_scene().FindGameObjectByID(item.owner);
        ModelEffectStackComponent* effect = object != nullptr
            ? object->GetComponent<ModelEffectStackComponent>() : nullptr;
        if (effect == nullptr || !effect->ActiveInHierarchy() || !effect->enabled ||
            !effect->HasActiveEffects(&asset_database))
        {
            continue;
        }

        model_effect_screen_rect base_bounds{};
        model_effect_screen_rect expanded_bounds{};
        if (!compute_model_effect_screen_rect(item.owner, camera_viewport,
            base_bounds, expanded_bounds))
        {
            continue;
        }
        ModelScreenRect rect{};
        rect.left = expanded_bounds.left;
        rect.top = expanded_bounds.top;
        rect.right = expanded_bounds.right;
        rect.bottom = expanded_bounds.bottom;
        if (!rect.Valid()) continue;

        ReplayEngine::UI::UIRenderTarget* full_target =
            scene_effect_targets.Acquire(full_width, full_height,
                DXGI_FORMAT_R16G16B16A16_FLOAT);
        ReplayEngine::UI::UIRenderTarget* cropped_target =
            scene_effect_targets.Acquire(rect.Width(), rect.Height(),
                DXGI_FORMAT_R16G16B16A16_FLOAT);
        if (full_target == nullptr || cropped_target == nullptr) continue;

        ID3D11ShaderResourceView* null_srvs[2]{};
        immediate_context->PSSetShaderResources(0, 2, null_srvs);
        ID3D11DepthStencilView* source_depth =
            effect->depth_mode == ModelEffectStackComponent::PreserveDepth
                ? framebuffers[0]->depth_stencil_view.Get() : nullptr;
        immediate_context->OMSetRenderTargets(1, full_target->rtv.GetAddressOf(),
            source_depth);
        immediate_context->RSSetViewports(1, &camera_viewport);
        const FLOAT transparent[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
        immediate_context->ClearRenderTargetView(full_target->rtv.Get(), transparent);
        immediate_context->OMSetBlendState(
            blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xffffffff);
        immediate_context->OMSetDepthStencilState(
            depth_stencil_states[(size_t)(
                effect->depth_mode == ModelEffectStackComponent::PreserveDepth
                    ? DEPTH_STATE::ZT_ON_ZW_OFF : DEPTH_STATE::ZT_OFF_ZW_OFF)].Get(), 0);
        immediate_context->RSSetState(
            rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());

        // 既存 Mesh/Material/Animation の Forward 描画を owner だけに限定して再利用する。
        // 元の scene 描画は残るので Effect 無し Object の経路は一切変わらない。
        draw_object_scene_meshes(nullptr, false, false, item.owner, false);

        immediate_context->OMSetRenderTargets(0, nullptr, nullptr);
        D3D11_BOX source_box{};
        source_box.left = static_cast<UINT>((std::max)(LONG{ 0 }, rect.left));
        source_box.top = static_cast<UINT>((std::max)(LONG{ 0 }, rect.top));
        source_box.front = 0;
        source_box.right = source_box.left + rect.Width();
        source_box.bottom = source_box.top + rect.Height();
        source_box.back = 1;
        immediate_context->CopySubresourceRegion(cropped_target->texture.Get(), 0,
            0, 0, 0, full_target->texture.Get(), 0, &source_box);

        ReplayEngine::UI::UIRenderTarget* effected = apply_scene_effect_chain(
            cropped_target->srv.Get(), effect->EffectiveEffects(&asset_database), rect.Width(), rect.Height(),
            DXGI_FORMAT_R16G16B16A16_FLOAT, shader_composer_time,
            static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(effect)),
            &effect->effect_region);
        ID3D11ShaderResourceView* result =
            effected != nullptr ? effected->srv.Get() : cropped_target->srv.Get();

        // Opaque 本体は alpha=1 なので既存のモデル色を Effect 結果で置き換える。
        // Glow/Blur のはみ出しは意味のある depth を持てないため、
        // PreserveDepth でも「本体の可視判定は depth、bleed は depth なし」で合成する。
        immediate_context->OMSetRenderTargets(1,
            framebuffers[0]->render_target_view.GetAddressOf(),
            framebuffers[0]->depth_stencil_view.Get());
        D3D11_VIEWPORT composite_viewport{};
        composite_viewport.TopLeftX = static_cast<float>(rect.left);
        composite_viewport.TopLeftY = static_cast<float>(rect.top);
        composite_viewport.Width = static_cast<float>(rect.Width());
        composite_viewport.Height = static_cast<float>(rect.Height());
        composite_viewport.MinDepth = 0.0f;
        composite_viewport.MaxDepth = 1.0f;
        immediate_context->RSSetViewports(1, &composite_viewport);
        immediate_context->OMSetDepthStencilState(
            depth_stencil_states[(size_t)DEPTH_STATE::ZT_OFF_ZW_OFF].Get(), 0);
        immediate_context->OMSetBlendState(
            blend_states[(size_t)BLEND_STATE::ALPHA].Get(), nullptr, 0xffffffff);
        immediate_context->RSSetState(
            rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());
        ID3D11SamplerState* sampler =
            sampler_states[(size_t)SAMPLER_STATE::LINEAR].Get();
        immediate_context->PSSetSamplers(0, 1, &sampler);
        bit_block_transfer->blit(immediate_context.Get(), &result, 0, 1);
        ID3D11ShaderResourceView* null_result = nullptr;
        immediate_context->PSSetShaderResources(0, 1, &null_result);

        // 後続の transparent/line/particle が従来の target と viewport を使えるよう戻す。
        immediate_context->OMSetRenderTargets(1,
            framebuffers[0]->render_target_view.GetAddressOf(),
            framebuffers[0]->depth_stencil_view.Get());
        immediate_context->RSSetViewports(1, &camera_viewport);
    }
}

bool framework::compute_model_effect_screen_rect(ReplayEngine::Core::ObjectID owner,
    const D3D11_VIEWPORT& camera_viewport, model_effect_screen_rect& out_base,
    model_effect_screen_rect& out_expanded)
{
    using ReplayEngine::Components::ModelEffectStackComponent;
    using ReplayEngine::Rendering::RenderItem;

    out_base = {};
    out_expanded = {};
    if (!owner.Valid()) return false;

    ReplayEngine::Core::GameObject* object = active_object_scene().FindGameObjectByID(owner);
    ModelEffectStackComponent* effect = object != nullptr
        ? object->GetComponent<ModelEffectStackComponent>() : nullptr;
    if (effect == nullptr || !effect->ActiveInHierarchy() || !effect->enabled ||
        !effect->HasActiveEffects(&asset_database))
    {
        return false;
    }

    // 半透明は、元の scene 色と「モデルだけを描いた RT」を単純 alpha 合成すると
    // 本体が二重に混ざるため Effect の対象から外す。影の面消しも同じ条件で外す。
    for (const RenderItem& candidate : object_render_items.Items())
    {
        if (candidate.owner != owner) continue;

        const RenderItem resolved = resolve_render_item_material(candidate);
        if (resolved.material_base_color.w < 0.999f || resolved.tint.w < 0.999f ||
            resolved.legacy_tint.w < 0.999f)
        {
            return false;
        }

        if (const ReplayEngine::Rendering::MaterialAsset* material =
            resolve_object_material(candidate.material_asset))
        {
            if (material->alpha_mode == ReplayEngine::Rendering::MaterialAlphaMode::Blend)
                return false;
        }

        if (candidate.material_asset.empty())
        {
            if (skinned_mesh* mesh = resolve_object_mesh(candidate.mesh_asset);
                mesh != nullptr && mesh->IsGltf())
            {
                for (const auto& entry : mesh->materials)
                {
                    const skinned_mesh::gltf_material_info* info =
                        mesh->GltfMaterial(entry.first);
                    if ((info != nullptr && info->alpha_mode == 2) ||
                        entry.second.Kd.w < 0.999f)
                    {
                        return false;
                    }
                }
            }
        }
    }

    // 1 GameObject が複数 Renderer / RenderItem を持つ場合も owner 全体で 1 つの
    // Effect 対象として扱う。最初の RenderItem だけで crop すると別パーツが欠ける。
    const DirectX::XMMATRIX view = viewport_view_matrix();
    const DirectX::XMMATRIX projection = viewport_projection_matrix();
    ModelScreenRect base_rect{};
    bool have_screen_bounds = false;
    for (const RenderItem& candidate : object_render_items.Items())
    {
        if (candidate.owner != owner) continue;

        DirectX::XMFLOAT3 minimum{};
        DirectX::XMFLOAT3 maximum{};
        bool have_bounds = false;
        if (candidate.mesh_asset.rfind("builtin:", 0) == 0)
        {
            if (static_mesh* mesh = resolve_builtin_primitive_mesh(candidate.mesh_asset))
            {
                minimum = mesh->bounding_box[0];
                maximum = mesh->bounding_box[1];
                have_bounds = finite_bounds(minimum, maximum);
            }
        }
        else if (gltf_model* model = resolve_object_gltf(candidate.mesh_asset))
        {
            have_bounds = model->ComputeBounds(minimum, maximum) &&
                finite_bounds(minimum, maximum);
        }

        if (!have_bounds)
        {
            if (skinned_mesh* mesh = resolve_object_mesh(candidate.mesh_asset))
            {
                minimum = { D3D11_FLOAT32_MAX, D3D11_FLOAT32_MAX, D3D11_FLOAT32_MAX };
                maximum = { -D3D11_FLOAT32_MAX, -D3D11_FLOAT32_MAX, -D3D11_FLOAT32_MAX };
                for (const skinned_mesh::mesh& part : mesh->meshes)
                {
                    if (!finite_bounds(part.bounding_box[0], part.bounding_box[1])) continue;
                    minimum.x = (std::min)(minimum.x, part.bounding_box[0].x);
                    minimum.y = (std::min)(minimum.y, part.bounding_box[0].y);
                    minimum.z = (std::min)(minimum.z, part.bounding_box[0].z);
                    maximum.x = (std::max)(maximum.x, part.bounding_box[1].x);
                    maximum.y = (std::max)(maximum.y, part.bounding_box[1].y);
                    maximum.z = (std::max)(maximum.z, part.bounding_box[1].z);
                }
                have_bounds = finite_bounds(minimum, maximum);
            }
        }
        if (!have_bounds) continue;

        const ModelScreenRect candidate_rect = project_bounds(minimum, maximum,
            candidate.world, view, projection, camera_viewport, {}, 0.0f);
        if (!candidate_rect.Valid()) continue;
        if (!have_screen_bounds)
        {
            base_rect = candidate_rect;
            have_screen_bounds = true;
        }
        else
        {
            base_rect.left = (std::min)(base_rect.left, candidate_rect.left);
            base_rect.top = (std::min)(base_rect.top, candidate_rect.top);
            base_rect.right = (std::max)(base_rect.right, candidate_rect.right);
            base_rect.bottom = (std::max)(base_rect.bottom, candidate_rect.bottom);
        }
    }
    if (!have_screen_bounds || !base_rect.Valid()) return false;

    const DirectX::XMFLOAT4 expansion = effect->ExpandBounds(
        static_cast<float>(base_rect.Width()), static_cast<float>(base_rect.Height()),
        &asset_database);
    const ModelScreenRect expanded = expand_rect(base_rect, camera_viewport, expansion,
        effect->max_bleed_pixels);
    if (!expanded.Valid()) return false;

    out_base.left = base_rect.left;
    out_base.top = base_rect.top;
    out_base.right = base_rect.right;
    out_base.bottom = base_rect.bottom;
    out_expanded.left = expanded.left;
    out_expanded.top = expanded.top;
    out_expanded.right = expanded.right;
    out_expanded.bottom = expanded.bottom;
    return true;
}

namespace
{
    // 面(アルファ)を減らす Effect だけが影に意味を持つ。色を変えるだけのものは対象外。
    bool IsShadowCoverageEffect(ReplayEngine::UI::UIEffectKind kind) noexcept
    {
        using ReplayEngine::UI::UIEffectKind;
        return kind == UIEffectKind::Mask || kind == UIEffectKind::Wipe ||
            kind == UIEffectKind::Dissolve || kind == UIEffectKind::BurnReveal;
    }

    // 影で再現できるのは矩形と楕円だけ。投げ縄と画像マスクはここで弾く。
    bool IsShadowCoverageRegionShape(int shape) noexcept
    {
        using ReplayEngine::UI::UIEffectRegionShape;
        return shape == static_cast<int>(UIEffectRegionShape::Rectangle) ||
            shape == static_cast<int>(UIEffectRegionShape::Ellipse);
    }

    void FillShadowCoverageRegion(const ReplayEngine::UI::UIEffectRegionData& source,
        DirectX::XMFLOAT4& params, DirectX::XMFLOAT4& settings) noexcept
    {
        params = {
            source.center.x, source.center.y,
            (std::max)(source.size.x, 0.0001f),
            (std::max)(source.size.y, 0.0001f) };
        settings = {
            source.rotation, (std::max)(source.feather, 0.0f),
            (std::max)(0.0f, (std::min)(1.0f, source.strength)),
            static_cast<float>((std::max)(0, (std::min)(3, source.shape))) +
                (source.invert ? 4.0f : 0.0f) };
    }
}

void framework::collect_shadow_coverage(const D3D11_VIEWPORT& camera_viewport)
{
    using ReplayEngine::Components::ModelEffectStackComponent;
    using ReplayEngine::Core::GameObject;
    using ReplayEngine::Rendering::RenderItem;
    using ReplayEngine::UI::UIEffect;
    using ReplayEngine::UI::UIEffectKind;
    using ReplayEngine::UI::UIEffectRegionScope;

    shadow_coverage_entries.clear();
    if (object_render_items.Empty() || device == nullptr) return;

    const DirectX::XMMATRIX view_projection =
        viewport_view_matrix() * viewport_projection_matrix();
    std::unordered_set<ReplayEngine::Core::ObjectID> visited;

    for (const RenderItem& item : object_render_items.Items())
    {
        if (!item.owner.Valid() || !visited.insert(item.owner).second) continue;

        GameObject* object = active_object_scene().FindGameObjectByID(item.owner);
        ModelEffectStackComponent* effect = object != nullptr
            ? object->GetComponent<ModelEffectStackComponent>() : nullptr;
        if (effect == nullptr) continue;

        model_effect_screen_rect base_bounds{};
        model_effect_screen_rect expanded_bounds{};
        if (!compute_model_effect_screen_rect(item.owner, camera_viewport,
            base_bounds, expanded_bounds))
        {
            continue;
        }

        shadow_coverage_entry entry{};
        int count = 0;
        int mask_index = -1;
        for (const UIEffect& source : effect->EffectiveEffects(&asset_database))
        {
            if (!source.enabled) continue;
            const UIEffectKind kind = static_cast<UIEffectKind>(source.kind);
            // カスタムシェーダーは面を消すかどうかを外から判定できない。
            if (!source.custom_shader.empty() || !IsShadowCoverageEffect(kind)) continue;
            if (count >= shadow_coverage_max_effects)
            {
                ++shadow_stats.coverage_unsupported;
                continue;
            }

            ID3D11ShaderResourceView* mask = source.mask.empty()
                ? nullptr : resolve_scene_effect_texture(source.mask);
            const bool bind_mask = mask != nullptr &&
                (entry.mask == nullptr || entry.mask == mask);
            if (mask != nullptr && !bind_mask) ++shadow_stats.coverage_unsupported;
            if (bind_mask)
            {
                entry.mask = mask;
                mask_index = count;
            }

            entry.constants.params0[count] = {
                source.radius, source.intensity, source.threshold, source.amount };
            entry.constants.params1[count] = {
                source.angle, source.progress, source.softness, source.speed };
            entry.constants.params2[count] = {
                source.direction.x, source.direction.y, source.seed, shader_composer_time };
            entry.constants.params3[count] = {
                static_cast<float>(source.waveform), bind_mask ? 1.0f : 0.0f, 0.0f, 0.0f };

            if (kind == UIEffectKind::Mask)
            {
                // EffectChain と同じ読み替え。direction を中心、seed/speed を半径に使う。
                const float center_x = source.direction.x > 0.0f && source.direction.x < 1.0f
                    ? source.direction.x : 0.5f;
                const float center_y = source.direction.y > 0.0f && source.direction.y < 1.0f
                    ? source.direction.y : 0.5f;
                const float half_width = source.seed > 0.0f && source.seed < 1.0f
                    ? source.seed : 0.5f;
                const float half_height = source.speed > 0.0f && source.speed < 1.0f
                    ? source.speed : 0.5f;
                entry.constants.params2[count] = {
                    center_x, center_y, half_width, half_height };
                entry.constants.params1[count].w = bind_mask ? 1.0f : 0.0f;
            }

            const bool region_applies = effect->effect_region.enabled &&
                (effect->effect_region.scope ==
                    static_cast<int>(UIEffectRegionScope::AllEffects) ||
                    source.region_enabled);
            entry.constants.meta[count] = {
                static_cast<float>(source.kind), region_applies ? 1.0f : 0.0f, 0.0f, 0.0f };
            ++count;
        }
        if (count == 0) continue;

        int region_count = 0;
        if (effect->effect_region.enabled)
        {
            if (!IsShadowCoverageRegionShape(effect->effect_region.shape))
            {
                // 範囲を再現できないときは面消しごと諦める。影だけ消えるより安全。
                ++shadow_stats.coverage_unsupported;
                continue;
            }
            FillShadowCoverageRegion(effect->effect_region,
                entry.constants.region_params[0], entry.constants.region_settings[0]);
            region_count = 1;
            for (const auto& additional : effect->effect_region.additional)
            {
                if (region_count >= shadow_coverage_max_regions) break;
                if (!additional.enabled) continue;
                if (!IsShadowCoverageRegionShape(additional.shape))
                {
                    ++shadow_stats.coverage_unsupported;
                    continue;
                }
                FillShadowCoverageRegion(additional,
                    entry.constants.region_params[region_count],
                    entry.constants.region_settings[region_count]);
                ++region_count;
            }
        }

        DirectX::XMStoreFloat4x4(&entry.constants.view_projection, view_projection);
        entry.constants.viewport = {
            camera_viewport.TopLeftX, camera_viewport.TopLeftY,
            camera_viewport.Width, camera_viewport.Height };
        entry.constants.rect = {
            static_cast<float>(expanded_bounds.left),
            static_cast<float>(expanded_bounds.top),
            static_cast<float>(expanded_bounds.Width()),
            static_cast<float>(expanded_bounds.Height()) };
        entry.constants.control = {
            static_cast<float>(count), static_cast<float>(mask_index),
            static_cast<float>(region_count), 0.0f };
        shadow_coverage_entries.emplace(item.owner.Value(), entry);
    }
}

bool framework::bind_shadow_coverage_constants(ReplayEngine::Core::ObjectID owner)
{
    if (shadow_coverage_cb == nullptr || immediate_context == nullptr) return false;

    shadow_coverage_constants constants{};
    ID3D11ShaderResourceView* mask = nullptr;
    bool active = false;
    if (owner.Valid() && !shadow_coverage_entries.empty())
    {
        const auto found = shadow_coverage_entries.find(owner.Value());
        if (found != shadow_coverage_entries.end())
        {
            constants = found->second.constants;
            mask = found->second.mask;
            active = true;
        }
    }

    immediate_context->UpdateSubresource(shadow_coverage_cb.Get(), 0, nullptr,
        &constants, 0, 0);
    immediate_context->PSSetConstantBuffers(8, 1, shadow_coverage_cb.GetAddressOf());
    immediate_context->PSSetShaderResources(46, 1, &mask);
    if (active) ++shadow_stats.coverage_casters;
    return active;
}
