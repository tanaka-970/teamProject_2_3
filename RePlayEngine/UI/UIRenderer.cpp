// UIRenderer のうち「GPU 初期化、Asset / Shader キャッシュ、リソース管理」を持つ。
//
//   UIRenderer.cpp       … 初期化、解放、Asset / Shader キャッシュ（このファイル）
//   UIRendererRender.cpp … UI の描画補助、Flush、Render
//
// Render の Effect Stack を含む描画分岐は、分割前の構造をそのまま移している。
#include "UIRenderer.h"

#include "../Assets/AssetDatabase.h"
#include "../Rendering/Shaders/ShaderCatalog.h"
#include "../Rendering/Shaders/ShaderConstantPacker.h"
#include "../Rendering/Shaders/ShaderAsset.h"
#include "../../Source/core/shader.h"
#include "../../Source/core/texture.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>

namespace ReplayEngine::UI
{
    bool UIRenderer::Initialize(ID3D11Device* device)
    {
        Release();
        if (device == nullptr) return false;
        device_ = device;

        D3D11_INPUT_ELEMENT_DESC input_desc[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
                D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8,
                D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 16,
                D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24,
                D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 40,
                D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        if (FAILED(create_vs_from_cso(device, "ui_vs.cso",
            vertex_shader_.GetAddressOf(), input_layout_.GetAddressOf(),
            input_desc, static_cast<UINT>(sizeof(input_desc) / sizeof(input_desc[0])))))
            return false;

        if (FAILED(create_ps_from_cso(device, "ui_ps.cso",
            pixel_shader_.GetAddressOf())))
            return false;

        const std::array<const char*, effect_shader_count> effect_cso_names{
            "ui_effect_blur.cso",
            "ui_effect_glow.cso",
            "ui_effect_color_adjust.cso",
            "ui_effect_noise.cso",
            "ui_effect_shake.cso",
            "ui_effect_mask.cso",
            "ui_effect_wipe.cso",
            "ui_effect_dissolve.cso",
            "ui_effect_distortion.cso",
            "ui_effect_chromatic_aberration.cso",
            "ui_effect_kuwahara.cso",
            "ui_effect_halftone.cso",
            "ui_effect_directional_blur.cso",
            "ui_effect_radial_blur.cso",
            "ui_effect_rotational_blur.cso",
            "ui_effect_vignette.cso",
            "ui_effect_light_streaks.cso",
            "ui_effect_lens_distortion.cso",
            "ui_effect_posterize.cso",
            "ui_effect_threshold.cso",
            "ui_effect_color_ramp.cso",
            "ui_effect_levels.cso",
            "ui_effect_temperature.cso",
            "ui_effect_edge_detect.cso",
            "ui_effect_outline.cso",
            "ui_effect_long_shadow.cso",
            "ui_effect_cross_hatch.cso",
            "ui_effect_brush_stroke.cso",
            "ui_effect_mosaic.cso",
            "ui_effect_crystallize.cso",
            "ui_effect_stained_glass.cso",
            "ui_effect_twirl.cso",
            "ui_effect_spherize.cso",
            "ui_effect_ripple.cso",
            "ui_effect_polar_coordinates.cso",
            "ui_effect_scanlines.cso",
            "ui_effect_crt.cso",
            "ui_effect_glitch.cso",
            "ui_effect_dither.cso",
            "ui_effect_vhs.cso",
            "ui_effect_letterbox.cso",
            "ui_effect_waveform.cso",
        };
        for (std::size_t index = 0; index < effect_cso_names.size(); ++index)
        {
            if (FAILED(create_ps_from_cso(device, effect_cso_names[index],
                effect_pixel_shaders_[index].GetAddressOf())))
                return false;
        }

        D3D11_BUFFER_DESC cb_desc{};
        cb_desc.ByteWidth = sizeof(Constants);
        cb_desc.Usage = D3D11_USAGE_DEFAULT;
        cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device->CreateBuffer(&cb_desc, nullptr, constant_buffer_.GetAddressOf())))
            return false;

        cb_desc.ByteWidth = sizeof(EffectConstants);
        if (FAILED(device->CreateBuffer(&cb_desc, nullptr,
            effect_constant_buffer_.GetAddressOf())))
            return false;

        cb_desc.ByteWidth = sizeof(VisualConstants);
        if (FAILED(device->CreateBuffer(&cb_desc, nullptr,
            visual_constant_buffer_.GetAddressOf())))
            return false;

        if (FAILED(make_dummy_texture(device, white_texture_.GetAddressOf(),
            0xFFFFFFFFu, 1)))
            return false;

