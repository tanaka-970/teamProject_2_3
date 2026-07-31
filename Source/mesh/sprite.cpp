#include "sprite.h"
#include "misc.h"
#include <sstream>
#include <memory> // unique_ptr のために追加
#include <cstdint>
#include<WICTextureLoader.h>

using namespace DirectX;
using namespace Microsoft::WRL; // ComPtrのために追加
sprite::sprite(ID3D11Device* device, const wchar_t* filename, const char* pixel_shader_cso)
{
    HRESULT hr{ S_OK };
    // ①頂点情報のセット
    vertex vertices[] =
    {
        // 左上: x = -1.0, y = +1.0
    { { -1.0f, +1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0, 0 } },//xyz rgba
    // 右上: x = +1.0, y = +1.0
    { { +1.0f, +1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1, 0 } },
    // 左下: x = -1.0, y = -1.0
    { { -1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f }, { 0, 1 }   },
    // 右下: x = +1.0, y = -1.0
    { { +1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f }, { 1, 1 } },
    };
    {
        //③画像ファイルのロードとシェーダーリソースビューオブジェクトの生成

        ComPtr<ID3D11Resource> resource;
        const auto create_fallback_texture = [&]
        {
            const std::uint32_t transparent = 0x00000000u;
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA initial_data{ &transparent, sizeof(transparent), 0 };
            ComPtr<ID3D11Texture2D> texture;
            HRESULT fallback_hr = device->CreateTexture2D(&desc, &initial_data, texture.GetAddressOf());
            if (SUCCEEDED(fallback_hr))
                fallback_hr = device->CreateShaderResourceView(texture.Get(), nullptr,
                    shader_resource_view.ReleaseAndGetAddressOf());
            if (SUCCEEDED(fallback_hr)) resource = texture;
            return fallback_hr;
        };
        if (filename)
        {
            hr = DirectX::CreateWICTextureFromFile(
                device, filename, resource.GetAddressOf(), shader_resource_view.GetAddressOf());
            if (FAILED(hr))
            {
                OutputDebugStringW(L"[sprite] WIC load failed; using transparent fallback: ");
                OutputDebugStringW(filename);
                OutputDebugStringW(L"\n");
                resource.Reset();
                hr = create_fallback_texture();
            }
        }
        else
        {
            const std::uint32_t white = 0xffffffffu;
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = 1;
            desc.Height = 1;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA initial_data{ &white, sizeof(white), 0 };

            ComPtr<ID3D11Texture2D> texture;
            hr = device->CreateTexture2D(&desc, &initial_data, texture.GetAddressOf());
            _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
            hr = device->CreateShaderResourceView(texture.Get(), nullptr, shader_resource_view.GetAddressOf());
            _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
            resource = texture;
        }

        // テクスチャ情報の取得
        ComPtr<ID3D11Texture2D> texture2d;
        hr = resource.As(&texture2d); // QueryInterfaceの代わり
        _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

        texture2d->GetDesc(&this->texture2d_desc);
        // Release() 呼び出しは不要
    }
    // ②頂点バッファオブジェクトの生成
    D3D11_BUFFER_DESC buffer_desc{};
    buffer_desc.ByteWidth = sizeof(vertices);
    buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    buffer_desc.MiscFlags = 0;
    buffer_desc.StructureByteStride = 0;
    D3D11_SUBRESOURCE_DATA subresource_data{};
    subresource_data.pSysMem = vertices;
    subresource_data.SysMemPitch = 0;
    subresource_data.SysMemSlicePitch = 0;

    hr = device->CreateBuffer(&buffer_desc, &subresource_data, vertex_buffer.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
    
    //アルファモードバッファに設定を書き込み。
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(DirectX::XMFLOAT4); // 16バイト境界
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;//定数バッファとして扱うという意味
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = device->CreateBuffer(&cbDesc, nullptr, alphaModeBuffer.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
    
    // ③頂点シェーダーオブジェクトの生成
    {
        const char* cso_name{ "Shader\\compiled\\sprite_vs.cso" };
        FILE* fp{};
        fopen_s(&fp, cso_name, "rb");
        _ASSERT_EXPR_A(fp, "CSO File not found");

        fseek(fp, 0, SEEK_END);
        long cso_sz{ ftell(fp) };
        fseek(fp, 0, SEEK_SET);

        std::unique_ptr<unsigned char[]> cso_data{ std::make_unique<unsigned char[]>(cso_sz) };
        fread(cso_data.get(), cso_sz, 1, fp);
        fclose(fp);
        hr = device->CreateVertexShader(cso_data.get(), cso_sz, nullptr, vertex_shader.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
        // ④入力レイアウトオブジェクトの生成
        D3D11_INPUT_ELEMENT_DESC input_element_desc[]
        {

            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        hr = device->CreateInputLayout(input_element_desc, _countof(input_element_desc), cso_data.get(), cso_sz, input_layout.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
    }

    // ⑤ピクセルシェーダーオブジェクトの生成
    {
        std::string cso_path = "Shader\\compiled\\";
        cso_path += pixel_shader_cso ? pixel_shader_cso : "sprite_ps.cso";
        FILE* fp{};
        fopen_s(&fp, cso_path.c_str(), "rb");
        _ASSERT_EXPR_A(fp, "CSO File not found");

        fseek(fp, 0, SEEK_END);
        long cso_sz{ ftell(fp) };
        fseek(fp, 0, SEEK_SET);

        std::unique_ptr<unsigned char[]> cso_data{ std::make_unique<unsigned char[]>(cso_sz) };
        fread(cso_data.get(), cso_sz, 1, fp);
        fclose(fp);

        hr = device->CreatePixelShader(cso_data.get(), cso_sz, nullptr, pixel_shader.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
    }
}
//ユニット１０
//文字描画
void sprite::textout(ID3D11DeviceContext* immediate_context, std::string s,
    float x, float y, float w, float h,
    float r, float g, float b, float a)
{
    // フォント画像内の1文字あたりのサイズ（テクセル単位）を計算
    float sw = static_cast<float>(texture2d_desc.Width / 16);
    float sh = static_cast<float>(texture2d_desc.Height / 16);

    float carriage = 0; // 文字の送り幅

    for (const char c : s)
    {
        // 切り出し範囲の計算
        // c & 0x0F はアスキーコードの下位4ビット（列番号 0~15）
        // c >> 4 は上位4ビット（行番号 0~15）
        render(immediate_context, x + carriage, y, w, h, r, g, b, a, 0,
            sw * (c & 0x0F), sh * (c >> 4), sw, sh);

        // 次の文字の位置へ移動
        carriage += w;
    }
}
void sprite::render(ID3D11DeviceContext* immediate_context, float dx, float dy, float dw, float dh)
{
    render(immediate_context, dx, dy, dw, dh, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f);
}
void sprite::render(ID3D11DeviceContext* immediate_context,
    float dx, float dy,
    float  dw, float dh,
    float r, float g, float b, float a,
    float angle/*degree*/
)
{
    render(immediate_context, dx, dy, dw, dh, r, g, b, a, angle,
        0.0f, 0.0f,
        static_cast<float>(texture2d_desc.Width),
        static_cast<float>(texture2d_desc.Height)
    );
   
}

// 追加：切り抜き範囲を指定可能なオーバーロードされた render メソッド
void sprite::render(ID3D11DeviceContext* immediate_context,
    float dx, float dy, float dw, float dh,
    float r, float g, float b, float a,
    float angle,
    float sx, float sy, float sw, float sh)
{
    // ① テクセル座標(pixel)からUV座標(0.0~1.0)へ変換
    float u0 = sx / texture2d_desc.Width;
    float v0 = sy / texture2d_desc.Height;
    float u1 = (sx + sw) / texture2d_desc.Width;
    float v1 = (sy + sh) / texture2d_desc.Height;

    // スクリーン（ビューポート）のサイズを取得する
    D3D11_VIEWPORT viewport{};
    UINT num_viewports{ 1 };
    immediate_context->RSGetViewports(&num_viewports, &viewport);

    float x0{ dx }, y0{ dy };
    float x1{ dx + dw }, y1{ dy };
    float x2{ dx }, y2{ dy + dh };
    float x3{ dx + dw }, y3{ dy + dh };

    auto rotate = [](float& x, float& y, float cx, float cy, float angle)
        {
            x -= cx;
            y -= cy;
            float cos{ cosf(DirectX::XMConvertToRadians(angle)) };
            float sin{ sinf(DirectX::XMConvertToRadians(angle)) };
            float tx{ x }, ty{ y };
            x = cos * tx - sin * ty;
            y = sin * tx + cos * ty;
            x += cx;
            y += cy;
        };
    float cx = dx + dw / 2;
    float cy = dy + dh / 2;
    rotate(x0, y0, cx, cy, angle);
    rotate(x1, y1, cx, cy, angle);
    rotate(x2, y2, cx, cy, angle);
    rotate(x3, y3, cx, cy, angle);

    x0 = 2.0f * x0 / viewport.Width - 1.0f;
    y0 = 1.0f - 2.0f * y0 / viewport.Height;
    x1 = 2.0f * x1 / viewport.Width - 1.0f;
    y1 = 1.0f - 2.0f * y1 / viewport.Height;
    x2 = 2.0f * x2 / viewport.Width - 1.0f;
    y2 = 1.0f - 2.0f * y2 / viewport.Height;
    x3 = 2.0f * x3 / viewport.Width - 1.0f;
    y3 = 1.0f - 2.0f * y3 / viewport.Height;

    // ④ 計算結果で頂点バッファオブジェクトを更新する (Map/Unmap)
    HRESULT hr{ S_OK };
    D3D11_MAPPED_SUBRESOURCE mapped_subresource{};

    hr = immediate_context->Map(vertex_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_subresource);
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
    vertex* vertices{ reinterpret_cast<vertex*>(mapped_subresource.pData) };
    if (vertices != nullptr)
    {
        vertices[0].position = { x0, y0, 0 };
        vertices[1].position = { x1, y1, 0 };
        vertices[2].position = { x2, y2, 0 };
        vertices[3].position = { x3, y3, 0 };

        // ★計算したUV座標を代入
        vertices[0].texcoord = { u0, v0 };
        vertices[1].texcoord = { u1, v0 };
        vertices[2].texcoord = { u0, v1 };
        vertices[3].texcoord = { u1, v1 };

        vertices[0].color = vertices[1].color = vertices[2].color = vertices[3].color = { r, g, b, a };
    }
    immediate_context->Unmap(vertex_buffer.Get(), 0);
    // アルファモードを定数バッファへ書き込み
    D3D11_MAPPED_SUBRESOURCE mapped;
    immediate_context->Map(alphaModeBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    {
        int* data = static_cast<int*>(mapped.pData);
        data[0] = static_cast<int>(mAlphaMode);
        //追加のアルファモード設定を追加する場合は、この下で()[0]以外に書き込む。
    }
    immediate_context->Unmap(alphaModeBuffer.Get(), 0);
    // ⑤ 描画の実行
    UINT stride{ sizeof(vertex) };
    UINT offset{ 0 };
    // 修正後
    immediate_context->IASetVertexBuffers(0, 1, vertex_buffer.GetAddressOf(), &stride, &offset);
    immediate_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    immediate_context->IASetInputLayout(input_layout.Get());
    immediate_context->VSSetShader(vertex_shader.Get(), nullptr, 0);
    immediate_context->PSSetShader(pixel_shader.Get(), nullptr, 0);
    immediate_context->PSSetShaderResources(0, 1, shader_resource_view.GetAddressOf());
    immediate_context->Draw(4, 0);
}
