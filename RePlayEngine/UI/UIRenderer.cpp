// UIRenderer のうち「GPU 初期化、Asset / Shader キャッシュ、リソース管理」を持つ。
//
//   UIRenderer.cpp       … 初期化、解放、Asset / Shader キャッシュ（このファイル）
//   UIRendererRender.cpp … UI の描画補助、Flush、Render
//
// Render の Effect Stack を含む描画分岐は、分割前の構造をそのまま移している。
#include "UIRenderer.h"

#include "../Assets/AssetDatabase.h"
#include "../Components/UI/UIImageComponent.h"
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
        texture_path_cache_.clear();
        sprite_atlas_cache_.clear();
        for (auto& entry : temporal_history_cache_) entry.second.target.Release();
        temporal_history_cache_.clear();
        render_serial_ = 0;
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

    UIRenderer::TemporalHistoryEntry* UIRenderer::TemporalHistoryFor(
        std::uint64_t owner_key, std::uint32_t width, std::uint32_t height)
    {
        if (device_ == nullptr || owner_key == 0 || width == 0 || height == 0) return nullptr;
        // Preview と Game View が同一Objectを別解像度で描く場合でも履歴を混ぜない。
        std::uint64_t key = owner_key;
        key ^= static_cast<std::uint64_t>(width) * 0x9E3779B185EBCA87ull;
        key ^= static_cast<std::uint64_t>(height) * 0xC2B2AE3D27D4EB4Full;
        TemporalHistoryEntry& entry = temporal_history_cache_[key];
        const bool size_changed = entry.target.width != width ||
            entry.target.height != height || entry.target.format != DXGI_FORMAT_R8G8B8A8_UNORM;
        if (!entry.target.Resize(device_.Get(), width, height, DXGI_FORMAT_R8G8B8A8_UNORM))
        {
            temporal_history_cache_.erase(key);
            return nullptr;
        }
        if (size_changed) entry.valid = false;
        entry.last_used_serial = render_serial_;
        return &entry;
    }

    void UIRenderer::PruneTemporalHistory() noexcept
    {
        constexpr std::uint64_t keep_render_calls = 240;
        for (auto it = temporal_history_cache_.begin(); it != temporal_history_cache_.end();)
        {
            const bool stale = render_serial_ > it->second.last_used_serial + keep_render_calls;
            if (stale)
            {
                it->second.target.Release();
                it = temporal_history_cache_.erase(it);
            }
            else ++it;
        }
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
        for (const auto& entry : temporal_history_cache_)
        {
            if (!entry.second.target.texture) continue;
            total += static_cast<std::uint64_t>(entry.second.target.width) *
                static_cast<std::uint64_t>(entry.second.target.height) * 4u;
        }
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

    bool UIRenderer::ResolveImageSource(const Components::UIImageComponent& image,
        const Assets::AssetDatabase* asset_database, ResolvedImageSource& out)
    {
        out = ResolvedImageSource{};
        out.texture_guid = image.sprite.guid;
        out.uv = { image.uv_offset.x, image.uv_offset.y,
            image.uv_scale.x, image.uv_scale.y };

        if (image.atlas.guid.empty() || image.atlas_region.empty() ||
            asset_database == nullptr)
        {
            return !out.texture_guid.empty();
        }

        const Assets::AssetRecord* record = asset_database->FindByGuid(image.atlas.guid);
        if (record == nullptr || record->kind != Assets::AssetKind::SpriteAtlas)
            return !out.texture_guid.empty();

        const std::filesystem::path path =
            record->cache_path.empty() ? record->source_path : record->cache_path;
        std::error_code ec;
        const std::filesystem::file_time_type timestamp =
            std::filesystem::last_write_time(path, ec);
        CachedSpriteAtlas& cached = sprite_atlas_cache_[image.atlas.guid];
        if (!cached.loaded || cached.path != path || (!ec && cached.timestamp != timestamp))
        {
            Assets::SpriteAtlasAsset loaded;
            std::string error;
            if (!Assets::SpriteAtlasAsset::LoadFromFile(path, loaded, error))
                return !out.texture_guid.empty();
            cached.asset = std::move(loaded);
            cached.path = path;
            cached.timestamp = ec ? std::filesystem::file_time_type{} : timestamp;
            cached.loaded = true;
        }

        const Assets::SpriteAtlasRegion* region = cached.asset.FindRegion(image.atlas_region);
        if (region == nullptr || (cached.asset.image_guid.empty() &&
            cached.asset.embedded_texture_path.empty()))
            return !out.texture_guid.empty();

        if (!cached.asset.embedded_texture_path.empty())
        {
            const std::filesystem::path embedded_path = path.parent_path() /
                std::filesystem::u8path(cached.asset.embedded_texture_path);
            std::error_code embedded_error;
            if (std::filesystem::exists(embedded_path, embedded_error) && !embedded_error)
                out.texture_path = embedded_path;
        }
        out.texture_guid = cached.asset.image_guid;
        out.uv = {
            region->uv_rect.x + region->uv_rect.z * image.uv_offset.x,
            region->uv_rect.y + region->uv_rect.w * image.uv_offset.y,
            region->uv_rect.z * image.uv_scale.x,
            region->uv_rect.w * image.uv_scale.y };
        if (region->path_points.size() >= 3 && region->uv_rect.z > 0.000001f &&
            region->uv_rect.w > 0.000001f)
        {
            out.path_points.reserve(region->path_points.size());
            for (const DirectX::XMFLOAT2& point : region->path_points)
            {
                const float relative_x =
                    (point.x - region->uv_rect.x) / region->uv_rect.z;
                const float relative_y =
                    (point.y - region->uv_rect.y) / region->uv_rect.w;
                out.path_points.push_back(region->rotated
                    ? DirectX::XMFLOAT2{ relative_y, relative_x }
                    : DirectX::XMFLOAT2{ relative_x, 1.0f - relative_y });
            }
        }
        out.atlas_pivot = region->pivot;
        out.rotated = region->rotated;
        out.from_atlas = true;
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

    ID3D11ShaderResourceView* UIRenderer::TextureForPath(
        const std::filesystem::path& path)
    {
        if (path.empty() || device_ == nullptr) return white_texture_.Get();
        const std::filesystem::path normalized = path.lexically_normal();
        const std::string key = normalized.generic_u8string();
        if (const auto it = texture_path_cache_.find(key); it != texture_path_cache_.end())
            return it->second.Get();

        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> loaded;
        if (FAILED(load_texture_from_file(device_.Get(), normalized.wstring().c_str(),
            loaded.GetAddressOf(), nullptr)) || !loaded)
            return white_texture_.Get();

        texture_path_cache_[key] = loaded;
        return loaded.Get();
    }

}
