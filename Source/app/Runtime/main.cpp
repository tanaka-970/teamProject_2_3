#include <time.h>
#include <array>
#include <string>

#include "framework.h"

LRESULT CALLBACK window_procedure(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	framework* p{ reinterpret_cast<framework*>(GetWindowLongPtr(hwnd, GWLP_USERDATA)) };
	return p ? p->handle_message(hwnd, msg, wparam, lparam) : DefWindowProc(hwnd, msg, wparam, lparam);
}

int WINAPI WinMain(_In_ HINSTANCE instance, _In_opt_  HINSTANCE prev_instance, _In_ LPSTR cmd_line, _In_ int cmd_show)
{
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
