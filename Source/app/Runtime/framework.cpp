#include "framework.h"
#include "shader.h"
#include "texture.h"
#include "skinned_mesh.h"
#include "gltf_model.h"

#include <shlobj.h>

namespace
{
    std::filesystem::path NormalizeAbsolute(std::filesystem::path path)
    {
        std::error_code error;
        if (path.empty()) path = std::filesystem::current_path(error);
        if (error) return path.lexically_normal();
        return std::filesystem::absolute(path, error).lexically_normal();
    }

    std::wstring Utf8ToWide(const std::string& text)
    {
        if (text.empty()) return std::wstring();
        const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(),
            static_cast<int>(text.size()), nullptr, 0);
        if (size <= 0) return std::wstring(text.begin(), text.end());
        std::wstring result(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
            result.data(), size);
        return result;
    }

    std::string SafeFolderName(std::string name)
    {
        for (char& character : name)
        {
            if (character == '<' || character == '>' || character == ':' ||
                character == '"' || character == '/' || character == '\\' ||
                character == '|' || character == '?' || character == '*')
            {
                character = '_';
            }
        }
        while (!name.empty() && (name.back() == ' ' || name.back() == '.'))
            name.pop_back();
        return name.empty() ? "RePlayGame" : name;
    }

    std::filesystem::path LocalAppDataRoot()
    {
        PWSTR known_folder = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE,
            nullptr, &known_folder)) && known_folder != nullptr)
        {
            std::filesystem::path result = known_folder;
            CoTaskMemFree(known_folder);
            if (!result.empty()) return result;
        }

        wchar_t local_app_data[32768]{};
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA",
            local_app_data, static_cast<DWORD>(_countof(local_app_data)));
        if (length != 0 && length < _countof(local_app_data))
        {
            return std::filesystem::path(local_app_data);
        }

        std::error_code error;
        const std::filesystem::path temp = std::filesystem::temp_directory_path(error);
        return error ? std::filesystem::path(".") : temp / "RePlayEngine";
    }

    std::filesystem::path LocalAppDataGameFolder(const std::string& game_name)
    {
        return LocalAppDataRoot() /
            Utf8ToWide(SafeFolderName(game_name));
    }
}

framework::framework(HWND hwnd) : hwnd(hwnd)
{
    std::error_code error;
    configure_content_root(std::filesystem::current_path(error));
}

void framework::configure_content_root(std::filesystem::path content_root)
{
    content_root_path_ = NormalizeAbsolute(std::move(content_root));
    saved_root_path_ = content_root_path_ / "Saved";

    asset_database = ReplayEngine::Assets::AssetDatabase(
        content_path(std::filesystem::path("resources") / "AssetDatabase.replaydb"));
    collision_cooker = ReplayEngine::Physics::MeshCollisionCooker(
        content_path(std::filesystem::path("resources") / ".replay_cache" / "collisions"));
    gltf_model::SetCacheRoot(
        content_path(std::filesystem::path("resources") / ".replay_cache"));
    set_shutdown_log_folder(saved_root_path_);
}

void framework::configure_standalone_game(std::filesystem::path content_root,
    std::string game_name)
{
    configure_content_root(std::move(content_root));

    standalone_game_mode = true;
    standalone_game_name = SafeFolderName(std::move(game_name));
    saved_root_path_ = NormalizeAbsolute(LocalAppDataGameFolder(standalone_game_name));
    editor_mode = false;
    edit_mode_active = false;
    editor_session_active = false;
    csharp_auto_reload = false;
    shader_auto_recompile = false;

    collision_cooker = ReplayEngine::Physics::MeshCollisionCooker(
        saved_path(std::filesystem::path("Cache") / "collisions"));
    gltf_model::SetCacheRoot(saved_path(std::filesystem::path("Cache") / "gltf"));
    set_shutdown_log_folder(saved_root_path_);
}

void framework::set_startup_scene_path(std::filesystem::path scene_path)
{
    standalone_startup_scene_path = std::move(scene_path);
}

void framework::set_startup_window_size(UINT width, UINT height) noexcept
{
    client_width = (std::max)(1u, width);
    client_height = (std::max)(1u, height);
    pending_client_width = client_width;
    pending_client_height = client_height;
}

std::filesystem::path framework::content_path(
    const std::filesystem::path& relative) const
{
    if (relative.empty()) return content_root_path_;
    if (relative.is_absolute()) return relative.lexically_normal();
    return (content_root_path_ / relative).lexically_normal();
}

std::filesystem::path framework::saved_path(
    const std::filesystem::path& relative) const
{
    if (relative.empty()) return saved_root_path_;
    if (relative.is_absolute()) return relative.lexically_normal();
    return (saved_root_path_ / relative).lexically_normal();
}

