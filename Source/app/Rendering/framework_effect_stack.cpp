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
}

ReplayEngine::UI::UIRenderTarget* framework::apply_scene_effect_chain(
    ID3D11ShaderResourceView* source,
    const std::vector<ReplayEngine::UI::UIEffect>& effects,
    std::uint32_t width, std::uint32_t height, DXGI_FORMAT format,
    float effect_time)
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

    ID3D11ShaderResourceView* null_srvs[2]{};
    immediate_context->PSSetShaderResources(0, 2, null_srvs);
    return result;
}
