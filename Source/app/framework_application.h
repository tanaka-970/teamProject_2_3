#pragma once

#include <windows.h>
#include <tchar.h>
#include <sstream>
#include <filesystem>
#include <vector>
#include <array>
#include <algorithm>
#include <utility>
#include "skinned_mesh.h"
#include "misc.h"
#include "high_resolution_timer.h"

#ifdef USE_IMGUI
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern ImWchar glyphRangesJapanese[];
#include <chrono>
#include <fstream>
#endif

class gltf_model;

namespace ReplayEngine::Components
{
    class CameraComponent;
}

CONST LONG SCREEN_WIDTH{ 1600 };
CONST LONG SCREEN_HEIGHT{ 900 };
CONST BOOL FULLSCREEN{ FALSE };
CONST LPWSTR APPLICATION_NAME{ L"X3DGP_Upgraded" };
