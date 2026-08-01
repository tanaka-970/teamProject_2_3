#include <time.h>
#include <array>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>

#include "framework.h"
#include "../../../RePlayEngine/Assets/AssetDatabase.h"
#include "../../../RePlayEngine/Editor/Validation/SceneValidator.h"
#include "../../../RePlayEngine/Object/Registry/BuiltInComponents.h"
#include "../../../RePlayEngine/Scene/Runtime/Scene.h"
#include "../../../RePlayEngine/Scene/Serialization/SceneData.h"
#include "../../../RePlayEngine/Scene/Serialization/SceneSerializer.h"

namespace
{
    int RunHeadlessSceneValidation(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        std::string scene_path_text;
        if (!(arguments >> command) || command != "--validate-scene") return -1;
        if (!(arguments >> std::quoted(scene_path_text)) || scene_path_text.empty())
        {
            std::fprintf(stderr, "--validate-scene requires a path\n");
            return 2;
        }

        ReplayEngine::Core::RegisterBuiltInComponents();
        ReplayEngine::Assets::AssetDatabase assets;
        std::string asset_error;
        assets.Load(asset_error);

        namespace Serialization = ReplayEngine::Scene::Serialization;
        Serialization::SceneData source;
        std::string error;
        const std::filesystem::path scene_path(scene_path_text);
        if (!Serialization::SceneSerializer::LoadFromFile(source, scene_path, error))
        {
            std::fprintf(stderr, "Scene load failed: %s\n", error.c_str());
            return 3;
        }

        ReplayEngine::Scene::Scene scene;
        Serialization::SceneLoadReport load_report;
        Serialization::ApplySceneData(source, scene, load_report);
        const auto issues = ReplayEngine::Editor::SceneValidator::Validate(scene, &assets);
        int errors = 0;
        for (const ReplayEngine::Editor::ValidationIssue& issue : issues)
        {
            if (issue.severity == ReplayEngine::Editor::ValidationSeverity::Error) ++errors;
            std::fprintf(stderr, "[%s] %s: %s\n",
                issue.severity == ReplayEngine::Editor::ValidationSeverity::Error ? "ERROR" :
                issue.severity == ReplayEngine::Editor::ValidationSeverity::Warning ? "WARN" : "INFO",
                issue.code.c_str(), issue.message.c_str());
        }

        Serialization::SceneData captured;
        Serialization::CaptureScene(scene, captured);
        const std::filesystem::path roundtrip_path = std::filesystem::path("Saved") /
            "Validation" / (scene_path.stem().string() + ".roundtrip.replayscene");
        if (!Serialization::SceneSerializer::SaveToFile(captured, roundtrip_path, error))
        {
            std::fprintf(stderr, "Round-trip save failed: %s\n", error.c_str());
            return 4;
        }
        Serialization::SceneData roundtrip;
        if (!Serialization::SceneSerializer::LoadFromFile(roundtrip, roundtrip_path, error) ||
            roundtrip.objects.size() != captured.objects.size() ||
            roundtrip.version != Serialization::SceneData::current_version)
        {
            std::fprintf(stderr, "Round-trip verification failed: %s\n", error.c_str());
            return 5;
        }

        std::fprintf(stderr, "Validated %zu objects, %zu warnings, %d errors; round-trip v%d OK\n",
            source.objects.size(), issues.size() - static_cast<std::size_t>(errors), errors,
            roundtrip.version);
        return errors == 0 ? 0 : 6;
    }
}

LRESULT CALLBACK window_procedure(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	framework* p{ reinterpret_cast<framework*>(GetWindowLongPtr(hwnd, GWLP_USERDATA)) };
	return p ? p->handle_message(hwnd, msg, wparam, lparam) : DefWindowProc(hwnd, msg, wparam, lparam);
}

int WINAPI WinMain(_In_ HINSTANCE instance, _In_opt_  HINSTANCE prev_instance, _In_ LPSTR cmd_line, _In_ int cmd_show)
{
    const int validation_result = RunHeadlessSceneValidation(cmd_line);
    if (validation_result >= 0) return validation_result;

    // WICの画像読み込みはCOMを使うため、エンジンの生存期間中は初期化状態を維持する。
    // シーン切り替え後もWICファクトリを確実に利用できるようにする。
	const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // 相対パスの基準を実行ファイルの場所へ統一する。
    // Visual Studioの作業ディレクトリ設定に依存せず、直接起動でも同じ動作にする。
	std::array<wchar_t, 32768> executable_path{};
	const DWORD path_length = GetModuleFileNameW(nullptr, executable_path.data(),
		static_cast<DWORD>(executable_path.size()));
	if (path_length > 0 && path_length < executable_path.size())
	{
		std::wstring directory(executable_path.data(), path_length);
		const size_t separator = directory.find_last_of(L"\\/");
		if (separator != std::wstring::npos)
		{
			directory.resize(separator);
			const std::wstring packaged_resources = directory + L"\\resources";
			const DWORD attributes = GetFileAttributesW(packaged_resources.c_str());
			if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
			{
        // Visual Studioの配置は「プロジェクト/x64/構成/3dgp.exe」となる。
				directory += L"\\..\\..";
			}
			SetCurrentDirectoryW(directory.c_str());
		}
	}

	srand(static_cast<unsigned int>(time(nullptr)));

#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	WNDCLASSEXW wcex{};
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = window_procedure;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = instance;
	wcex.hIcon = 0;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = APPLICATION_NAME;
	wcex.hIconSm = 0;
	RegisterClassExW(&wcex);

	RECT rc{ 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
	HWND hwnd = CreateWindowExW(0, APPLICATION_NAME, L"", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
		NULL, NULL, instance, NULL);
	ShowWindow(hwnd, cmd_show);

	framework framework(hwnd);
	SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&framework));
	const int exit_code = framework.run();
	if (SUCCEEDED(com_result)) CoUninitialize();
	return exit_code;
}
