#include "framework.h"
#include "shader.h"
#include "texture.h"
#include "skinned_mesh.h"
#include "gltf_model.h"

framework::framework(HWND hwnd) : hwnd(hwnd) {}

bool framework::is_fullscreen() const
{
    return borderless_fullscreen;
}

bool framework::toggle_fullscreen()
{
    if (!IsWindow(hwnd)) return false;

    if (!borderless_fullscreen)
    {
        MONITORINFO monitor_info{};
        monitor_info.cbSize = sizeof(monitor_info);
        const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        if (!GetMonitorInfoW(monitor, &monitor_info) || !GetWindowRect(hwnd, &windowed_rect))
        {
            return false;
        }

        windowed_style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
        windowed_ex_style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
        const DWORD fullscreen_style =
            (windowed_style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP | WS_VISIBLE;
        const DWORD fullscreen_ex_style = windowed_ex_style &
            ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);

        borderless_fullscreen = true;
        SetWindowLongPtrW(hwnd, GWL_STYLE, static_cast<LONG_PTR>(fullscreen_style));
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, static_cast<LONG_PTR>(fullscreen_ex_style));
        SetWindowPos(hwnd, HWND_TOP,
            monitor_info.rcMonitor.left, monitor_info.rcMonitor.top,
            monitor_info.rcMonitor.right - monitor_info.rcMonitor.left,
            monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top,
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    else
    {
        borderless_fullscreen = false;
        SetWindowLongPtrW(hwnd, GWL_STYLE, static_cast<LONG_PTR>(windowed_style));
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, static_cast<LONG_PTR>(windowed_ex_style));
        SetWindowPos(hwnd, HWND_NOTOPMOST,
            windowed_rect.left, windowed_rect.top,
            windowed_rect.right - windowed_rect.left,
            windowed_rect.bottom - windowed_rect.top,
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    return true;
}

bool framework::resize_back_buffers(UINT width, UINT height)
{
    if (!device || !immediate_context || !swap_chain || width == 0 || height == 0)
    {
        return false;
    }

    ID3D11RenderTargetView* null_target = nullptr;
    immediate_context->OMSetRenderTargets(1, &null_target, nullptr);
    immediate_context->ClearState();
    render_target_view.Reset();
    depth_stencil_view.Reset();

    HRESULT result = swap_chain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(result))
    {
        OutputDebugStringA("[Window] Back-buffer resize failed.\n");
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> back_buffer;
    result = swap_chain->GetBuffer(0, IID_PPV_ARGS(back_buffer.GetAddressOf()));
    if (FAILED(result)) return false;
    result = device->CreateRenderTargetView(back_buffer.Get(), nullptr, render_target_view.GetAddressOf());
    if (FAILED(result)) return false;

    D3D11_TEXTURE2D_DESC depth_desc{};
    depth_desc.Width = width;
    depth_desc.Height = height;
    depth_desc.MipLevels = 1;
    depth_desc.ArraySize = 1;
    depth_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depth_desc.SampleDesc.Count = 1;
    depth_desc.Usage = D3D11_USAGE_DEFAULT;
    depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> depth_buffer;
    result = device->CreateTexture2D(&depth_desc, nullptr, depth_buffer.GetAddressOf());
    if (FAILED(result)) return false;
    result = device->CreateDepthStencilView(depth_buffer.Get(), nullptr, depth_stencil_view.GetAddressOf());
    if (FAILED(result)) return false;

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MaxDepth = 1.0f;
    immediate_context->RSSetViewports(1, &viewport);

    framebuffers[0] = std::make_unique<framebuffer>(device.Get(), width, height);
    bloom_pass.Initialize(device.Get(), width, height);
    const bool deferred_requested = enable_deferred;
    const bool deferred_ready = deferred.initialize(device.Get(), width, height);
    enable_deferred = deferred_requested && deferred_ready;

    client_width = width;
    client_height = height;
    if (game_scene)
    {
        game_scene->Gameplay().SetAspect(static_cast<float>(width) / static_cast<float>(height));
    }
    return true;
}

void framework::apply_pending_resize()
{
    if (!resize_pending) return;
    resize_pending = false;
    resize_back_buffers(pending_client_width, pending_client_height);
}

bool framework::uninitialize() { return true; }
framework::~framework() {}

