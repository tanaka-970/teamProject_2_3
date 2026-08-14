#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "GoldenImage.h"

#include <wincodec.h>
#include <wrl.h>

#include <algorithm>
#include <sstream>
#include <system_error>

#pragma comment(lib, "windowscodecs.lib")

namespace ReplayEngine::Rendering::Capture
{
    namespace
    {
        // WIC は COM。呼ぶ側が初期化済みかどうか分からないので、
        // ここで面倒を見る。すでに初期化されていれば何もしない。
        class ComScope final
        {
        public:
            ComScope()
            {
                const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                // S_FALSE       … すでに同じモードで初期化済み
                // RPC_E_CHANGED_MODE … 別モードで初期化済み。使うぶんには問題ない
                owned_ = (hr == S_OK);
            }
            ~ComScope() { if (owned_) CoUninitialize(); }

            ComScope(const ComScope&) = delete;
            ComScope& operator=(const ComScope&) = delete;

        private:
            bool owned_ = false;
        };

        bool CreateFactory(Microsoft::WRL::ComPtr<IWICImagingFactory>& out,
            std::string& error)
        {
            const HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(out.GetAddressOf()));
            if (FAILED(hr))
            {
                error = "WIC を初期化できません";
                return false;
            }
            return true;
        }

        bool EnsureParentDirectory(const std::filesystem::path& path,
            std::string& error)
        {
            if (path.parent_path().empty()) return true;
            std::error_code code;
            std::filesystem::create_directories(path.parent_path(), code);
            if (code)
            {
                error = "フォルダを作れません: " +
                    path.parent_path().generic_u8string();
                return false;
            }
            return true;
        }
    }

    std::string CompareResult::Summary() const
    {
        std::ostringstream stream;
        if (!compared)
        {
            stream << "比較できていません";
            return stream.str();
        }
        if (size_mismatch)
        {
            stream << "大きさが違います（ウィンドウサイズを合わせてください）";
            return stream.str();
        }
        if (differing_pixels == 0)
        {
            stream << "一致 / " << width << "x" << height
                   << " / " << total_pixels << " ピクセル";
            return stream.str();
        }

        const double percent = total_pixels != 0
            ? (100.0 * static_cast<double>(differing_pixels) /
                static_cast<double>(total_pixels))
            : 0.0;

        stream.setf(std::ios::fixed);
        stream.precision(3);
        stream << "差分あり / " << differing_pixels << " / " << total_pixels
               << " ピクセル (" << percent << "%)"
               << " / 最大差 " << max_channel_delta
               << " / 最初の差分 (" << first_difference_x
               << ", " << first_difference_y << ")";
        return stream.str();
    }

    std::filesystem::path GoldenImage::GoldenPath(const std::string& name)
    {
        return std::filesystem::path("Saved") / "Golden" / (name + ".png");
    }

    std::filesystem::path GoldenImage::LatestPath(const std::string& name)
    {
        return std::filesystem::path("Saved") / "Golden" / (name + "_latest.png");
    }

    std::filesystem::path GoldenImage::DiffPath(const std::string& name)
    {
        return std::filesystem::path("Saved") / "Golden" / (name + "_diff.png");
    }

    bool GoldenImage::CaptureBackBuffer(ID3D11Device* device,
        ID3D11DeviceContext* context, IDXGISwapChain* swap_chain,
        Image& out, std::string& error)
    {
        if (device == nullptr || context == nullptr || swap_chain == nullptr)
        {
            error = "デバイスまたはスワップチェインがありません";
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D11Texture2D> back_buffer;
        HRESULT hr = swap_chain->GetBuffer(0, IID_PPV_ARGS(back_buffer.GetAddressOf()));
        if (FAILED(hr) || !back_buffer)
        {
            error = "バックバッファを取得できません";
            return false;
        }

        D3D11_TEXTURE2D_DESC desc{};
        back_buffer->GetDesc(&desc);

        Microsoft::WRL::ComPtr<ID3D11Texture2D> source = back_buffer;

        // MSAA なら解決してから読む。
        // 解決せずに CopyResource するとサイズ違いで失敗する。
        if (desc.SampleDesc.Count > 1)
        {
            D3D11_TEXTURE2D_DESC resolved_desc = desc;
            resolved_desc.SampleDesc.Count = 1;
            resolved_desc.SampleDesc.Quality = 0;
            resolved_desc.Usage = D3D11_USAGE_DEFAULT;
            resolved_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            resolved_desc.CPUAccessFlags = 0;
            resolved_desc.MiscFlags = 0;

            Microsoft::WRL::ComPtr<ID3D11Texture2D> resolved;
            hr = device->CreateTexture2D(&resolved_desc, nullptr,
                resolved.GetAddressOf());
            if (FAILED(hr))
            {
                error = "MSAA の解決先を作れません";
                return false;
            }
            context->ResolveSubresource(resolved.Get(), 0, back_buffer.Get(), 0,
                desc.Format);
            source = resolved;
            desc = resolved_desc;
        }

        D3D11_TEXTURE2D_DESC staging_desc = desc;
        staging_desc.Usage = D3D11_USAGE_STAGING;
        staging_desc.BindFlags = 0;
        staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        staging_desc.MiscFlags = 0;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        hr = device->CreateTexture2D(&staging_desc, nullptr, staging.GetAddressOf());
        if (FAILED(hr))
        {
            error = "読み出し用テクスチャを作れません";
            return false;
        }

        context->CopyResource(staging.Get(), source.Get());

        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr))
        {
            error = "読み出し用テクスチャを Map できません";
            return false;
        }

        // 対応する形式だけ扱う。
        //
        // 知らない形式のときは黙って変な色にせず、必ず失敗させること。
        // 変な色のまま基準画像になると、以後ずっと嘘の基準で比べ続ける。
        bool swap_red_blue = false;
        switch (desc.Format)
        {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            swap_red_blue = false;
            break;
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8X8_UNORM:
            swap_red_blue = true;
            break;
        default:
            context->Unmap(staging.Get(), 0);
            error = "対応していないバックバッファ形式です (DXGI_FORMAT=" +
                std::to_string(static_cast<int>(desc.Format)) + ")";
            return false;
        }

        out.width = desc.Width;
        out.height = desc.Height;
        out.rgba.assign(static_cast<std::size_t>(desc.Width) * desc.Height * 4u, 0u);

        const auto* rows = static_cast<const std::uint8_t*>(mapped.pData);
        for (std::uint32_t y = 0; y < desc.Height; ++y)
        {
            const std::uint8_t* row = rows + static_cast<std::size_t>(y) * mapped.RowPitch;
            std::uint8_t* destination =
                out.rgba.data() + static_cast<std::size_t>(y) * desc.Width * 4u;
            for (std::uint32_t x = 0; x < desc.Width; ++x)
            {
                const std::uint8_t* pixel = row + static_cast<std::size_t>(x) * 4u;
                if (swap_red_blue)
                {
                    destination[x * 4u + 0] = pixel[2];
                    destination[x * 4u + 1] = pixel[1];
                    destination[x * 4u + 2] = pixel[0];
                }
                else
                {
                    destination[x * 4u + 0] = pixel[0];
                    destination[x * 4u + 1] = pixel[1];
                    destination[x * 4u + 2] = pixel[2];
                }
                // alpha は 255 で固定する。
                // バックバッファの alpha は描画パスによって中身が定まらず、
                // そのまま保存すると PNG ビューアで透けて見えることがある。
                destination[x * 4u + 3] = 255u;
            }
        }

        context->Unmap(staging.Get(), 0);
        return true;
    }

    bool GoldenImage::SavePng(const std::filesystem::path& path,
        const Image& image, std::string& error)
    {
        if (!image.Valid())
        {
            error = "保存する画像が空です";
            return false;
        }
        if (!EnsureParentDirectory(path, error)) return false;

        ComScope com;
        Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
        if (!CreateFactory(factory, error)) return false;

        Microsoft::WRL::ComPtr<IWICStream> stream;
        if (FAILED(factory->CreateStream(stream.GetAddressOf())))
        {
            error = "書き込みストリームを作れません";
            return false;
        }
        if (FAILED(stream->InitializeFromFilename(path.wstring().c_str(), GENERIC_WRITE)))
        {
            error = "書き込めません: " + path.generic_u8string();
            return false;
        }

        Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
        if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr,
            encoder.GetAddressOf())) ||
            FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)))
        {
            error = "PNG エンコーダを作れません";
            return false;
        }

        Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
        Microsoft::WRL::ComPtr<IPropertyBag2> properties;
        if (FAILED(encoder->CreateNewFrame(frame.GetAddressOf(),
            properties.GetAddressOf())) ||
            FAILED(frame->Initialize(properties.Get())))
        {
            error = "PNG フレームを作れません";
            return false;
        }

        WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;
        if (FAILED(frame->SetSize(image.width, image.height)) ||
            FAILED(frame->SetPixelFormat(&format)))
        {
            error = "PNG の形式を設定できません";
            return false;
        }

        const UINT stride = image.width * 4u;
        const UINT total = stride * image.height;
        if (FAILED(frame->WritePixels(image.height, stride, total,
            const_cast<BYTE*>(image.rgba.data()))))
        {
            error = "PNG へ書き出せません";
            return false;
        }

        if (FAILED(frame->Commit()) || FAILED(encoder->Commit()))
        {
            error = "PNG を確定できません";
            return false;
        }
        return true;
    }

    bool GoldenImage::LoadPng(const std::filesystem::path& path,
        Image& out, std::string& error)
    {
        std::error_code code;
        if (!std::filesystem::exists(path, code) || code)
        {
            error = "ファイルがありません: " + path.generic_u8string();
            return false;
        }

        ComScope com;
        Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
        if (!CreateFactory(factory, error)) return false;

        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(factory->CreateDecoderFromFilename(path.wstring().c_str(),
            nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
            decoder.GetAddressOf())))
        {
            error = "画像を開けません: " + path.generic_u8string();
            return false;
        }

        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, frame.GetAddressOf())))
        {
            error = "画像のフレームを読めません";
            return false;
        }

        // 何で保存されていても 32bpp RGBA へそろえてから読む。
        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf())) ||
            FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone, nullptr, 0.0,
                WICBitmapPaletteTypeCustom)))
        {
            error = "画像の形式を変換できません";
            return false;
        }

        UINT width = 0;
        UINT height = 0;
        if (FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0)
        {
            error = "画像の大きさを取れません";
            return false;
        }

        out.width = width;
        out.height = height;
        out.rgba.assign(static_cast<std::size_t>(width) * height * 4u, 0u);

        const UINT stride = width * 4u;
        const UINT total = stride * height;
        if (FAILED(converter->CopyPixels(nullptr, stride, total, out.rgba.data())))
        {
            error = "画像を読み出せません";
            return false;
        }
        return true;
    }

    bool GoldenImage::Compare(const Image& golden, const Image& current,
        int tolerance, CompareResult& out, Image* diff_out)
    {
        out = CompareResult{};

        if (!golden.Valid() || !current.Valid())
        {
            // compared は false のまま。
            // 「読めなかった」を「差分 0」と混同させない。
            return false;
        }

        out.width = current.width;
        out.height = current.height;
        out.total_pixels = current.PixelCount();

        if (golden.width != current.width || golden.height != current.height)
        {
            out.compared = true;
            out.size_mismatch = true;
            return false;
        }

        if (diff_out != nullptr)
        {
            diff_out->width = current.width;
            diff_out->height = current.height;
            diff_out->rgba.assign(out.total_pixels * 4u, 0u);
        }

        const int limit = (std::max)(0, tolerance);
        bool found_first = false;

        for (std::size_t index = 0; index < out.total_pixels; ++index)
        {
            const std::uint8_t* a = golden.rgba.data() + index * 4u;
            const std::uint8_t* b = current.rgba.data() + index * 4u;

            // alpha は見ない。理由はヘッダに書いた。
            int worst = 0;
            for (int channel = 0; channel < 3; ++channel)
            {
                const int delta = std::abs(static_cast<int>(a[channel]) -
                    static_cast<int>(b[channel]));
                worst = (std::max)(worst, delta);
            }

            out.max_channel_delta = (std::max)(out.max_channel_delta, worst);

            const bool differs = worst > limit;
            if (differs)
            {
                ++out.differing_pixels;
                if (!found_first)
                {
                    found_first = true;
                    out.first_difference_x =
                        static_cast<std::uint32_t>(index % current.width);
                    out.first_difference_y =
                        static_cast<std::uint32_t>(index / current.width);
                }
            }

            if (diff_out != nullptr)
            {
                std::uint8_t* d = diff_out->rgba.data() + index * 4u;
                if (differs)
                {
                    // 差分は赤。強さで差の大きさが分かるようにする。
                    d[0] = static_cast<std::uint8_t>(
                        (std::min)(255, 128 + worst * 2));
                    d[1] = 0u;
                    d[2] = 0u;
                }
                else
                {
                    // 一致は暗い灰。元の形が薄く見えると位置が分かりやすい。
                    const int gray = (b[0] + b[1] + b[2]) / 3 / 4;
                    d[0] = static_cast<std::uint8_t>(gray);
                    d[1] = static_cast<std::uint8_t>(gray);
                    d[2] = static_cast<std::uint8_t>(gray);
                }
                d[3] = 255u;
            }
        }

        out.compared = true;
        return true;
    }
}
