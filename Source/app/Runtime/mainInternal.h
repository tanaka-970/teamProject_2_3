// これは main.cpp 系を分割するための内部事情だけを共有するヘッダ。
// 外部のコードから include して使うものではない。
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#if defined(_DEBUG)
#include <dxgidebug.h>
#include <wrl/client.h>
#endif

namespace ReplayEngine::Runtime::Detail
{
    bool ParseShutdownRegression(const char* command_line);
    bool ParseStartupSceneBoot(const char* command_line);
    bool ParseCaptureFrame(const char* command_line, std::string& capture_name);
    std::string TrimCopy(std::string text);
    void StripUtf8Bom(std::string& text);
    std::string LowerCopy(std::string text);

    struct ExecutableLayout
    {
        std::filesystem::path executable_path;
        std::filesystem::path executable_directory;
        std::filesystem::path content_root;
    };
    ExecutableLayout ResolveExecutableLayout();

    struct GameLaunchConfig
    {
        bool file_found = false;
        bool file_loaded = false;
        std::filesystem::path file;
        std::string name{ "RePlayGame" };
        std::filesystem::path startup_scene;
        UINT window_width{ SCREEN_WIDTH };
        UINT window_height{ SCREEN_HEIGHT };
        bool fullscreen = false;
        std::vector<std::string> warnings;
    };
    GameLaunchConfig LoadGameLaunchConfig(const std::filesystem::path& executable_directory);
    std::uint32_t ParseAutomatedSmokeTestFrames(const char* command_line);

    struct ProfileBenchmarkConfig
    {
        bool requested = false;
        bool valid = true;
        std::filesystem::path scene;
        std::uint32_t frames = 300;
        std::uint32_t warmup_frames = 30;
        std::string output_name{ "benchmark" };
        std::string error;
    };
    ProfileBenchmarkConfig ParseProfileBenchmark(const char* command_line);

    struct D3D11LiveObjectFileSummary
    {
        std::uint64_t stored_messages = 0;
        std::uint64_t readable_messages = 0;
        std::uint64_t live_object_detail_lines = 0;
        std::uint64_t live_object_summary_count = 0;
        bool live_object_summary_found = false;
        std::uint64_t live_device_lines = 0;
        std::uint64_t live_device_refcount = 0;
        bool live_device_refcount_found = false;
        std::uint64_t live_context_lines = 0;
        std::uint64_t live_debug_interface_lines = 0;
    };
    D3D11LiveObjectFileSummary WriteD3D11LiveObjectReportFile(
        ID3D11InfoQueue* info_queue, bool report_available, HRESULT report_result);

#if defined(_DEBUG)
    struct DXGILiveObjectFileSummary
    {
        std::uint64_t stored_messages = 0;
        std::uint64_t readable_messages = 0;
        std::uint64_t live_object_lines = 0;
        std::uint64_t live_d3d11_device_lines = 0;
    };
    DXGILiveObjectFileSummary WriteDXGILiveObjectReportFile(
        IDXGIInfoQueue* info_queue, bool report_available, HRESULT report_result);
    bool AcquireDXGIDebugInterfaces(
        Microsoft::WRL::ComPtr<IDXGIDebug1>& debug,
        Microsoft::WRL::ComPtr<IDXGIInfoQueue>& info_queue,
        HMODULE& module) noexcept;
#endif

    std::filesystem::path ValidationFolder();
    void WriteValidationResultFile(const char* file_name, const char* header,
        bool ok, const std::vector<std::string>& lines);

    int RunHeadlessLargeSceneValidation(const char* command_line);
    int RunHeadlessSceneValidation(const char* command_line);
    int RunHeadlessMaterialValidation(const char* command_line);
    int RunHeadlessLandscapeValidation(const char* command_line);
    int RunHeadlessPrefabValidation(const char* command_line);
    int RunHeadlessHandleValidation(const char* command_line);
    int RunHeadlessCameraComponentValidation(const char* command_line);
    int RunHeadlessPlayerSpeedValidation(const char* command_line);
    int RunHeadlessInputValidation();
    int RunHeadlessMotionEventsValidation();
    int RunHeadlessPhysicsValidation(const char* command_line);
    int RunHeadlessMotionTriggerValidation(const char* command_line);
    int RunHeadlessPropertyLinkValidation(const char* command_line);
    int RunHeadlessScenePersistenceValidation(const char* command_line);
    int RunHeadlessSerializationValidation(const char* command_line);
}
