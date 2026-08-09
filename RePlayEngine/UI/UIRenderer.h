#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl.h>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace ReplayEngine::Assets { class AssetDatabase; }
namespace ReplayEngine::Scene { class Scene; }

namespace ReplayEngine::UI
{
    class FontAtlas;

    class UIRenderer final
    {
    public:
        struct RenderStates
        {
            ID3D11DepthStencilState* depth_disabled = nullptr;
            ID3D11RasterizerState* rasterizer = nullptr;
            ID3D11RasterizerState* rasterizer_scissor = nullptr;
            ID3D11BlendState* blend_alpha = nullptr;
            ID3D11BlendState* blend_add = nullptr;
            ID3D11BlendState* blend_multiply = nullptr;
            ID3D11BlendState* blend_screen = nullptr;
            ID3D11SamplerState* sampler = nullptr;
        };

        bool Initialize(ID3D11Device* device);
        void Release() noexcept;

        void Render(ID3D11DeviceContext* context,
            Scene::Scene& scene,
            const Assets::AssetDatabase* asset_database,
            FontAtlas& font_atlas,
            float screen_width,
            float screen_height,
            const RenderStates& states);

    private:
        struct Vertex
        {
            DirectX::XMFLOAT2 position{ 0.0f, 0.0f };
            DirectX::XMFLOAT2 uv{ 0.0f, 0.0f };
            DirectX::XMFLOAT4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        };

        struct Constants
        {
            DirectX::XMFLOAT4 screen_size{ 1.0f, 1.0f, 0.0f, 0.0f };
        };

        bool EnsureVertexCapacity(ID3D11Device* device, std::size_t vertex_count);
        ID3D11ShaderResourceView* TextureFor(const std::string& guid,
            const Assets::AssetDatabase* asset_database);
        void Flush(ID3D11DeviceContext* context, ID3D11ShaderResourceView* texture,
            ID3D11BlendState* blend_state, const RenderStates& states,
            const D3D11_RECT* scissor);

        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> vertex_buffer_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> constant_buffer_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> white_texture_;
        std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> texture_cache_;
        std::vector<Vertex> vertices_;
        std::size_t vertex_capacity_ = 0;
    };
}
