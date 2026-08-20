#include "framework.h"
#include "texture.h"

#include "../../../RePlayEngine/Assets/AssetCache.h"

#include <algorithm>

ID3D11ShaderResourceView* framework::resolve_scene_effect_texture(
    const std::string& asset_guid)
{
    if (asset_guid.empty() || !device) return nullptr;

    const ReplayEngine::Assets::AssetRecord* record =
        asset_database.FindByGuid(asset_guid);
    if (record == nullptr || record->kind != ReplayEngine::Assets::AssetKind::Image)
        return nullptr;

    const std::filesystem::path project_path = record->cache_path.empty()
        ? record->source_path : record->cache_path;
    const std::filesystem::path path = content_path(project_path);

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> loaded;
    if (FAILED(load_texture_from_file(device.Get(), path.wstring().c_str(),
        loaded.GetAddressOf(), nullptr)) || !loaded)
    {
        return nullptr;
    }

    // load_texture_from_file 自体がプロジェクト共通の共有キャッシュを持つ。
    // ここでは EffectChain::Apply の間だけ AddRef を保持し、2つ目の恒久 cache を作らない。
    scene_effect_texture_refs.push_back(loaded);
    return loaded.Get();
}

void framework::begin_scene_effect_frame() noexcept
{
    scene_effect_targets.BeginFrame();
    scene_effect_texture_refs.clear();
    ++scene_effect_frame_serial;
    if ((scene_effect_frame_serial & 63ull) == 0ull)
    {
        for (auto it = scene_effect_temporal_history.begin();
            it != scene_effect_temporal_history.end();)
        {
            if (scene_effect_frame_serial - it->second.last_used_serial > 240ull)
                it = scene_effect_temporal_history.erase(it);
            else
                ++it;
        }
    }
}