std::filesystem::path framework::collision_cache_path(
    const std::string& identity) const
{
    const std::filesystem::path root = standalone_game_mode
        ? saved_path(std::filesystem::path("Cache") / "collisions")
        : content_path(std::filesystem::path("resources") / ".replay_cache" / "collisions");
    return root / (identity + "_v1.replaycollision");
}

Microsoft::WRL::ComPtr<ID3D11Debug> framework::acquire_d3d11_debug() const noexcept
{
    Microsoft::WRL::ComPtr<ID3D11Debug> debug;
    if (device) device.As(&debug);
    return debug;
}

Microsoft::WRL::ComPtr<ID3D11InfoQueue> framework::acquire_d3d11_info_queue() const noexcept
{
    Microsoft::WRL::ComPtr<ID3D11InfoQueue> info_queue;
    if (device) device.As(&info_queue);
    return info_queue;
}

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

    // These passes own textures whose dimensions must exactly follow the current
    // render size.  Leaving their startup-size resources alive after ResizeBuffers
    // makes SSR copy a 1920x1080 lit texture into a 1600x900 history texture and
    // D3D11 reports COPYRESOURCE_INVALIDSOURCE every frame.
    ssao_pass.Initialize(device.Get(), width, height);
    ssr_pass.Initialize(device.Get(), width, height);
    taa_pass.Initialize(device.Get(), width, height);
    tiled_deferred.Initialize(device.Get(), width, height);

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

bool framework::uninitialize()
{
    // Scene View の視点を残す。再起動後に同じ場所から再開できる。
    // 失敗しても続行する（次回は既定位置になるだけ）。
    if (!standalone_game_mode) save_editor_camera_state();

    // ---- D3D リソースの解放順 -------------------------------------------
    //
    // ここで明示的に手放さないと、ID3D11Debug::ReportLiveDeviceObjects が
    // 走る時点でまだ生きている所有者が残る。
    // とくに次の 2 つは「関数内 static / ファイルスコープ static」なので、
    // 破棄されるのは main() が返ったあと = Report より後になる。
    //   - Source/core/texture.cpp の テクスチャキャッシュ (SRV)
    //   - RePlayEngine/Rendering/RenderStats.h の Stats() (ID3D11Query)
    // どちらも Device より先に、ここで落とす。

    // Dynamics が保持する Body 表は Scene の非所有 ID だけだが、Scene を
    // 破棄する前に明示的に切り離す。これで終了時にも古い World を参照する
    // Backend 状態が残らない。
    detach_collision_world();

    // 1) Scene を止めて GameObject / Component / Behaviour を破棄する。
    //    Renderer Component が握っているメッシュ参照はここで切れる。
    object_runtime_scenes.ResetToEmptyWorld();
    object_runtime_scenes.ActiveWorld().Services().SetRuntimeScene(nullptr);
    object_runtime_scenes.ActiveWorld().Services().SetSceneFlow(nullptr);
    object_runtime_scenes.ActiveWorld().Services().SetRuntime(nullptr);
    object_scene.Clear();
    object_audio_system.Shutdown();

    // 2) LoadingScene の Task はモデル Cache へ書き込むため、
    //    Cache 解放より先に停止・join する。
    scene_manager.Clear();

    // 3) GameObject シーンが抱えているメッシュ / マテリアルを手放す。
    clear_object_mesh_cache();
    clear_object_material_cache();

    // 4) 衝突用の Cook データ。参照が 0 になったものを表から外す。
    object_collision_cook_cache.Collect();

    // 5) Material Catalog が作った PixelShader / 既定Texture / Asset Texture。
    //    Device の Live Object Report より先に必ず解放する。
    material_gpu_binder.Clear();

    // 5.4) UI Effect 用 RT pool。SRV/RTV を UI Renderer 本体より先に明示解放する。
    ui_renderer.ReleaseTransientTargets();
    line_stroke_renderer.Release();

    // 5.5) UI Renderer / FontAtlas。内部の SRV を texture cache より先に手放す。
    ui_renderer.Release();
    ui_font_atlas.Release();

    // 6) 旧テクスチャキャッシュ (SRV)。
    //    static なので明示的に呼ばないと Report まで生き残る。
    release_all_textures();

    // 7) GPU 統計の Query Pool。同じく static。
    ReplayEngine::Rendering::Stats().Release();

    // 7.5) Compute / Deferred が持つ UAV と DepthStencilState。
    //      この 2 つは initialize() の作り直しでリセット対象から漏れており、
    //      resize のたびに前の実体が孤児になっていた。
    //      作り直し側は release() を通すよう直したので、
    //      ここでは終了時の最後の所有参照を落とす。
    particles.release();
    deferred.release();

    // 8) パイプラインに残ったバインドを外してから、積んだコマンドを流し切る。
    //    参照カウントを持っているのはバインド状態も同じなので、
    //    ClearState を通さないと最後の描画で使ったリソースが残る。
    if (immediate_context)
    {
        immediate_context->ClearState();
        immediate_context->Flush();
    }
    return true;
}

framework::~framework() {}
