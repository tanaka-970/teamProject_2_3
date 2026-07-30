#include "sprite_batch.h"
#include "misc.h"
#include <sstream>
#include <memory> // unique_ptr のために追加
#include<WICTextureLoader.h>
#include<vector>
#include "texture.h" // 追加：テクスチャモジュール
#include "shader.h"  // 追加：シェーダーモジュール
using namespace DirectX;
using namespace std;
using namespace Microsoft::WRL;
sprite_batch::sprite_batch(ID3D11Device* device, const wchar_t* filename, size_t max_sprites)
	: max_vertices(max_sprites * 6)
{
	HRESULT hr{ S_OK };
    // ①頂点情報のセット
    //vertex vertices[] =
    //{
    //    // 左上: x = -1.0, y = +1.0
    //{ { -1.0f, +1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0, 0 } },//xyz rgba
    //// 右上: x = +1.0, y = +1.0
    //{ { +1.0f, +1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1, 0 } },
    //// 左下: x = -1.0, y = -1.0
    //{ { -1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f }, { 0, 1 }   },
    //// 右下: x = +1.0, y = -1.0
    //{ { +1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f }, { 1, 1 } },
    //};
    {
        //③画像ファイルのロードとシェーダーリソースビューオブジェクトの生成

        ComPtr<ID3D11Resource> resource;
        hr = DirectX::CreateWICTextureFromFile(device, filename, resource.GetAddressOf(), shader_resource_view.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

        // テクスチャ情報の取得
        ComPtr<ID3D11Texture2D> texture2d;
        hr = resource.As(&texture2d); // QueryInterfaceの代わり
        _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

        texture2d->GetDesc(&this->texture2d_desc);
    }
    // ②頂点バッファオブジェクトの生成
    D3D11_BUFFER_DESC buffer_desc{};
    buffer_desc.ByteWidth = sizeof(vertex) * static_cast<UINT>(max_vertices);
    buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    buffer_desc.MiscFlags = 0;
    buffer_desc.StructureByteStride = 0;
   


    // &vertex_buffer ではなく .GetAddressOf() を使用
    hr = device->CreateBuffer(&buffer_desc, NULL, vertex_buffer.GetAddressOf());
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
        const char* cso_name{ "Shader\\compiled\\sprite_ps.cso" }; // ここは ps.cso
        FILE* fp{};
        fopen_s(&fp, cso_name, "rb");
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
sprite_batch::~sprite_batch()
{
    //メモリ解法必要なし

}
//beginメンバ関数の実装
void sprite_batch::begin(ID3D11DeviceContext* immediate_context)
{
    // 描画用頂点情報をクリア
    vertices.clear();
    // 引数として渡す際は .Get() を使用（省略可能な場合も多いですが明示的に）
    immediate_context->VSSetShader(vertex_shader.Get(), nullptr, 0);
    immediate_context->PSSetShader(pixel_shader.Get(), nullptr, 0);
    immediate_context->PSSetShaderResources(0, 1, shader_resource_view.GetAddressOf());
}
void sprite_batch::render(ID3D11DeviceContext* immediate_context,
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
void sprite_batch::render(ID3D11DeviceContext* immediate_context,
    float dx, float dy, float dw, float dh,
    float r, float g, float b, float a,
    float angle,
    float sx, float sy, float sw, float sh)
{
    D3D11_VIEWPORT viewport{};
    UINT num_viewports{ 1 };
    immediate_context->RSGetViewports(&num_viewports, &viewport);

    // 四隅の頂点座標の初期化
    float x0{ dx };      float y0{ dy };      // 左上
    float x1{ dx + dw }; float y1{ dy };      // 右上
    float x2{ dx };      float y2{ dy + dh }; // 左下
    float x3{ dx + dw }; float y3{ dy + dh }; // 右下

    // ★最適化：三角関数の処理を外部（頂点計算の前）に移す
    float cx = dx + dw * 0.5f;
    float cy = dy + dh * 0.5f;

    // 回転の中心（原点）へ移動
    x0 -= cx; y0 -= cy;
    x1 -= cx; y1 -= cy;
    x2 -= cx; y2 -= cy;
    x3 -= cx; y3 -= cy;

    // ★最適化：ラムダ式を排除し、直接インラインで書き下ろす
    // 角度をラジアンに変換し、sin/cosを各1回だけ計算
    float cos = cosf(angle * 0.01745f);
    float sin = sinf(angle * 0.01745f);

    float tx, ty;
    // 左上回転
    tx = x0; ty = y0;
    x0 = cos * tx + -sin * ty; y0 = sin * tx + cos * ty;
    // 右上回転
    tx = x1; ty = y1;
    x1 = cos * tx + -sin * ty; y1 = sin * tx + cos * ty;
    // 左下回転
    tx = x2; ty = y2;
    x2 = cos * tx + -sin * ty; y2 = sin * tx + cos * ty;
    // 右下回転
    tx = x3; ty = y3;
    x3 = cos * tx + -sin * ty; y3 = sin * tx + cos * ty;

    // 元の位置に戻す
    x0 += cx; y0 += cy;
    x1 += cx; y1 += cy;
    x2 += cx; y2 += cy;
    x3 += cx; y3 += cy;

    // NDC空間への変換（-1.0 ～ 1.0）
    x0 = 2.0f * x0 / viewport.Width - 1.0f;
    y0 = 1.0f - 2.0f * y0 / viewport.Height;
    x1 = 2.0f * x1 / viewport.Width - 1.0f;
    y1 = 1.0f - 2.0f * y1 / viewport.Height;
    x2 = 2.0f * x2 / viewport.Width - 1.0f;
    y2 = 1.0f - 2.0f * y2 / viewport.Height;
    x3 = 2.0f * x3 / viewport.Width - 1.0f;
    y3 = 1.0f - 2.0f * y3 / viewport.Height;

    // UV座標の計算
    float u0{ sx / texture2d_desc.Width };
    float v0{ sy / texture2d_desc.Height };
    float u1{ (sx + sw) / texture2d_desc.Width };
    float v1{ (sy + sh) / texture2d_desc.Height };

    // 三角形リスト形式で頂点データを蓄積
    vertices.push_back(vertex{ { x0, y0, 0 }, { r, g, b, a }, { u0, v0 } });
    vertices.push_back(vertex{ { x1, y1, 0 }, { r, g, b, a }, { u1, v0 } });
    vertices.push_back(vertex{ { x2, y2, 0 }, { r, g, b, a }, { u0, v1 } });
    vertices.push_back(vertex{ { x2, y2, 0 }, { r, g, b, a }, { u0, v1 } });
    vertices.push_back(vertex{ { x1, y1, 0 }, { r, g, b, a }, { u1, v0 } });
    vertices.push_back(vertex{ { x3, y3, 0 }, { r, g, b, a }, { u1, v1 } });
}
void  sprite_batch::end(ID3D11DeviceContext* immediate_context)
    {
    HRESULT hr{ S_OK };
    D3D11_MAPPED_SUBRESOURCE mapped_subresource{};

    // 手順 4-5：頂点バッファをマップする
    hr = immediate_context->Map(vertex_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_subresource);
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    // 手順 8-9：頂点数チェック
    size_t vertex_count = vertices.size();
    _ASSERT_EXPR(max_vertices >= vertex_count, "Buffer overflow");

    // 手順 10-15：memcpy_s による頂点データの転送
    vertex* data{ reinterpret_cast<vertex*>(mapped_subresource.pData) };
    if (data != nullptr)
    {
        const vertex* p = vertices.data(); // vertices.data() は &vertices[0] と同義
        memcpy_s(data, max_vertices * sizeof(vertex), p, vertex_count * sizeof(vertex));
    }

    // 手順 16：アンマップ
    immediate_context->Unmap(vertex_buffer.Get(), 0);

    // 手順 18-20：頂点バッファのセット
    UINT stride{ sizeof(vertex) };
    UINT offset{ 0 };
    immediate_context->IASetVertexBuffers(0, 1, vertex_buffer.GetAddressOf(), &stride, &offset);

    // 手順 21：トポロジを TRIANGLELIST に設定
    immediate_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // 手順 22：レイアウトとシェーダーのセット
    immediate_context->IASetInputLayout(input_layout.Get());
    immediate_context->VSSetShader(vertex_shader.Get(), nullptr, 0);
    immediate_context->PSSetShader(pixel_shader.Get(), nullptr, 0);
    immediate_context->PSSetShaderResources(0, 1, shader_resource_view.GetAddressOf());
    
    // 手順 24：蓄積された全頂点を一気に描画
    immediate_context->Draw(static_cast<UINT>(vertex_count), 0);
    vertices.clear(); // 描画が終わったので頂点リストを空にする
}