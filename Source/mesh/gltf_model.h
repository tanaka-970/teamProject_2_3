#pragma once

#include <d3d11.h>
#include <wrl.h>
#include <DirectXMath.h>

#include <cstdint>
#include <string>
#include <vector>
#include "../../RePlayEngine/Physics/SphereCast.h"

// glTF 2.0／GLBの静的メッシュ資産。従来のメタデータだけの仮実装を置き換える。
// スキンとアニメーションは能力情報として保持し、実行時再生は別コンポーネントで扱う。
class gltf_model
{
public:
    explicit gltf_model(ID3D11Device* device, const std::string& filename);
    ~gltf_model() = default;

    bool IsLoaded() const noexcept { return loaded_; }
    bool HasSkins() const noexcept { return has_skins_; }
    bool HasAnimations() const noexcept { return has_animations_; }
    const std::string& Error() const noexcept { return error_; }
    size_t PrimitiveCount() const noexcept { return primitives_.size(); }
    const std::vector<ReplayEngine::Physics::Triangle>& CollisionTriangles() const noexcept
    {
        return collision_triangles_;
    }

    void render(ID3D11DeviceContext* context,
        const DirectX::XMFLOAT4X4& world,
        const DirectX::XMFLOAT4& tint = { 1, 1, 1, 1 },
        ID3D11PixelShader* alternative_pixel_shader = nullptr);

private:
    struct Vertex
    {
        DirectX::XMFLOAT3 position{};
        DirectX::XMFLOAT3 normal{ 0, 1, 0 };
        DirectX::XMFLOAT2 texcoord{};
    };

    struct Primitive
    {
        Microsoft::WRL::ComPtr<ID3D11Buffer> vertex_buffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> index_buffer;
        DirectX::XMFLOAT4X4 node_transform{};
        uint32_t index_count = 0;
        int material = -1;
    };

    struct Material
    {
        DirectX::XMFLOAT4 base_color{ 1, 1, 1, 1 };
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> base_color_texture;
    };

    struct Constants
    {
        DirectX::XMFLOAT4X4 world;
        DirectX::XMFLOAT4 material_color;
    };

    bool Load(ID3D11Device* device, const std::string& filename);

    std::vector<Primitive> primitives_;
    std::vector<Material> materials_;
    std::vector<ReplayEngine::Physics::Triangle> collision_triangles_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> white_texture_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constant_buffer_;
    std::string error_;
    bool loaded_ = false;
    bool has_skins_ = false;
    bool has_animations_ = false;
};
