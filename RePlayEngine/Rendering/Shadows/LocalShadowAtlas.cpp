#include "LocalShadowAtlas.h"
#include "../RenderStats.h"

#include "../../../Source/core/shader.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace DirectX;

namespace ReplayEngine::Rendering
{
    namespace
    {
        // Point Light の 6 面。TextureCube と同じ順番 (+X,-X,+Y,-Y,+Z,-Z) に
        // 揃えてある。シェーダー側の面選択もこの順番を前提にしている。
        const XMFLOAT3 kFaceForward[LocalShadowAtlas::kPointFaceCount] = {
            {  1.0f,  0.0f,  0.0f },
            { -1.0f,  0.0f,  0.0f },
            {  0.0f,  1.0f,  0.0f },
            {  0.0f, -1.0f,  0.0f },
            {  0.0f,  0.0f,  1.0f },
            {  0.0f,  0.0f, -1.0f },
        };
        const XMFLOAT3 kFaceUp[LocalShadowAtlas::kPointFaceCount] = {
            { 0.0f, 1.0f,  0.0f },
            { 0.0f, 1.0f,  0.0f },
            { 0.0f, 0.0f, -1.0f },
            { 0.0f, 0.0f,  1.0f },
            { 0.0f, 1.0f,  0.0f },
            { 0.0f, 1.0f,  0.0f },
        };
    }

