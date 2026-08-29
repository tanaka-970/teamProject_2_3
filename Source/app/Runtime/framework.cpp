#include "framework.h"
#include "skinned_mesh.h"
#include "gltf_model.h"
#include "../Editor/GoldenImageState.h"

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
    golden_state_ = std::make_unique<ReplayEngine::Editor::GoldenImageState>();
    object_loading_progress_provider.Bind(&scene_manager);
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
    ReplayEngine::Rendering::Stats().SetOutputDirectory(saved_root_path_ / "Profile");
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
    // 出荷ゲームではProfilerを既定非表示にし、F4で必要なときだけ開く。
    show_render_stats = false;
    csharp_auto_reload = false;
    shader_auto_recompile = false;

    collision_cooker = ReplayEngine::Physics::MeshCollisionCooker(
        saved_path(std::filesystem::path("Cache") / "collisions"));
    gltf_model::SetCacheRoot(saved_path(std::filesystem::path("Cache") / "gltf"));
    set_shutdown_log_folder(saved_root_path_);
    ReplayEngine::Rendering::Stats().SetOutputDirectory(saved_root_path_ / "Profile");
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
    if (width == 0 || height == 0 || !dx12_device_context.Resize(width, height))
        return false;
    client_width = width;
    client_height = height;
    if (game_scene)
    {
        game_scene->Gameplay().SetAspect(
            static_cast<float>(width) / static_cast<float>(height));
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
    if (object_loading_scene)
    {
        object_loading_scene->Services().SetRuntime(nullptr);
        object_loading_scene->Services().SetRuntimeScene(nullptr);
        object_loading_scene->Services().SetSceneFlow(nullptr);
        object_loading_scene->Services().SetLoadingProgress(nullptr);
        object_loading_scene.reset();
    }
    object_loading_runtime_context.reset();

    // 3) GameObject シーンが抱えているメッシュ / マテリアルを手放す。
    clear_object_mesh_cache();
    clear_object_material_cache();

    // 4) 衝突用の Cook データ。参照が 0 になったものを表から外す。
    object_collision_cook_cache.Collect();

    // Scene Effect の GPU 資源は DX12 側が持つため、Shutdown で一緒に解放される。
    ui_preview_runtime_width = 0;
    ui_preview_runtime_height = 0;

    if (dx12_device_context.IsInitialized()) dx12_device_context.Shutdown();
    dx12_framework_active = false;
    dx12_framework_render_error_reported = false;

    return true;
}

framework::~framework() {}
