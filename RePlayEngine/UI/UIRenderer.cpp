// UIRenderer のうち「GPU 初期化、Asset / Shader キャッシュ、リソース管理」を持つ。
//
//   UIRenderer.cpp       … 初期化、解放、Asset / Shader キャッシュ（このファイル）
//   UIRendererRender.cpp … UI の描画補助、Flush、Render
//
// Render の Effect Stack を含む描画分岐は、分割前の構造をそのまま移している。
#include "UIRenderer.h"

#include "../Assets/AssetDatabase.h"
#include "../Rendering/Shaders/ShaderCatalog.h"
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

        if (!effect_chain_.Initialize(device)) return false;

        D3D11_BUFFER_DESC cb_desc{};
        cb_desc.ByteWidth = sizeof(Constants);
        cb_desc.Usage = D3D11_USAGE_DEFAULT;
        cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device->CreateBuffer(&cb_desc, nullptr, constant_buffer_.GetAddressOf())))
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
        effect_chain_.Release();
        texture_cache_.clear();
        vertices_.clear();
        vertex_capacity_ = 0;
        white_texture_.Reset();
        visual_constant_buffer_.Reset();
        constant_buffer_.Reset();
        vertex_buffer_.Reset();
        input_layout_.Reset();
        world_space_canvas_ = false;
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

    std::uint64_t UIRenderer::TrackedBufferBytes() const noexcept
    {
        std::uint64_t total = 0;
        if (vertex_buffer_)
            total += static_cast<std::uint64_t>(vertex_capacity_) * sizeof(Vertex);
        if (constant_buffer_) total += sizeof(Constants);
        if (visual_constant_buffer_) total += sizeof(VisualConstants);
        total += effect_chain_.AllocatedBufferBytes();
        return total;
    }

    void UIRenderer::AppendResidentTextureIdentities(
        std::vector<std::pair<std::string, const void*>>& out) const
    {
        for (const auto& entry : texture_cache_)
        {
            if (entry.first.empty() || !entry.second) continue;
            Microsoft::WRL::ComPtr<ID3D11Resource> resource;
            entry.second->GetResource(resource.GetAddressOf());
            if (resource) out.emplace_back(entry.first, resource.Get());
        }
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

}