ReplayEngine::UI::UIRenderTarget* framework::apply_scene_effect_chain(
    ID3D11ShaderResourceView* source,
    const std::vector<ReplayEngine::UI::UIEffect>& effects,
    std::uint32_t width, std::uint32_t height, DXGI_FORMAT format,
    float effect_time, std::uint64_t temporal_owner_key)
{
    if (source == nullptr || immediate_context == nullptr || bit_block_transfer == nullptr ||
        width == 0 || height == 0)
    {
        return nullptr;
    }

    bool has_active = false;
    for (const ReplayEngine::UI::UIEffect& effect : effects)
    {
        if (effect.enabled)
        {
            has_active = true;
            break;
        }
    }
    if (!has_active) return nullptr;

    bool needs_temporal_history = false;
    for (const ReplayEngine::UI::UIEffect& effect : effects)
    {
        if (!effect.enabled) continue;
        const ReplayEngine::UI::UIEffectKind kind =
            static_cast<ReplayEngine::UI::UIEffectKind>(effect.kind);
        if (kind == ReplayEngine::UI::UIEffectKind::MotionBlur ||
            kind == ReplayEngine::UI::UIEffectKind::Echo)
        {
            needs_temporal_history = true;
            break;
        }
    }

    scene_effect_temporal_history_entry* temporal_history = nullptr;
    if (needs_temporal_history && temporal_owner_key != 0 && device != nullptr)
    {
        scene_effect_temporal_history_entry& entry =
            scene_effect_temporal_history[temporal_owner_key];
        const bool size_changed = entry.target.width != width ||
            entry.target.height != height || entry.target.format != format;
        if (!entry.target.Resize(device.Get(), width, height, format))
        {
            entry.target.Release();
            entry.valid = false;
        }
        else
        {
            if (size_changed) entry.valid = false;
            temporal_history = &entry;
            entry.last_used_serial = scene_effect_frame_serial;
        }
    }

    ReplayEngine::UI::UIRenderTarget* first =
        scene_effect_targets.Acquire(width, height, format);
    ReplayEngine::UI::UIRenderTarget* second =
        scene_effect_targets.Acquire(width, height, format);
    if (first == nullptr || second == nullptr) return nullptr;

    const auto configure_target = [this](ReplayEngine::UI::UIRenderTarget& target)
    {
        ID3D11ShaderResourceView* null_srvs[2]{};
        immediate_context->PSSetShaderResources(0, 2, null_srvs);
        immediate_context->OMSetRenderTargets(1, target.rtv.GetAddressOf(), nullptr);
        D3D11_VIEWPORT effect_viewport{};
        effect_viewport.TopLeftX = 0.0f;
        effect_viewport.TopLeftY = 0.0f;
        effect_viewport.Width = static_cast<float>(target.width);
        effect_viewport.Height = static_cast<float>(target.height);
        effect_viewport.MinDepth = 0.0f;
        effect_viewport.MaxDepth = 1.0f;
        immediate_context->RSSetViewports(1, &effect_viewport);
    };

    configure_target(*first);
    const FLOAT clear[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
    immediate_context->ClearRenderTargetView(first->rtv.Get(), clear);
    immediate_context->OMSetDepthStencilState(
        depth_stencil_states[(size_t)DEPTH_STATE::ZT_OFF_ZW_OFF].Get(), 0);
    immediate_context->RSSetState(
        rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());
    immediate_context->OMSetBlendState(
        blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xffffffff);
    ID3D11SamplerState* sampler =
        sampler_states[(size_t)SAMPLER_STATE::LINEAR].Get();
    immediate_context->PSSetSamplers(0, 1, &sampler);
    ID3D11ShaderResourceView* source_srv = source;
    bit_block_transfer->blit(immediate_context.Get(), &source_srv, 0, 1);

    ID3D11ShaderResourceView* null_source = nullptr;
    immediate_context->PSSetShaderResources(0, 1, &null_source);

    ReplayEngine::Rendering::Effects::EffectChain::Context context{};
    context.device_context = immediate_context.Get();
    context.asset_database = &asset_database;
    context.shader_catalog = &shader_library.Catalog();
    context.time = effect_time;
    context.depth_disabled =
        depth_stencil_states[(size_t)DEPTH_STATE::ZT_OFF_ZW_OFF].Get();
    context.rasterizer =
        rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get();
    context.blend_none = blend_states[(size_t)BLEND_STATE::NONE].Get();
    context.blend_alpha = blend_states[(size_t)BLEND_STATE::ALPHA].Get();
    context.sampler = sampler_states[(size_t)SAMPLER_STATE::LINEAR].Get();
    context.resolve_texture = [this](const std::string& guid)
    {
        return resolve_scene_effect_texture(guid);
    };
    context.runtime_history_texture = temporal_history != nullptr &&
        temporal_history->valid ? temporal_history->target.srv.Get() : nullptr;
    context.configure_target = configure_target;
    context.draw_plain_fullscreen = [this](float, float,
        ID3D11ShaderResourceView* input, ID3D11BlendState* blend)
    {
        immediate_context->OMSetDepthStencilState(
            depth_stencil_states[(size_t)DEPTH_STATE::ZT_OFF_ZW_OFF].Get(), 0);
        immediate_context->RSSetState(
            rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());
        immediate_context->OMSetBlendState(blend, nullptr, 0xffffffff);
        ID3D11SamplerState* local_sampler =
            sampler_states[(size_t)SAMPLER_STATE::LINEAR].Get();
        immediate_context->PSSetSamplers(0, 1, &local_sampler);
        ID3D11ShaderResourceView* input_srv = input;
        bit_block_transfer->blit(immediate_context.Get(), &input_srv, 0, 1);
    };
    context.draw_effect_fullscreen = [this](float, float,
        ID3D11ShaderResourceView* input, ID3D11BlendState* blend,
        ID3D11PixelShader* pixel_shader, ID3D11Buffer* constants)
    {
        immediate_context->OMSetDepthStencilState(
            depth_stencil_states[(size_t)DEPTH_STATE::ZT_OFF_ZW_OFF].Get(), 0);
        immediate_context->RSSetState(
            rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());
        immediate_context->OMSetBlendState(blend, nullptr, 0xffffffff);
        ID3D11SamplerState* local_sampler =
            sampler_states[(size_t)SAMPLER_STATE::LINEAR].Get();
        immediate_context->PSSetSamplers(0, 1, &local_sampler);
        immediate_context->PSSetConstantBuffers(0, 1, &constants);
        ID3D11ShaderResourceView* input_srv = input;
        bit_block_transfer->blit(immediate_context.Get(), &input_srv, 0, 1,
            pixel_shader);
    };

    ReplayEngine::UI::UIRenderTarget* result = scene_effect_chain.Apply(
        context, effects, first, first, second);
    if (temporal_history != nullptr && result != nullptr && result->texture &&
        temporal_history->target.texture)
    {
        ID3D11ShaderResourceView* null_history_srvs[2]{};
        immediate_context->PSSetShaderResources(0, 2, null_history_srvs);
        immediate_context->CopyResource(temporal_history->target.texture.Get(),
            result->texture.Get());
        temporal_history->valid = true;
        temporal_history->last_used_serial = scene_effect_frame_serial;
    }

    ID3D11ShaderResourceView* null_srvs[2]{};
    immediate_context->PSSetShaderResources(0, 2, null_srvs);
    return result;
}
