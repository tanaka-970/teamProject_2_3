#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/PropertyDesc.h"
#include "../../Reflection/Property/References.h"

#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl.h>

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace ReplayEngine::Assets { class AssetDatabase; }

namespace ReplayEngine::Rendering
{
    struct LineStrokeStyle final
    {
        float width_start = 0.1f;
        float width_end = 0.1f;
        bool billboard = true;
        int uv_mode = 0;
        float uv_tiling = 1.0f;
        float uv_scroll = 0.0f;
        std::string texture_guid;
        DirectX::XMFLOAT4 fill_color{ 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 fill_color_2{ 1.0f, 1.0f, 1.0f, 1.0f };
        int fill_mode = 0;
        float trim_start = 0.0f;
        float trim_end = 1.0f;
        float trim_offset = 0.0f;
        bool closed = false;
    };

    // LineRenderer と Trail が同じ帯生成・GPU 描画を通るための共有実装。
    class LineStrokeRenderer final
    {
    public:
        bool Initialize(ID3D11Device* device);
        void Release() noexcept;

        bool Draw(ID3D11Device* device, ID3D11DeviceContext* context,
            const Assets::AssetDatabase* asset_database,
            ID3D11SamplerState* sampler,
            const std::vector<DirectX::XMFLOAT3>& points,
            const std::vector<float>& point_alpha,
            const LineStrokeStyle& style,
            const DirectX::XMFLOAT3& camera_position);

    private:
        struct Vertex final
        {
            DirectX::XMFLOAT3 position{};
            DirectX::XMFLOAT4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
            DirectX::XMFLOAT2 uv{};
        };

        bool EnsureVertexCapacity(ID3D11Device* device, std::size_t vertex_count);
        ID3D11ShaderResourceView* TextureFor(const std::string& guid,
            const Assets::AssetDatabase* asset_database);

        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> vertex_buffer_;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> white_texture_;
        std::unordered_map<std::string,
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> texture_cache_;
        std::vector<Vertex> vertices_;
        std::size_t vertex_capacity_ = 0;
    };

    std::vector<DirectX::XMFLOAT3> BuildCatmullRomLinePath(
        const std::vector<DirectX::XMFLOAT3>& control_points,
        int smoothing, bool closed);
}

namespace ReplayEngine::Components
{
    class LineRendererComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(LineRendererComponent)

    public:
        enum UVMode : int { Stretch = 0, RepeatByDistance = 1 };
        enum FillMode : int { Solid = 0, Linear = 1 };

        LineRendererComponent();

        const std::vector<Reflection::PropertyDesc>* DynamicProperties()
            const noexcept override;
        void OnSerialize(Reflection::PropertyBag& output) const override;
        void OnDeserialize(const Reflection::PropertyBag& input) override;
        void OnPropertyChanged(const char* property_name) override;

        Rendering::LineStrokeStyle StrokeStyle() const;

        int point_count = 2;
        std::vector<DirectX::XMFLOAT3> points;
        int smoothing = 0;
        bool closed = false;
        float width_start = 0.1f;
        float width_end = 0.1f;
        bool billboard = true;
        int uv_mode = Stretch;
        float uv_tiling = 1.0f;
        float uv_scroll = 0.0f;
        Reflection::AssetReference texture;
        DirectX::XMFLOAT4 fill_color{ 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 fill_color_2{ 1.0f, 1.0f, 1.0f, 1.0f };
        int fill_mode = Solid;
        float trim_start = 0.0f;
        float trim_end = 1.0f;
        float trim_offset = 0.0f;

    private:
        void ResizePoints();
        void RebuildDynamicProperties();

        std::vector<Reflection::PropertyDesc> dynamic_properties_;
    };
}
