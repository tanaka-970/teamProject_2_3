#include "TiledDeferredPass.h"

#include "../../../Source/core/shader.h"

#include <algorithm>

using namespace DirectX;

namespace ReplayEngine::Rendering
{
    bool TiledDeferredPass::CreateLightBuffer(ID3D11Device* device, uint32_t capacity)
    {
        light_buffer_.Reset();
        light_srv_.Reset();
        light_capacity_ = 0;
        if (!device || capacity == 0) return false;

        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(sizeof(Light) * capacity);
        // 毎フレーム内容が変わるのでDYNAMIC+Mapで更新する。
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(Light);
        if (FAILED(device->CreateBuffer(&desc, nullptr, light_buffer_.GetAddressOf())))
            return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_UNKNOWN; // StructuredBufferはUNKNOWN固定
        view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        view.Buffer.FirstElement = 0;
        view.Buffer.NumElements = capacity;
        if (FAILED(device->CreateShaderResourceView(light_buffer_.Get(), &view,
            light_srv_.GetAddressOf()))) return false;

        light_capacity_ = capacity;
        return true;
    }

    bool TiledDeferredPass::Initialize(ID3D11Device* device, uint32_t width, uint32_t height)
    {
        initialized_ = false;
        lighting_shader_.Reset();
        constants_.Reset();
        if (!device || width < kTileSize || height < kTileSize) return false;

        device_ = device;
        width_ = width;
        height_ = height;
        // 端のタイルが欠けないよう切り上げる。CS側は画面外スレッドを捨てる。
        tile_count_x_ = (width + kTileSize - 1) / kTileSize;
        tile_count_y_ = (height + kTileSize - 1) / kTileSize;

        if (!CreateLightBuffer(device, kMaxLightCapacity)) return false;

        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = sizeof(Constants);
        buffer.Usage = D3D11_USAGE_DEFAULT;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device->CreateBuffer(&buffer, nullptr, constants_.GetAddressOf())))
            return false;

        create_cs_from_cso(device, "tiled_deferred_lighting_cs.cso",
            lighting_shader_.GetAddressOf());

        initialized_ = lighting_shader_ != nullptr;
        return initialized_;
    }

    void TiledDeferredPass::AddPointLight(const XMFLOAT3& position, float radius,
        const XMFLOAT3& color, float intensity)
    {
        if (lights_.size() >= kMaxLightCapacity || radius <= 0.0f) return;
        Light light{};
        light.position_radius = { position.x, position.y, position.z, radius };
        light.color_intensity = { color.x, color.y, color.z, intensity };
        light.direction_cone = { 0.0f, -1.0f, 0.0f, 1.0f };
        light.params = { 0.0f, static_cast<float>(LightType::Point), 0.0f, 0.0f };
        lights_.push_back(light);
    }

    void TiledDeferredPass::AddSpotLight(const XMFLOAT3& position, float radius,
        const XMFLOAT3& direction, float inner_cosine, float outer_cosine,
        const XMFLOAT3& color, float intensity)
    {
        if (lights_.size() >= kMaxLightCapacity || radius <= 0.0f) return;
        Light light{};
        light.position_radius = { position.x, position.y, position.z, radius };
        light.color_intensity = { color.x, color.y, color.z, intensity };
        light.direction_cone = { direction.x, direction.y, direction.z, inner_cosine };
        // 内コーンが外コーンより内側(cosが大きい)であることを保証する。
        light.params = { (std::min)(outer_cosine, inner_cosine - 1.0e-4f),
            static_cast<float>(LightType::Spot), 0.0f, 0.0f };
        lights_.push_back(light);
    }

    bool TiledDeferredPass::Dispatch(ID3D11DeviceContext* context,
        ID3D11UnorderedAccessView* output_uav,
        ID3D11ShaderResourceView* const gbuffer[4],
        ID3D11ShaderResourceView* depth,
        ID3D11ShaderResourceView* ambient_occlusion,
        ID3D11ShaderResourceView* screen_reflection)
    {
        if (!initialized_ || !context || !output_uav || !gbuffer || !depth) return false;

        // ライト配列をアップロード。0件でも平行光源とIBLは評価するのでDispatchは行う。
        if (light_srv_)
        {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(context->Map(light_buffer_.Get(), 0,
                D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
                if (!lights_.empty())
                {
                    const size_t count = (std::min)(lights_.size(),
                        static_cast<size_t>(light_capacity_));
                    std::memcpy(mapped.pData, lights_.data(), sizeof(Light) * count);
                }
                context->Unmap(light_buffer_.Get(), 0);
            }
        }

        Constants data{};
        data.counts = {
            static_cast<int32_t>((std::min)(lights_.size(),
                static_cast<size_t>(light_capacity_))),
            static_cast<int32_t>(tile_count_x_),
            static_cast<int32_t>(tile_count_y_),
            debug_heatmap ? 1 : 0 };
        data.params = { (std::max)(heatmap_scale, 1.0f), 0.0f, 0.0f, 0.0f };
        context->UpdateSubresource(constants_.Get(), 0, nullptr, &data, 0, 0);
        context->CSSetConstantBuffers(kConstantSlot, 1, constants_.GetAddressOf());

        // t0-t3=G-Buffer, t6=深度, t7=SSAO, t8=SSR。
        ID3D11ShaderResourceView* views[9]{
            gbuffer[0], gbuffer[1], gbuffer[2], gbuffer[3],
            nullptr, nullptr, depth, ambient_occlusion, screen_reflection };
        context->CSSetShaderResources(0, 9, views);
        context->CSSetShaderResources(kLightBufferSlot, 1, light_srv_.GetAddressOf());

        ID3D11UnorderedAccessView* uavs[1]{ output_uav };
        context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
        context->CSSetShader(lighting_shader_.Get(), nullptr, 0);
        context->Dispatch(tile_count_x_, tile_count_y_, 1);

        // 次のパスが同じリソースを書き込み対象にできるよう必ず外す。
        ID3D11UnorderedAccessView* null_uavs[1]{ nullptr };
        context->CSSetUnorderedAccessViews(0, 1, null_uavs, nullptr);
        ID3D11ShaderResourceView* null_views[9]{};
        context->CSSetShaderResources(0, 9, null_views);
        ID3D11ShaderResourceView* null_light[1]{ nullptr };
        context->CSSetShaderResources(kLightBufferSlot, 1, null_light);
        context->CSSetShader(nullptr, nullptr, 0);
        return true;
    }
}