    bool LocalShadowAtlas::Initialize(ID3D11Device* device)
    {
        if (device == nullptr) return false;
        device_ = device;

        // 影マップ本体はここでは作らない。影付きの Point / Spot が
        // 1 つも無い Scene では GPU メモリを一切使わないようにする。
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        sampler.AddressU = sampler.AddressV = sampler.AddressW =
            D3D11_TEXTURE_ADDRESS_BORDER;
        sampler.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
        // 範囲外は「遮蔽なし」。Spot の円錐の外や Point の面の継ぎ目で
        // 黒い縁が出ないよう、境界色は必ず 1.0 にする。
        sampler.BorderColor[0] = sampler.BorderColor[1] =
            sampler.BorderColor[2] = sampler.BorderColor[3] = 1.0f;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device->CreateSamplerState(&sampler,
            comparison_sampler_.GetAddressOf()))) return false;

        D3D11_BUFFER_DESC slice_desc{};
        slice_desc.ByteWidth = static_cast<UINT>(sizeof(Slice) * kSliceCount);
        slice_desc.Usage = D3D11_USAGE_DYNAMIC;
        slice_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        slice_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        slice_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        slice_desc.StructureByteStride = sizeof(Slice);
        if (FAILED(device->CreateBuffer(&slice_desc, nullptr,
            slice_buffer_.GetAddressOf()))) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC slice_view{};
        slice_view.Format = DXGI_FORMAT_UNKNOWN; // StructuredBuffer は UNKNOWN 固定
        slice_view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        slice_view.Buffer.FirstElement = 0;
        slice_view.Buffer.NumElements = kSliceCount;
        if (FAILED(device->CreateShaderResourceView(slice_buffer_.Get(), &slice_view,
            slice_srv_.GetAddressOf()))) return false;

        D3D11_BUFFER_DESC pass_desc{};
        pass_desc.ByteWidth = sizeof(PassConstants);
        pass_desc.Usage = D3D11_USAGE_DEFAULT;
        pass_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device->CreateBuffer(&pass_desc, nullptr,
            pass_constants_.GetAddressOf()))) return false;

        // キャスターの Vertex Shader は CSM のものをそのまま使い回す。
        // どちらも「ワールド座標へ変換して GS へ渡すだけ」で同じ仕事のため、
        // 同じ .hlsl を二重に持たない。差し替わるのはこの GS だけ。
        create_gs_from_cso(device, "local_shadow_caster_gs.cso",
            caster_gs_.GetAddressOf());

        return caster_gs_ != nullptr;
    }

    bool LocalShadowAtlas::EnsureAtlas(ID3D11Device* device, uint32_t resolution)
    {
        resolution = (std::max)(256u, (std::min)(4096u, resolution));
        if (atlas_srv_ != nullptr && resolution_ == resolution) return true;
        if (device == nullptr) return false;

        atlas_.Reset();
        atlas_dsv_.Reset();
        atlas_srv_.Reset();
        resolution_ = 0;

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = resolution;
        desc.Height = resolution;
        desc.MipLevels = 1;
        desc.ArraySize = kSliceCount;
        desc.Format = DXGI_FORMAT_R32_TYPELESS;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, atlas_.GetAddressOf())))
            return false;

        D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};
        dsv.Format = DXGI_FORMAT_D32_FLOAT;
        dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        dsv.Texture2DArray.MipSlice = 0;
        dsv.Texture2DArray.FirstArraySlice = 0;
        dsv.Texture2DArray.ArraySize = kSliceCount;
        if (FAILED(device->CreateDepthStencilView(atlas_.Get(), &dsv,
            atlas_dsv_.GetAddressOf()))) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R32_FLOAT;
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        srv.Texture2DArray.MipLevels = 1;
        srv.Texture2DArray.ArraySize = kSliceCount;
        if (FAILED(device->CreateShaderResourceView(atlas_.Get(), &srv,
            atlas_srv_.GetAddressOf()))) return false;

        viewport_.TopLeftX = 0.0f;
        viewport_.TopLeftY = 0.0f;
        viewport_.Width = static_cast<float>(resolution);
        viewport_.Height = static_cast<float>(resolution);
        viewport_.MinDepth = 0.0f;
        viewport_.MaxDepth = 1.0f;
        resolution_ = resolution;
        return true;
    }

    void LocalShadowAtlas::BeginFrame() noexcept
    {
        next_slice_ = 0;
        cleared_this_frame_ = false;
        for (Slice& slice : slices_)
        {
            // 使われないスライスは「範囲外＝遮蔽なし」になる行列を残す。
            slice = Slice{};
        }
    }

    int LocalShadowAtlas::AllocateSpotSlice() noexcept
    {
        // Spot は先頭側から 1 枚ずつ取る。
        if (next_slice_ >= kMaxSpotShadows) return -1;
        return static_cast<int>(next_slice_++);
    }

    int LocalShadowAtlas::AllocatePointSlices() noexcept
    {
        // Point は 6 面をまとめて取る。Spot 用の領域を跨がないよう、
        // 先頭を必ず kMaxSpotShadows 以降へ寄せる。
        uint32_t base = (std::max)(next_slice_, kMaxSpotShadows);
        // 6 の倍数境界へ揃えて、シェーダー側の面計算を単純に保つ。
        const uint32_t offset = (base - kMaxSpotShadows) % kPointFaceCount;
        if (offset != 0) base += kPointFaceCount - offset;
        if (base + kPointFaceCount > kSliceCount) return -1;
        next_slice_ = base + kPointFaceCount;
        return static_cast<int>(base);
    }

    void LocalShadowAtlas::SetSlice(int slice, const XMFLOAT4X4& view_projection,
        float near_plane, float far_plane, float depth_bias) noexcept
    {
        if (slice < 0 || slice >= static_cast<int>(kSliceCount)) return;
        slices_[slice].view_projection = view_projection;
        slices_[slice].params = { near_plane, far_plane, depth_bias, 0.0f };
    }

    XMFLOAT4X4 LocalShadowAtlas::MakeSpotViewProjection(
        const XMFLOAT3& position, const XMFLOAT3& direction,
        float outer_angle_degrees, float near_plane, float far_plane) noexcept
    {
        XMVECTOR forward = XMVector3Normalize(XMLoadFloat3(&direction));
        if (XMVector3Equal(forward, XMVectorZero()))
            forward = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);

        // 視線とほぼ平行な up を選ぶと LookAt が壊れるので切り替える。
        const float forward_y = XMVectorGetY(forward);
        const XMVECTOR up = std::fabs(forward_y) > 0.99f
            ? XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)
            : XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

        const XMVECTOR eye = XMLoadFloat3(&position);
        const XMMATRIX view = XMMatrixLookToLH(eye, forward, up);

        // 外コーンの全角を FOV にする。半角ではないので 2 倍する。
        // 縁のフィルタ半径ぶんだけ余裕を持たせて、円錐の境界で
        // 影がぶつ切りにならないようにする。
        const float outer = (std::max)(1.0f, (std::min)(179.0f, outer_angle_degrees));
        const float fov = (std::min)(XM_PI * 0.98f,
            XMConvertToRadians(outer) * 2.0f * 1.1f);
        const XMMATRIX projection = XMMatrixPerspectiveFovLH(
            fov, 1.0f, (std::max)(near_plane, 0.01f),
            (std::max)(far_plane, near_plane + 0.1f));

        XMFLOAT4X4 result{};
        XMStoreFloat4x4(&result, view * projection);
        return result;
    }

    XMFLOAT4X4 LocalShadowAtlas::MakePointFaceViewProjection(
        const XMFLOAT3& position, int face,
        float near_plane, float far_plane) noexcept
    {
        const int index = (std::max)(0,
            (std::min)(static_cast<int>(kPointFaceCount) - 1, face));
        const XMVECTOR eye = XMLoadFloat3(&position);
        const XMMATRIX view = XMMatrixLookToLH(eye,
            XMLoadFloat3(&kFaceForward[index]), XMLoadFloat3(&kFaceUp[index]));
        // 立方体の 1 面なので FOV は必ず 90 度。ここを変えると面の境目に
        // 隙間か重なりができる。
        const XMMATRIX projection = XMMatrixPerspectiveFovLH(
            XM_PIDIV2, 1.0f, (std::max)(near_plane, 0.01f),
            (std::max)(far_plane, near_plane + 0.1f));

        XMFLOAT4X4 result{};
        XMStoreFloat4x4(&result, view * projection);
        return result;
    }

    void LocalShadowAtlas::UploadSlices(ID3D11DeviceContext* context)
    {
        if (context == nullptr || slice_buffer_ == nullptr) return;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(slice_buffer_.Get(), 0,
            D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
        std::memcpy(mapped.pData, slices_, sizeof(slices_));
        context->Unmap(slice_buffer_.Get(), 0);
    }

    void LocalShadowAtlas::BeginLight(ID3D11DeviceContext* context,
        int base_slice, int slice_count)
    {
        if (context == nullptr || atlas_dsv_ == nullptr) return;

        // 影マップを読む側 (t13) から必ず外してから書き込む。
        // 外し忘れると D3D11 が SRV と DSV の同時バインドを警告し、
        // 片方を勝手に null にするため影が出なくなる。
        ID3D11ShaderResourceView* null_srv[1] = { nullptr };
        context->PSSetShaderResources(kAtlasSlot, 1, null_srv);
        context->CSSetShaderResources(kAtlasSlot, 1, null_srv);

        context->OMSetRenderTargets(0, nullptr, atlas_dsv_.Get());
        if (!cleared_this_frame_)
        {
            // クリアは 1 フレームに 1 回だけ。ライトごとに消すと
            // 前のライトのスライスまで消えてしまう。
            context->ClearDepthStencilView(atlas_dsv_.Get(),
                D3D11_CLEAR_DEPTH, 1.0f, 0);
            cleared_this_frame_ = true;
        }
        context->RSSetViewports(1, &viewport_);

        PassConstants constants{};
        constants.range = { base_slice, slice_count, 0, 0 };
        context->UpdateSubresource(pass_constants_.Get(), 0, nullptr,
            &constants, 0, 0);

        Stats().CountStateSet(RenderStats::StateKind::Shader, false);
        context->GSSetShader(caster_gs_.Get(), nullptr, 0);
        context->GSSetConstantBuffers(kPassConstantSlot, 1,
            pass_constants_.GetAddressOf());
        context->GSSetShaderResources(kSliceBufferSlot, 1, slice_srv_.GetAddressOf());
        Stats().CountStateSet(RenderStats::StateKind::Shader, false);
        context->PSSetShader(nullptr, nullptr, 0);
    }

    void LocalShadowAtlas::End(ID3D11DeviceContext* context,
        ID3D11RenderTargetView* restore_rtv,
        ID3D11DepthStencilView* restore_dsv,
        const D3D11_VIEWPORT& restore_viewport)
    {
        if (context == nullptr) return;
        ID3D11RenderTargetView* rtv[1] = { restore_rtv };
        context->OMSetRenderTargets(1, rtv, restore_dsv);
        context->RSSetViewports(1, &restore_viewport);
        Stats().CountStateSet(RenderStats::StateKind::Shader, false);
        context->GSSetShader(nullptr, nullptr, 0);
        ID3D11ShaderResourceView* null_srv[1] = { nullptr };
        context->GSSetShaderResources(kSliceBufferSlot, 1, null_srv);
    }

    void LocalShadowAtlas::BindResources(ID3D11DeviceContext* context)
    {
        if (context == nullptr) return;
        context->PSSetSamplers(kSamplerSlot, 1, comparison_sampler_.GetAddressOf());
        context->PSSetShaderResources(kSliceBufferSlot, 1, slice_srv_.GetAddressOf());
        // 影マップが未生成のフレームは null のまま貼る。
        // シェーダー側は「スライス番号 < 0 なら影なし」で判定するので、
        // null が読まれることはない。
        ID3D11ShaderResourceView* atlas[1] = { atlas_srv_.Get() };
        context->PSSetShaderResources(kAtlasSlot, 1, atlas);
    }

    void LocalShadowAtlas::UnbindResources(ID3D11DeviceContext* context)
    {
        if (context == nullptr) return;
        ID3D11ShaderResourceView* null_srv[1] = { nullptr };
        context->PSSetShaderResources(kAtlasSlot, 1, null_srv);
        context->PSSetShaderResources(kSliceBufferSlot, 1, null_srv);
    }

    void LocalShadowAtlas::BindComputeResources(ID3D11DeviceContext* context)
    {
        if (context == nullptr) return;
        context->CSSetSamplers(kSamplerSlot, 1, comparison_sampler_.GetAddressOf());
        context->CSSetShaderResources(kSliceBufferSlot, 1, slice_srv_.GetAddressOf());
        ID3D11ShaderResourceView* atlas[1] = { atlas_srv_.Get() };
        context->CSSetShaderResources(kAtlasSlot, 1, atlas);
    }

    void LocalShadowAtlas::UnbindComputeResources(ID3D11DeviceContext* context)
    {
        if (context == nullptr) return;
        ID3D11ShaderResourceView* null_srv[1] = { nullptr };
        context->CSSetShaderResources(kAtlasSlot, 1, null_srv);
        context->CSSetShaderResources(kSliceBufferSlot, 1, null_srv);
    }
}