        render_target_pool_.Initialize(device);
        return true;
    }

    void UIRenderer::Release() noexcept
    {
        render_target_pool_.Release();
        texture_cache_.clear();
        custom_effect_shader_cache_.clear();
        vertices_.clear();
        vertex_capacity_ = 0;
        white_texture_.Reset();
        custom_effect_constant_buffer_.Reset();
        custom_effect_constant_buffer_size_ = 0;
        effect_constant_buffer_.Reset();
        visual_constant_buffer_.Reset();
        constant_buffer_.Reset();
        vertex_buffer_.Reset();
        input_layout_.Reset();
        world_space_canvas_ = false;
        for (Microsoft::WRL::ComPtr<ID3D11PixelShader>& shader : effect_pixel_shaders_)
        {
            shader.Reset();
        }
        pixel_shader_.Reset();
        vertex_shader_.Reset();
        device_.Reset();
    }

    void UIRenderer::ReleaseTransientTargets() noexcept
    {
        render_target_pool_.Release();
    }

    bool UIRenderer::EnsureVertexCapacity(ID3D11Device* device, std::size_t vertex_count)
    {
        if (vertex_count <= vertex_capacity_) return true;
        std::size_t next_capacity = (std::max)(std::size_t{ 6 }, vertex_capacity_);
        while (next_capacity < vertex_count) next_capacity *= 2;

        vertex_buffer_.Reset();
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(sizeof(Vertex) * next_capacity);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreateBuffer(&desc, nullptr, vertex_buffer_.GetAddressOf())))
        {
            vertex_capacity_ = 0;
            return false;
        }
        vertex_capacity_ = next_capacity;
        return true;
    }

    bool UIRenderer::EnsureCustomEffectConstantBuffer(std::uint32_t byte_width)
    {
        if (device_ == nullptr) return false;
        const std::uint32_t aligned = (std::max)(16u,
            Rendering::ShaderConstantPacker::Align16(byte_width));
        if (custom_effect_constant_buffer_ != nullptr &&
            custom_effect_constant_buffer_size_ == aligned)
        {
            return true;
        }

        // GetAddressOf は既存ポインタを Release しないため、作り直す前に必ず Reset。
        custom_effect_constant_buffer_.Reset();
        custom_effect_constant_buffer_size_ = 0;
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = aligned;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device_->CreateBuffer(&desc, nullptr,
            custom_effect_constant_buffer_.GetAddressOf())))
        {
            return false;
        }
        custom_effect_constant_buffer_size_ = aligned;
        return true;
    }

    ID3D11ShaderResourceView* UIRenderer::TextureFor(const std::string& guid,
        const Assets::AssetDatabase* asset_database)
    {
        if (guid.empty() || asset_database == nullptr) return white_texture_.Get();
        if (const auto it = texture_cache_.find(guid); it != texture_cache_.end())
            return it->second.Get();

        const Assets::AssetRecord* record = asset_database->FindByGuid(guid);
        if (record == nullptr || record->kind != Assets::AssetKind::Image)
            return white_texture_.Get();

        const std::filesystem::path path =
            record->cache_path.empty() ? record->source_path : record->cache_path;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> loaded;
        if (FAILED(load_texture_from_file(device_.Get(), path.wstring().c_str(),
            loaded.GetAddressOf(), nullptr)) || !loaded)
            return white_texture_.Get();

        texture_cache_[guid] = loaded;
        return loaded.Get();
    }

    ID3D11PixelShader* UIRenderer::EffectShaderFor(UIEffectKind kind) const noexcept
    {
        const int index = static_cast<int>(kind);
        if (index < 0 || index >= static_cast<int>(effect_pixel_shaders_.size()))
        {
            return nullptr;
        }
        return effect_pixel_shaders_[static_cast<std::size_t>(index)].Get();
    }

    ID3D11PixelShader* UIRenderer::CustomEffectShaderFor(
        const std::string& shader_guid,
        const Assets::AssetDatabase* asset_database,
        const Rendering::ShaderCatalog* shader_catalog)
    {
        if (shader_guid.empty() || asset_database == nullptr ||
            shader_catalog == nullptr || device_ == nullptr)
        {
            return nullptr;
        }
        const Assets::AssetRecord* record = asset_database->FindByGuid(shader_guid);
        if (record == nullptr || record->kind != Assets::AssetKind::Shader)
            return nullptr;

        const auto normalize = [](std::filesystem::path path)
        {
            std::error_code error;
            std::filesystem::path absolute = path.is_absolute()
                ? path : std::filesystem::absolute(path, error);
            if (error) absolute = path;
            error.clear();
            const std::filesystem::path canonical =
                std::filesystem::weakly_canonical(absolute, error);
            return error ? absolute.lexically_normal() : canonical.lexically_normal();
        };

        const std::filesystem::path source_path = normalize(record->source_path);
        const Rendering::ShaderCatalog::Entry* matched = nullptr;
        for (const Rendering::ShaderCatalog::Entry& entry : shader_catalog->All())
        {
            if (entry.info.domain != Rendering::ShaderDomain::PostProcess) continue;
            if (normalize(entry.info.source_path) == source_path)
            {
                matched = &entry;
                break;
            }
        }
        if (matched == nullptr) return nullptr;

        const Rendering::ShaderCatalog::VariantResult& variant =
            matched->At(Rendering::ShaderVariant::Static);
        // 最新コンパイルが失敗しても、Catalog が保持する最後の成功 bytecode は使い続ける。
        if (!variant.bytecode) return nullptr;

        CachedCustomEffectShader& cached = custom_effect_shader_cache_[shader_guid];
        const std::size_t bytecode_size = variant.bytecode->GetBufferSize();
        if (cached.shader && cached.bytecode_identity == variant.bytecode.Get() &&
            cached.bytecode_size == bytecode_size)
        {
            return cached.shader.Get();
        }

        Microsoft::WRL::ComPtr<ID3D11PixelShader> replacement;
        if (FAILED(device_->CreatePixelShader(
            variant.bytecode->GetBufferPointer(), bytecode_size, nullptr,
            replacement.GetAddressOf())) || !replacement)
        {
            // Hot Reload の新 bytecode だけ作成に失敗しても、直前の成功 PS は残す。
            return cached.shader.Get();
        }

        cached.shader = replacement;
        cached.bytecode_identity = variant.bytecode.Get();
        cached.bytecode_size = bytecode_size;
        return cached.shader.Get();
    }
}
